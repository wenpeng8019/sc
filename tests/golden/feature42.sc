# 由 scc --emit-sc 从 AST 再生成

inc mt.sc

rpc serve: i4, tag: i4, info: i4
    printf("  served tag=%d (%d)\n", tag, info)
    return tag

fnc main: i4
    var q: queue& = default_queue(nil)
    var a1: promise& = async<q, prio:1> serve(1, 1)
    var a2: promise& = async<q, prio:5> serve(2, 5)
    var a3: promise& = async<q, prio:3> serve(3, 3)
    printf("priority order (expect tag 2,3,1):\n")
    q->pull(-1)
    q->pull(-1)
    q->pull(-1)
    a1->wait()
    a2->wait()
    a3->wait()
    a1->drop()
    a2->drop()
    a3->drop()
    q->drop()
    var q2: queue& = default_queue(nil)
    var d1: promise& = async<q2, delay:60> serve(100, 60)
    var d2: promise& = async<q2, delay:20> serve(200, 20)
    var d3: promise& = async<q2, delay:40> serve(300, 40)
    printf("delay order (expect tag 200,300,100):\n")
    q2->pull(-1)
    q2->pull(-1)
    q2->pull(-1)
    d1->wait()
    d2->wait()
    d3->wait()
    d1->drop()
    d2->drop()
    d3->drop()
    q2->drop()
    return 0
