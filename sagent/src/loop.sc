# loop —— 单次 loop 全生命周期编排模块（inc 引用，@ 导出；可独立 --test）
# OUTLINE §2：构造上下文 → 调 LLM → 执行工具调用 → 验证 → 复盘写回 → git commit。
# 二期任务 7-11，见 PLAN.md §4.2。

inc io.sc
inc os.sc
inc adt.sc
inc util.sc
inc sagent_dir.sc
inc config.sc
inc json.sc
inc llm.sc

# ---------- 任务 7：loop 协议与上下文构造 ----------

# 动作输出协议（system prompt）。
fnc sa_loop_protocol: out: string&
    out->append("你是 sagent 的执行体，在一次 loop 中工作。规则：\n")
    out->append("1. 若需执行命令，输出 ```sh 代码块（每行一条命令，只允许白名单工具）；\n")
    out->append("2. 代码块外的文字是你的推理与说明，会归档但不执行；\n")
    out->append("3. 目标完成或无需动作时不输出代码块；\n")
    out->append("4. 修改文件用受控写入：printf/cat 重定向亦须在白名单内。\n")

# 追加一节选材（文件存在才追加）：## <标题>\n<内容截尾 max 字节>
fnc sa_ctx_section: ctx: string&, title: const char&, path: const char&, max: i4
    var t: char& = sa_read_file(path)
    if t == nil
        return
    var n: i4 = (sa_slen(t): i4)
    var b: i4 = 0
    if n > max
        b = n - max                       # 取尾部（最近的信息更相关）
    ctx->printf("## %s\n\n", title)
    ctx->append((t + b: const char&))
    ctx->append("\n\n")
    ::free((t: &))

# 构造本 loop 初始上下文（OUTLINE §5 选材 1/3：task 三件 + 上一 loop 复盘 + 消息）。
@fnc sa_ctx_build: ctx: string&, loop_no: i4, user_msg: const char&
    ctx->printf("# loop-%03d 初始上下文\n\n", loop_no)
    sa_ctx_section(ctx, "目的（goal）", ".sagent/task/goal.md", 4096)
    sa_ctx_section(ctx, "计划（plan）", ".sagent/task/plan.md", 4096)
    sa_ctx_section(ctx, "已发生（state 尾部）", ".sagent/task/state.md", 2048)
    if loop_no > 1
        var prev: string& = string()
        prev->printf(".sagent/task/loop-%03d/review.md", loop_no - 1)
        sa_ctx_section(ctx, "上一 loop 复盘", prev->cstr(), 4096)
        prev->drop()
    ctx->printf("## 用户消息\n\n%s\n", user_msg)

# ---------- 任务 8：动作解析与受控执行 ----------

# 白名单校验：line 的首个词是否在 allow（空格分隔表）中。
@fnc sa_tool_allowed: bool, allow: const char&, line: const char&
    var b: i4 = 0
    while line[b] == (32: char) || line[b] == (9: char)
        b = b + 1
    var e: i4 = b
    while line[e] != 0 && line[e] != (32: char) && line[e] != (9: char)
        e = e + 1
    if e == b
        return true                        # 空行
    var i: i4 = 0
    while allow[i] != 0
        # allow 中每个词与 line[b,e) 比对
        var j: i4 = i
        while allow[j] != 0 && allow[j] != (32: char)
            j = j + 1
        if j - i == e - b
            var k: i4 = 0
            var hit: bool = true
            while k < e - b
                if allow[i + k] != line[b + k]
                    hit = false
                    break
                k = k + 1
            if hit
                return true
        i = j
        while allow[i] == (32: char)
            i = i + 1
    return false

# 提取应答中第 idx 个 ```sh 块（0 起）。命中返回 0 并填 out；无则 -1。
@fnc sa_actions_extract: i4, answer: const char&, idx: i4, out: string&
    var i: i4 = 0
    var cnt: i4 = 0
    while answer[i] != 0
        # 找 "```sh"
        if answer[i] == (96: char) && answer[i+1] == (96: char) && answer[i+2] == (96: char) && answer[i+3] == (115: char) && answer[i+4] == (104: char)
            var b: i4 = i + 5
            while answer[b] != 0 && answer[b] != (10: char)
                b = b + 1
            if answer[b] == 0
                return -1
            b = b + 1
            # 找闭合 ```
            var e: i4 = b
            while answer[e] != 0
                if answer[e] == (10: char) && answer[e+1] == (96: char) && answer[e+2] == (96: char) && answer[e+3] == (96: char)
                    break
                e = e + 1
            if answer[e] == 0
                return -1
            if cnt == idx
                out->clear()
                out->append_n((answer + b: const char&), (e - b: u8))
                out->append("\n")
                return 0
            cnt = cnt + 1
            i = e + 4
            continue
        i = i + 1
    return -1

# 执行一个动作块：逐行白名单校验（违例整块拒执行），执行并捕获输出。
# log 追加记录。返回块的 rc（拒执行 = -100）。
@fnc sa_actions_run: i4, block: const char&, allow: const char&, log: string&
    # 逐行校验
    var i: i4 = 0
    while block[i] != 0
        var b: i4 = i
        while block[i] != 0 && block[i] != (10: char)
            i = i + 1
        var line: string& = string()
        line->append_n((block + b: const char&), (i - b: u8))
        if !sa_tool_allowed(allow, line->cstr())
            log->printf("### 拒执行（白名单外）\n\n```sh\n%s\n```\n\n", line->cstr())
            line->drop()
            return -100
        line->drop()
        if block[i] != 0
            i = i + 1
    # 落盘执行
    fs_mkdirs(".sagent/tmp")
    if sa_write_file(".sagent/tmp/act.sh", block) != 0
        return -101
    var rc: i4 = ::system("sh .sagent/tmp/act.sh > .sagent/tmp/act_out.txt 2>&1")
    var outp: char& = sa_read_file(".sagent/tmp/act_out.txt")
    log->printf("### 动作（rc=%d）\n\n```sh\n%s```\n\n输出：\n\n```\n%s```\n\n",
        rc, block, outp != nil ? (outp: const char&) : "")
    if outp != nil
        ::free((outp: &))
    return rc

