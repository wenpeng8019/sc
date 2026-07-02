# 由 scc --emit-sc 从 AST 再生成

inc adt.sc

def counter: {
    n: i4
    init: fnc
        this->n = 100
    add: fnc: i4, k: i4
        this->n = (this->n + k)
        return this->n
}

fnc cnt_cmp -> list_cmp
    return (a: counter&)->n - (b: counter&)->n

fnc main: i4
    var c: counter
    ::printf("counter: init=%d add(5)=%d\n", c.n, c.add(5))
    var s: string& = string()
    s->append("Hello")
    s->append(", sc!")
    ::printf("s=%s len=%llu\n", s->cstr(), s->len())
    ::printf("find \"sc\"=%lld starts_with(Hello)=%d\n", s->find("sc", 0), s->starts_with("Hello"))
    var part: string& = string()
    s->slice(-3, -1, part)
    ::printf("slice(-3,-1)=%s\n", part->cstr())
    s->upper()
    ::printf("upper=%s\n", s->cstr())
    var l: list
    var c1: counter@ = counter()
    c1->add(30)
    var c2: counter@ = counter()
    c2->add(10)
    var c3: counter@ = counter()
    c3->add(20)
    l.push((c1: @))
    l.push((c2: @))
    l.push((c3: @))
    l.sort(cnt_cmp)
    var i: u8 = 0
    for i = 0; i < l.len(); i++
        ::printf("list[%llu]=%d\n", i, (l.get(i): counter&)->n)
    var lp: list& = &l
    lp->drop()
    part->drop()
    s->drop()
    var hs: string& = string()
    hs->append("on the heap")
    ::printf("heap: %s\n", hs->cstr())
    hs->drop()
    return 0
