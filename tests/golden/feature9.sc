# 由 scc --emit-sc 从 AST 再生成

inc stdio.h

inc stdlib.h

inc adt.sc

def task: ~ {
    id: i4
}

fnc dump: tag&: char, l&: chain
    printf("%s:", tag)
    var it&: task = (l->first(): task&)
    while it != nil
        printf(" %d", it->id)
        it = (next(it): task&)
    printf("\n")

fnc main: i4
    var l: chain
    var t[6]: task
    var i: i4
    for i = 0; i < 6; i++
        t[i].id = i
    l.append(&t[2])
    l.append(&t[3])
    l.push(&t[1])
    dump("append/push", &l)
    l.before(&t[1], &t[0])
    l.after(&t[3], &t[4])
    dump("before/after", &l)
    var f&: task = (l.first(): task&)
    var b&: task = (l.last(): task&)
    var r&: task = (prev(f): task&)
    printf("first=%d last=%d rear=%d\n", f->id, b->id, r->id)
    l.remove(&t[2])
    var p&: task = (l.pop(): task&)
    printf("pop=%d\n", p->id)
    dump("remove/pop", &l)
    l.revert()
    dump("revert", &l)
    var seg: chain
    l.cut(&t[3], &t[1], &seg)
    dump("cut-out", &seg)
    dump("cut-rest", &l)
    seg.append_to(&l)
    dump("append_to", &l)
    printf("seg empty=%d\n", seg.first() == nil)
    l.cut(&t[3], &t[1], &seg)
    seg.push_to(&l)
    dump("push_to", &l)
    var h&: task = task()
    h->id = 9
    l.append(h)
    dump("heap", &l)
    l.remove(h)
    free((h: void&))
    return 0
