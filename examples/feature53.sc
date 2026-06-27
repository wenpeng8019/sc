# feature53：token 依赖图可达性 / 传递闭包（dep…map）—— 脏标记影响范围。
#
# 二梯队机制：在 dep…map 显式 DAG 之上，编译期对每个 token 求传递闭包，烘焙「可达下游
#   token 数」为字面量常量（O(1) 查表）。语义 = 该 token 变更后须重算的下游总数：
#   失效爆炸半径 / 脏标记波及面。改高 reach 的 token 代价大，改叶子（reach 0）零波及。
#
# 本例搭一棵双分支树，看 reach 随位置衰减：
#   root → m1 → leaf1
#   root → m2 → leaf2
#   root 可达 {m1,leaf1,m2,leaf2}=4；m1 仅 {leaf1}=1；m2 仅 {leaf2}=1；叶子=0。

tok root:  "rch.root"     # 根（改它波及全树）
tok m1:    "rch.m1"
tok m2:    "rch.m2"
tok leaf1: "rch.leaf1"
tok leaf2: "rch.leaf2"

dep all: s:"rch.root" map t:"rch.m1"
    return false
dep all: s:"rch.root" map t:"rch.m2"
    return false
dep all: s:"rch.m1" map t:"rch.leaf1"
    return false
dep all: s:"rch.m2" map t:"rch.leaf2"
    return false

fnc main: i4
    printf("node     reach\n")
    printf("root     %5d\n", root->reach())
    printf("m1       %5d\n", m1->reach())
    printf("m2       %5d\n", m2->reach())
    printf("leaf1    %5d\n", leaf1->reach())
    printf("leaf2    %5d\n", leaf2->reach())
    return 0
