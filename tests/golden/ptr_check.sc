# 由 scc --emit-sc 从 AST 再生成

@def node: {
    v: i4
}

@fnc sum: i4, arr: i4&, n: i4
    var s: i4 = 0
    var i: i4 = 0
    while i < n
        s = (s + arr[i])
        i = (i + 1)
    return s

@fnc deref: i4, p: i4&
    return *p

@fnc main: i4
    var arr[3]: i4 = [10, 20, 30]
    var total: i4 = sum(&arr[0], 3)
    var one: i4 = arr[0]
    var x: node
    x.v = 7
    var p: node& = &x
    var d: i4 = deref(&one)
    return (total + p->v) + d
