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
inc src/keys.sc
inc src/llm.sc
inc src/loop.sc

mix ARGS_S(false, llm, 'l', "llm", "选用 config.sa 的 [llm.<名>] 配置段")

fnc main: i4, argc: i4, argv: char&&
    ARGS_usage("<init | next | archive [名] | \"消息\">",
        "示例: $0 init\n      $0 \"hi, llm\" --llm openai\n      $0 next          # 消费 plan.md 队列头（退出码 42=队列空 43=预算尽）\n      $0 archive 重构  # task 归档并重建")

    var pos_count: i4 = ARGS_parse(argc, argv)
    if pos_count < 1
        ARGS_print(argv[0])
        return 1

    # 位置参数已被 ARGS_parse 压到 argv 尾部
    var cmd: char& = argv[argc - pos_count]

    if sa_streq(cmd, "init")
        return sa_init()

    if sa_streq(cmd, "archive")
        # 可选第二位置参数 = 归档名
        var aname: const char& = pos_count >= 2 ? (argv[argc - pos_count + 1]: const char&) : nil
        if sa_archive(aname) != 0
            ::printf("sagent: 归档失败（无 task 或移动失败）\n")
            return 1
        sa_init()                          # 重建空骨架，开启下一 task
        ::printf("sagent: task 已归档，新 task 就绪\n")
        return 0

    # 读配置（next 与消息路径共用）
    var cfg: sa_cfg
    ::memset(&cfg, 0, sizeof(::sa_cfg))
    var lr: i4 = sa_cfg_load(&cfg)
    if lr == 1
        ::printf("sagent: 缺 .sagent/config.sa（先跑 sca init）\n")
        return 1
    if lr > 1
        ::printf("sagent: config.sa 第 %d 行格式错误\n", lr)
        return 1

    if sa_streq(cmd, "next")
        # 预算门槛（退出码协议：42=队列空，43=预算耗尽，其余同 loop）
        var bs: const char& = sa_cfg_get(&cfg, "loop.budget", "10")
        var budget: i4 = 0
        var bi: i4 = 0
        while bs[bi] >= (48: char) && bs[bi] <= (57: char)
            budget = budget * 10 + ((bs[bi]: i4) - 48)
            bi = bi + 1
        if budget > 0 && sa_loop_count() >= budget
            ::printf("sagent: loop 预算耗尽（%d）\n", budget)
            return 43
        var msg: string& = string()
        if sa_plan_next(msg) != 0
            ::printf("sagent: plan 队列空\n")
            msg->drop()
            return 42
        ::printf("sagent: 消费计划项: %s\n", msg->cstr())
        var nrc: i4 = sa_loop_run(&cfg, ARGS_llm, msg->cstr())
        if nrc == 0
            sa_plan_done()                 # 本 loop 验证通过才标记完成
        msg->drop()
        return nrc

    # 非子命令 = 用户消息：单次 loop 全生命周期（OUTLINE §2）
    var rc: i4 = sa_loop_run(&cfg, ARGS_llm, cmd)
    return rc
