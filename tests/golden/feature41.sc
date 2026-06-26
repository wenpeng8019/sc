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
    var f1: promise& = async<q> compute(3, 4)
    var f2: promise& = async<q> compute(100, 23)
    var r1: i4 = (f1->wait(): i4)
    var r2: i4 = (f2->wait(): i4)
    printf("pool async: r1=%d r2=%d\n", r1, r2)
    f1->drop()
    f2->drop()
    q->drop()
    p->drop()
    var q2: queue& = default_queue(nil)
    var ct: thread& = nil
    run consume_n(q2, 2), &ct
    var g1: promise& = async<q2> compute(10, 20)
    var g2: promise& = async<q2> compute(5, 6)
    var s1: i4 = (g1->wait(): i4)
    var s2: i4 = (g2->wait(): i4)
    printf("thread async: s1=%d s2=%d\n", s1, s2)
    g1->drop()
    g2->drop()
    ct->join()
    q2->drop()
    return 0
