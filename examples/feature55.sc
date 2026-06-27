# feature55：token 依赖图支配树（dep…map）—— 检查点 / 缓存边界识别。
#
# 三梯队机制：在 dep…map 显式 DAG 之上（编译期引虚拟超级源后求支配关系）：d 支配 n ⇔ 自源
#   到 n 的每条路径都经过 d。支配者是流经其下游全部数据的**咽喉**——天然的检查点 / 缓存
#   边界（在此缓存即可覆盖其支配子树的全部重算）。烘焙每 token 的「checkpoint 标志 +
#   dom_size 支配子树规模」为字面量常量（O(1)）。
#
# 本例搭一个「菱形 + 尾」图，看哪些点是真正的咽喉：
#   in → l, in → r       菱形上游分叉
#   l → mid, r → mid     菱形下游汇合
#   mid → out            尾
#   · in  支配 {l,r,mid,out} → 检查点，dom_size 4（全图咽喉）
#   · mid 支配 {out}        → 检查点，dom_size 1
#   · l/r 各有旁路（mid 经另一支也可达）→ 非支配者，非检查点
#   · out 无下游 → 非检查点

tok tin:  "dom.in"     # 入口（全图咽喉）
tok l:    "dom.l"      # 菱形左（旁路，非咽喉）
tok r:    "dom.r"      # 菱形右（旁路，非咽喉）
tok mid:  "dom.mid"    # 汇合点（尾段咽喉）
tok tout: "dom.out"    # 出口

dep all: s:"dom.in" map t:"dom.l"
    return false
dep all: s:"dom.in" map t:"dom.r"
    return false
dep all: s:"dom.l" map t:"dom.mid"
    return false
dep all: s:"dom.r" map t:"dom.mid"
    return false
dep all: s:"dom.mid" map t:"dom.out"
    return false

fnc main: i4
    printf("node     checkpoint dom_size\n")
    printf("in       %10d %8d\n", tin->checkpoint(),  tin->dom_size())
    printf("l        %10d %8d\n", l->checkpoint(),    l->dom_size())
    printf("r        %10d %8d\n", r->checkpoint(),    r->dom_size())
    printf("mid      %10d %8d\n", mid->checkpoint(),  mid->dom_size())
    printf("out      %10d %8d\n", tout->checkpoint(), tout->dom_size())
    return 0
