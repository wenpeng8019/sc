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

@fnc sum_each -> bst_each_fn
    var a: acc& = (ctx: acc&)
    a->sum += (value: node&)->v
    a->cnt += 1
    return true

@fnc dump_keys: i4, t: bst&
    var c: i8 = t->first()
    var n: i4 = 0
    while c != 0
        var kp: i4& = (t->key_at(c): i4&)
        printf(" %d", kp[0])
        n += 1
        c = t->next(c)
    printf("\n")
    return n

@fnc make: node@, x: i4
    var p: node@ = node()
    p->v = x
    return p

@fnc main: i4
    var ta: bst
    ta.init(0, 4, nil, nil)
    var ha[8]: node@
    var keys[7]: i4
    keys[0] = 50
    keys[1] = 30
    keys[2] = 70
    keys[3] = 20
    keys[4] = 40
    keys[5] = 60
    keys[6] = 80
    var i: i4 = 0
    while i < 7
        ha[i] = make(keys[i] * 10)
        ta.put((&keys[i]: const &), (ha[i]: @))
        i += 1
    printf("A len=%llu\n", ta.len())
    printf("A inorder:")
    dump_keys(&ta)
    var k40: i4 = 40
    var k99: i4 = 99
    printf("A has40=%d miss99=%d get40=%d\n", ta.has((&k40: const &)), ta.has((&k99: const &)), (ta.get((&k40: const &)): node&)->v)
    var sa: acc
    sa.sum = 0
    sa.cnt = 0
    ta.each(sum_each, (&sa: &))
    printf("A each sum=%d cnt=%d\n", sa.sum, sa.cnt)
    var fs: i4 = 0
    var c: i8 = ta.first()
    while c != 0
        fs += (ta.value_at(c): node&)->v
        c = ta.next(c)
    var bs: i4 = 0
    c = ta.last()
    while c != 0
        bs += (ta.value_at(c): node&)->v
        c = ta.prev(c)
    printf("A fwd_sum=%d bwd_sum=%d\n", fs, bs)
    printf("A idx40=%lld\n", ta.index_of((&k40: const &)))
    var c0: i8 = ta.at(0)
    var c0k: i4& = (ta.key_at(c0): i4&)
    printf("A at0_key=%d at0_val=%d\n", c0k[0], (ta.value_at(c0): node&)->v)
    var k45: i4 = 45
    var cm: i8 = ta.most((&k45: const &))
    var cl: i8 = ta.least((&k45: const &))
    var cmk: i4& = (ta.key_at(cm): i4&)
    var clk: i4& = (ta.key_at(cl): i4&)
    printf("A most45=%d least45=%d\n", cmk[0], clk[0])
    ha[7] = make(999)
    ta.put((&k40: const &), (ha[7]: @))
    printf("A put40b=%d len=%llu\n", (ta.get((&k40: const &)): node&)->v, ta.len())
    var k50: i4 = 50
    var k20: i4 = 20
    var k70: i4 = 70
    printf("A rm50=%d rm20=%d rm70=%d rmmiss=%d len=%llu\n", ta.remove((&k50: const &)), ta.remove((&k20: const &)), ta.remove((&k70: const &)), ta.remove((&k99: const &)), ta.len())
    printf("A inorder2:")
    dump_keys(&ta)
    ta.drop()
    printf("A after_drop len=%llu\n", ta.len())
    var tb: bst
    tb.init(1, 4, nil, nil)
    var hb[15]: node@
    var bk[15]: i4
    i = 0
    while i < 15
        bk[i] = (i + 1)
        hb[i] = make(101 + i)
        tb.put((&bk[i]: const &), (hb[i]: @))
        i += 1
    printf("B len=%llu\n", tb.len())
    printf("B inorder:")
    dump_keys(&tb)
    var b8: i8 = tb.at(7)
    var b8k: i4& = (tb.key_at(b8): i4&)
    printf("B at7_key=%d idx_key10=%lld\n", b8k[0], tb.index_of((&bk[9]: const &)))
    i = 1
    while i < 15
        tb.remove((&bk[i]: const &))
        i += 2
    printf("B after_rm_even len=%llu inorder:", tb.len())
    dump_keys(&tb)
    tb.drop()
    var tc: bst
    tc.init(0, 0 - 1, nil, nil)
    var hc[4]: node@
    hc[0] = make(4)
    tc.put("delta", (hc[0]: @))
    hc[1] = make(1)
    tc.put("alpha", (hc[1]: @))
    hc[2] = make(3)
    tc.put("charlie", (hc[2]: @))
    hc[3] = make(2)
    tc.put("bravo", (hc[3]: @))
    printf("C len=%llu inorder:", tc.len())
    var cc: i8 = tc.first()
    while cc != 0
        printf(" %s=%d", (tc.key_at(cc): char&), (tc.value_at(cc): node&)->v)
        cc = tc.next(cc)
    printf("\n")
    printf("C get_charlie=%d has_x=%d\n", (tc.get(("charlie": const &)): node&)->v, tc.has(("zzz": const &)))
    tc.remove(("alpha": const &))
    printf("C after_rm len=%llu\n", tc.len())
    tc.drop()
    return 0
