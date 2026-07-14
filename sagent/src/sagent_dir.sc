# sagent_dir —— .sagent/ 目录初始化与读写模块（inc 引用，@ 导出）
# 目录规范见 OUTLINE.md §4。

inc io.sc
inc os.sc
inc adt.sc
inc mem.sc

# 开启新 loop 档案：取下一个编号并建 .sagent/task/loop-NNN/。
# 出参 dir 得到目录路径；返回 loop 编号（>0），失败 <0。
@fnc sa_loop_open: i4, dir: string&
    if !fs_is_dir(".sagent/task")
        return -1
    var n: i4 = 1
    while n < 1000
        dir->clear()
        dir->printf(".sagent/task/loop-%03d", n)
        if !fs_exists(dir->cstr())
            fs_mkdirs(dir->cstr())
            return n
        n = n + 1
    return -2

# loop 档案落一个文本文件（<dir>/<name>）。
@fnc sa_loop_put: i4, dir: string&, name: const char&, text: const char&
    var p: string& = string()
    p->printf("%s/%s", dir->cstr(), name)
    var c: com@1 = file(p->cstr(), true, 0, 1)
    var r: i4 = -1
    if c != nil
        c << text
        r = 0
    p->drop()
    return r

# state.md 追加一行摘要（已发生序列）。
@fnc sa_state_append: i4, line: const char&
    var rc: com@1 = file(".sagent/task/state.md", true, 1, 0)
    var old: char& = nil
    if rc != nil
        var rs: com[0]
        rs = rc
        rc >> rs
        old = (rs.take(): char&)
    var s: string& = string()
    if old != nil
        s->append(old)
        recycle((old: &))
    s->append(line)
    s->append("\n")
    var wc: com@1 = file(".sagent/task/state.md", true, 0, 1)
    var r: i4 = -1
    if wc != nil
        wc << s
        r = 0
    s->drop()
    return r

# ---------- M2：plan 队列原语 ----------

# 取 plan.md 首个未完成项（"- [ ] xxx" 的 xxx）。命中返回 0；队列空 -1。
@fnc sa_plan_next: i4, msg: string&
    var rc: com@1 = file(".sagent/task/plan.md", true, 1, 0)
    var t: char& = nil
    if rc != nil
        var rs: com[0]
        rs = rc
        rc >> rs
        t = (rs.take(): char&)
    if t == nil
        return -1
    var i: i4 = 0
    while t[i] != 0
        var b: i4 = i
        while t[i] != 0 && t[i] != (10: char)
            i = i + 1
        # 匹配行首 "- [ ] "
        if t[b] == (45: char) && t[b+1] == (32: char) && t[b+2] == (91: char) && t[b+3] == (32: char) && t[b+4] == (93: char) && t[b+5] == (32: char)
            msg->clear()
            msg->append_n((t + b + 6: const char&), (i - b - 6: u8))
            recycle((t: &))
            return 0
        if t[i] != 0
            i = i + 1
    recycle((t: &))
    return -1

# 把 plan.md 中首个未完成项标记完成（[ ] → [x]）。返回 0 成功。
@fnc sa_plan_done: i4
    var rc: com@1 = file(".sagent/task/plan.md", true, 1, 0)
    var t: char& = nil
    if rc != nil
        var rs: com[0]
        rs = rc
        rc >> rs
        t = (rs.take(): char&)
    if t == nil
        return -1
    var i: i4 = 0
    while t[i] != 0
        var b: i4 = i
        while t[i] != 0 && t[i] != (10: char)
            i = i + 1
        if t[b] == (45: char) && t[b+1] == (32: char) && t[b+2] == (91: char) && t[b+3] == (32: char) && t[b+4] == (93: char)
            t[b+3] = (120: char)          # 'x'
            var wc: com@1 = file(".sagent/task/plan.md", true, 0, 1)
            var r: i4 = -1
            if wc != nil
                wc << t
                r = 0
            recycle((t: &))
            return r
        if t[i] != 0
            i = i + 1
    recycle((t: &))
    return -1

# 现有 loop 档案数（预算判断用）。
@fnc sa_loop_count: i4
    var n: i4 = 0
    var p: string& = string()
    while n < 1000
        p->clear()
        p->printf(".sagent/task/loop-%03d", n + 1)
        if !fs_exists(p->cstr())
            break
        n = n + 1
    p->drop()
    return n

# task 归档（闭包封存）：task/ 整体移入 archive/NNN-<名>/，重建空骨架。
@fnc sa_archive: i4, name: const char&
    if !fs_is_dir(".sagent/task")
        return -1
    var n: i4 = 1
    var dst: string& = string()
    while n < 1000
        dst->clear()
        dst->printf(".sagent/archive/%03d-%s", n, name != nil && name[0] != 0 ? name : "task")
        if !fs_exists(dst->cstr())
            break
        n = n + 1
    fs_mkdirs(".sagent/archive")
    var r: i4 = fs_rename(".sagent/task", dst->cstr())
    dst->drop()
    if r != 0
        return -2
    return 0

