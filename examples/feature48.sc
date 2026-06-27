# feature48：token 依赖图（dep…map）—— 显式声明 源→目标 依赖边，构成有向无环工作流。
#
# 二期机制：dep 的 `map` 关键字在「源」与「目标」之间架起一条显式有向边——
#   dep all/any: <源...> map <目标...>
#       <follow 体>
#   · 源（触发/上游）：门按其状态开合；其变更/就绪触发本 dep。
#   · 目标（输出/下游）：follow 体写入；本身不触发本 dep（杜绝自环回灌）。
#   · 源与目标均注入局部名糖：follow 体内可直接以局部名引用句柄（token&）。
#   · 全单元的 map 边汇成有向图，编译器（语义分析）做有向环检测——
#     若出现环（含自环 a→a）即编译期报错：依赖图须为 DAG。
# 运行时生成 token_depend_map(_deps={源++目标}, nsrc, ntgt, 门, follow, NULL)。
#
# 本例搭一条线性工作流（DAG）：raw → clean → report。
#   raw 变更 → 计算 clean（去负、限幅 0）→ clean 变更 → 汇总 report（翻倍）。

tok raw:    "wf.raw"
tok clean:  "wf.clean"
tok report: "wf.report"

# 源 raw → 目标 clean：去负（负值归零）。块式 map 写法。
dep any:
    r: "wf.raw"
    map
    c: "wf.clean"
    -
    var v: i8 = (r->get(): i8)
    if v < 0
        v = 0
    c->set((v: @), 0)
    return false

# 源 clean → 目标 report：翻倍。行内 map 写法。
dep any: c:"wf.clean" map o:"wf.report"
    var v: i8 = (c->get(): i8)
    o->set(((v * 2): @), 0)
    return false

fnc main: i4
    # 本模块为三量之主：各须 form 激活方可 set/get 与级联（自下游向上游 form，初值灌定）
    form report, (0: @)
    form clean,  (0: @)
    form raw,    (0: @)

    # 深度分层（编译期烘焙的常量，O(1) 查表）：raw=0 → clean=1 → report=2
    printf("depth: raw=%d clean=%d report=%d\n", raw->depth(), clean->depth(), report->depth())

    raw->set(((0 - 5): @), 0)            # -5 → clean 归零 0 → report 0
    printf("raw=-5: clean=%lld report=%lld\n", (clean->get(): i8), (report->get(): i8))

    raw->set((21: @), 0)                 # 21 → clean 21 → report 42
    printf("raw=21: clean=%lld report=%lld\n", (clean->get(): i8), (report->get(): i8))

    return 0
