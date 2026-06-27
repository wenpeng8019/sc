# 由 scc --emit-sc 从 AST 再生成

tok tin: "dom.in"

tok l: "dom.l"

tok r: "dom.r"

tok mid: "dom.mid"

tok tout: "dom.out"

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
    printf("in       %10d %8d\n", tin->checkpoint(), tin->dom_size())
    printf("l        %10d %8d\n", l->checkpoint(), l->dom_size())
    printf("r        %10d %8d\n", r->checkpoint(), r->dom_size())
    printf("mid      %10d %8d\n", mid->checkpoint(), mid->dom_size())
    printf("out      %10d %8d\n", tout->checkpoint(), tout->dom_size())
    return 0
