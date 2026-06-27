# feature54：token 依赖图拓扑分批（dep…map）—— antichain 并行波次（接 MT）。
#
# 二梯队机制：在 dep…map 显式 DAG 之上，编译期按最长路径分层——同层 token 两两无路径
#   （反链 antichain），可并行触发。烘焙每 token 的「波次编号 batch（=depth）+ 本波并行
#   宽度 batch_width」为字面量常量（O(1)）。调度器按 batch 0,1,2… 逐波推进，每波内 width
#   个 token 派发到线程池并行执行（接 MT 调度）。
#
# 本例搭一个「单源 → 三路并行 → 单汇」的三层图：
#   s → a,b,c        波次 1 = {a,b,c}，并行宽度 3
#   a,b,c → t        波次 2 = {t}
#   s 波次 0、t 波次 2，各宽度 1。

tok s: "bat.s"     # 源（波次 0）
tok a: "bat.a"     # ┐
tok b: "bat.b"     # ├ 波次 1：三路可并行
tok c: "bat.c"     # ┘
tok t: "bat.t"     # 汇（波次 2）

dep all: s:"bat.s" map t:"bat.a"
    return false
dep all: s:"bat.s" map t:"bat.b"
    return false
dep all: s:"bat.s" map t:"bat.c"
    return false
dep all: s:"bat.a" map t:"bat.t"
    return false
dep all: s:"bat.b" map t:"bat.t"
    return false
dep all: s:"bat.c" map t:"bat.t"
    return false

fnc main: i4
    printf("node     batch width\n")
    printf("s        %5d %5d\n", s->batch(), s->batch_width())
    printf("a        %5d %5d\n", a->batch(), a->batch_width())
    printf("b        %5d %5d\n", b->batch(), b->batch_width())
    printf("c        %5d %5d\n", c->batch(), c->batch_width())
    printf("t        %5d %5d\n", t->batch(), t->batch_width())
    return 0
