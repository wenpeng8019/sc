# ssl_https —— TLS 客户端端到端示例：sock_connect → tcp 同步 com → ssl_com → HTTPS GET。
# 验证 ssl com 适配层叠在阻塞 tcp com 之上，完成握手 + 加解密收发。
# 需用 OpenSSL（或 mbedTLS）后端构建的 scc（CMake -DSCC_SSL_BACKEND=openssl）；none 后端会握手失败。
# 需联网。运行：scc examples/ssl_https.sc

inc sys.sc
inc io.sc
inc ssl.sc

# C 风格 strlen。
fnc slen: u4, s: char&
    var i: u4 = 0
    while s[i] != 0
        i = i + 1
    return i

fnc main: i4
    var host: char& = "example.com"

    var fd: i4 = sock_connect(host, 443)
    if fd < 0
        print "FAIL: connect %s:443\n", host
        return 1

    var tc: com& = tcp(fd, false, 1, 1)   # 阻塞同步传输
    if tc == nil
        print "FAIL: tcp com\n"
        return 1

    var sec: com& = ssl_com(tc, host, 1)  # TLS（verify=1 校验证书）
    if sec == nil
        print "FAIL: TLS 握手（后端=", (ssl_backend_name(): char&), "？）\n"
        tc->close()
        return 1
    print "TLS 握手完成（后端=", (ssl_backend_name(): char&), "）\n"

    # 发 HTTP GET
    var req: char& = "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\nUser-Agent: sc-ssl\r\n\r\n"
    var wn: u4 = slen(req)
    var wr: i4 = sec->write((req: &), &wn)
    if wr < 0
        print "FAIL: write\n"
        sec->close()
        return 1

    # 循环读响应（TLS 记录可能分多次到达，累积到缓冲）
    var buf[2048]: u1
    var total: u4 = 0
    while total < 2047
        var rn: u4 = 2047 - total
        var rd: i4 = sec->read((&buf[total]: &), &rn)
        if rd < 0
            print "FAIL: read\n"
            sec->close()
            return 1
        if rd == 2          # sc_eof
            break
        total = total + rn
        if rn == 0
            break
    buf[total] = 0
    print "读取字节=", (total: "%d"), "\n"

    # 打印状态行（截到首个 \r/\n）
    var i: u4 = 0
    while i < total && buf[i] != 13 && buf[i] != 10
        i = i + 1
    buf[i] = 0
    print "响应状态行: ", (&buf[0]: char&), "\n"

    sec->close()   # 关闭 TLS；底层 tc 由调用方负责
    tc->close()
    return 0
