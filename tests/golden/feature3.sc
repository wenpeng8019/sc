# 由 scc --emit-sc 从 AST 再生成

rpc add: i4, a: i4, b: i4
    return a + b

rpc greet: n: i4
    printf("hello rpc x%d\n", n)

rpc strlen2: i4, s: char&
    var n: i4 = 0
    while s[n] != 0
        n++
    return n * 2

rpc dot: i4, a[3]: i4, b[3]: i4
    var s: i4 = 0
    var i: i4 = 0
    for i = 0; i < 3; i++
        s = (s + (a[i] * b[i]))
    return s

@rpc square: i4, x: i4
    return x * x

fnc main: i4
    printf("add(3,4) = %d\n", add(3, 4))
    greet(2)
    printf("strlen2 = %d\n", strlen2("abc"))
    var u[3]: i4 = [1, 2, 3]
    var v[3]: i4 = [4, 5, 6]
    printf("dot = %d\n", dot(u, v))
    printf("square(9) = %d\n", square(9))
    return 0
