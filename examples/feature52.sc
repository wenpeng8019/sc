# feature52：token 依赖图扇入/扇出度（dep…map）—— 枢纽识别（hub）。
#
# 二梯队机制：在 dep…map 显式 DAG 之上，编译期统计每个 token 的扇入度（被多少上游依赖）
#   与扇出度（驱动多少下游），烘焙为字面量常量（O(1) 查表）。度数高者即依赖图枢纽：
#   · 高扇出 = 广播源 / 配置中心（牵一发动全身，改它会触发大量下游）；
#   · 高扇入 = 聚合汇点 / 瓶颈消费者（依赖众多，任一上游变更都重算）。
#
# 本例搭一个「配置中心 → 多消费者 → 聚合汇」的扇形图：
#   config → a,b,c      config 扇出 3（广播枢纽）
#   a,b,c  → sink       sink   扇入 3（聚合枢纽）

tok config: "hub.config"   # 配置中心（扇出枢纽）
tok a:      "hub.a"
tok b:      "hub.b"
tok c:      "hub.c"
tok sink:   "hub.sink"      # 聚合汇（扇入枢纽）

dep all: s:"hub.config" map t:"hub.a"
    return false
dep all: s:"hub.config" map t:"hub.b"
    return false
dep all: s:"hub.config" map t:"hub.c"
    return false
dep all: s:"hub.a" map t:"hub.sink"
    return false
dep all: s:"hub.b" map t:"hub.sink"
    return false
dep all: s:"hub.c" map t:"hub.sink"
    return false

fnc main: i4
    printf("node     fanin fanout\n")
    printf("config   %5d %6d\n", config->fanin(), config->fanout())
    printf("a        %5d %6d\n", a->fanin(),      a->fanout())
    printf("b        %5d %6d\n", b->fanin(),      b->fanout())
    printf("c        %5d %6d\n", c->fanin(),      c->fanout())
    printf("sink     %5d %6d\n", sink->fanin(),   sink->fanout())
    return 0
