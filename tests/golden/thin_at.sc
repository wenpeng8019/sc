# 由 scc --emit-sc 从 AST 再生成

@def node: {
    v: i4
    bump: fnc
        this->v = (this->v + 1)
    drop: fnc
        ::printf("drop %d\n", this->v)
}

@fnc take: i4, p: node*
    p->bump()
    return p->v

@fnc main: i4
    var a: node@ = node()
    a->v = 7
    var t: node* = a
    var c: node@ = t
    var b: node* = node()
    b->v = 40
    var e:@ = b
    var r: i4 = take(b)
    ::printf("a=%d c=%d r=%d\n", a->v, c->v, r)
    t = nil
    return 0
