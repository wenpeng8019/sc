# sagent —— sc agent 命令行工具（sca）入口
# 纲领 OUTLINE.md；一期方案 PLAN.md。
#
# 用法：
#   sca init              初始化 .sagent/ 目录结构
#   sca "消息" [--llm 名]  单次 loop（一期：起手到 LLM 应答）
#
# 运行：scc sagent/sagent.sc -- init
# 构建：scc sagent/sagent.sc --build -o sca
@@

inc sys.sc
inc os.sc
inc io.sc
inc adt.sc
inc mem.sc

inc src/repo.sc
inc json.sc
inc http.sc
inc src/keys.sc
inc src/llm.sc
inc src/loop.sc

@def cfg: {
    keys[64][96]: char          # "段.key"
    vals[64][512]: char
    count: i4
    err_line: i4                # 首个坏行行号（0=无错）
}

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

    if ::strcmp(cmd, "init") == 0
        return repo_init()

    if ::strcmp(cmd, "archive") == 0
        # 可选第二位置参数 = 归档名
        var aname: const char& = pos_count >= 2 ? (argv[argc - pos_count + 1]: const char&) : nil
        if repo_archive(aname) != 0
            print "sagent: 归档失败（无 task 或移动失败）\n"
            return 1
        repo_init()                        # 重建空骨架，开启下一 task
        print "sagent: task 已归档，新 task 就绪\n"
        return 0

    # 读配置（next 与消息路径共用）
    var cfg: cfg
    ::memset(&cfg, 0, sizeof(::cfg))
    var lr: i4 = cfg_load(&cfg)
    if lr == 1
        print "sagent: 缺 .sagent/config.sa（先跑 sca init）\n"
        return 1
    if lr > 1
        print "sagent: config.sa 第 ", lr, " 行格式错误\n"
        return 1

    if ::strcmp(cmd, "next") == 0
        # 预算门槛（退出码协议：42=队列空，43=预算耗尽，其余同 loop）
        var bs: const char& = cfg_get(&cfg, "loop.budget", "10")
        var budget: i4 = 0
        var bi: i4 = 0
        while bs[bi] >= (48: char) && bs[bi] <= (57: char)
            budget = budget * 10 + ((bs[bi]: i4) - 48)
            bi = bi + 1
        if budget > 0 && repo_loop_count() >= budget
            print "sagent: loop 预算耗尽（", budget, "）\n"
            return 43
        var msg: string@1 = string()
        if repo_plan_next(msg) != 0
            print "sagent: plan 队列空\n"
            return 42
        print "sagent: 消费计划项: ", msg->cstr(), "\n"
        var nrc: i4 = loop_run(&cfg, ARGS_llm, msg->cstr())
        if nrc == 0
            repo_plan_done()               # 本 loop 验证通过才标记完成
        return nrc

    # 非子命令 = 用户消息：单次 loop 全生命周期（OUTLINE §2）
    var rc: i4 = loop_run(&cfg, ARGS_llm, cmd)
    return rc

# 行内截段：跳过前导空白。返回新下标。
fnc cfg_skip_ws: i4, s: const char&, i: i4
    var j: i4 = i
    while s[j] == (32: char) || s[j] == (9: char)
        j = j + 1
    return j

# 去尾空白：返回截止下标（开区间）。
fnc cfg_trim_end: i4, s: const char&, from: i4, to: i4
    var j: i4 = to
    while j > from && (s[j - 1] == (32: char) || s[j - 1] == (9: char) || s[j - 1] == (13: char))
        j = j - 1
    return j

