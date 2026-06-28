# ============================================================
# workflow-graph —— 工作流计算图框架脚手架（缓冲队列 + 线程池 + drain）
# ============================================================
#
# 设计要旨：与 tok 机制「配合」而非「合入」——
#   · tok 负责【拓扑 + 烘焙度量 + 反向遍历 + 节点钩子】：节点连边（dep…map）、编译期
#     算好的 depth/critical/batch/checkpoint…、drain 用的反向遍历 back（支持 break）、
#     以及节点私有上下文 ctx 与节点处理钩子 exec（二者均经 form 绑定）。
#   · 本文件的协作层负责【缓冲 + 调度 + 状态机】：每节点一条环形队列做流水线缓冲、
#     一个线程池跑节点处理、一套状态机标记「是否有待处理帧」。二者经 mt 模块的
#     mutex/cond/thread 黏合。tok 不感知队列/线程，协作层不改 tok 内核。
#
# 职责划分（本次重构核心）：「dep 管关系路由，node 管函数实现」——
#   · dep（边）= 纯路由器：上游 set 落值即把数据【入队】到下游节点；下游是谁，经
#     目标 token 的 ctx 侧车（t->ctx()）取得，dep 体不硬编码任何节点变量、不含算子。
#   · node（点）= 函数实现：每个处理节点把自己的 kernel（纯函数 i8→i8）挂在侧车
#     wnode 上，并经 form 第4参把统一的处理钩子 node_drain 绑为节点 exec。算子（真实计算）
#     只在节点上，dep 上没有任何 if active==BACK 的反向分支。
#
# 执行模型（拉取式 drain，区别于「设值即同步级联」）：
#   1) 缓冲：dep 前向路由——上游 set → 经 t->ctx() 取下游节点 → 入队（不计算、不阻塞，
#      解耦快慢级，多帧在管）。
#   2) 处理：worker 自 sink 反向遍历 `back`；tok 见任一可达节点绑定了 exec，即进入
#      【节点 drain 模式】，按反拓扑序（最深先行）对节点唤起 exec → node_drain。
#   3) node_drain：认领节点 → 出队一帧 → 跑节点 kernel → set 输出（再触发下游路由器
#      入队）→ 认领即处理一节点后【返回 1】令 back 中止本轮、重扫——天然「下游优先排空」。
#
# 内置流水线（线性，单 sink；并行分支/汇合见文末扩展）：
#     capture ─► gray ─► blur ─► edges ─► fuse(sink)
#   capture 是纯源（set 即产出，入 gray 队列，无 exec）；gray/blur/edges/fuse 是
#   处理节点（各有一条环形队列 + kernel + exec，由池中 worker 经 back 认领处理）。
# ============================================================

inc mt.sc
inc adt.sc      # 复用内置 ADT：ring（SPSC 无锁环形队列）做节点缓冲，不再手搓

# ---- 常量 ----
let QCAP: i4 = 32        # 每节点环形队列容量
let NW:   i4 = 4         # 工作线程数
let NF:   i4 = 8         # 提交帧数

let MODE_RESIDENT: i4 = 0   # 策略 A：常驻 worker（default_pool + 阻塞等 flow.work）
let MODE_ONDEMAND: i4 = 1   # 策略 B：按需 worker（drain_pool：run<dp> 激活、有活就跑、无活即退）
let WORKER_MODE: i4 = MODE_ONDEMAND  # 配置切换：MODE_RESIDENT / MODE_ONDEMAND

let S_IDLE: i4 = 0       # 状态机：空闲（无待处理帧）
let S_PEND: i4 = 1       # 待处理（队列有帧，未被认领）—— 可被 drain 认领
let S_RUN:  i4 = 2       # 处理中（已被某 worker 认领）

# ============================================================
# 协作层数据结构
# ============================================================

# 节点算子类型：纯函数 i8→i8（无副作用，承载本节点真实计算）。换真实数据时改签名即可。
@fnc kfn: i8, x: i8