# ---------- 任务 9：验证步 ----------

# 执行 config [loop] verify 命令（未配置返回 0 并注记）。log 追加记录。
@fnc sa_verify_run: i4, cfg: sa_cfg&, log: string&
    var vcmd: const char& = sa_cfg_get(cfg, "loop.verify", nil)
    if vcmd == nil || vcmd[0] == 0
        log->append("### 验证\n\n（未配置 loop.verify，跳过）\n\n")
        return 0
    var full: string& = string()
    full->printf("%s > .sagent/tmp/verify_out.txt 2>&1", vcmd)
    var rc: i4 = ::system(full->cstr())
    full->drop()
    var outp: char& = sa_read_file(".sagent/tmp/verify_out.txt")
    log->printf("### 验证（%s，rc=%d）\n\n```\n%s```\n\n",
        vcmd, rc, outp != nil ? (outp: const char&) : "")
    if outp != nil
        ::free((outp: &))
    return rc

# ---------- 任务 10-11：复盘写回 + git commit（编排主体） ----------

# 单次 loop 全生命周期。返回 0 = 本 loop 收敛（验证通过）。
@fnc sa_loop_run: i4, cfg: sa_cfg&, sect: const char&, user_msg: const char&
    var dir: string& = string()
    var loop_no: i4 = sa_loop_open(dir)
    if loop_no < 0
        ::printf("sagent: 无法开启 loop 档案（先跑 sca init）\n")
        dir->drop()
        return 1

    # 1) 构造上下文并归档
    var ctx: string& = string()
    sa_ctx_build(ctx, loop_no, user_msg)
    sa_loop_put(dir, "context.md", ctx->cstr())

    # 2) 调 LLM（协议 prompt + 上下文）
    var proto: string& = string()
    sa_loop_protocol(proto)
    var answer: string& = string()
    var rc: i4 = sa_llm_request(cfg, sect, proto->cstr(), ctx->cstr(), answer)
    proto->drop()
    ctx->drop()
    if rc != 0
        dir->drop()
        answer->drop()
        return rc
    sa_loop_put(dir, "answer.md", answer->cstr())
    var raw: char& = sa_read_file(".sagent/tmp/resp.json")
    if raw != nil
        sa_loop_put(dir, "response.json", raw)
        ::free((raw: &))

    # 3) 执行动作块（白名单受控）
    var allow: const char& = sa_cfg_get(cfg, "tools.allow", "")
    var log: string& = string()
    var k: i4 = 0
    var acted: i4 = 0
    while k < 16
        var block: string& = string()
        if sa_actions_extract(answer->cstr(), k, block) != 0
            block->drop()
            break
        sa_actions_run(block->cstr(), allow, log)
        block->drop()
        acted = acted + 1
        k = k + 1
    if acted == 0
        log->append("### 动作\n\n（应答无动作块）\n\n")

    # 4) 验证
    var vrc: i4 = sa_verify_run(cfg, log)
    sa_loop_put(dir, "actions.md", log->cstr())

    # 5) 复盘（第二次 LLM 调用：动作+输出+验证 → 陷阱/事实）
    var rproto: const char& = "对本 loop 复盘。只输出两节 markdown：## 陷阱（失败路径及原因，无则写 无）与 ## 事实（本 loop 确认的新事实）。不要输出代码块。"
    var review: string& = string()
    if sa_llm_request(cfg, sect, rproto, log->cstr(), review) == 0
        sa_loop_put(dir, "review.md", review->cstr())
    review->drop()
    log->drop()

    # 6) state 追加 + git commit（代码 + .sagent 同一提交）
    var st: string& = string()
    st->printf("- loop-%03d: %s（动作 %d 块，验证 rc=%d）", loop_no, user_msg, acted, vrc)
    sa_state_append(st->cstr())
    st->drop()
    var cmt: const char& = sa_cfg_get(cfg, "loop.commit", "off")
    if sa_streq(cmt, "on")
        var g: string& = string()
        g->printf("git add -A > /dev/null 2>&1 && git commit -q -m 'sca loop-%03d: %s'", loop_no, user_msg)
        ::system(g->cstr())
        g->drop()

    ::printf("%s\n", answer->cstr())
    ::printf("sagent: loop-%03d 完成（动作 %d 块，验证 rc=%d）\n", loop_no, acted, vrc)
    answer->drop()
    dir->drop()
    return vrc == 0 ? 0 : 10

tst "动作提取：单块与多块"
    var a: const char& = "先看\n```sh\nls -1\n```\n再来\n```sh\ngit status\n```\n完"
    var b: string& = string()
    assert sa_actions_extract(a, 0, b) == 0
    assert b->equals("ls -1\n")
    assert sa_actions_extract(a, 1, b) == 0
    assert b->equals("git status\n")
    assert sa_actions_extract(a, 2, b) == -1
    b->drop()

tst "白名单：首词匹配"
    assert sa_tool_allowed("scc git curl", "git status")
    assert sa_tool_allowed("scc git curl", "  scc a.sc --test")
    assert !sa_tool_allowed("scc git curl", "rm -rf /")
    assert sa_tool_allowed("scc git curl", "")

tst "受控执行：白名单外整块拒执行"
    var log: string& = string()
    assert sa_actions_run("echo hi\nrm -rf x\n", "echo", log) == -100
    log->drop()
