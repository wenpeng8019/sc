# 新特性综合测试：多维数组 / @导出 / inc
inc string.h

@def Color: i1
    Red = 0
    Green

@def Point: {
    x: i4
    y: i4
}

@var total: i4 = 0
let grid[2][3]: i4

@fnc add: i4, a:i4, b:i4
    return a + b

fnc main: i4
    var m[2][3]: i4
    var i: i4 = 0, j: i4 = 0
    for i = 0; i < 2; i++
        for j = 0; j < 3; j++
            m[i][j] = i * 3 + j
    var s: i4 = 0
    for i = 0; i < 2; i++
        for j = 0; j < 3; j++
            s += m[i][j]
    printf("sum = %d\n", s)
    var name[8][16]: u1
    strcpy(name[0], "hi")
    printf("name0 = %s\n", name[0])
    printf("add = %d\n", add(2, 3))
    return 0