# 节点侧车：与 token 一一对应——token 管拓扑 + ctx + exec，wnode 管缓冲 + 状态机 + kernel。
#   缓冲直接复用内置 ring（adt）：kfifo 风格有界环形队列，省去自造 buf/head/tail/cnt。
#   ring 本是 SPSC 无锁；此处多 worker + 多路由器均在 flow.mu 下访问（多生产/多消费 + 外部
#   加锁），正是 ring 文档允许的用法（锁串行化即满足约束）。
#   侧车经 form t,v,&n 绑定到 token 的 ctx：dep 路由用 t->ctx() 取下游节点，back drain 用
#   exec 回传 ctx 取本节点——故 dep/钩子均无须硬编码节点变量。
def wnode: {
    q: ring             # 待处理输入帧的环形缓冲（容量 QCAP，elem=i8）
    state: i4           # S_IDLE / S_PEND / S_RUN
    cur:  i8            # 已认领待处理的当前帧（认领后本 worker 独占读）
    nproc: i4           # 累计已处理帧数（统计）
    t: token&           # 关联 token（拓扑句柄）
    kernel: kfn         # 本节点算子（纯函数 i8→i8）：node_drain 出队后唤起
    is_sink: i4         # 是否汇点（无下游 dep）：1 则 set 后额外登记输出（wn_emit）

    init: fnc
        var z: i8 = 0
        this->q.init(sizeof(z), QCAP)   # 全局节点声明即构造：自动备好缓冲与状态
        this->state = S_IDLE
        this->cur = 0
        this->nproc = 0
        this->t = nil
        this->kernel = noop_k
        this->is_sink = 0

    drop: fnc
        this->q.drop()
}

# 流水线运行时（全局单例）：一把全局锁守所有节点状态 + 计数（脚手架从简；
#   真实工程可细化为每节点锁，见文末扩展）。
def wflow: {
    mu:   mutex         # 守 navail / inflight / 各 wnode 状态与队列
    work: cond          # 有可认领工作 → 唤醒 worker
    idle: cond          # 全部完成（inflight==0）→ 唤醒主线程
    navail: i4          # 当前处于 S_PEND 的节点数（可认领工作量）
    inflight: i4        # 已入队未完成的帧总数（排空判据）
    stop: i4            # 停机标志（主线程置位 → worker 退出）
    mode: i4            # worker 策略（常驻 default_pool / 按需 drain_pool）
    ready: i4           # 协作层就绪（main form 完成后置 1）——拦截 form 期的 ready 入队
    out_sum: i8         # sink 输出校验和（确定性统计）
    out_cnt: i4         # 完成帧数

    init: fnc
        this->mu.init()
        this->work.init()
        this->idle.init()
        this->navail = 0
        this->inflight = 0
        this->stop = 0
        this->mode = MODE_RESIDENT
        this->ready = 0
        this->out_sum = 0
        this->out_cnt = 0

    drop: fnc
        this->idle.drop()
        this->work.drop()
        this->mu.drop()
}

var flow: wflow
# 工作池（pool&）：常驻策略=default_pool、按需策略=drain_pool；二者同 pool&、同凭 run 投递。
#   按需模式下 wn_push 经 run<g_pool> wf_drain 投递；常驻模式下仅赋值不读（worker 已在跑、阻塞等 flow.work）。
var g_pool: pool& = nil

# ---- 节点声明（token + 侧车）----
# 本流水线每帧独立流过、不做跨帧合并，真正的算子在节点 kernel 上（node_drain 唤起），
#   故节点 token 均无 combine 体。但「共享量需主的启动点」：token 唯被 form 后方就绪，set
#   才落值并触发下游路由器；未 form 则 set 仅入挂起队列、不传播。因此 main 启动时对每个节点
#   form（见下），确立就绪起点——处理节点 form 时连同侧车地址一并绑定（form t,v,&n → t->ctx()），
#   form 期触发的 ready 入队由 flow.ready==0 守卫拦截（彼时尚无真实帧）。bind 仅取共享壳、不就绪：
#   dep 注册时源为 NEW，故注册期不会伪触发。
#   若某节点需跨帧合并（滑动累加/去抖/峰值保持），再给它写 combine 体——form 流程不变。
tok capture: "vx.capture"   # 纯源（depth 0）：form 后 set 即产出，无 exec
tok gray:    "vx.gray"      # 灰度化
tok blur:    "vx.blur"      # 模糊
tok edges:   "vx.edges"     # 边缘
tok fuse:    "vx.fuse"      # 汇点 sink

var gray_n:  wnode
var blur_n:  wnode
var edges_n: wnode
var fuse_n:  wnode

# ============================================================
# 节点算子（kernel）：每个处理节点的真实计算，纯函数 i8→i8（node = 函数实现）
# ============================================================

fnc gray_k:  i8, x: i8      # 灰度：in / 3
    return x / 3
fnc blur_k:  i8, x: i8      # 模糊：恒等占位
    return x
fnc edges_k: i8, x: i8      # 边缘：in * 2
    return x * 2
