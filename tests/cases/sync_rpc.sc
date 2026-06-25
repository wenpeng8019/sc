# sync 同步驱动 rpc（无目标 = 当前线程直接执行，替代裸 rpc 调用）。
# 覆盖：有返回值取结果、无返回值作语句、指针参数、数组参数、@导出 rpc。
rpc add: i4, a: i4, b: i4
    return a + b

rpc greet: n: i4
    printf("hi %d\n", n)

rpc strlen2: i4, s: char&
    var n: i4 = 0
    while s[n] != 0
        n++
    return n

rpc dot: i4, a[3]: i4, b[3]: i4
    var s: i4 = 0
    var i: i4 = 0
    for i = 0; i < 3; i++
        s = s + a[i] * b[i]
    return s

@rpc square: i4, x: i4
    return x * x

fnc main: i4
    var r: i4 = sync add(3, 4)
    printf("add = %d\n", r)
    sync greet(7)
    printf("len = %d\n", sync strlen2("abcd"))
    var u[3]: i4 = [1, 2, 3]
    var v[3]: i4 = [4, 5, 6]
    printf("dot = %d\n", sync dot(u, v))
    printf("sq = %d\n", sync square(9))
    return 0
