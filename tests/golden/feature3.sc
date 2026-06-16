# 由 scc --emit-sc 从 AST 再生成

inc stdio.h

rpc add: i4, a: i4, b: i4
    return a + b

rpc greet: n: i4
    printf("hello rpc x%d\n", n)

rpc strlen2: i4, s&: char
    var n: i4 = 0
    while s[n] != 0
        n++
    return n * 2

@rpc square: i4, x: i4
    return x * x

fnc main: i4
    printf("add(3,4) = %d\n", add(3, 4))
    greet(2)
    printf("strlen2 = %d\n", strlen2("abc"))
    printf("square(9) = %d\n", square(9))
    return 0
