# sagent_dir —— .sagent/ 目录初始化与读写（被 sagent.sc add 内联）
# 目录规范见 OUTLINE.md §4。

inc io.sc

# 写整文件（覆盖创建）。返回 0 成功 / <0 失败。
fnc sa_write_file: i4, path: const char&, text: const char&
    var c: com& = file(path, true, 0, 1)
    if c == nil
        return -1
    var n: u4 = sa_slen(text)
    if n > 0
        var wr: i4 = c->write((text: &), &n)
        if wr < 0
            c->close()
            return -2
    c->close()
    return 0

# 初始化 .sagent/ 骨架。已存在时不覆盖（幂等，只补缺）。
fnc sa_init: i4
    if fs_exists(".sagent") && !fs_is_dir(".sagent")
        print "sagent: .sagent 已存在且不是目录\n"
        return 1

    fs_mkdirs(".sagent/task")
    fs_mkdirs(".sagent/memory")
    fs_mkdirs(".sagent/archive")

    # config.sa 模板（存在则不动，保护用户配置）
    if !fs_exists(".sagent/config.sa")
        var cfg: const char& = "# sagent 配置（格式：[段] + key: value；# 注释）\n\n[llm]\nendpoint: https://api.openai.com/v1/chat/completions\nmodel: gpt-4o-mini\napi_key_env: OPENAI_API_KEY\ntimeout: 120\n\n[loop]\nbudget: 10\n\n[tools]\nallow: scc git curl\n"
        if sa_write_file(".sagent/config.sa", cfg) != 0
            print "sagent: 写 config.sa 失败\n"
            return 1

    # task 三件（存在则不动）
    if !fs_exists(".sagent/task/goal.md")
        sa_write_file(".sagent/task/goal.md", "# 目的与最终验证标准\n\n（待解构：目的 / 术语定义 / 边界 / 可判定的验收标准）\n")
    if !fs_exists(".sagent/task/plan.md")
        sa_write_file(".sagent/task/plan.md", "# 计划（loop 目标队列）\n\n- [ ] （待入队：每项须有可判定标准）\n")
    if !fs_exists(".sagent/task/state.md")
        sa_write_file(".sagent/task/state.md", "# 已发生序列（每 loop 追加一行）\n")

    # memory 四件（存在则不动）
    if !fs_exists(".sagent/memory/structure.md")
        sa_write_file(".sagent/memory/structure.md", "# 结构和关系：空间/概念/意义/理解\n\n（工具可再生部分由 scc --graph/--api 生成，人工只注释）\n")
    if !fs_exists(".sagent/memory/paths.md")
        sa_write_file(".sagent/memory/paths.md", "# 条件和发生：实在/域/范围（路径、分支）\n")
    if !fs_exists(".sagent/memory/facts.md")
        sa_write_file(".sagent/memory/facts.md", "# 前提和习惯：约束/风格/优化/事实\n")
    if !fs_exists(".sagent/memory/history.md")
        sa_write_file(".sagent/memory/history.md", "# 事实和计划：发生序列/存在位置/未来方向/事务排期\n")

    print "sagent: .sagent/ 就绪（config.sa + task/ + memory/ + archive/）\n"
    return 0
