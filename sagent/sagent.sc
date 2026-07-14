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
# src/ 全部为 inc 模块（@ 导出，符号对编译器/插件一致可见；各自可独立 --test）
inc src/util.sc
inc src/sagent_dir.sc
inc src/config.sc
inc src/json.sc
inc src/http.sc
inc src/llm.sc

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

    # 非子命令 = 用户消息：读配置 → llm request → 打印应答（任务 5 通路）
    var cfg: sa_cfg
    ::memset(&cfg, 0, sizeof(::sa_cfg))
    var lr: i4 = sa_cfg_load(&cfg)
    if lr == 1
        ::printf("sagent: 缺 .sagent/config.sa（先跑 sca init）\n")
        return 1
    if lr > 1
        ::printf("sagent: config.sa 第 %d 行格式错误\n", lr)
        return 1

    var answer: string& = string()
    var rc: i4 = sa_llm_request(&cfg, ARGS_llm, nil, cmd, answer)

    # loop 档案（OUTLINE §4：context 快照 + 原始响应 + 应答 + state 追加）
    var dir: string& = string()
    var loop_no: i4 = sa_loop_open(dir)
    if loop_no > 0
        var ctx: string& = string()
        ctx->printf("# loop-%03d 初始上下文\n\n## 用户消息\n\n%s\n", loop_no, cmd)
        sa_loop_put(dir, "context.md", ctx->cstr())
        ctx->drop()
        var raw: char& = sa_read_file(".sagent/tmp/resp.json")
        if raw != nil
            sa_loop_put(dir, "response.json", raw)
            ::free((raw: &))
        if rc == 0
            sa_loop_put(dir, "answer.md", answer->cstr())
        var st: string& = string()
        st->printf("- loop-%03d: %s（rc=%d）", loop_no, cmd, rc)
        sa_state_append(st->cstr())
        st->drop()
    dir->drop()

    if rc == 0
        ::printf("%s\n", answer->cstr())
    answer->drop()
    return rc
