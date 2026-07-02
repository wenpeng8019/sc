# 由 scc --emit-sc 从 AST 再生成

inc adt.sc

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
        item->prev = this->tail
        item->next = nil
        if this->tail != nil
            this->tail->next = item
        else
            this->head = item
        this->tail = item
        return ok
    remove: fnc: ret, item: tnode&
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

def cnode: ~ {
    v: i4
}

fnc main: i4
    ::printf("闭区间[1,5]:")
    for i in [1, 5]
        ::printf(" %d", i)
    ::printf("\n")
    ::printf("半开[1,5):")
    for i in [1, 5)
        ::printf(" %d", i)
    ::printf("\n")
    ::printf("整数 4:")
    for i in 4
        ::printf(" %d", i)
    ::printf("\n")
    ::printf("范围逆序:")
    for i in [1, 5] revert
        ::printf(" %d", i)
    ::printf("\n")
    ::printf("范围步进2:")
    for i in [0, 10] step 2
        ::printf(" %d", i)
    ::printf("\n")
    var a[5]: i4
    var k: i4
    for k = 0; k < 5; k++
        a[k] = ((k + 1) * 11)
    ::printf("数组:")
    for x in a
        ::printf(" %d", x)
    ::printf("\n")
    ::printf("数组逆序跳1:")
    for x in a revert offset 1
        ::printf(" %d", x)
    ::printf("\n")
    var s: char& = "hello"
    ::printf("字符串:")
    for c in s
        ::printf(" %c", c)
    ::printf("\n")
    ::printf("字符串前3:")
    for c in s num 3
        ::printf(" %c", c)
    ::printf("\n")
    var l: chain
    var cn[4]: cnode
    for k = 0; k < 4; k++
        cn[k].v = ((k + 1) * 100)
        l.append(&cn[k])
    ::printf("链:")
    for it: cnode& in l
        ::printf(" %d", it->v)
    ::printf("\n")
    ::printf("链逆序:")
    for it: cnode& in l revert
        ::printf(" %d", it->v)
    ::printf("\n")
    var lst: slist
    var t[4]: task
    for k = 0; k < 4; k++
        t[k].id = ((k + 1) * 10)
        lst.insert(&t[k], 0)
    ::printf("容器:")
    for it: task& in lst
        ::printf(" %d", it->id)
    ::printf("\n")
    ::printf("容器逆序步2:")
    for it: task& in lst revert step 2
        ::printf(" %d", it->id)
    ::printf("\n")
    ::printf("数组带下标:")
    for x, i in a
        ::printf(" a[%d]=%d", i, x)
    ::printf("\n")
    ::printf("数组逆序带下标:")
    for x, i in a revert
        ::printf(" a[%d]=%d", i, x)
    ::printf("\n")
    ::printf("链带计数:")
    for it: cnode&, i in l
        ::printf(" #%d=%d", i, it->v)
    ::printf("\n")
    var m[2][3]: i4
    var r: i4
    var c: i4
    for r = 0; r < 2; r++
        for c = 0; c < 3; c++
            m[r][c] = ((r * 10) + c)
    ::printf("二维:")
    for v, i, j in m
        ::printf(" m[%d][%d]=%d", i, j, v)
    ::printf("\n")
    ::printf("二维逆序:")
    for v, i, j in m revert
        ::printf(" m[%d][%d]=%d", i, j, v)
    ::printf("\n")
    return 0
