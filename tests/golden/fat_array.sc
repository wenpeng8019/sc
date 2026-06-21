# 由 scc --emit-sc 从 AST 再生成

@def node: {
    v: i4
    child: node@
}

@fnc main: i4
    var arr[3]: node@
    var i: i4 = 0
    for i = 0; i < 3; i++
        arr[i] = node()
        arr[i]->v = (i * 10)
    arr[0]->child = node()
    arr[0]->child->v = 99
    var pick: node@ = arr[1]
    printf("%d %d %d\n", arr[0]->v, pick->v, arr[0]->child->v)
    arr[0]->child = nil
    return 0
