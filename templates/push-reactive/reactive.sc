# ============================================================
# push-reactive —— 推送式反应数据流脚手架（同步级联 + 节点观察）
# ============================================================
#
# 与 workflow-graph（拉取式）成对照：同一张 tok 依赖图，两种执行模型——
#
#   · 拉取式（workflow-graph）：源 set → dep 路由【入队】到下游节点 → worker 线程经
#     `back` 拉取（exec）→ 出队跑 kernel → set。异步、缓冲、多线程、快慢级解耦。
#   · 推送式（本模板）：源 set → 沿图【同步级联】→ 节点 combine 即时算出新值 → 节点
#     观察钩子（exec）即时反应。同步、即时、单线程、求值与传播耦合。
#
# 职责划分（与 workflow 一致的「dep 管路由，node 管函数」，但同步形态）：
#   · node（点）= 函数实现：单输入派生格的【公式】写在该格的 combine 体里（this->input
#     即上游值，return 新值）——格即函数。多输入扇入格的公式天然落在 follow（需读全部
#     输入），见扩展指南。
#   · dep（边）= 关系路由：单输入边的 follow 只把上游值【前推】给下游格（t->set(s->get())），
#     触发下游 combine 重算——边只连线、不算公式。
#   · exec（节点反应）= 副作用/观察点：格的值落定后于【锁外】唤起，做 combine 不能做的
#     副作用（产出、统计、日志、外部推送）。combine 须纯（锁内只算值），副作用归 exec。
#
# 反应式记忆化（变更检测在推送场景是【特性】而非缺陷）：
#   set 仅在「新值≠原值」时落值并传播。推送反应式里这正是【辉光抑制/去抖】——上游变了但
#   某格重算后值不变（如 clamp 饱和），级联就在该格【自然截断】，下游不做冗余重算、观察钩子
#   不被无谓唤起。这与拉取式流水线（每帧皆事件、相同值也不可丢）相反：那里同值抑制会丢帧，
#   需要【全传播】逻辑（每次 set 皆事件，相同值也不可丢）：那里同值抑制会丢帧/使迭代失效，
#   需 t.pulse(v, tag) 脉冲原语强制传播（见 builtins/tok.md §1.3.5）。同一机制，两种场景两种语义。
#
# 内置反应图（线性级联 + 三处观察，与 workflow 同形以便对照）：
#     sensor ─► norm ─► level ─► warn
#   sensor 纯源；norm/level/warn 为派生格（各带 combine 公式）；norm/level/warn 上各挂
#   一个 exec 观察钩子，累计「该格实际变更」的统计——级联随饱和逐级衰减，观察次数递减。
# ============================================================

# ---- 常量 ----
let NR:    i4 = 8        # 提交读数个数
let LCAP:  i8 = 5        # level 饱和上限（clamp 上界）
let ALimit: i8 = 4      # warn 阈值：level >= ALimit 即报警

# ============================================================
# 节点观察侧车：与被观察 token 一一对应——token 管值与拓扑，cnode 管观察累计。
#   经 form t,v,&n 绑定到 token 的 ctx；exec 钩子回传 ctx 取本格累计器（与 workflow 的
#   wnode+ctx 同构：ctx 是节点私有状态的通用载体，拉取式装队列、推送式装观察累计）。
# ============================================================
def cnode: {
    sum:   i8           # 该格历次「变更后值」之和（确定性统计）
    cnt:   i4           # 该格实际变更次数（= exec 被唤起次数）
    last:  i8           # 最近一次观察到的值

    init: fnc                       # 全局节点声明即构造：累计器自清零（无须手动 cn_init）
        this->sum = 0
        this->cnt = 0
        this->last = 0
}

var norm_n:  cnode
var level_n: cnode
var warn_n:  cnode

# ============================================================
# 节点公式（combine）：单输入派生格的真实计算，this->input 即上游前推值（node = 函数）。
#   combine 须纯：仅由 base/input 算 value，不得 set/get 其它 token（它在写者临界区内运行）。
# ============================================================

tok sensor: "px.sensor"     # 纯源（depth 0）：无 combine，set 即原值

tok norm: "px.norm"         # 归一：input / 10
    return ((this->input: i8) / 10: @)

tok level: "px.level"       # 饱和：min(input, LCAP)（clamp → 反应式截断的来源）
    var i: i8 = (this->input: i8)
    if i > LCAP
        return (LCAP: @)
    return (i: @)

tok warn: "px.warn"         # 报警：input >= ALimit ? 1 : 0
    var i: i8 = (this->input: i8)
    if i >= ALimit
        return (1: @)
    return (0: @)

# ============================================================
# 节点观察钩子（exec）：格值落定后于锁外唤起，累计统计（node = 副作用归节点）。
#   ctx 即 form 绑定的侧车 cnode；仅在该格【实际变更】时被唤起（同值抑制 → 反应式去抖）。
# ============================================================
fnc observe: i4, t: token&, ctx: &
    var n: cnode& = (ctx: cnode&)
    var v: i8 = (t->get(): i8)
    n->sum = n->sum + v
    n->cnt = n->cnt + 1
    n->last = v
    return 0

# ============================================================
# 计算图边：每条 dep…map 仅是【前向路由器】——把上游值前推给下游格，触发其 combine 重算。
#   边体不含公式（公式在下游格 combine 上）；this->active>=0 之外（form 就绪事件）也照常前推，
#   彼时各格皆 0 → combine 算得 0 → 同值抑制，不会伪触发观察（apply 仅在 set 变更时唤起）。
# ============================================================

