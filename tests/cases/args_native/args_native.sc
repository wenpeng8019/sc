# ARGS 原生机制（inc env.sc + mix ARGS_*）+ 根模块导出注入（@@）专项回归
# 覆盖点：
# 1) 参数定义宏在 sc 中原生定义并展开为真实全局声明（参与 AST / 语义 / 生命期）
# 2) 顶层 mix ARGS_* 展开出的 ARGS_DEF_xxx 由「声明即构造」自动登记到 arg_defs
# 3) ARGS_parse 优先采用自注册链：无需手写 &ARGS_DEF_xxx 变参，直接传 nil
# 4) @@ 根模块导出注入：mix 展开出的 @var ARGS_verbose 经 scm_<root>.h 末位注入，
#    子模块 args_native_mod.sc 无需 inc 本根，即可直接访问 ARGS_verbose 属性

@@                                  # 根模块标记：开启「导出注入」（root-prelude）

inc env.sc
inc args_native_mod.sc              # 引入消费子模块（其内部直接引用根的 ARGS_verbose）

mix ARGS_B(false, verbose, 'v', "verbose", "Enable verbose output")
mix ARGS_I(false, count, 'n', "count", "Number of iterations")
mix ARGS_Sv("stdin", input, 'i', "input", "Input source")
mix ARGS_L(false, files, 'f', "files", "Input file list")

fnc main: i4, argc: i4, argv: char&&
    ARGS_usage("<paths...>", "demo: $0 -v -n 3 -f a b")

    # 参数定义已由构造自注册，ARGS_parse 直接采用 arg_defs（变参传 nil 即可）
    var pos_count: i4 = ARGS_parse(argc, argv, nil)

    # 子模块直接读取根经注入可见的 ARGS_verbose（验证 @@ 头文件透传）
    args_report_verbose()

    printf("count=%lld input=%s files=%d pos=%d\n",
        ARGS_count.i64,
        ARGS_input.str,
        ARGS_ls_count(&ARGS_files),
        pos_count)

    return 0
