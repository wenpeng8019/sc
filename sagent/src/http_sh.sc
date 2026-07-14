# http_sh —— HTTPS 通路的系统 curl 子进程实现（回退备用，缺省不编译；主路为 http.sc=libcurl）
# 一期实现：系统 curl 子进程（PLAN.md §1.2 回退路径先行）；libcurl vendor 后
# 本文件按同签名替换实现，调用方不动。
# 安全：API 密钥不进命令行（ps 可见），写入 0600 的 curl config 文件；
# 请求体经 @file 传递，避免 shell 转义面。

inc os.sc
inc adt.sc
inc io.sc
inc mem.sc

# 发 HTTPS POST（JSON）。
#   url/auth_bearer/body 入参；resp 出参（响应体全文）。
#   auth_bearer 为 nil 或空串时不加 Authorization 头。
#   返回 HTTP 状态码（>0）；进程/IO 失败返回 <0。
@fnc http_post: i4, url: const char&, auth_bearer: const char&, body: const char&, timeout_s: i4, resp: string&
    fs_mkdirs(".sagent/tmp")

    # 请求体落盘
    var rq: com@1 = file(".sagent/tmp/req.json", true, 0, 1)
    if rq == nil
        return -1
    rq << body
    rq = nil                                   # 关闭刷盘，供 curl 子进程读取

    # curl config（含密钥，先建 0600 空文件再写入）
    var cfg: string& = string()
    cfg->printf("url = \"%s\"\n", url)
    cfg->append("request = \"POST\"\n")
    cfg->append("header = \"Content-Type: application/json\"\n")
    if auth_bearer != nil && auth_bearer[0] != 0
        cfg->printf("header = \"Authorization: Bearer %s\"\n", auth_bearer)
    cfg->append("data = \"@.sagent/tmp/req.json\"\n")
    cfg->append("output = \".sagent/tmp/resp.json\"\n")
    cfg->append("write-out = \"%{http_code}\"\n")
    cfg->append("silent\nshow-error\n")
    cfg->printf("max-time = %d\n", timeout_s)
    ::system("umask 077; : > .sagent/tmp/curl.cfg")
    var cw: com@1 = file(".sagent/tmp/curl.cfg", true, 0, 1)
    var wr: i4 = -1
    if cw != nil
        cw << cfg
        wr = 0
    cw = nil                                    # 刷盘关闭（密钥文件，供 curl 读）
    cfg->drop()
    if wr != 0
        return -2

    # 执行：write-out 的状态码经 stdout 落 status.txt
    var rc: i4 = ::system("curl --config .sagent/tmp/curl.cfg > .sagent/tmp/status.txt 2> .sagent/tmp/err.txt")
    ::system("rm -f .sagent/tmp/curl.cfg")     # 用毕即焚（密钥文件）
    if rc != 0
        var erc: com@1 = file(".sagent/tmp/err.txt", true, 1, 0)
        var err: char& = nil
        if erc != nil
            var ers: com[0]
            ers = erc
            erc >> ers
            err = (ers.take(): char&)
        if err != nil
            ::fprintf(::stderr, "sagent: curl 失败: %s", err)
            recycle((err: &))
        return -3

    # 状态码
    var stc: com@1 = file(".sagent/tmp/status.txt", true, 1, 0)
    var st: char& = nil
    if stc != nil
        var sts: com[0]
        sts = stc
        stc >> sts
        st = (sts.take(): char&)
    if st == nil
        return -4
    var code: i4 = 0
    var i: i4 = 0
    while st[i] >= (48: char) && st[i] <= (57: char)
        code = code * 10 + ((st[i]: i4) - 48)
        i = i + 1
    recycle((st: &))

    # 响应体
    var bc: com@1 = file(".sagent/tmp/resp.json", true, 1, 0)
    var b: char& = nil
    if bc != nil
        var bs: com[0]
        bs = bc
        bc >> bs
        b = (bs.take(): char&)
    if b != nil
        resp->assign(b)
        recycle((b: &))
    else
        resp->clear()
    return code

# ---------- SSE 流式通道（curl -N 子进程，popen 行读） ----------

# 打开 SSE POST 流。返回 popen 句柄（fgets 逐行读，用毕 http_sse_close）；
# 失败返回 nil。密钥同样走 0600 config 文件（open 后即焚）。
@fnc http_sse_open: &, url: const char&, auth_bearer: const char&, body: const char&, timeout_s: i4
    fs_mkdirs(".sagent/tmp")
    var rq: com@1 = file(".sagent/tmp/req.json", true, 0, 1)
    if rq == nil
        return nil
    rq << body
    rq = nil                                   # 关闭刷盘，供 curl 子进程读取
    var cfg: string& = string()
    cfg->printf("url = \"%s\"\n", url)
    cfg->append("request = \"POST\"\n")
    cfg->append("header = \"Content-Type: application/json\"\n")
    cfg->append("header = \"Accept: text/event-stream\"\n")
    if auth_bearer != nil && auth_bearer[0] != 0
        cfg->printf("header = \"Authorization: Bearer %s\"\n", auth_bearer)
    cfg->append("data = \"@.sagent/tmp/req.json\"\n")
    cfg->append("no-buffer\nsilent\nshow-error\n")
    cfg->printf("max-time = %d\n", timeout_s)
    ::system("umask 077; : > .sagent/tmp/curl_sse.cfg")
    var cw: com@1 = file(".sagent/tmp/curl_sse.cfg", true, 0, 1)
    var wr: i4 = -1
    if cw != nil
        cw << cfg
        wr = 0
    cw = nil                                    # 刷盘关闭（密钥文件，供 curl 读）
    cfg->drop()
    if wr != 0
        return nil
    var fp: & = ::popen("curl --config .sagent/tmp/curl_sse.cfg 2> .sagent/tmp/err.txt", "r")
    # 注意：popen 异步启动，config（含密钥，0600）不能在此删——由 sse_close 清理
    return fp

# 关闭 SSE 流（并清理含密钥的 config）。返回 curl 退出码（0 正常）。
@fnc http_sse_close: i4, fp: &
    ::system("rm -f .sagent/tmp/curl_sse.cfg")
    if fp == nil
        return -1
    return ::pclose(fp)
