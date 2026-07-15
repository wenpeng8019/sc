# llm —— chat completions 请求组装/响应提取模块（inc 引用，@ 导出；可独立 --test）
# OpenAI 兼容 /chat/completions，非流式（一期）。见 PLAN.md §4 任务 5。

inc adt.sc
inc json.sc
inc http.sc
inc keys.sc

# 组装请求体：{"model":..., "messages":[{system},{user}][,"stream":true]}
@fnc llm_body: out: string&, model: const char&, sys_msg: const char&, user_msg: const char&, stream: bool
    out->append("{\"model\":")
    json_str(out, model)
    out->append(",\"messages\":[")
    if sys_msg != nil && sys_msg[0] != 0
        out->append("{\"role\":\"system\",\"content\":")
        json_str(out, sys_msg)
        out->append("},")
    out->append("{\"role\":\"user\",\"content\":")
    json_str(out, user_msg)
    out->append("}]")
    if stream
        out->append(",\"stream\":true")
    out->append("}")

# 取段内键值：键 = "llm.<段>.<name>"，未命中兜底到基段 "llm.<name>"
# （provider 段只需写差异项，timeout 等公共项写在 [llm]，同 gptme/codex 范式）。
@fnc llm_cfg: const char&, cfg: cfg&, sect: const char&, name: const char&, dflt: const char&
    var k: string& = string()
    if sect != nil && sect[0] != 0
        k->printf("llm.%s.%s", sect, name)
        var v: const char& = cfg_get(cfg, k->cstr(), nil)
        if v != nil
            k->drop()
            return v
        k->clear()
    k->printf("llm.%s", name)
    var v2: const char& = cfg_get(cfg, k->cstr(), dflt)
    k->drop()
    return v2

# 流式响应执行：使用通用 HTTP 请求取得响应体，再按应用层协议解析 data 块。
# HTTP 组件不包含 SSE 语义；这里仅处理 OpenAI 兼容响应格式。
fnc llm_stream_exec: i4, endpoint: const char&, bearer: const char&, body: const char&, timeout_s: i4, answer: string&
    var hdr: string& = string()
    hdr->append("Content-Type: application/json\n")
    if bearer != nil && bearer[0] != 0
        hdr->printf("Authorization: Bearer %s\n", bearer)
    var resp: string& = string()
    var code: i4 = http_request("POST", endpoint, body, hdr->cstr(), timeout_s, resp)
    hdr->drop()
    if code < 0
        ::printf("sagent: HTTP 通路失败（%d）\n", code)
        resp->drop()
        return 3
    var errbuf: string& = string()
    var lnb: string& = string()
    var all: char& = resp->cstr()
    var pos: i4 = 0
    while all[pos] != 0
        lnb->clear()
        while all[pos] != 0 && all[pos] != (10: char)
            var one[2]: char
            one[0] = all[pos]
            one[1] = 0
            lnb->append(one)
            pos = pos + 1
        if all[pos] == (10: char)
            pos = pos + 1
        var buf: char& = lnb->cstr()
        # 只处理 "data: " 行；其它行（错误 JSON/注释）收入 errbuf
        if buf[0] == (100: char) && buf[1] == (97: char) && buf[2] == (116: char) && buf[3] == (97: char) && buf[4] == (58: char)
            var p: i4 = 5
            while buf[p] == (32: char)
                p = p + 1
            if buf[p] == (91: char)               # "[DONE]"
                continue
            var chunk: string& = string()
            if json_get_str((buf + p: const char&), "content", chunk) == 0 && chunk->len() > 0
                ::printf("%s", chunk->cstr())
                ::fflush(::stdout)
                answer->append(chunk->cstr())
            chunk->drop()
        else if buf[0] != 0
            errbuf->append(buf)
            errbuf->append("\n")
    lnb->drop()
    if answer->len() == 0
        var em: string& = string()
        if errbuf->len() > 0 && json_get_str(errbuf->cstr(), "message", em) == 0
            ::printf("sagent: SSE 错误: %s\n", em->cstr())
        else
            ::printf("sagent: 流式响应无内容（HTTP %d）%s\n", code, errbuf->cstr())
        em->drop()
        errbuf->drop()
        return 4
    ::printf("\n")
    errbuf->drop()
    return 0

