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
inc adt.sc

# add 顺序即分层：util 最底（无 src 内依赖）← 各功能单元 ← 本入口消费
add src/util.sc
add src/sagent_dir.sc
add src/config.sc
add src/json.sc

mix ARGS_S(false, llm, 'l', "llm", "选用 config.sa 的 [llm.<名>] 配置段")

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
