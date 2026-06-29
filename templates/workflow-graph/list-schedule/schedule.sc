# ============================================================
# list-schedule —— 工作流计算图框架脚手架（就绪优先队列 + 列表调度）
# ============================================================
#
# 与 back-drain（拉取式 drain）成对照：同一张 tok 依赖图、同一条流水线、同样的结果，
# 唯独【如何发现下一个要处理的节点】不同——
#
#   · back-drain（反向扫描发现）：worker 调 `back fuse` 沿反向邻接【遍历整张图】，
#     按反拓扑序逐节点查「有没有待处理帧」，扫到一个就认领。发现 = O(图规模) 反向迭代。
#   · 本模板（就绪队列发现）：节点的输入一旦就绪（入度满足）即【按权重压入一条全局优先
#     队列】；worker 直接 pop 队顶（权重最高的就绪节点）执行。发现 = O(log n) 出堆。
#
# 这就是经典的【列表调度 / list scheduling】：维护一张 ready-list，入度归零即入列，
# 线程从表头取「最该先跑」的节点。表的排序键就是【节点依赖性权重】。
#
# 权重从哪来：tok 在编译期就把每个节点的 `depth()`（拓扑层级 = 最长路径分层）烘焙为常量，
# O(1) 查表即得。本模板以 depth 为权重、用【最大堆】（深者在顶）——深度越大越靠近 sink，
# 优先排空下游 → 最小化在管缓冲。这恰好复现 back-drain 中 `back` 的「最深先行」次序
# （tok 的 back 节点模式本就按 depth 降序唤起），但把「每个 worker 每轮反向扫全图」换成
# 「一次 O(log n) 出堆」。换权重（critical/slack）或换堆向（最小堆）即换调度纪律——见文末。
#
# 职责划分（与 workflow 一致：dep 管路由、node 管函数；仅发现机制不同）：
#   · dep（边）= 前向路由器：上游 set 落值即把数据【入队】到下游节点（经 t->ctx() 取下游）。
#   · node（点）= 函数实现：kernel（纯函数 i8→i8）挂在侧车 wnode 上。
#   · 发现 = 就绪优先队列：节点 IDLE→PEND 时按权重入堆；worker pop 堆顶认领，不再 back 扫描。
#
# 内置流水线（线性，单 sink；与 back-drain 同形、同 kernel、同确定性结果，便于对照）：
#     capture ─► gray ─► blur ─► edges ─► fuse(sink)
# ============================================================

inc mt.sc
inc adt.sc      # 复用内置 ADT：ring（节点缓冲）+ heap（就绪优先队列），均不手搓

# ---- 常量 ----
let QCAP: i4 = 32        # 每节点环形队列容量
let NW:   i4 = 4         # 工作线程数
let NF:   i4 = 8         # 提交帧数

let S_IDLE: i4 = 0       # 状态机：空闲（无待处理帧、不在就绪堆）
let S_PEND: i4 = 1       # 待处理（队列有帧、已在就绪堆，待 worker 认领）
let S_RUN:  i4 = 2       # 处理中（已被某 worker pop 出堆认领）

# ============================================================
# 协作层数据结构
# ============================================================

# 节点算子类型：纯函数 i8→i8（承载本节点真实计算）。
@fnc kfn: i8, x: i8

# 节点侧车：与 token 一一对应——token 管拓扑 + 烘焙度量 + ctx，wnode 管缓冲 + 状态机 + kernel + 权重。
#   缓冲复用内置 ring；多 worker + 多路由器均在 flow.mu 下访问（外部加锁即满足 ring 约束）。
#   侧车经 form t,v,&n 绑定到 token 的 ctx：dep 路由用 t->ctx() 取下游节点；就绪堆亦以 t->ctx()
#   （即 form 已装箱的 &n）作为堆元素 value，故无须自行装箱、无须硬编码节点变量。
def wnode: {
    q: ring             # 待处理输入帧的环形缓冲（容量 QCAP，elem=i8）
    state: i4           # S_IDLE / S_PEND / S_RUN
    cur:  i8            # 已认领待处理的当前帧（认领后本 worker 独占读）
    nproc: i4           # 累计已处理帧数（统计）
    t: token&           # 关联 token（拓扑句柄）
    kernel: kfn         # 本节点算子（纯函数 i8→i8）
    weight: i4          # 调度权重（= token.depth()，main 中从烘焙度量灌入）：就绪堆排序键
    is_sink: i4         # 是否汇点（无下游 dep）：1 则 set 后额外登记输出（wn_emit）

    init: fnc
        var z: i8 = 0
        this->q.init(sizeof(z), QCAP)   # 全局节点声明即构造：自动备好缓冲与状态
        this->state = S_IDLE
        this->cur = 0
        this->nproc = 0
        this->t = nil
        this->kernel = noop_k
        this->weight = 0
        this->is_sink = 0

    drop: fnc
        this->q.drop()
}

