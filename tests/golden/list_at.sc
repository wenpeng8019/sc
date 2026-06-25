# 由 scc --emit-sc 从 AST 再生成

inc adt.sc

@def node: {
    v: i4
    drop: fnc
        printf("drop %d\n", this->v)
}

@fnc node_cmp -> list_cmp
    return (a: node&)->v - (b: node&)->v

@fnc main: i4
    var l: list
    var a: node@ = node()
    a->v = 10
    var b: node@ = node()
    b->v = 30
    var c: node@ = node()
    c->v = 20
    l.push((a: @))
    l.push((b: @))
    l.push((c: @))
    printf("len=%llu\n", l.len())
    var g: node@ = (l.get(1): node@)
    printf("get1=%d\n", g->v)
    printf("get2_raw=%d\n", (l.get(2): node&)->v)
    printf("idx_b=%lld idx_a=%lld\n", l.index_of((b: @)), l.index_of((a: @)))
    l.sort(node_cmp)
    var i: u8 = 0
    for i = 0; i < l.len(); i++
        printf("sorted[%llu]=%d\n", i, (l.get(i): node&)->v)
    l.reverse()
    printf("rev0=%d rev2=%d\n", (l.get(0): node&)->v, (l.get(2): node&)->v)
    var d: node@ = node()
    d->v = 99
    l.insert(0, (d: @))
    printf("after_insert len=%llu head=%d\n", l.len(), (l.get(0): node&)->v)
    l.remove_at(l.len() - 1)
    printf("after_remove len=%llu\n", l.len())
    l.set(1, (a: @))
    printf("set1=%d\n", (l.get(1): node&)->v)
    l.pop()
    printf("after_pop len=%llu\n", l.len())
    l.clear()
    printf("after_clear len=%llu\n", l.len())
    return 0
