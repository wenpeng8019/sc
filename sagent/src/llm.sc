# llm —— chat completions 请求组装/响应提取模块（inc 引用，@ 导出；可独立 --test）
# OpenAI 兼容 /chat/completions，非流式（一期）。见 PLAN.md §4 任务 5。

inc adt.sc
inc util.sc
inc config.sc
inc json.sc
inc http.sc

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

# 取段内键值：键 = "llm.<name>" 或 "llm.<段>.<name>"。
fnc sa_llm_cfg: const char&, cfg: sa_cfg&, sect: const char&, name: const char&, dflt: const char&
    var k: string& = string()
    if sect == nil || sect[0] == 0
        k->printf("llm.%s", name)
    else
        k->printf("llm.%s.%s", sect, name)
    var v: const char& = sa_cfg_get(cfg, k->cstr(), dflt)
    k->drop()
    return v

# 单次请求。cfg 取 [llm]（或 --llm 指定的 [llm.<段>]）；answer 出参。
# 返回 0 成功；非 0 失败（原因已打印）。
@fnc sa_llm_request: i4, cfg: sa_cfg&, sect: const char&, sys_msg: const char&, user_msg: const char&, answer: string&
    var endpoint: const char& = sa_llm_cfg(cfg, sect, "endpoint", nil)
    var model: const char& = sa_llm_cfg(cfg, sect, "model", nil)
    if endpoint == nil || model == nil
        ::printf("sagent: config.sa 缺 endpoint/model\n")
        return 1

    # 密钥：api_key_env 存环境变量名，运行时取值
    var bearer: const char& = nil
    var key_env: const char& = sa_llm_cfg(cfg, sect, "api_key_env", nil)
    if key_env != nil && key_env[0] != 0
        bearer = (::getenv(key_env): const char&)
        if bearer == nil
            ::printf("sagent: 环境变量 %s 未设置（api_key_env）\n", key_env)
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
        return 3
    if code != 200
        var em: string& = string()
        if sa_json_get_str(resp->cstr(), "message", em) == 0
            ::printf("sagent: HTTP %d: %s\n", code, em->cstr())
        else
            ::printf("sagent: HTTP %d: %s\n", code, resp->cstr())
        em->drop()
        resp->drop()
        return 4
    if sa_json_get_str(resp->cstr(), "content", answer) != 0
        ::printf("sagent: 响应缺 content: %s\n", resp->cstr())
        resp->drop()
        return 5
    resp->drop()
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