# 流水线运行时（全局单例）：一把全局锁守就绪堆 + 各节点状态 + 计数。
#   rq 是本模板的核心——就绪优先队列（ready-list）：key=节点权重(depth)、value=节点（t->ctx() 的 @）。
#   最大堆（min=0）：权重最大（最深）的就绪节点在顶，worker pop 即得「最该先跑」者。
def wflow: {
    mu:   mutex         # 守 rq / 各 wnode 状态与队列 / inflight
    work: cond          # 就绪堆非空 → 唤醒一个等待的 worker
    idle: cond          # 全部完成（inflight==0）→ 唤醒主线程
    rq:   heap          # 就绪优先队列（列表调度的 ready-list）
    inflight: i4        # 已入队未完成的帧总数（排空判据）
    stop: i4            # 停机标志（主线程置位 → worker 退出）
    armed: i4           # 协作层就绪（main form 完成后置 1）——拦截 form 期的伪入队
    out_sum: i8         # sink 输出校验和（确定性统计）
    out_cnt: i4         # 完成帧数

    init: fnc
        this->mu.init()
        this->work.init()
        this->idle.init()
        var z: i4 = 0
        this->rq.init((0: u1), sizeof(z), nil, nil)   # min=0 最大堆；key 宽=sizeof(i4)；内置数值比较
        this->inflight = 0
        this->stop = 0
        this->armed = 0
        this->out_sum = 0
        this->out_cnt = 0

    drop: fnc
        this->rq.drop()
        this->idle.drop()
        this->work.drop()
        this->mu.drop()
}

var flow: wflow
var g_pool: pool& = nil      # 工作池（default_pool）：NW 个常驻 worker，阻塞等就绪堆非空

# ---- 节点声明（token + 侧车）----
tok capture: "lx.capture"   # 纯源（depth 0）：form 后 set 即产出，入 gray 队列
tok gray:    "lx.gray"      # 灰度化
tok blur:    "lx.blur"      # 模糊
tok edges:   "lx.edges"     # 边缘
tok fuse:    "lx.fuse"      # 汇点 sink

var gray_n:  wnode
var blur_n:  wnode
var edges_n: wnode
var fuse_n:  wnode

# ============================================================
# 节点算子（kernel）：纯函数 i8→i8（与 back-drain 同，确保结果可对照）
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
# 协作层：就绪优先队列 + 状态机（均在 flow.mu 下操作）
# ============================================================

# 入队（路由器调用）：把一帧压入节点队列；IDLE→PEND 则【按权重压入就绪堆】并唤醒一个 worker。
#   这正是「入度归零即入 ready-list」：节点拿到输入即成就绪，按依赖性权重排队待取。
fnc wn_push: i4, n: wnode&, v: i8
    if flow.armed == 0           # form 期的伪触发：协作层未就绪，丢弃（无真实帧）
        return 0
    flow.mu.lock()
    if n->q.push(&v)             # ring 入队（满则返回 false，自然背压丢弃）
        flow.inflight = flow.inflight + 1
        if n->state == S_IDLE
            n->state = S_PEND
            flow.rq.push(&n->weight, (n: *))         # 按权重(depth)入就绪堆；value=节点指针（装箱为 @）
            flow.work.one()                          # 唤醒一个阻塞 worker 来 pop 堆顶
    flow.mu.unlock()
    return 0

# 认领（worker 调用，调用方已持 flow.mu）：pop 就绪堆顶 → 出队一帧到 cur → 转 RUN → 返回节点。
#   发现与认领在此一步完成：无图遍历，直接取「权重最高的就绪节点」。
fnc wf_take: wnode&
    var n: wnode& = (flow.rq.peek(): wnode&)   # 借用堆顶 value（最深就绪节点）
    flow.rq.pop()                              # 出堆（release 该 @ 句柄；非托管，无 free）
    var out: i8 = 0
    n->q.pop(&out)                             # PEND 必非空，pop 成功
    n->cur = out
    n->state = S_RUN
    return n