# 初始化 .sagent/ 骨架。已存在时不覆盖（幂等，只补缺）。
@fnc sa_init: i4
    if fs_exists(".sagent") && !fs_is_dir(".sagent")
        print "sagent: .sagent 已存在且不是目录\n"
        return 1

    fs_mkdirs(".sagent/task")
    fs_mkdirs(".sagent/memory")
    fs_mkdirs(".sagent/archive")

    # config.sa 模板（存在则不动，保护用户配置）
    if !fs_exists(".sagent/config.sa")
        var cfg: const char& = "# sagent 配置（格式：[段] + key: value；# 注释）\n# 多 provider：[llm] 为默认；[llm.<名>] 为备选段（--llm <名> 选用，缺键兜底到 [llm]）\n\n[llm]\nendpoint: https://api.deepseek.com/v1/chat/completions\nmodel: deepseek-chat\napi_key_env: DEEPSEEK_API_KEY\ntimeout: 120\n\n[llm.openai]\nendpoint: https://api.openai.com/v1/chat/completions\nmodel: gpt-4o-mini\napi_key_env: OPENAI_API_KEY\n\n[loop]\nbudget: 10\n# verify: 验证命令（如 scc x.sc --test）；commit: on 则每 loop 自动提交\ncommit: off\n\n[tools]\nallow: scc git curl ls cat echo test\n"
        var c: com@1 = file(".sagent/config.sa", true, 0, 1)
        if c == nil
            print "sagent: 写 config.sa 失败\n"
            return 1
        c << cfg

    # task 三件（存在则不动）
    if !fs_exists(".sagent/task/goal.md")
        var c: com@1 = file(".sagent/task/goal.md", true, 0, 1)
        if c != nil
            c << "# 目的与最终验证标准\n\n（待解构：目的 / 术语定义 / 边界 / 可判定的验收标准）\n"
    if !fs_exists(".sagent/task/plan.md")
        var c: com@1 = file(".sagent/task/plan.md", true, 0, 1)
        if c != nil
            c << "# 计划（loop 目标队列）\n\n- [ ] （待入队：每项须有可判定标准）\n"
    if !fs_exists(".sagent/task/state.md")
        var c: com@1 = file(".sagent/task/state.md", true, 0, 1)
        if c != nil
            c << "# 已发生序列（每 loop 追加一行）\n"

    # memory 四件（存在则不动）
    if !fs_exists(".sagent/memory/structure.md")
        var c: com@1 = file(".sagent/memory/structure.md", true, 0, 1)
        if c != nil
            c << "# 结构和关系：空间/概念/意义/理解\n\n（工具可再生部分由 scc --graph/--api 生成，人工只注释）\n"
    if !fs_exists(".sagent/memory/paths.md")
        var c: com@1 = file(".sagent/memory/paths.md", true, 0, 1)
        if c != nil
            c << "# 条件和发生：实在/域/范围（路径、分支）\n"
    if !fs_exists(".sagent/memory/facts.md")
        var c: com@1 = file(".sagent/memory/facts.md", true, 0, 1)
        if c != nil
            c << "# 前提和习惯：约束/风格/优化/事实\n"
    if !fs_exists(".sagent/memory/history.md")
        var c: com@1 = file(".sagent/memory/history.md", true, 0, 1)
        if c != nil
            c << "# 事实和计划：发生序列/存在位置/未来方向/事务排期\n"

    # prompts 两件（可手编，loop 自动加载；存在则不动）
    fs_mkdirs(".sagent/prompts")
    if !fs_exists(".sagent/prompts/loop.md")
        var c: com@1 = file(".sagent/prompts/loop.md", true, 0, 1)
        if c != nil
            c << "你是 sagent 的执行体，在一次 loop 中工作。规则：\n1. 若需执行命令，输出 ```sh 代码块（每行一条命令，只允许白名单工具）；\n2. 代码块外的文字是你的推理与说明，会归档但不执行；\n3. 目标完成或无需动作时不输出代码块；\n4. 修改文件用受控写入：printf/cat 重定向亦须在白名单内；\n5. 不要在应答中输出复盘（陷阱/事实）格式——复盘另有专门环节。\n"
    if !fs_exists(".sagent/prompts/review.md")
        var c: com@1 = file(".sagent/prompts/review.md", true, 0, 1)
        if c != nil
            c << "对本 loop 复盘。只输出两节 markdown：## 陷阱（失败路径及原因，无则写 无）与 ## 事实（本 loop 确认的新事实）。不要输出代码块。\n"

    print "sagent: .sagent/ 就绪（config.sa + prompts/ + task/ + memory/ + archive/）\n"
    return 0
