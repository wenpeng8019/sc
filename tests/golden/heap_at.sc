# 由 scc --emit-sc 从 AST 再生成

inc adt.sc

@def node: {
    v: i4
    drop: fnc
        ::printf("drop %d\n", this->v)
}

@fnc make: node@, x: i4
    var p: node@ = node()
    p->v = x
    return p

@fnc drain_keys: i4, h: heap&
    var n: i4 = 0
    while !h->is_empty()
        var kp: i4& = (h->peek_key(): i4&)
        ::printf(" %d", kp[0])
        h->pop()
        n += 1
    ::printf("\n")
    return n

@fnc main: i4
    var ta: heap
    ta.init(1, 4, nil, nil)
    ta.reserve(16)
    var ha[7]: node@
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
        ta.push((&keys[i]: const &), (ha[i]: @))
        i += 1
    ::printf("A len=%llu empty=%d\n", ta.len(), ta.is_empty())
    var topk: i4& = (ta.peek_key(): i4&)
    ::printf("A top_key=%d top_val=%d\n", topk[0], (ta.peek(): node&)->v)
    ::printf("A drain:")
    var cnt: i4 = drain_keys(&ta)
    ::printf("A drained=%d empty=%d\n", cnt, ta.is_empty())
    var tb: heap
    tb.init(0, 4, nil, nil)
    var hb[7]: node@
    i = 0
    while i < 7
        hb[i] = make(keys[i] * 10)
        tb.push((&keys[i]: const &), (hb[i]: @))
        i += 1
    var tbk: i4& = (tb.peek_key(): i4&)
    ::printf("B top_key=%d len=%llu\n", tbk[0], tb.len())
    ::printf("B drain:")
    drain_keys(&tb)
    tb.drop()
    var tc: heap
    tc.init(1, 0 - 1, nil, nil)
    var hc[4]: node@
    hc[0] = make(4)
    tc.push("delta", (hc[0]: @))
    hc[1] = make(1)
    tc.push("alpha", (hc[1]: @))
    hc[2] = make(3)
    tc.push("charlie", (hc[2]: @))
    hc[3] = make(2)
    tc.push("bravo", (hc[3]: @))
    ::printf("C len=%llu top_key=%s\n", tc.len(), (tc.peek_key(): char&))
    ::printf("C pop:")
    var topc: char& = (tc.peek_key(): char&)
    ::printf(" %s", topc)
    tc.pop()
    topc = (tc.peek_key(): char&)
    ::printf(" %s\n", topc)
    tc.pop()
    ::printf("C after_pop len=%llu\n", tc.len())
    tc.clear()
    ::printf("C after_clear len=%llu empty=%d\n", tc.len(), tc.is_empty())
    tc.drop()
    return 0