# 完成（处理 + set 后调用）：队列仍有帧 → 按权重重入就绪堆（回 PEND）；否则 IDLE。
#   inflight 归零则唤醒主线程（排空完成）。
fnc wn_done: i4, n: wnode&
    flow.mu.lock()
    n->nproc = n->nproc + 1
    flow.inflight = flow.inflight - 1
    if n->q.is_empty() == false
        n->state = S_PEND
        flow.rq.push(&n->weight, (n: *))         # 仍有积压：按权重重入就绪堆
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
# 计算图边：每条 dep…map 仅是【前向路由器】——经目标 token 的 ctx 取下游节点并入队。
#   边体不含算子、不硬编码节点变量（下游是谁由 t->ctx() 决定）；算子全在节点 kernel 上。
#   扩展点：加一处理节点 = 加 tok + wnode + kernel + 一条 dep…map 边（仅一行路由）。
# ============================================================

dep all: s:"lx.capture" map t:"lx.gray"
    wn_push((t->ctx(): wnode&), (s->get(): i8))
    return false

dep all: s:"lx.gray" map t:"lx.blur"
    wn_push((t->ctx(): wnode&), (s->get(): i8))
    return false

dep all: s:"lx.blur" map t:"lx.edges"
    wn_push((t->ctx(): wnode&), (s->get(): i8))
    return false

dep all: s:"lx.edges" map t:"lx.fuse"
    wn_push((t->ctx(): wnode&), (s->get(): i8))
    return false

# ============================================================
# worker：常驻线程（default_pool + run<g_pool>，阻塞等就绪堆非空）
#   列表调度的执行体——纯粹「pop 就绪队顶 → 跑 → set」，无任何图遍历：
#   1) 锁内等就绪堆非空（或停机）；2) wf_take 取权重最高的就绪节点并认领一帧；3) 锁外跑 kernel；
#   4) set 输出（触发下游路由器入队，可能把下游节点压入就绪堆）；5) wn_done 收尾（仍有积压则重入堆）。
# ============================================================

rpc worker_ls: id: i4
    var alive: i4 = 1
    while alive == 1
        flow.mu.lock()
        while flow.rq.is_empty() && flow.stop == 0
            flow.work.wait(&flow.mu, 0, 0)
        if flow.rq.is_empty() && flow.stop == 1
            flow.mu.unlock()
            alive = 0
        else
            var n: wnode& = wf_take()            # 锁内：pop 堆顶 + 认领一帧
            flow.mu.unlock()
            var outv: i8 = n->kernel(n->cur)     # 节点算子：本节点真实计算（锁外）
            n->t->set((outv: *), 0)              # 写输出 → 触发下游路由器入队
            if n->is_sink == 1
                wn_emit(outv)                    # 汇点：无下游 dep，节点自登记输出
            wn_done(n)

# ============================================================
# 自省 + 主程序
# ============================================================

# 打印静态图度量（编译期烘焙常量，O(1) 查表）——weight 列即调度用的 depth。
fnc report:
    print "node      depth crit slack batch reach checkpoint"
    print "capture   ", (capture->depth(): "%5d"), (capture->critical(): "%5d"), (capture->slack(): "%6d"), (capture->batch(): "%6d"), (capture->reach(): "%6d"), (capture->checkpoint(): "%11d")
    print "gray      ", (gray->depth(): "%5d"), (gray->critical(): "%5d"), (gray->slack(): "%6d"), (gray->batch(): "%6d"), (gray->reach(): "%6d"), (gray->checkpoint(): "%11d")
    print "blur      ", (blur->depth(): "%5d"), (blur->critical(): "%5d"), (blur->slack(): "%6d"), (blur->batch(): "%6d"), (blur->reach(): "%6d"), (blur->checkpoint(): "%11d")
    print "edges     ", (edges->depth(): "%5d"), (edges->critical(): "%5d"), (edges->slack(): "%6d"), (edges->batch(): "%6d"), (edges->reach(): "%6d"), (edges->checkpoint(): "%11d")
    print "fuse      ", (fuse->depth(): "%5d"), (fuse->critical(): "%5d"), (fuse->slack(): "%6d"), (fuse->batch(): "%6d"), (fuse->reach(): "%6d"), (fuse->checkpoint(): "%11d")
    return