fnc fuse_k:  i8, x: i8      # 汇点：in + 7
    return x + 7
fnc noop_k:  i8, x: i8      # 构造期占位：main 会在 form 前覆写为真实 kernel
    return x

# ============================================================
# 协作层：队列 + 状态机（均在 flow.mu 下操作；助手返回 i4 规避 void+形参）
# ============================================================

# 入队（路由器调用）：把一帧压入节点队列，IDLE→PEND 则计入可认领并唤醒/补投 worker。
fnc wn_push: i4, n: wnode&, v: i8
    if flow.ready == 0           # form 期的 ready 触发：协作层未就绪，丢弃（无真实帧）
        return 0
    var need_run: i4 = 0
    flow.mu.lock()
    if n->q.push(&v)             # ring 入队（满则返回 false，自然背压丢弃）
        flow.inflight = flow.inflight + 1
        if n->state == S_IDLE
            n->state = S_PEND
            flow.navail = flow.navail + 1
        if flow.mode == MODE_RESIDENT
            flow.work.one()      # 常驻：唤醒一个阻塞 worker
        else
            need_run = 1         # 按需：解锁后再 run<g_pool>（投递不持 flow.mu）
    flow.mu.unlock()
    if need_run == 1 && g_pool != nil
        run<g_pool> wf_drain(0)  # 通知 drain_pool 有新活：内部按需激活一个 worker（世代代检防丢唤醒）
    return 0

# 认领（node_drain 调用）：若节点 PEND 则出队一帧到 cur、转 RUN、返回 1；否则 0。
fnc wn_claim: i4, n: wnode&
    flow.mu.lock()
    var got: i4 = 0
    if n->state == S_PEND
        var out: i8 = 0
        n->q.pop(&out)           # PEND 必非空，pop 成功
        n->cur = out
        n->state = S_RUN
        flow.navail = flow.navail - 1
        got = 1
    flow.mu.unlock()
    return got

# 完成（处理 + set 后调用）：队列仍有帧 → 回到 PEND（重新可认领）；否则 IDLE。
#   inflight 归零则唤醒主线程（排空完成）。
fnc wn_done: i4, n: wnode&
    flow.mu.lock()
    n->nproc = n->nproc + 1
    flow.inflight = flow.inflight - 1
    if n->q.is_empty() == false
        n->state = S_PEND
        flow.navail = flow.navail + 1
        flow.work.one()
    else
        n->state = S_IDLE
    if flow.inflight == 0
        flow.idle.all()
    flow.mu.unlock()
    return 0

# sink 输出登记（确定性统计，避免多线程打印交错）。
fnc wn_emit: i4, v: i8
    flow.mu.lock()
    flow.out_sum = flow.out_sum + v
    flow.out_cnt = flow.out_cnt + 1
    flow.mu.unlock()
    return 0

# ============================================================
# 节点处理钩子：统一的拉取式 drain（node = 函数实现的执行体）
#   form t,v,&n,node_drain 绑定后，tok 经 back 反向遍历按反拓扑序对每个注册节点唤起本钩子，
#   ctx 即 form 绑定的侧车。认领成功→出队→跑本节点 kernel→set 输出（触发下游路由器入队）→
#   返 1 令 back 停扫重扫；未认领（无待处理帧）→返 0，back 继续往更浅节点扫描。
fnc node_drain: i4, t: token&, ctx: &
    var n: wnode& = (ctx: wnode&)            # 侧车：form t,v,&n 绑定，exec 回传
    if wn_claim(n) == 1
        var outv: i8 = n->kernel(n->cur)     # 节点算子：本节点真实计算
        t->set((outv: @), 0)                 # 写输出 → 触发本节点下游路由器入队
        if n->is_sink == 1
            wn_emit(outv)                    # 汇点：无下游 dep，节点自登记输出
        wn_done(n)
        return 1                             # 已处理一节点：请求 back 中止本轮、重扫
    return 0                                 # 未认领：back 继续反向扫描

# ============================================================
# 计算图边：每条 dep…map 仅是【前向路由器】——经目标 token 的 ctx 取下游节点并入队。
#   边体不含任何算子、不硬编码节点变量（下游是谁由 t->ctx() 决定）；算子全在节点 kernel 上。
#   扩展点：加一处理节点 = 加 tok + wnode + kernel + 一条 dep…map 边（仅一行路由）。
# ============================================================

