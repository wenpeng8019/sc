# 由 scc --emit-sc 从 AST 再生成

inc adt.sc

@def node: {
    v: i4
    drop: fnc
        printf("drop %d\n", this->v)
}

@fnc make: node@, x: i4
    var p: node@ = node()
    p->v = x
    return p

@fnc dump_str: bool, key: const &, value:*, ctx: &
    printf(" %s=%d", (key: char&), (value: node&)->v)
    return true

@fnc dump_int: bool, key: const &, value:*, ctx: &
    var kp: i4& = (key: i4&)
    printf(" %d=%d", kp[0], (value: node&)->v)
    return true

@fnc main: i4
    var ca: lru
    ca.init(0 - 1, 3)
    var ha[5]: node@
    ha[0] = make(1)
    ca.put("a", (ha[0]: @))
    ha[1] = make(2)
    ca.put("b", (ha[1]: @))
    ha[2] = make(3)
    ca.put("c", (ha[2]: @))
    printf("A len=%llu cap=%llu mru=%s lru=%s\n", ca.len(), ca.cap(), (ca.mru_key(): char&), (ca.lru_key(): char&))
    printf("A peek_a=%d lru_still=%s\n", (ca.peek("a"): node&)->v, (ca.lru_key(): char&))
    printf("A get_a=%d mru=%s lru=%s\n", (ca.get("a"): node&)->v, (ca.mru_key(): char&), (ca.lru_key(): char&))
    ha[3] = make(4)
    ca.put("d", (ha[3]: @))
    printf("A after_put_d: has_b=%d has_d=%d len=%llu\n", ca.has("b"), ca.has("d"), ca.len())
    printf("A each:")
    ca.each(dump_str, nil)
    printf("\n")
    ha[4] = make(99)
    ca.put("a", (ha[4]: @))
    printf("A replace_a=%d len=%llu\n", (ca.get("a"): node&)->v, ca.len())
    printf("A each2:")
    ca.each(dump_str, nil)
    printf("\n")
    ca.drop()
    var cb: lru
    cb.init(4, 0)
    var keys[4]: i4
    keys[0] = 10
    keys[1] = 20
    keys[2] = 30
    keys[3] = 40
    var hb[4]: node@
    var i: i4 = 0
    while i < 4
        hb[i] = make((keys[i] + 1) * 10)
        cb.put((&keys[i]: const &), (hb[i]: @))
        i += 1
    var mk: i4& = (cb.mru_key(): i4&)
    var lk: i4& = (cb.lru_key(): i4&)
    printf("B len=%llu cap=%llu mru=%d lru=%d empty=%d\n", cb.len(), cb.cap(), mk[0], lk[0], cb.is_empty())
    printf("B remove20=%d miss=%d has20=%d len=%llu\n", cb.remove((&keys[1]: const &)), cb.remove((&keys[1]: const &)), cb.has((&keys[1]: const &)), cb.len())
    cb.set_cap(2)
    var lk2: i4& = (cb.lru_key(): i4&)
    printf("B after_set_cap2: cap=%llu len=%llu has10=%d lru=%d\n", cb.cap(), cb.len(), cb.has((&keys[0]: const &)), lk2[0])
    printf("B each:")
    cb.each(dump_int, nil)
    printf("\n")
    cb.drop()
    return 0
