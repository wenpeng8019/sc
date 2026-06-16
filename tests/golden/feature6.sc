# 由 scc --emit-sc 从 AST 再生成

inc stdio.h

inc stdlib.h

inc adt.sc

def task: ~ {
    id: i4
}

def node: ~ {
    id: i4
    name[8]: char
    pos: point
    score: f8
}

def point: {
    x: i4
    y: i4
}

fnc dump: tag&: char, l&: chain
    printf("%s:", tag)
    var it&: task = (l->first(): task&)
    while it != nil
        printf(" %d", it->id)
        it = it->next
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
    var r&: task = (f->prev: task&)
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
    var l2: chain
    var n[3]: node
    for i = 0; i < 3; i++
        n[i].id = i
    l2.append(&n[0])
    l2.append(&n[1])
    l2.append(&n[2])
    var it&: node = (l2.first(): node&)
    printf("正向:")
    while it != nil
        printf(" %d", it->id)
        it = it->next
    printf("\n")
    var rear&: node = (l2.last(): node&)
    printf("反向:")
    while rear != nil
        printf(" %d", rear->id)
        rear = rear->prev
    printf("\n")
    return 0
