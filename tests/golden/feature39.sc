# 由 scc --emit-sc 从 AST 再生成

inc mt.sc

def acc: {
    mu: mutex
    sum: i4
    cnt: i4
}

rpc add: a: acc&, v: i4
    a->mu.lock()
    a->sum = (a->sum + v)
    a->cnt = (a->cnt + 1)
    a->mu.unlock()

fnc main: i4
    var a: acc
    a.sum = 0
    a.cnt = 0
    a.mu.init()
    var p: pool& = default_pool(4)
    var q: queue& = default_queue(p)
    var i: i4 = 0
    for i = 1; i <= 100; i++
        q << add(&a, i)
    p->join()
    ::printf("pool queue: sum=%d cnt=%d\n", a.sum, a.cnt)
    q->drop()
    p->drop()
    return 0