fnc main: i4
    # ---- 补齐节点业务绑定（缓冲/锁/就绪堆已由全局对象构造自动完成）：kernel + 权重（烘焙 depth）----
    gray_n.t = gray
    gray_n.kernel = gray_k
    gray_n.weight = gray->depth()
    blur_n.t = blur
    blur_n.kernel = blur_k
    blur_n.weight = blur->depth()
    edges_n.t = edges
    edges_n.kernel = edges_k
    edges_n.weight = edges->depth()
    fuse_n.t = fuse
    fuse_n.kernel = fuse_k
    fuse_n.weight = fuse->depth()
    fuse_n.is_sink = 1                        # fuse 是汇点：is_sink=1

    # ---- form 各节点：确立就绪起点 + 绑定侧车 ctx（form t,v,&n → t->ctx() 取侧车；本模板无 exec 钩子，
    #      发现走就绪堆而非 back）。capture 是纯源无侧车。form 期触发的入队被 flow.armed==0 守卫拦截。----
    form capture, (0: *)
    form gray,    (0: *), &gray_n
    form blur,    (0: *), &blur_n
    form edges,   (0: *), &edges_n
    form fuse,    (0: *), &fuse_n

    print "=== list-schedule 反应图自省（编译期烘焙度量；weight=depth）==="
    report()

    # ---- 启动常驻线程池：NW 个 worker 各 run<g_pool> worker_ls，阻塞等就绪堆非空 ----
    g_pool = default_pool((NW: u4))
    for i in NW
        run<g_pool> worker_ls(i)

    flow.armed = 1                           # 协作层就绪：自此 wn_push 真正入队/入堆

    # ---- 提交帧（源 set 即产出，入 gray 队列 → 就绪堆；池中 worker 按权重列表调度）----
    print "=== 提交 ", NF, " 帧，", NW, " 线程列表调度（就绪堆 pop）==="
    for i in NF
        var frame: i8 = (100: i8) + (i: i8) * (10: i8)
        capture->set((frame: *), 0)

    # ---- 等待排空 ----
    flow.mu.lock()
    while flow.inflight > 0
        flow.idle.wait(&flow.mu, 0, 0)
    flow.mu.unlock()

    # ---- 停机 + 回收线程池 ----
    flow.mu.lock()
    flow.stop = 1
    flow.work.all()                          # 唤醒常驻 worker 令其退出
    flow.mu.unlock()
    g_pool->drop()                           # 等 worker 退出后停池并回收
    g_pool = nil

    # ---- 结果（确定性，与 back-drain 一致）----
    print "=== 结果 ==="
    print "完成帧数=", flow.out_cnt, "  输出校验和=", flow.out_sum
    print "各级处理计数: gray=", gray_n.nproc, " blur=", blur_n.nproc, " edges=", edges_n.nproc, " fuse=", fuse_n.nproc

    return 0

# ============================================================
# 扩展指南
# ------------------------------------------------------------
# 1) 加一处理节点 N（上游 U，算子 N_k）：
#      tok N: "lx.N"
#      var N_n: wnode
#      fnc N_k: i8, x: i8        # 本节点真实算子
#          return <...>
#      dep all: s:"lx.U" map t:"lx.N"   # 边只前推（dep = 路由）
#          wn_push((t->ctx(): wnode&), (s->get(): i8))
#          return false
#    main 中：N_n.t=N; N_n.kernel=N_k; N_n.weight=N->depth(); form N,(0:*),&N_n。
#    depth/critical/reach 等度量与权重由编译器自动重新烘焙，无须手算。
#
# 2) 换调度纪律（只改权重与堆向，调度骨架不动）：
#    · 权重 = depth、最大堆（本模板）：最深就绪先行 → 优先排空下游、最小化在管缓冲，
#      复现 back-drain 的 back「最深先行」次序，但发现 O(log n) 而非 O(N) 反向扫描。
#    · 权重 = depth、最小堆（rq.init(1,...)）：最浅先行 → 前馈优先、最大化并行铺开。
#    · 权重 = critical()（是否关键路径）：关键路径节点优先 → 经典 HLFET，缩短 makespan。
#    · 权重 = slack()（松弛余量，取负入最小堆）：余量最小者优先 → 最小松弛优先（MSF）。
#    权重一律来自 tok 编译期烘焙度量，运行时 O(1) 查表，调度器零图遍历。
#
# 3) 与 back-drain 的取舍：
#    · 反向扫描发现（back-drain）：无额外数据结构，与「同值抑制/记忆化」天然耦合；
#      发现成本随图规模上升（每 worker 每轮 O(N)）。适合小图、强记忆化、拉取语义。
#    · 就绪队列发现（本模板）：发现 O(log n)，权重可插拔（depth/critical/slack…）；
#      需维护一张就绪堆。适合大图、需显式优先级/QoS 的调度。
#    二者同享 tok 拓扑 + 烘焙度量 + dep 路由 + 节点 kernel，仅「如何找下一个节点」不同。
# ============================================================
