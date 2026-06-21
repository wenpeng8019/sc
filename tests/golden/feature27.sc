# 由 scc --emit-sc 从 AST 再生成

inc stdio.h

fnc show_suffix: void
    var a: = 5b
    var b: = 300w
    var c: = 7ub
    var d: = 100uw
    var e: = 5u
    var f: = 9000000000l
    printf("suffix: %d %d %u %u %u %lld\n", a, b, c, d, e, f)

fnc classify: ret, n: i4
    if n < 0
        return -1
    if n == 0
        return 0
    return 1

fnc demo_sugar: void
    ! classify(0)
        printf("ok branch, $=%d\n", $)
    > classify(7)
        printf("warn branch, $=%d\n", $)
    < classify(-2)
        printf("err branch, $=%d\n", $)
    !! classify(0)
    printf("after assert, $=%d\n", $)

fnc main: i4
    show_suffix()
    demo_sugar()
    return 0
