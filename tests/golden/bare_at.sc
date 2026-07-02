# 由 scc --emit-sc 从 AST 再生成

@def node: {
    v: i4
    bump: fnc
        this->v = (this->v + 1)
    drop: fnc
        ::printf("drop %d\n", this->v)
}

@fnc take: i4, p:@
    (p: node@)->bump()
    return (p: node@)->v

@fnc main: i4
    var d:@ = node()
    (d: node@)->v = 7
    var t: node@ = node()
    t->v = 40
    var e:@ = t
    var r: i4 = take((t: @))
    ::printf("d=%d e=%d r=%d\n", (d: node@)->v, (e: node@)->v, r)
    var f:@ = nil
    f = d
    ::printf("f=%d\n", (f: node@)->v)
    return 0
