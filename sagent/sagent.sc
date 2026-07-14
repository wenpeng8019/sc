# sagent —— sc agent 命令行工具（sca）入口
# 纲领 OUTLINE.md；一期方案 PLAN.md。
#
# 用法：
#   sca init              初始化 .sagent/ 目录结构
#   sca "消息" [--llm 名]  单次 loop（一期：起手到 LLM 应答）
#
# 运行：scc sagent/sagent.sc -- init
# 构建：scc sagent/sagent.sc --build -o sca

inc sys.sc
inc os.sc
inc io.sc

add src/sagent_dir.sc
add src/config.sc

mix ARGS_S(false, llm, 'l', "llm", "选用 config.sa 的 [llm.<名>] 配置段")

# C 风格 strlen（sc FFI 惯例：不依赖 libc 头解析）。
fnc sa_slen: u4, s: const char&
    var i: u4 = 0
    while s[i] != 0
        i = i + 1
    return i

# 字符串等值比较（数值比较惯例，print 坑规避）。
fnc sa_streq: bool, a: const char&, b: const char&
    var i: u4 = 0
    while a[i] != 0 && b[i] != 0
        if a[i] != b[i]
            return false
        i = i + 1
    return a[i] == b[i]

fnc main: i4, argc: i4, argv: char&&
    ARGS_usage("<init | \"消息\">",
        "示例: $0 init\n      $0 \"hi, llm\" --llm main")

    var pos_count: i4 = ARGS_parse(argc, argv)
    if pos_count < 1
        ARGS_print(argv[0])
        return 1

    # 位置参数已被 ARGS_parse 压到 argv 尾部
    var cmd: char& = argv[argc - pos_count]

    if sa_streq(cmd, "init")
        return sa_init()

    # 非子命令 = 用户消息，走单次 loop（一期分步实现中）
    ::printf("sagent: loop 尚未接通（一期任务 2-6），收到消息: %s\n", cmd)
    return 0
