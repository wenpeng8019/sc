# http —— 通用 HTTP/HTTPS 客户端组件
#
# 定位：templates/.scenv/modules 下的标准扩展组件。这里仅暴露 HTTP
#   客户端模型；当前 native 后端是 vendor 库，后端名称不进入 sc API。
#   API 分为轻量请求入口和可复用客户端句柄：请求方法、URL、请求头、请求体、超时、
#   代理、TLS 校验、执行、响应码、错误和重置。
#
# 响应体由组件复制到 sc string，调用方不需要了解 native 内存所有权。
# 需要流式协议时，调用方使用普通响应数据自行实现协议层；组件不包含
#   SSE/WebSocket 等上层协议语义。

inc os.sc
inc adt.sc
inc io.sc

# native 依赖：同目录的 *_impl.c 按模块约定自动编译；由本目录 build.sh
# 生成的静态库按目标自动匹配变体。
add libcurl.a
add libmbedtls_all.a

# ---- 可复用客户端句柄（高级 API） ----
@fnc http_client_init:: &
@fnc http_client_cleanup:: h: &
@fnc http_client_reset:: h: &
@fnc http_client_set_url:: i4, h: &, url: const char&
@fnc http_client_set_method:: i4, h: &, method: const char&
@fnc http_client_set_header:: i4, h: &, header: const char&
@fnc http_client_set_body:: i4, h: &, body: const char&
@fnc http_client_set_timeout:: i4, h: &, seconds: i4
@fnc http_client_set_connect_timeout:: i4, h: &, seconds: i4
@fnc http_client_set_follow:: i4, h: &, enabled: i4
@fnc http_client_set_max_redirects:: i4, h: &, count: i4
@fnc http_client_set_verify:: i4, h: &, enabled: i4
@fnc http_client_set_ca_file:: i4, h: &, path: const char&
@fnc http_client_set_proxy:: i4, h: &, proxy: const char&
@fnc http_client_set_user_agent:: i4, h: &, agent: const char&
@fnc http_client_set_referer:: i4, h: &, referer: const char&
@fnc http_client_set_cookie:: i4, h: &, cookie: const char&
@fnc http_client_set_accept_encoding:: i4, h: &, encodings: const char&
@fnc http_client_set_http_version:: i4, h: &, version: i4
@fnc http_client_set_verbose:: i4, h: &, enabled: i4
@fnc http_client_perform:: i4, h: &, response: char&&
@fnc http_client_response_code:: i4, h: &
@fnc http_client_error:: const char&, h: &
@fnc http_client_effective_url:: const char&, h: &
@fnc http_client_content_type:: const char&, h: &
@fnc http_client_release:: p: char&

# 一个常用但仍保持通用语义的请求辅助：调用方仍可通过客户端句柄
#   自行设置任意方法、请求头和传输参数。
@fnc http_request: i4, method: const char&, url: const char&, body: const char&, headers: const char&, timeout_s: i4, response: string&
    var h: & = http_client_init()
    if h == nil
        response->clear()
        return -1
    http_client_set_method(h, method)
    http_client_set_url(h, url)
    if headers != nil
        http_client_set_header(h, headers)
    if body != nil
        http_client_set_body(h, body)
    http_client_set_timeout(h, timeout_s)
    var raw: char& = nil
    var code: i4 = http_client_perform(h, &raw)
    if raw != nil
        response->assign(raw)
        http_client_release(raw)
    else
        response->clear()
    http_client_cleanup(h)
    return code

# 轻量入口：普通 GET，使用组件默认超时。
@fnc http_get: i4, url: const char&, response: string&
    return http_request("GET", url, nil, nil, 120, response)

# 轻量入口：HEAD，只取响应头，使用组件默认超时。
@fnc http_head: i4, url: const char&, response: string&
    var h: & = http_client_init()
    if h == nil
        response->clear()
        return -1
    http_client_set_method(h, "HEAD")
    http_client_set_url(h, url)
    http_client_set_timeout(h, 120)
    var raw: char& = nil
    var code: i4 = http_client_perform(h, &raw)
    if raw != nil
        response->assign(raw)
        http_client_release(raw)
    else
        response->clear()
    http_client_cleanup(h)
    return code

# 轻量入口：JSON POST，使用组件默认超时。
@fnc http_post: i4, url: const char&, body: const char&, response: string&
    return http_request("POST", url, body, "Content-Type: application/json", 120, response)
