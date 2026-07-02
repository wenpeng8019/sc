# 由 scc --emit-sc 从 AST 再生成

@def node: {
    v: i4
    drop: fnc
        ::printf("drop v=%d\n", this->v)
}

var g: node@

var arr[3]: node@

var grid[2][2]: node@

@fnc main: i4
    g = node()
    g->v = 1
    var i: i4 = 0
    for i = 0; i < 3; i++
        arr[i] = node()
        arr[i]->v = (i * 10)
    grid[0][0] = node()
    grid[0][0]->v = 99
    ::printf("%d %d %d\n", g->v, arr[2]->v, grid[0][0]->v)
    return 0
