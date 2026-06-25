# 由 scc --emit-sc 从 AST 再生成

inc mt.sc

def acc: {
    sum: i4
    cnt: i4
}

rpc add: a: acc&, v: i4
    a->sum = (a->sum + v)
    a->cnt = (a->cnt + 1)

rpc tag: a: acc&, label: char&, v: i4
    printf("  [%s] += %d\n", label, v)
    a->sum = (a->sum + v)
    a->cnt = (a->cnt + 1)

fnc main: i4
    var a: acc
    a.sum = 0
    a.cnt = 0
    var q: queue& = default_queue(main)
    q << add(&a, 10)
    (q << add(&a, 20)) << add(&a, 30)
    q << tag(&a, "boost", 40)
    var n: i4 = 0
    while q->pull(0) > 0
        n = (n + 1)
    printf("queue drained: msgs=%d sum=%d cnt=%d\n", n, a.sum, a.cnt)
    q->drop()
    var q2: queue& = default_queue(nil)
    q2 << add(&a, 5)
    q2 << add(&a, 7)
    var m: i4 = 0
    while q2->pull(0) > 0
        m = (m + 1)
    printf("deferred queue: msgs=%d sum=%d cnt=%d\n", m, a.sum, a.cnt)
    q2->drop()
    return 0
