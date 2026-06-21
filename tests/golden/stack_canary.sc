# 由 scc --emit-sc 从 AST 再生成

var gtable[6]: i4

var gmat[2][3]: i4

fnc fill_buf
    var tmp[8]: i4
    var i: i4 = 0
    for i = 0; i < 8; i++
        tmp[i] = i
    if tmp[0] == 0
        var inner[4]: char
        inner[0] = 'x'
        printf("inner=%c\n", inner[0])
    printf("tmp7=%d\n", tmp[7])

fnc sum_first: i4
    var data[5]: i4 = {2, 4, 6, 8, 10}
    var s: i4 = 0
    var i: i4 = 0
    for i = 0; i < 5; i++
        s = (s + data[i])
    return s

fnc zero_buf: i4
    var buf[8]: i4
    memset(buf, 0, sizeof(buf))
    return sizeof(buf)

fnc grid_sum: i4
    var grid[3][4]: i4
    var i: i4 = 0
    var j: i4 = 0
    for i = 0; i < 3; i++
        for j = 0; j < 4; j++
            grid[i][j] = ((i * 4) + j)
    return grid[2][3]

fnc use_globals: i4
    var i: i4 = 0
    for i = 0; i < 6; i++
        gtable[i] = (i * i)
    gmat[1][2] = 42
    return gtable[5] + gmat[1][2]

fnc main: i4
    fill_buf()
    printf("sum=%d\n", sum_first())
    printf("bytes=%d\n", zero_buf())
    printf("grid=%d\n", grid_sum())
    printf("glob=%d\n", use_globals())
    return 0
