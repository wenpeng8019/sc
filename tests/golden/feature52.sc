# 由 scc --emit-sc 从 AST 再生成

tok config: "hub.config"

tok a: "hub.a"

tok b: "hub.b"

tok c: "hub.c"

tok sink: "hub.sink"

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
    printf("a        %5d %6d\n", a->fanin(), a->fanout())
    printf("b        %5d %6d\n", b->fanin(), b->fanout())
    printf("c        %5d %6d\n", c->fanin(), c->fanout())
    printf("sink     %5d %6d\n", sink->fanin(), sink->fanout())
    return 0
