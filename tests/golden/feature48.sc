# 由 scc --emit-sc 从 AST 再生成

tok raw: "wf.raw"

tok clean: "wf.clean"

tok report: "wf.report"

dep any: r:"wf.raw" map c:"wf.clean"
    var v: i8 = (r->get(): i8)
    if v < 0
        v = 0
    c->set((v: @), 0)
    return false

dep any: c:"wf.clean" map o:"wf.report"
    var v: i8 = (c->get(): i8)
    o->set((v * 2: @), 0)
    return false

fnc main: i4
    form report, (0: @)
    form clean, (0: @)
    form raw, (0: @)
    printf("depth: raw=%d clean=%d report=%d\n", raw->depth(), clean->depth(), report->depth())
    raw->set((0 - 5: @), 0)
    printf("raw=-5: clean=%lld report=%lld\n", (clean->get(): i8), (report->get(): i8))
    raw->set((21: @), 0)
    printf("raw=21: clean=%lld report=%lld\n", (clean->get(): i8), (report->get(): i8))
    return 0
