# 由 scc --emit-sc 从 AST 再生成

inc mt.sc

def ctx: {
    mu: mutex
    n: i4
}

rpc work: c: ctx&, rounds: i4
    var i: i4 = 0
    for i = 0; i < rounds; i++
        c->mu.lock()
        c->n = (c->n + 1)
        c->mu.unlock()

rpc note: tag: i4
    ::printf("detached note: tag=%d\n", tag)

tls hits: i4 = 0

rpc bump: c: ctx&, rounds: i4
    var i: i4 = 0
    for i = 0; i < rounds; i++
        hits = (hits + 1)
    if hits == rounds
        c->mu.lock()
        c->n = (c->n + 1)
        c->mu.unlock()

fnc next_id: i4
    tls id: i4 = 100
    id++
    return id

def sig: {
    mu: mutex
    cv: cond
    ready: i4
}

rpc ping: s: sig&
    s->mu.lock()
    s->ready = 1
    s->cv.one()
    s->mu.unlock()

def bctx: {
    bar: barrier
    mu: mutex
    arrived: i4
    serial: i4
}

rpc bwork: b: bctx&
    b->mu.lock()
    b->arrived = (b->arrived + 1)
    b->mu.unlock()
    if b->bar.wait()
        b->mu.lock()
        b->serial = (b->serial + 1)
        b->mu.unlock()

fnc main: i4
    var c: ctx
    c.n = 0
    c.mu.init()
    var t1: thread& = nil
    var t2: thread& = nil
    run work(&c, 10000), &t1
    run<stack:262144, prio:5> work(&c, 10000), &t2
    ::printf("t1 id set: %d\n", t1 != nil)
    t1->join()
    t2->join()
    ::printf("threads done: n=%d\n", c.n)
    run note(7)
    ::P_usleep(50000)
    if c.mu.try_lock()
        ::printf("try_lock ok\n")
        c.mu.unlock()
    next_id()
    next_id()
    ::printf("tls id=%d\n", next_id())
    c.n = 0
    var b1: thread& = nil
    var b2: thread& = nil
    run bump(&c, 10000), &b1
    run bump(&c, 20000), &b2
    b1->join()
    b2->join()
    ::printf("tls threads ok: %d\n", c.n)
    c.mu.drop()
    var s: sig
    s.ready = 0
    s.mu.init()
    s.cv.init()
    run ping(&s)
    s.mu.lock()
    while s.ready == 0
        s.cv.wait(&s.mu)
    s.mu.unlock()
    ::printf("cond wait ok: ready=%d\n", s.ready)
    s.mu.lock()
    s.cv.wait(&s.mu, 5000000, 0)
    s.mu.unlock()
    ::printf("cond timeout ok\n")
    s.cv.drop()
    s.mu.drop()
    var c2: ctx
    c2.n = 0
    c2.mu.init()
    var p: pool& = default_pool(4)
    var k: i4 = 0
    for k = 0; k < 8; k++
        run<p> work(&c2, 1000)
    p->join()
    ::printf("pool done: n=%d\n", c2.n)
    run<p> work(&c2, 1000)
    p->drop()
    ::printf("pool drop: n=%d\n", c2.n)
    c2.mu.drop()
    var bc: bctx
    bc.arrived = 0
    bc.serial = 0
    bc.mu.init()
    bc.bar.init(3)
    var bt1: thread& = nil
    var bt2: thread& = nil
    var bt3: thread& = nil
    run bwork(&bc), &bt1
    run bwork(&bc), &bt2
    run bwork(&bc), &bt3
    bt1->join()
    bt2->join()
    bt3->join()
    ::printf("barrier ok: arrived=%d serial=%d\n", bc.arrived, bc.serial)
    bc.bar.drop()
    bc.mu.drop()
    return 0