dep all: s:"px.sensor" map t:"px.norm"
    t->set((s->get()), 0)
    return false

dep all: s:"px.norm" map t:"px.level"
    t->set((s->get()), 0)
    return false

dep all: s:"px.level" map t:"px.warn"
    t->set((s->get()), 0)
    return false

# ============================================================
# 自省 + 主程序
# ============================================================

# 打印静态图度量（编译期烘焙常量，O(1) 查表）。
fnc report:
    print "cell      depth crit slack batch reach checkpoint"
    print "sensor    ", (sensor->depth(): "%5d"), (sensor->critical(): "%5d"), (sensor->slack(): "%6d"), (sensor->batch(): "%6d"), (sensor->reach(): "%6d"), (sensor->checkpoint(): "%11d")
    print "norm      ", (norm->depth(): "%5d"), (norm->critical(): "%5d"), (norm->slack(): "%6d"), (norm->batch(): "%6d"), (norm->reach(): "%6d"), (norm->checkpoint(): "%11d")
    print "level     ", (level->depth(): "%5d"), (level->critical(): "%5d"), (level->slack(): "%6d"), (level->batch(): "%6d"), (level->reach(): "%6d"), (level->checkpoint(): "%11d")
    print "warn      ", (warn->depth(): "%5d"), (warn->critical(): "%5d"), (warn->slack(): "%6d"), (warn->batch(): "%6d"), (warn->reach(): "%6d"), (warn->checkpoint(): "%11d")
    return

fnc main: i4
    # ---- form 各格：确立就绪起点 + 绑定观察侧车 + 处理钩子（form t,v,&n,exec → t->ctx() / 推送模式唤起 exec）。
    #      sensor 纯源无侧车无钩子；派生格连同 cnode 地址 + observe 绑定。form 期前推皆 0 → 同值抑制，观察静默。----
    form sensor, (0: @)
    form norm,  (0: @), &norm_n,  observe
    form level, (0: @), &level_n, observe
    form warn,  (0: @), &warn_n,  observe

    print "=== push-reactive 反应图自省（编译期烘焙度量）==="
    report()

    # ---- 提交读数：每次 sensor set 同步级联 sensor→norm→level→warn，逐格 combine 重算，
    #      变更格的 exec 即时累计。无队列、无线程：全在本线程同步完成。----
    print "=== 提交 ", NR, " 个读数，同步反应级联 ==="
    var i: i4 = 0
    for i = 0; i < NR; i++
        var reading: i8 = (10: i8) + (i: i8) * (10: i8)   # 10,20,...,80
        sensor->set((reading: @), 0)

    # ---- 结果（确定性）：级联随 level 饱和逐级衰减——norm 每次都变（8 次），level 饱和后
    #      截断（5 次），warn 仅在跨阈那一次变更（1 次）。----
    print "=== 结果（反应式记忆化：级联随饱和逐级截断）==="
    print "norm  观察: 累计=", norm_n.sum, " 变更次数=", norm_n.cnt, " 末值=", norm_n.last
    print "level 观察: 累计=", level_n.sum, " 变更次数=", level_n.cnt, " 末值=", level_n.last
    print "warn  观察: 累计=", warn_n.sum, " 变更次数=", warn_n.cnt, " 末值=", warn_n.last
    return 0

# ============================================================
# 扩展指南
# ------------------------------------------------------------
# 1) 加一单输入派生格 N（上游 U，公式 f）：
#      tok N: "px.N"
#          return (<f(this->input)>: @)        # 公式写在格的 combine（node = 函数）
#      dep all: s:"px.U" map t:"px.N"          # 边只前推（dep = 路由）
#          t->set((s->get()), 0)
#          return false
#    要观察该格：var N_n: cnode（声明即经 init 自动清零）+ form N, (0:@), &N_n, observe。
#    depth/critical/reach 等度量由编译器自动重新烘焙。
#
# 2) 扇入格 C（公式依赖多输入 a、b）：公式天然落在 follow（需读全部输入）——
#      tok C: "px.C"                            # 无 combine 体（值由 follow 直赋）
#      dep all: a:"px.a", b:"px.b" map t:"px.C" # 与门：a、b 皆就绪/变更才重算
#          var x: i8 = (a->get(): i8)
#          var y: i8 = (b->get(): i8)
#          t->set(((x + y): @), 0)              # 多输入公式在边上
#          return false
#    单输入「格即函数」、多输入「公式在边」——按扇入度自然取舍。
#
# 3) 「每事件皆须传播」的逃生门（推送场景里少见，事件流/脉冲才需）：
#    变更检测会吞掉「重算后同值」的传播。两条逃生门：combine 体内 `return tok_modified()` 强制传播
#    （即便合成不变，下游收到 modified 哨兵）；或对源/直赋格用 t.pulse(v, tag) 脉冲设值（绕过相等
#    抑制，每次皆事件）。值流水线要「同值也不丢」则属拉取式语义，请用 workflow-graph 模板
#    （值走队列、tok 仅作唤醒 ping）。
#
# 4) 接 MT：exec 在锁外唤起、天然 MT 安全；把 id 以 '/' 开头即启 seqlock 值同步，
#    combine 仍须纯（锁内），副作用仍归 exec（锁外）。
# ============================================================
