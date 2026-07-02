# feature50：token 依赖图受控反馈环（dep loop）—— 不动点迭代（Newton 整数平方根）。
#
# 四期机制：在 dep…map 显式 DAG 之上，`dep …: 源 loop 目标` 声明「受控反馈环」边——
#   与 map 同维度（边拓扑），唯一区别：loop 边**允许成环**（map 成环编译期报错）。
#   · 门逻辑 all/any 与边拓扑 map/loop 正交：dep all/any 照常决定触发条件。
#   · 编译期 Tarjan 缩点：把 loop 边的强连通分量（反馈簇）识别出来，烘焙为每个 token 的
#     「簇编号 scc + 簇大小」字面量常量（token_set_scc，O(1) 查表，运行时零图遍历）。
#   · loop 源**不反挂** deps[]：set 不会自动级联（杜绝无限环）；反馈仅由 t.loop_run(max)
#     显式驱动——每轮对簇内每条 loop dep 以 acting=TOK_LOOP 唤起 follow（读旧值→算新值→
#     set 写回，下轮读到新值），跑满 max 轮（一期无 eps 收敛判据）。
#
# 本例搭一个 2 节点反馈簇 a ⇄ b（边 a→b、b→a，强连通，scc_size=2），实现 Newton 迭代
#   a ← (a + b)/2、b ← N/a，自 a=100 迭代收敛到 a=b=sqrt(100)=10（整数不动点）。

tok a: "fp.a"          # 主迭代量（反馈簇成员）
tok b: "fp.b"          # 中间量 N/a（反馈簇成员）

# a → b：b = N / a   （受控反馈边 loop；闭合环的前半）
dep all: x:"fp.a" loop y:"fp.b"
    var av: i8 = (x->get(): i8)
    if av != 0
        y->set(((100 / av): *), 0)
    return false

# b → a：a = (a + b) / 2  （受控反馈边 loop；闭合环 a→b→a 的后半）
dep all: p:"fp.b" loop q:"fp.a"
    var bv: i8 = (p->get(): i8)
    var av: i8 = (q->get(): i8)
    q->set((((av + bv) / 2): *), 0)
    return false

fnc main: i4
    form b, (0: *)             # 反馈簇两成员均为本模块所主，须 form 激活方可 set/get
    form a, (0: *)

    a->set((100: *), 0)        # 初值 a=100（loop 源不级联，仅置初值，不触发反馈）
    ::printf("init: a=%lld  scc=%d size=%d\n", (a->get(): i8), a->scc(), a->scc_size())

    # 显式驱动反馈簇迭代至多 10 轮（Newton 整数 sqrt 收敛到不动点）
    var rounds: i4 = a->loop_run(10)
    ::printf("after %d rounds: a=%lld b=%lld (sqrt100=10)\n", rounds, (a->get(): i8), (b->get(): i8))

    return 0
