# 由 scc --emit-sc 从 AST 再生成

inc mt.sc

rpc compute: i4, a: i4, b: i4
    return a + b

rpc consume_n: qq: queue&, n: i4
    var i: i4 = 0
    for i = 0; i < n; i++
        qq->pull(-1)

fnc main: i4
    var p: pool& = default_pool(2)
    var q: queue& = default_queue(p)
    var r1: i4 = sync<q> compute(3, 4)
    var r2: i4 = sync<q> compute(100, 23)
    printf("pool sync: r1=%d r2=%d\n", r1, r2)
    q->drop()
    p->drop()
    var q2: queue& = default_queue(nil)
    var ct: thread& = nil
    run consume_n(q2, 2), &ct
    var s1: i4 = sync<q2> compute(10, 20)
    var s2: i4 = sync<q2> compute(5, 6)
    printf("thread sync: s1=%d s2=%d\n", s1, s2)
    ct->join()
    q2->drop()
    return 0
