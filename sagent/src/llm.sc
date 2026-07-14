# llm —— chat completions 请求组装/响应提取模块（inc 引用，@ 导出；可独立 --test）
# OpenAI 兼容 /chat/completions，非流式（一期）。见 PLAN.md §4 任务 5。

inc adt.sc
inc util.sc
inc config.sc
inc json.sc
inc http.sc
inc keys.sc

# 组装请求体：{"model":..., "messages":[{system},{user}]}
@fnc sa_llm_body: out: string&, model: const char&, sys_msg: const char&, user_msg: const char&
    out->append("{\"model\":")
    sa_json_str(out, model)
    out->append(",\"messages\":[")
    if sys_msg != nil && sys_msg[0] != 0
        out->append("{\"role\":\"system\",\"content\":")
        sa_json_str(out, sys_msg)
        out->append("},")
    out->append("{\"role\":\"user\",\"content\":")
    sa_json_str(out, user_msg)
    out->append("}]}")

# 取段内键值：键 = "llm.<段>.<name>"，未命中兜底到基段 "llm.<name>"
# （provider 段只需写差异项，timeout 等公共项写在 [llm]，同 gptme/codex 范式）。
fnc sa_llm_cfg: const char&, cfg: sa_cfg&, sect: const char&, name: const char&, dflt: const char&
    var k: string& = string()
    if sect != nil && sect[0] != 0
        k->printf("llm.%s.%s", sect, name)
        var v: const char& = sa_cfg_get(cfg, k->cstr(), nil)
        if v != nil
            k->drop()
            return v
        k->clear()
    k->printf("llm.%s", name)
    var v2: const char& = sa_cfg_get(cfg, k->cstr(), dflt)
    k->drop()
    return v2

# 单次请求。cfg 取 [llm]（或 --llm 指定的 [llm.<段>]）；answer 出参。
# 返回 0 成功；非 0 失败（原因已打印）。
@fnc sa_llm_request: i4, cfg: sa_cfg&, sect: const char&, sys_msg: const char&, user_msg: const char&, answer: string&
    var endpoint: const char& = sa_llm_cfg(cfg, sect, "endpoint", nil)
    var model: const char& = sa_llm_cfg(cfg, sect, "model", nil)
    if endpoint == nil || model == nil
        ::printf("sagent: config.sa 缺 endpoint/model\n")
        return 1

    # 密钥三级解析：env → ~/.sagent/keys（0600 用户级）→ 交互输入（关回显）
    var bearer: const char& = nil
    var kbuf: string& = string()          # 文件/交互来源的密钥缓冲（活到请求后）
    var asked: bool = false               # 本次为交互输入（成功后询问保存）
    var key_env: const char& = sa_llm_cfg(cfg, sect, "api_key_env", nil)
    if key_env != nil && key_env[0] != 0
        bearer = (::getenv(key_env): const char&)
        if bearer == nil
            if sa_keys_get(key_env, kbuf) == 0
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
    var tos: const char& = sa_llm_cfg(cfg, sect, "timeout", "120")
    var timeout_s: i4 = 0
    var ti: i4 = 0
    while tos[ti] >= (48: char) && tos[ti] <= (57: char)
        timeout_s = timeout_s * 10 + ((tos[ti]: i4) - 48)
        ti = ti + 1
    if timeout_s <= 0
        timeout_s = 120

    # 组装请求 → POST → 提取
    var body: string& = string()
    sa_llm_body(body, model, sys_msg, user_msg)
    var resp: string& = string()
    var code: i4 = sa_http_post(endpoint, bearer, body->cstr(), timeout_s, resp)
    body->drop()

    if code < 0
        ::printf("sagent: http 通路失败（%d）\n", code)
        resp->drop()
        kbuf->drop()
        return 3
    if code != 200
        var em: string& = string()
        if sa_json_get_str(resp->cstr(), "message", em) == 0
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
                if sa_keys_put(key_env, kbuf->cstr()) == 0
                    ::fprintf(::stderr, "sagent: 已保存（0600）\n")
            else if ans->equals("x") || ans->equals("X")
                sa_keys_put(key_env, "!")
                ::fprintf(::stderr, "sagent: 已记不再提示（编辑 ~/.sagent/keys 可恢复）\n")
        ans->drop()

    if sa_json_get_str(resp->cstr(), "content", answer) != 0
        ::printf("sagent: 响应缺 content: %s\n", resp->cstr())
        resp->drop()
        kbuf->drop()
        return 5
    resp->drop()
    kbuf->drop()
    return 0

tst "llm 请求体组装：含 system 与转义"
    var b: string& = string()
    sa_llm_body(b, "gpt-4o-mini", "你是助手", "说 \"hi\"")
    assert b->equals("{\"model\":\"gpt-4o-mini\",\"messages\":[{\"role\":\"system\",\"content\":\"你是助手\"},{\"role\":\"user\",\"content\":\"说 \\\"hi\\\"\"}]}")
    b->drop()

tst "llm 请求体组装：无 system"
    var b: string& = string()
    sa_llm_body(b, "m", nil, "hi")
    assert b->equals("{\"model\":\"m\",\"messages\":[{\"role\":\"user\",\"content\":\"hi\"}]}")
    b->drop()
