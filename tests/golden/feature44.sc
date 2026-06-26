# 由 scc --emit-sc 从 AST 再生成

inc mt.sc

rpc add: i4, a: i4, b: i4
    return a + b

rpc kickA: i4, qb: queue&, qa: queue&
    var v: i4 = sync innerB(qa)
    return v

rpc innerB: i4, qa: queue&
    var v: i4 = sync add(10, 20)
    return v

rpc selfish: i4, qs: queue&
    var v: i4 = sync add(7, 8)
    return v

rpc consume1: qq: queue&
    qq->pull(-1)

fnc main: i4
    var qa: queue& = default_queue(nil)
    var qb: queue& = default_queue(nil)
    var ta: thread& = nil
    var tb: thread& = nil
    run consume1(qa), &ta
    run consume1(qb), &tb
    var rc: i4 = sync kickA(qb, qa)
    printf("circular substitution: rc=%d\n", rc)
    ta->join()
    tb->join()
    qa->drop()
    qb->drop()
    var qs: queue& = default_queue(nil)
    var ts: thread& = nil
    run consume1(qs), &ts
    var rs: i4 = sync selfish(qs)
    printf("self substitution: rs=%d\n", rs)
    ts->join()
    qs->drop()
    return 0
