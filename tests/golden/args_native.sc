# 由 scc --emit-sc 从 AST 再生成

@@

inc env.sc

inc args_native_mod.sc

mix ARGS_B(false, verbose, 'v', "verbose", "Enable verbose output")

mix ARGS_I(false, count, 'n', "count", "Number of iterations")

mix ARGS_Sv("stdin", input, 'i', "input", "Input source")

mix ARGS_L(false, files, 'f', "files", "Input file list")

fnc main: i4, argc: i4, argv: char&&
    ARGS_usage("<paths...>", "demo: $0 -v -n 3 -f a b")
    var pos_count: i4 = ARGS_parse(argc, argv)
    args_report_verbose()
    printf("count=%lld input=%s files=%d pos=%d\n", ARGS_count, ARGS_input, ARGS_ls_count(ARGS_files), pos_count)
    return 0
