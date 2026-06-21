# 由 scc --emit-sc 从 AST 再生成

@def node: {
    v: i4
}

@fnc main: i4
    var i: i4 = 0
    loop:
        var n: node@ = node()
        n->v = i
        i = (i + 1)
        if i < 3
            goto loop
    return 0
