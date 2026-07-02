# 由 scc --emit-sc 从 AST 再生成

tok s: "bat.s"

tok a: "bat.a"

tok b: "bat.b"

tok c: "bat.c"

tok t: "bat.t"

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
    ::printf("node     batch width\n")
    ::printf("s        %5d %5d\n", s->batch(), s->batch_width())
    ::printf("a        %5d %5d\n", a->batch(), a->batch_width())
    ::printf("b        %5d %5d\n", b->batch(), b->batch_width())
    ::printf("c        %5d %5d\n", c->batch(), c->batch_width())
    ::printf("t        %5d %5d\n", t->batch(), t->batch_width())
    return 0
