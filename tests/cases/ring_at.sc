# SPSC 无锁循环队列 ring 回归用例：覆盖
#   1. 单线程语义（确定性）：FIFO 顺序、is_empty/is_full、满时 push 失败、空时 pop 失败、
#      peek 借用队首、容量向上取 2 的幂、回绕复用槽位；
#   2. 并发 SPSC：一个生产者线程 push 1..N、一个消费者线程 pop N 个，校验计数与校验和
#      （与调度无关，输出确定）——验证 acquire/release 跨线程可见性与无锁正确性。
inc adt.sc
inc mt.sc
inc sys.sc

@def shared: {
    q: ring
    sum: i8       # 消费者累加（单消费者，无需原子）
    got: i4       # 消费者计数
}

@rpc producer: s: shared&, n: i4
    var i: i4 = 1
    for i = 1; i <= n; i++
        while !s->q.push(&i)        # 满则自旋让出
            usleep(0)

@rpc consumer: s: shared&, n: i4
    var v: i4 = 0
    while s->got < n
        if s->q.pop(&v)
            s->sum = s->sum + v
            s->got = s->got + 1
        else
            usleep(0)

@fnc main: i4
    # ---------- 单线程确定性语义 ----------
    var x: i4 = 0
    var r: ring
    r.init(sizeof(x), 3)                       # capacity=3 → 向上取 2 幂 = 4
    ::printf("cap=%llu empty=%d full=%d\n", r.cap(), r.is_empty(), r.is_full())

    x = 10
    r.push(&x)
    x = 20
    r.push(&x)
    x = 30
    r.push(&x)
    x = 40
    r.push(&x)                                  # 填满 4 槽
    ::printf("len=%llu full=%d push_when_full=%d\n", r.len(), r.is_full(), r.push(&x))

    var pk: i4& = r.peek(): i4&                  # 借用队首（不出队）
    ::printf("peek=%d len_after_peek=%llu\n", pk[0], r.len())

    var v: i4 = 0
    while r.pop(&v)                             # FIFO：10 20 30 40
        ::printf("pop=%d\n", v)
    ::printf("drained empty=%d len=%llu pop_when_empty=%d\n",
           r.is_empty(), r.len(), r.pop(&v))

    # 回绕：再填一轮验证槽位复用
    x = 100
    r.push(&x)
    x = 200
    r.push(&x)
    r.pop(&v)
    ::printf("wrap pop=%d len=%llu\n", v, r.len())
    r.drop()

    # ---------- 并发 SPSC 压力 ----------
    var s: shared
    s.sum = 0
    s.got = 0
    s.q.init(sizeof(x), 16)                    # 小容量 16，强制反复满/空交替
    var n: i4 = 50000
    var tp: thread& = nil
    var tc: thread& = nil
    run producer(&s, n), &tp
    run consumer(&s, n), &tc
    tp->join()
    tc->join()
    var expect: i8 = (n: i8) * (n + 1) / 2
    ::printf("concurrent got=%d sum=%lld expect=%lld ok=%d\n",
           s.got, s.sum, expect, s.sum == expect)
    ::printf("final empty=%d cap=%llu\n", s.q.is_empty(), s.q.cap())
    s.q.drop()

    return 0
