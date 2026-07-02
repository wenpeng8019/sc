# 由 scc --emit-sc 从 AST 再生成

def handler: {
    tag: i4
    fnc op: i4, x: i4
}

fnc dbl: i4, _this: handler&, x: i4
    return (x * 2) + _this->tag

fnc neg: i4, _this: handler&, x: i4
    return (0 - x) - _this->tag

fnc main: i4
    var a: handler
    a.tag = 100
    a.op = dbl
    var b: handler
    b.tag = 1
    b.op = neg
    ::printf("a.op(5) = %d\n", a.op(5))
    ::printf("b.op(5) = %d\n", b.op(5))
    var p: handler& = &a
    ::printf("p->op(7) = %d\n", p->op(7))
    var c: handler
    c.tag = 0
    if c.op == nil
        ::printf("c.op is nil\n")
    c.tag = 10
    c.op = dbl
    ::printf("c.op(5) = %d\n", c.op(5))
    return 0
