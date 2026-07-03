# 由 scc --emit-sc 从 AST 再生成

inc adt.sc

inc mt.sc

inc sys.sc

@def shared: {
    q: ring
    sum: i8
    got: i4
}

@rpc producer: s: shared&, n: i4
    var i: i4 = 1
    for i = 1; i <= n; i++
        while !s->q.push(&i)
            usleep(0)

@rpc consumer: s: shared&, n: i4
    var v: i4 = 0
    while s->got < n
        if s->q.pop(&v)
            s->sum = (s->sum + v)
            s->got = (s->got + 1)
        else
            usleep(0)

@fnc main: i4
    var x: i4 = 0
    var r: ring
    r.init(sizeof(x), 3)
    ::printf("cap=%llu empty=%d full=%d\n", r.cap(), r.is_empty(), r.is_full())
    x = 10
    r.push(&x)
    x = 20
    r.push(&x)
    x = 30
    r.push(&x)
    x = 40
    r.push(&x)
    ::printf("len=%llu full=%d push_when_full=%d\n", r.len(), r.is_full(), r.push(&x))
    var pk: i4& = (r.peek(): i4&)
    ::printf("peek=%d len_after_peek=%llu\n", pk[0], r.len())
    var v: i4 = 0
    while r.pop(&v)
        ::printf("pop=%d\n", v)
    ::printf("drained empty=%d len=%llu pop_when_empty=%d\n", r.is_empty(), r.len(), r.pop(&v))
    x = 100
    r.push(&x)
    x = 200
    r.push(&x)
    r.pop(&v)
    ::printf("wrap pop=%d len=%llu\n", v, r.len())
    r.drop()
    var s: shared
    s.sum = 0
    s.got = 0
    s.q.init(sizeof(x), 16)
    var n: i4 = 50000
    var tp: thread& = nil
    var tc: thread& = nil
    run producer(&s, n), &tp
    run consumer(&s, n), &tc
    tp->join()
    tc->join()
    var expect: i8 = ((n: i8) * (n + 1)) / 2
    ::printf("concurrent got=%d sum=%lld expect=%lld ok=%d\n", s.got, s.sum, expect, s.sum == expect)
    ::printf("final empty=%d cap=%llu\n", s.q.is_empty(), s.q.cap())
    s.q.drop()
    return 0