# 单次请求（统一入口）。stream=true 时 SSE 流式输出；cfg 取 [llm]（或 --llm 段）。
# 返回 0 成功；非 0 失败（原因已打印）。
@fnc llm_request_ex: i4, cfg: cfg&, sect: const char&, sys_msg: const char&, user_msg: const char&, answer: string&, stream: bool
    var endpoint: const char& = llm_cfg(cfg, sect, "endpoint", nil)
    var model: const char& = llm_cfg(cfg, sect, "model", nil)
    if endpoint == nil || model == nil
        ::printf("sagent: config.sa 缺 endpoint/model\n")
        return 1

    # 密钥三级解析：env → ~/.sagent/keys（0600 用户级）→ 交互输入（关回显）
    var bearer: const char& = nil
    var kbuf: string& = string()          # 文件/交互来源的密钥缓冲（活到请求后）
    var asked: bool = false               # 本次为交互输入（成功后询问保存）
    var key_env: const char& = llm_cfg(cfg, sect, "api_key_env", nil)
    if key_env != nil && key_env[0] != 0
        bearer = (::getenv(key_env): const char&)
        if bearer == nil
            if keys_get(key_env, kbuf) == 0
                if kbuf->equals("!")       # "不再提示"标记
                    ::printf("sagent: %s 未设置且已选择不再提示（export 或编辑 ~/.sagent/keys 恢复）\n", key_env)
                    kbuf->drop()
                    return 2
                bearer = (kbuf->cstr(): const char&)
            else if ::isatty(0) != 0
                var pr: string& = string()
                pr->printf("sagent: 输入 %s（回显已关）: ", key_env)
                if sa_prompt_line(pr->cstr(), true, kbuf) != 0 || kbuf->len() == 0
                    pr->drop()
                    kbuf->drop()
                    ::printf("sagent: 未输入密钥\n")
                    return 2
                pr->drop()
                bearer = (kbuf->cstr(): const char&)
                asked = true
            else
                ::printf("sagent: 环境变量 %s 未设置（api_key_env；非交互终端不可输入）\n", key_env)
                kbuf->drop()
                return 2

    # 超时（秒，十进制）
    var tos: const char& = llm_cfg(cfg, sect, "timeout", "120")
    var timeout_s: i4 = 0
    var ti: i4 = 0
    while tos[ti] >= (48: char) && tos[ti] <= (57: char)
        timeout_s = timeout_s * 10 + ((tos[ti]: i4) - 48)
        ti = ti + 1
    if timeout_s <= 0
        timeout_s = 120

    # 组装请求 → POST（流式/非流式）→ 提取
    var body: string& = string()
    llm_body(body, model, sys_msg, user_msg, stream)
    if stream
        ::system("rm -f .sagent/tmp/resp.json")   # 防旧响应被误归档
        var src: i4 = llm_stream_exec(endpoint, bearer, body->cstr(), timeout_s, answer)
        body->drop()
        if src == 0 && asked
            var sans: string& = string()
            if sa_prompt_line("sagent: 密钥有效。保存到 ~/.sagent/keys？[y=保存 / n=仅本次 / x=不再提示]: ", false, sans) == 0
                if sans->equals("y") || sans->equals("Y")
                    keys_put(key_env, kbuf->cstr())
                else if sans->equals("x") || sans->equals("X")
                    keys_put(key_env, "!")
            sans->drop()
        kbuf->drop()
        return src
    var resp: string& = string()
    var hdr: string& = string()
    hdr->append("Content-Type: application/json\n")
    if bearer != nil && bearer[0] != 0
        hdr->printf("Authorization: Bearer %s\n", bearer)
    var code: i4 = http_request("POST", endpoint, body->cstr(), hdr->cstr(), timeout_s, resp)
    hdr->drop()
    body->drop()

    if code < 0
        ::printf("sagent: http 通路失败（%d）\n", code)
        resp->drop()
        kbuf->drop()
        return 3
    if code != 200
        var em: string& = string()
        if json_get_str(resp->cstr(), "message", em) == 0
            ::printf("sagent: HTTP %d: %s\n", code, em->cstr())
        else
            ::printf("sagent: HTTP %d: %s\n", code, resp->cstr())
        em->drop()
        resp->drop()
        kbuf->drop()
        return 4

    # 交互输入的密钥验证成功（200）：询问保存 / 不保存 / 不再提示
    if asked
        var ans: string& = string()
        if sa_prompt_line("sagent: 密钥有效。保存到 ~/.sagent/keys？[y=保存 / n=仅本次 / x=不再提示]: ", false, ans) == 0
            if ans->equals("y") || ans->equals("Y")
                if keys_put(key_env, kbuf->cstr()) == 0
                    ::fprintf(::stderr, "sagent: 已保存（0600）\n")
            else if ans->equals("x") || ans->equals("X")
                keys_put(key_env, "!")
                ::fprintf(::stderr, "sagent: 已记不再提示（编辑 ~/.sagent/keys 可恢复）\n")
        ans->drop()

    if json_get_str(resp->cstr(), "content", answer) != 0
        ::printf("sagent: 响应缺 content: %s\n", resp->cstr())
        resp->drop()
        kbuf->drop()
        return 5
    resp->drop()
    kbuf->drop()
    return 0

# 非流式便捷入口（既有调用方兼容）。
@fnc llm_request: i4, cfg: cfg&, sect: const char&, sys_msg: const char&, user_msg: const char&, answer: string&
    var r: i4 = llm_request_ex(cfg, sect, sys_msg, user_msg, answer, false)
    return r

tst "llm 请求体组装：含 system 与转义"
    var b: string& = string()
    llm_body(b, "gpt-4o-mini", "你是助手", "说 \"hi\"", false)
    assert b->equals("{\"model\":\"gpt-4o-mini\",\"messages\":[{\"role\":\"system\",\"content\":\"你是助手\"},{\"role\":\"user\",\"content\":\"说 \\\"hi\\\"\"}]}")
    b->drop()

tst "llm 请求体组装：无 system + 流式标志"
    var b: string& = string()
    llm_body(b, "m", nil, "hi", true)
    assert b->equals("{\"model\":\"m\",\"messages\":[{\"role\":\"user\",\"content\":\"hi\"}],\"stream\":true}")
    b->drop()
