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
    var grid[2][2]: node@
    var r: i4 = 0
    for r = 0; r < 2; r++
        var c: i4 = 0
        for c = 0; c < 2; c++
            grid[r][c] = node()
            grid[r][c]->v = ((r * 10) + c)
    printf("%d %d %d %d\n", grid[0][0]->v, grid[0][1]->v, grid[1][0]->v, grid[1][1]->v)
    return 0
