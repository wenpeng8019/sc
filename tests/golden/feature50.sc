# 由 scc --emit-sc 从 AST 再生成

tok a: "fp.a"

tok b: "fp.b"

dep all: x:"fp.a" loop y:"fp.b"
    var av: i8 = (x->get(): i8)
    if av != 0
        y->set((100 / av: @), 0)
    return false

dep all: p:"fp.b" loop q:"fp.a"
    var bv: i8 = (p->get(): i8)
    var av: i8 = (q->get(): i8)
    q->set(((av + bv) / 2: @), 0)
    return false

fnc main: i4
    form b, (0: @)
    form a, (0: @)
    a->set((100: @), 0)
    printf("init: a=%lld  scc=%d size=%d\n", (a->get(): i8), a->scc(), a->scc_size())
    var rounds: i4 = a->loop_run(10)
    printf("after %d rounds: a=%lld b=%lld (sqrt100=10)\n", rounds, (a->get(): i8), (b->get(): i8))
    return 0
