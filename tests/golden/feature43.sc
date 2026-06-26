# 由 scc --emit-sc 从 AST 再生成

inc mt.sc

rpc compute: i4, a: i4, b: i4
    return a + b

rpc consume_n: qq: queue&, n: i4
    var i: i4 = 0
    for i = 0; i < n; i++
        qq->pull(-1)

fnc main: i4
    var q: queue& = default_queue(nil)
    var ct: thread& = nil
    run consume_n(q, 1), &ct
    var st1: i4 = -9
    var r1: i4 = sync<q, timeout:2000> compute(3, 4), &st1
    printf("ok path: r=%d st=%d\n", r1, st1)
    ct->join()
    var st2: i4 = -9
    var r2: i4 = sync<q, timeout:50> compute(10, 20), &st2
    printf("timeout path: r=%d st=%d\n", r2, st2)
    q->drop()
    return 0
