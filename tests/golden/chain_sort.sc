# 由 scc --emit-sc 从 AST 再生成

def item: ~ {
    key: i4
    seq: i4
}

@fnc by_key_asc: i4, a: &, b: &
    var x: item& = (a: item&)
    var y: item& = (b: item&)
    return x->key - y->key

@fnc by_key_desc: i4, a: &, b: &
    var x: item& = (a: item&)
    var y: item& = (b: item&)
    return y->key - x->key

fnc dump: tag: char&, l: chain&
    ::printf("%s:", tag)
    var it: item& = (l->first(): item&)
    while it != nil
        ::printf(" %d", it->key)
        it = it->next
    ::printf("\n")

fnc dump_rev: tag: char&, l: chain&
    ::printf("%s:", tag)
    var it: item& = (l->last(): item&)
    while it != nil
        ::printf(" %d", it->key)
        it = it->prev
    ::printf("\n")

fnc dump_stable: tag: char&, l: chain&
    ::printf("%s:", tag)
    var it: item& = (l->first(): item&)
    while it != nil
        ::printf(" %d.%d", it->key, it->seq)
        it = it->next
    ::printf("\n")

@fnc main: i4
    var n[8]: item
    var i: i4
    var keys[8]: i4
    keys[0] = 5
    keys[1] = 2
    keys[2] = 8
    keys[3] = 2
    keys[4] = 9
    keys[5] = 1
    keys[6] = 5
    keys[7] = 2
    for i = 0; i < 8; i++
        n[i].key = keys[i]
        n[i].seq = i
    var l: chain
    for i = 0; i < 8; i++
        l.append(&n[i])
    dump("before  ", &l)
    l.sort(by_key_asc)
    dump("asc     ", &l)
    dump_rev("asc(rev)", &l)
    dump_stable("stable  ", &l)
    l.sort(by_key_desc)
    dump("desc    ", &l)
    dump_rev("desc(rev", &l)
    var e: chain
    e.sort(by_key_asc)
    ::printf("empty head_nil=%d\n", e.first() == nil)
    var s: chain
    var one: item
    one.key = 42
    one.seq = 0
    s.append(&one)
    s.sort(by_key_asc)
    var sf: item& = (s.first(): item&)
    var sl: item& = (s.last(): item&)
    ::printf("single key=%d first==last=%d rear_next_nil=%d\n", sf->key, sf == sl, sf->next == nil)
    dump("sorted2 ", &l)
    l.sort(by_key_asc)
    dump("re-asc  ", &l)
    return 0