# 解析一整块 .sa 文本进 cfg。返回 0 成功 / >0 首个坏行行号。
@fnc cfg_parse: i4, cfg: cfg&, text: const char&
    cfg->count = 0
    cfg->err_line = 0
    var sect[64]: char
    sect[0] = 0
    var i: i4 = 0
    var line: i4 = 0
    while text[i] != 0
        line = line + 1
        # 行首→行尾
        var b: i4 = i
        while text[i] != 0 && text[i] != (10: char)
            i = i + 1
        var e: i4 = i
        if text[i] != 0
            i = i + 1                       # 吃掉换行
        b = cfg_skip_ws(text, b)
        e = cfg_trim_end(text, b, e)
        if e <= b || text[b] == (35: char)  # 空行 / # 注释
            continue
        if text[b] == (91: char)            # '[' 段头
            var ce: i4 = b + 1
            while ce < e && text[ce] != (93: char)
                ce = ce + 1
            if ce >= e                      # 无 ']'
                cfg->err_line = line
                return line
            var k: i4 = 0
            var p: i4 = b + 1
            while p < ce && k < 63
                sect[k] = text[p]
                k = k + 1
                p = p + 1
            sect[k] = 0
            continue
        # key: value
        var col: i4 = b
        while col < e && text[col] != (58: char)   # ':'
            col = col + 1
        if col >= e                          # 无冒号
            cfg->err_line = line
            return line
        var ke: i4 = cfg_trim_end(text, b, col)
        if ke <= b || cfg->count >= 64
            cfg->err_line = line
            return line
        # 组合 "段.key"
        var kk: i4 = 0
        var q: i4 = 0
        while sect[q] != 0 && kk < 90
            cfg->keys[cfg->count][kk] = sect[q]
            kk = kk + 1
            q = q + 1
        if kk > 0
            cfg->keys[cfg->count][kk] = (46: char)   # '.'
            kk = kk + 1
        q = b
        while q < ke && kk < 95
            cfg->keys[cfg->count][kk] = text[q]
            kk = kk + 1
            q = q + 1
        cfg->keys[cfg->count][kk] = 0
        # 值
        var vb: i4 = cfg_skip_ws(text, col + 1)
        var vv: i4 = 0
        while vb < e && vv < 511
            cfg->vals[cfg->count][vv] = text[vb]
            vv = vv + 1
            vb = vb + 1
        cfg->vals[cfg->count][vv] = 0
        cfg->count = cfg->count + 1
    return 0

# 取值：key 形如 "llm.model"。命中返回值指针，未命中返回 dflt。
@fnc cfg_get: const char&, cfg: cfg&, key: const char&, dflt: const char&
    var n: i4 = 0
    while n < cfg->count
        if ::strcmp((cfg->keys[n]: const char&), key) == 0
            return (cfg->vals[n]: const char&)
        n = n + 1
    return dflt

# 载入 .sagent/config.sa。返回 0 成功；1 文件缺失；>1 坏行行号。
@fnc cfg_load: i4, cfg: cfg&
    var rc: com@1 = file(".sagent/config.sa", true, 1, 0)
    var text: char& = nil
    if rc != nil
        var rs: com[0]
        rs = rc
        rc >> rs
        text = (rs.take(): char&)
    if text == nil
        return 1
    var r: i4 = cfg_parse(cfg, text)
    recycle((text: &))
    return r

tst "sa 解析：段+键值+注释+空行"
    var cfg: cfg
    var t: const char& = "# 注释\n[llm]\nmodel: gpt-4o-mini\nendpoint: https://x/v1\n\n[loop]\nbudget: 10\n"
    assert cfg_parse(&cfg, t) == 0
    assert cfg.count == 3
    assert ::strcmp(cfg_get(&cfg, "llm.model", "?"), "gpt-4o-mini") == 0
    assert ::strcmp(cfg_get(&cfg, "llm.endpoint", "?"), "https://x/v1") == 0
    assert ::strcmp(cfg_get(&cfg, "loop.budget", "?"), "10") == 0

tst "sa 解析：默认值与空白容忍"
    var cfg: cfg
    var t: const char& = "[llm]\n  model  :   abc  \n"
    assert cfg_parse(&cfg, t) == 0
    assert ::strcmp(cfg_get(&cfg, "llm.model", "?"), "abc") == 0
    assert ::strcmp(cfg_get(&cfg, "llm.miss", "dflt"), "dflt") == 0

tst "sa 解析：坏行报行号"
    var cfg: cfg
    var t: const char& = "[llm]\nmodel gpt\n"
    assert cfg_parse(&cfg, t) == 2
    assert cfg.err_line == 2

tst "sa 解析：无段裸键"
    var cfg: cfg
    var t: const char& = "top: 1\n[s]\nk: v\n"
    assert cfg_parse(&cfg, t) == 0
    assert ::strcmp(cfg_get(&cfg, "top", "?"), "1") == 0
    assert ::strcmp(cfg_get(&cfg, "s.k", "?"), "v") == 0