# capture → gray：路由上游帧入 gray 队列（下游节点经 t->ctx() 取得）
dep all: s:"vx.capture" map t:"vx.gray"
    wn_push((t->ctx(): wnode&), (s->get(): i8))
    return false

# gray → blur
dep all: s:"vx.gray" map t:"vx.blur"
    wn_push((t->ctx(): wnode&), (s->get(): i8))
    return false

# blur → edges
dep all: s:"vx.blur" map t:"vx.edges"
    wn_push((t->ctx(): wnode&), (s->get(): i8))
    return false

# edges → fuse（汇点）
dep all: s:"vx.edges" map t:"vx.fuse"
    wn_push((t->ctx(): wnode&), (s->get(): i8))
    return false

# ============================================================
# 工作策略 A：常驻 worker（default_pool + run<p>，阻塞等 flow.work）
#   线程函数经 run<p> 入池，常驻循环——无活则阻塞，stop 后退出。
# ============================================================

rpc worker_resident: id: i4
    var alive: i4 = 1
    while alive == 1
        flow.mu.lock()
        while flow.navail == 0 && flow.stop == 0
            flow.work.wait(&flow.mu, 0, 0)
        if flow.navail == 0 && flow.stop == 1
            flow.mu.unlock()
            alive = 0
        else
            flow.mu.unlock()
            # drain：tok 见节点绑定 exec → 节点模式，反拓扑唤起 node_drain，
            #   认领并处理一个最深可用节点（node_drain 返 1 后 back 中止本轮）
            back fuse

# ============================================================
# 工作策略 B：按需 worker（drain_pool 的工作单元 rpc，经 run<g_pool> 投递）
#   drain_pool 按需激活 worker，worker 反复跑本 rpc——本 rpc 自身循环排空：有活 back fuse、
#   无活（stop 或 navail==0）即返回，由池经世代代检判定（其间有新 run<g_pool> 则再来一轮，
#   否则 running-- 退出）。running 计数与丢唤醒由 drain_pool 内部保证，本层无须手搓信号量。
# ============================================================

rpc wf_drain: id: i4
    var alive: i4 = 1
    while alive == 1
        flow.mu.lock()
        if flow.stop == 1 || flow.navail == 0
            flow.mu.unlock()
            alive = 0            # 本视角无活：返回（池代检无新投递则令本 worker 退出）
        else
            flow.mu.unlock()
            back fuse            # 有活：排空一个最深可用节点

# ============================================================
# 自省 + 主程序
# ============================================================

# 打印静态图度量（编译期烘焙常量，O(1) 查表）。
fnc report:
    print "node      depth crit slack batch reach checkpoint"
    print "capture   ", (capture->depth(): "%5d"), (capture->critical(): "%5d"), (capture->slack(): "%6d"), (capture->batch(): "%6d"), (capture->reach(): "%6d"), (capture->checkpoint(): "%11d")
    print "gray      ", (gray->depth(): "%5d"), (gray->critical(): "%5d"), (gray->slack(): "%6d"), (gray->batch(): "%6d"), (gray->reach(): "%6d"), (gray->checkpoint(): "%11d")
    print "blur      ", (blur->depth(): "%5d"), (blur->critical(): "%5d"), (blur->slack(): "%6d"), (blur->batch(): "%6d"), (blur->reach(): "%6d"), (blur->checkpoint(): "%11d")
    print "edges     ", (edges->depth(): "%5d"), (edges->critical(): "%5d"), (edges->slack(): "%6d"), (edges->batch(): "%6d"), (edges->reach(): "%6d"), (edges->checkpoint(): "%11d")
    print "fuse      ", (fuse->depth(): "%5d"), (fuse->critical(): "%5d"), (fuse->slack(): "%6d"), (fuse->batch(): "%6d"), (fuse->reach(): "%6d"), (fuse->checkpoint(): "%11d")
    return

