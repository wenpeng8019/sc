# 栈数组尾哨兵（--check=mem）回归用例
# 覆盖：一维 int/char 栈数组、嵌套作用域退出校验、return 路径校验、无 return 落出校验。
# 默认 emit-c 下不注入哨兵（POSITIVE 快照）；--check=mem 下注入 fill/check（CHECKMEM 快照）。

# 全局栈数组：--check=mem 下超额分配尾哨兵，由 constructor 填充、destructor 退出时校验。
var gtable[6]: i4
var gmat[2][3]: i4

fnc fill_buf
    var tmp[8]: i4
    var i: i4 = 0
    for i = 0; i < 8; i++
        tmp[i] = i
    # 嵌套作用域内的栈数组：退出该 if 体时就地校验尾哨兵
    if tmp[0] == 0
        var inner[4]: char
        inner[0] = 'x'
        printf("inner=%c\n", inner[0])
    printf("tmp7=%d\n", tmp[7])

fnc sum_first: i4
    var data[5]: i4 = [2, 4, 6, 8, 10]
    var s: i4 = 0
    var i: i4 = 0
    for i = 0; i < 5; i++
        s = s + data[i]
    # return 路径：返回前校验 data 尾哨兵
    return s

# sizeof(栈数组) 在 --check=mem 下须回报逻辑大小（原维度×元素），
# 否则 memset(buf, 0, sizeof buf) 会抹掉尾哨兵造成误报。
fnc zero_buf: i4
    var buf[8]: i4
    memset(buf, 0, sizeof(buf))
    return sizeof(buf)

# 多维栈数组：外层维度超额若干「行」覆盖尾哨兵，逻辑大小=各维元素积×元素。
fnc grid_sum: i4
    var grid[3][4]: i4
    var i: i4 = 0
    var j: i4 = 0
    for i = 0; i < 3; i++
        for j = 0; j < 4; j++
            grid[i][j] = i * 4 + j
    return grid[2][3]

# 全局一维/多维栈数组的读写：触达启动填充与退出校验路径。
fnc use_globals: i4
    var i: i4 = 0
    for i = 0; i < 6; i++
        gtable[i] = i * i
    gmat[1][2] = 42
    return gtable[5] + gmat[1][2]

fnc main: i4
    fill_buf()
    printf("sum=%d\n", sum_first())
    printf("bytes=%d\n", zero_buf())
    printf("grid=%d\n", grid_sum())
    printf("glob=%d\n", use_globals())
    return 0
