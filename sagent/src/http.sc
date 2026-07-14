# http —— HTTPS 通路模块（libcurl vendor 实现；inc 引用，@ 导出）
# C 实现在 src/http_curl.c（sagent.sc `add` 编译），libcurl 静态链接
# （vendor/curl/build.sh 产物，TLS=mbedtls）。回退备用见 src/http_sh.sc。
# 密钥只在进程内存（Authorization 头），不落任何临时文件。

inc os.sc
inc adt.sc
inc io.sc

add http_curl.c                  # libcurl shim（链接自描述见同目录 .sc 段配置）

# ---- C shim externs（src/http_curl.c）----
@fnc sa_curl_post:: i4, url: const char&, bearer: const char&, body: const char&, timeout_s: i4, resp: char&&
@fnc sa_curl_free:: p: char&
@fnc sa_curl_sse_open:: &, url: const char&, bearer: const char&, body: const char&, timeout_s: i4
@fnc sa_curl_sse_line:: i4, p: &, line: char&&
@fnc sa_curl_sse_close:: i4, p: &

# 发 HTTPS POST（JSON）。返回 HTTP 状态码（>0）；传输失败 <0。
# 响应体填入 resp；同时落 .sagent/tmp/resp.json（loop 原始响应归档契约）。
@fnc http_post: i4, url: const char&, auth_bearer: const char&, body: const char&, timeout_s: i4, resp: string&
    var raw: char& = nil
    var code: i4 = sa_curl_post(url, auth_bearer, body, timeout_s, &raw)
    if raw != nil
        resp->assign(raw)
        fs_mkdirs(".sagent/tmp")
        var wc: com@1 = file(".sagent/tmp/resp.json", true, 0, 1)
        if wc != nil
            wc << raw
        sa_curl_free(raw)
    else
        resp->clear()
    return code

# 打开 SSE POST 流。返回句柄（http_sse_line 逐行读，用毕 close）；失败 nil。
@fnc http_sse_open: &, url: const char&, auth_bearer: const char&, body: const char&, timeout_s: i4
    return sa_curl_sse_open(url, auth_bearer, body, timeout_s)

# 取下一行（换行已剥）。1=得一行（填 out）/ 0=流结束 / <0=错误。
@fnc http_sse_line: i4, p: &, out: string&
    var line: char& = nil
    var r: i4 = sa_curl_sse_line(p, &line)
    if r == 1 && line != nil
        out->assign(line)
    else
        out->clear()
    return r

# 关闭 SSE 流。返回 HTTP 状态码（传输失败 <0）。
@fnc http_sse_close: i4, p: &
    return sa_curl_sse_close(p)