fnc main: i4
    # ---- 补齐节点业务绑定（缓冲/锁已由全局对象构造自动完成） ----
    gray_n.t = gray
    gray_n.kernel = gray_k
    blur_n.t = blur
    blur_n.kernel = blur_k
    edges_n.t = edges
    edges_n.kernel = edges_k
    fuse_n.t = fuse
    fuse_n.kernel = fuse_k
    fuse_n.is_sink = 1                        # fuse 是汇点：is_sink=1

    # ---- form 各节点：确立就绪起点（共享量需主的启动点）。处理节点连同侧车地址 + 处理钩子绑定
    #      （form t,v,&n,exec → t->ctx() 取侧车、back 模式唤起 exec(t, ctx)）；capture 是纯源无侧车无钩子。
    #      form 期触发的 ready 入队被 flow.ready==0 守卫拦截；自此各 token 就绪，set 方落值传播。----
    form capture, (0: @)
    form gray,    (0: @), &gray_n,  node_drain
    form blur,    (0: @), &blur_n,  node_drain
    form edges,   (0: @), &edges_n, node_drain
    form fuse,    (0: @), &fuse_n,  node_drain

    print "=== workflow graph 自省（编译期烘焙度量）==="
    report()

    # ---- 启动线程池（策略由 WORKER_MODE 决定，二者皆 pool&、皆凭 run 投递；统一全局 g_pool）----
    #   常驻：default_pool + run<g_pool> worker_resident（NW 个常驻线程函数，阻塞等 flow.work）
    #   按需：drain_pool（不预启 worker；wn_push 经 run<g_pool> wf_drain 在有新活时按需激活，上限 NW）
    flow.mode = WORKER_MODE
    if flow.mode == MODE_RESIDENT
        g_pool = default_pool((NW: u4))
        for i in NW
            run<g_pool> worker_resident(i)
    else
        g_pool = drain_pool((NW: u4))        # 按需模式：构造即可，worker 由首个 run<g_pool> 按需激活

    flow.ready = 1                           # 协作层就绪：自此 wn_push 真正入队

    # ---- 提交帧（源 set 即产出，入 gray 队列；池并发 drain）----
    print "=== 提交 ", NF, " 帧，", NW, " 线程 drain 流水线 ==="
    for i in NF
        var frame: i8 = (100: i8) + (i: i8) * (10: i8)
        capture->set((frame: @), 0)

    # ---- 等待排空 ----
    flow.mu.lock()
    while flow.inflight > 0
        flow.idle.wait(&flow.mu, 0, 0)
    flow.mu.unlock()

    # ---- 停机 + 回收线程池 ----
    flow.mu.lock()
    flow.stop = 1
    if flow.mode == MODE_RESIDENT
        flow.work.all()                      # 唤醒常驻 worker 令其退出
    flow.mu.unlock()
    g_pool->drop()                           # 统一回收：常驻=等 worker 退出后停池；按需=置停→等按需 worker 全退→回收
    g_pool = nil

    # ---- 结果（确定性）----
    print "=== 结果 ==="
    print "完成帧数=", flow.out_cnt, "  输出校验和=", flow.out_sum
    print "各级处理计数: gray=", gray_n.nproc, " blur=", blur_n.nproc, " edges=", edges_n.nproc, " fuse=", fuse_n.nproc

    return 0

# ============================================================
# 扩展指南
# ------------------------------------------------------------
# 1) 加一处理节点 N（上游 U，算子 N_k）：
#      tok N: "vx.N"
#      var N_n: wnode
#      fnc N_k: i8, x: i8        # 本节点真实算子（纯函数）
#          return <kernel(x)>
#      dep all: s:"vx.U" map t:"vx.N"        # 仅一行前向路由（下游经 t->ctx() 取）
#          wn_push((t->ctx(): wnode&), (s->get(): i8))
#          return false
#    并在 main 里：`N_n.t = N; N_n.kernel = N_k`（汇点再设 `N_n.is_sink = 1`）
#      + `form N, (0:@), &N_n, node_drain`
#    depth/critical/batch/checkpoint 等度量由编译器自动重新烘焙；算子与路由各居其位
#    （node 管函数实现，dep 管关系路由），无须改 node_drain / worker。
#
# 2) 承载真实数据（tensor/image）：ring 元素改存指向堆缓冲的句柄而非 i8，kernel 签名
#      （kfn）随之改为句柄→句柄——wnode.init 里 q.init(sizeof(tensor&), QCAP)；push/pop
#      的元素即指针，生命周期自管。
#
# 3) 并行分支 + 汇合（join）：fork 处加多条 dep…map（同一上游多个下游，各自队列）；
#    join 节点需按【帧序】配对多路输入——给帧打 id（贯穿各级队列），join 的 wn_claim
#    集齐同 id 的各路输入再处理。drain 仍从各 sink 反向遍历（多 sink 各 back 一次）。
#
# 4) 背压：wn_push 队满当前丢弃；可改为阻塞（cond 等待队列腾空）或反压上游降速。
#
# 5) 细粒度锁：本脚手架用单把 flow.mu；真实工程可每节点一锁（仅 navail/inflight 走
#    全局原子），提升独立子图并行度。
# ============================================================
