# 由 scc --emit-sc 从 AST 再生成

fnc show_suffix:
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

fnc demo_sugar:
    ! classify(-2)
        printf("fail branch, $=%d\n", $)
    ! classify(0)
        printf("never here\n")
    > classify(7)
        printf("warn branch, $=%d\n", $)
    < classify(-2)
        printf("err branch, $=%d\n", $)
    !! classify(0)
    printf("after assert, $=%d\n", $)

fnc check_pos: ret, n: i4
    if n < 0
        return -1
    return ok

fnc do_step: ret, n: i4
    ! check_pos(n) ?
        printf("do_step: check_pos(%d) failed, $=%d, propagate up\n", n, $)
    printf("do_step: ok n=%d\n", n)
    return ok

fnc run_pipeline: ret
    ! do_step(5) ?
    ! do_step(-1) ?
    printf("run_pipeline: never reached\n")
    return ok

fnc main: i4
    show_suffix()
    demo_sugar()
    var r: ret = run_pipeline()
    printf("pipeline result = %d\n", r)
    return 0
