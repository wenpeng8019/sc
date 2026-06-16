# 由 scc --emit-sc 从 AST 再生成

inc stdio.h

def tnode: {
    next: tnode&
    prev: tnode&
}

def slist: {
    head: tnode&
    tail: tnode&
    init: fnc
        this->head = nil
        this->tail = nil
    insert: fnc: ret, item: tnode&, tag: i4
        if tag != 0
            item->prev = nil
            item->next = this->head
            if this->head != nil
                this->head->prev = item
            else
                this->tail = item
            this->head = item
        else
            item->prev = this->tail
            item->next = nil
            if this->tail != nil
                this->tail->next = item
            else
                this->head = item
            this->tail = item
        return ok
    remove: fnc: ret, item: tnode&
        if item->prev != nil
            item->prev->next = item->next
        else
            this->head = item->next
        if item->next != nil
            item->next->prev = item->prev
        else
            this->tail = item->prev
        return ok
    find: fnc: ret, out: tnode&&, key: i4
        var p: tnode& = this->head
        while p != nil
            var t: task& = (p: task&)
            if t->id != key
                p = p->next
            else
                *out = p
                return ok
        return -1
    first: fnc: tnode&
        return this->head
    last: fnc: tnode&
        return this->tail
    next: fnc: tnode&, item: tnode&
        return item->next
    prev: fnc: tnode&, item: tnode&
        return item->prev
}

def task: <slist, tnode> {
    id: i4
}

fnc main: i4
    var lst: slist
    var a[4]: task
    var i: i4
    for i = 0; i < 4; i++
        a[i].id = ((i + 1) * 10)
    lst.insert(&a[0], 0)
    lst.insert(&a[1], 0)
    lst.insert(&a[2], 0)
    lst.insert(&a[3], 1)
    var it: task& = (lst.first(): task&)
    printf("正向:")
    while it != nil
        printf(" %d", it->id)
        it = (lst.next(it): task&)
    printf("\n")
    var rit: task& = (lst.last(): task&)
    printf("反向:")
    while rit != nil
        printf(" %d", rit->id)
        rit = (lst.prev(rit): task&)
    printf("\n")
    var found: task&
    if lst.find(&found, 20) == ok
        printf("find 20 -> id=%d\n", found->id)
    lst.remove(&a[1])
    var it2: task& = (lst.first(): task&)
    printf("remove 20 后:")
    while it2 != nil
        printf(" %d", it2->id)
        it2 = (lst.next(it2): task&)
    printf("\n")
    var pid: i4& = (base(&a[0]): i4&)
    printf("base(&a[0]) -> id=%d\n", *pid)
    return 0
