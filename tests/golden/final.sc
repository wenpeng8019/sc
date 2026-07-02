# 由 scc --emit-sc 从 AST 再生成

@def node: {
    v: i4
    child: node@
}

@fnc pick: i4, n: i4
    final
        ::printf("final A\n")
    if n > 0
        return 1
    return 0

@fnc loopy: i4, n: i4
    var i: i4 = 0
    for i = 0; i < n; i++
        final
            ::printf("iter %d\n", i)
        if i == 1
            continue
        if i == 2
            break
    return 0

@fnc withfat: i4
    var p: node@ = node()
    final
        ::printf("v=%d\n", p->v)
    p->v = 9
    return 0
