# 由 scc --emit-sc 从 AST 再生成

tok sig: "cd.sig"

tok gauge: "cd.gauge"
    var i: i8 = (this->input: i8)
    if i == 0
        return tok_modified()
    var b: i8 = (this->base: i8)
    if i > b
        return (i: @)
    return (b: @)

tok hits: "cd.hits"

tok ghits: "cd.ghits"

dep any: s:"cd.sig"
    if this->active >= 0
        var h: i8 = (hits->get(): i8)
        hits->set((h + 1: @), 0)
    return false

dep any: g:"cd.gauge"
    if this->active >= 0
        var c: i8 = (ghits->get(): i8)
        ghits->set((c + 1: @), 0)
    return false

fnc main: i4
    form sig, (0: @)
    form gauge, (0: @)
    form hits, (0: @)
    form ghits, (0: @)
    sig->set((5: @), 0)
    sig->set((5: @), 0)
    sig->set((5: @), 0)
    var h1: i8 = (hits->get(): i8)
    printf("直赋 set(5) x3:   hits=%lld (期望 1)\n", h1)
    sig->set((7: @), 0)
    var h2: i8 = (hits->get(): i8)
    printf("直赋 set(7):      hits=%lld (期望 2)\n", h2)
    gauge->set((10: @), 0)
    gauge->set((3: @), 0)
    var g1: i8 = (ghits->get(): i8)
    printf("combine 取峰抑制:  ghits=%lld (期望 1)\n", g1)
    gauge->set((0: @), 0)
    var g2: i8 = (ghits->get(): i8)
    printf("modified 强制刷新: ghits=%lld (期望 2)\n", g2)
    return 0
