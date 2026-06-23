# ARGS 原生机制（inc env.sc + mix ARGS_*）专项回归
# 覆盖点：
# 1) 参数定义宏在 sc 中原生定义并可展开
# 2) 宏展开出的符号（ARGS_xxx + ARGS_DEF_xxx）由语义层自动登记，无需手写认领
# 3) ARGS_parse / ARGS_usage / ARGS_print / ARGS_ls_count 可正常参与生成

inc env.sc
inc stdio.h

mix ARGS_B(false, verbose, 'v', "verbose", "Enable verbose output")
mix ARGS_I(false, count, 'n', "count", "Number of iterations")
mix ARGS_Sv("stdin", input, 'i', "input", "Input source")
mix ARGS_L(false, files, 'f', "files", "Input file list")

fnc main: i4, argc: i4, argv: char&&
    ARGS_usage("<paths...>", "demo: $0 -v -n 3 -f a b")

    var pos_count: i4 = ARGS_parse(argc, argv,
        &ARGS_DEF_verbose,
        &ARGS_DEF_count,
        &ARGS_DEF_input,
        &ARGS_DEF_files,
        nil)

    if ARGS_verbose.i64
        printf("v=1\n")

    printf("count=%lld input=%s files=%d pos=%d\n",
        ARGS_count.i64,
        ARGS_input.str,
        ARGS_ls_count(&ARGS_files),
        pos_count)

    return 0
