# 由 scc --emit-sc 从 AST 再生成

inc adt.sc

@def node: {
    v: i4
    drop: fnc
        printf("drop %d\n", this->v)
}

@def acc: {
    sum: i4
    cnt: i4
}

@fnc sum_each -> dict_each_fn
    var a: acc& = (ctx: acc&)
    a->sum += (value: node&)->v
    a->cnt += 1
    return true

@fnc main: i4
    var da: dict
    da.init(4)
    var a1: node@ = node()
    a1->v = 100
    var a2: node@ = node()
    a2->v = 200
    var a3: node@ = node()
    a3->v = 300
    var k1: i4 = 11
    var k2: i4 = 22
    var k3: i4 = 33
    da.put((&k1: const &), (a1: @))
    da.put((&k2: const &), (a2: @))
    da.put((&k3: const &), (a3: @))
    printf("A len=%llu\n", da.len())
    printf("A has22=%d has99=%d\n", da.has((&k2: const &)), da.has((&k3: const &)))
    var kx: i4 = 99
    printf("A miss=%d\n", da.has((&kx: const &)))
    printf("A get33=%d\n", (da.get((&k3: const &)): node&)->v)
    var a2b: node@ = node()
    a2b->v = 250
    da.put((&k2: const &), (a2b: @))
    printf("A put22b=%d len=%llu\n", (da.get((&k2: const &)): node&)->v, da.len())
    var sa: acc
    sa.sum = 0
    sa.cnt = 0
    da.each(sum_each, (&sa: &))
    printf("A each sum=%d cnt=%d\n", sa.sum, sa.cnt)
    var fs: i4 = 0
    var fc: i4 = 0
    var i: i8 = da.first()
    while i >= 0
        fs += (da.value_at(i): node&)->v
        fc += 1
        i = da.next(i)
    var bs: i4 = 0
    var bc: i4 = 0
    i = da.last()
    while i >= 0
        bs += (da.value_at(i): node&)->v
        bc += 1
        i = da.prev(i)
    printf("A fwd sum=%d cnt=%d  bwd sum=%d cnt=%d\n", fs, fc, bs, bc)
    printf("A rm11=%d rm99=%d len=%llu\n", da.remove((&k1: const &)), da.remove((&kx: const &)), da.len())
    da.drop()
    printf("A after_drop len=%llu\n", da.len())
    var db: dict
    db.init(0)
    var b1: node@ = node()
    b1->v = 10
    var b2: node@ = node()
    b2->v = 20
    db.put("alpha", (b1: @))
    db.put("beta", (b2: @))
    printf("B len=%llu get_beta=%d has_alpha=%d has_x=%d\n", db.len(), (db.get("beta"): node&)->v, db.has("alpha"), db.has("zzz"))
    db.remove("alpha")
    printf("B after_rm len=%llu\n", db.len())
    db.drop()
    var dc: dict
    dc.init(0 - 1)
    var c1: node@ = node()
    c1->v = 70
    var buf[8]: char
    buf[0] = 'k'
    buf[1] = 'e'
    buf[2] = 'y'
    buf[3] = 0
    dc.put((&buf[0]: const &), (c1: @))
    buf[0] = 'X'
    printf("C has_key=%d get_key=%d\n", dc.has((&buf[0]: const &)), dc.has(("key": const &)))
    printf("C lookup=%d\n", (dc.get(("key": const &)): node&)->v)
    dc.drop()
    return 0
