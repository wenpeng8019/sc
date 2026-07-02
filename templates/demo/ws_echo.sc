# ws_echo —— WebSocket 回显服务端示例 + socketpair 单进程自测
#
# 用 templates/utils/ws.sc 组件（构建在 io.sc 的 tcp com 设备之上）实现一个最小
# WebSocket echo 服务端，并用 sys.sc 的 sock_socketpair 在单进程里同时跑「服务端 + 测试
# 客户端」两个 async rpc，事件循环驱动一次完整闭环：握手 → 收发文本帧 → 关闭。
#
# 运行：scc templates/demo/ws_echo.sc
#   期望输出：cli got "hello"（客户端收到服务端回显），done=1。
#
# 真实部署时把 sock_socketpair 换成 listen/accept 得到的已连接 fd，对每个连接
# `async ws_echo_server(tcp(fd, true, 2, 2))` 即可。

inc async.sc
inc io.sc
inc os.sc
inc sys.sc
inc ../utils/ws.sc

var g_echo[64]: u1       # 客户端收到的回显载荷
var g_n: i4 = -1         # 回显长度
var g_done: i4 = 0       # 客户端完成标志
var g_frag[64]: u1       # 分片消息回显载荷
var g_fn: i4 = -1        # 分片回显长度

# 服务端：握手后循环收帧，并用「零拷贝转发」原样回发，直到对端 close 或出错。
#   buf[0, WS_HDR_MAX) 预留前缀，载荷收进 buf[WS_HDR_MAX..]；ws_forward 在前缀原地建头、
#   头+载荷连续单次写出（载荷不拷贝、单 syscall）。回发到同一连接即「零拷贝回显」。
rpc ws_echo_server: ret, c: com&
    var hs: i4 = await ws_handshake(c)
    if hs < 0
        return 0
    var cn: ws_conn
    ws_conn_init(&cn, 1)
    var buf[256]: u1
    var fr: ws_frame
    var alive: i4 = 1
    while alive == 1
        var n: i4 = await ws_recv(&cn, &fr, (&buf[WS_HDR_MAX]: u1&), 256 - WS_HDR_MAX, c)
        if n < 0
            alive = 0
        else if fr.opcode == WS_CLOSE
            alive = 0
        else
            var w: i4 = await ws_forward(&cn, fr.opcode, (&buf[0]: u1&), (n: u4), c)
            if w < 0
                alive = 0
    return 0

# 测试用：发一帧「定制首字节」帧（客户端，随机掩码）。b0 为完整首字节（FIN|RSV|opcode），
# 用于构造分片帧（FIN=0）与控制帧。借 ws_build_header 算长度/掩码键，再覆盖首字节。
rpc send_raw: ret, b0: u1, payload: u1&, plen: u4, c: com&
    var hdr[14]: u1
    var mk[4]: u1
    os_rand((&mk[0]: &), 4)
    var hl: i4 = ws_build_header((&hdr[0]: u1&), 0, plen, 1, (&mk[0]: u1&))
    hdr[0] = b0
    if plen > 0
        ws_unmask(payload, plen, (&mk[0]: u1&))
    var w1: i4 = await com_write_async(c, (&hdr[0]: &), (hl: u4))
    if w1 < 0
        return 0
    if plen > 0
        var w2: i4 = await com_write_async(c, (payload: &), plen)
    return 0

# 测试客户端：握手 → 发掩码文本帧 "hello" → 收回显（存全局）→ 发 close。
rpc ws_echo_client: ret, c: com&
    var hs: i4 = await ws_client_handshake(c)
    if hs < 0
        return 0
    var msg[16]: u1
    msg[0] = 'h'
    msg[1] = 'e'
    msg[2] = 'l'
    msg[3] = 'l'
    msg[4] = 'o'
    var s: i4 = await ws_send(WS_TEXT, (&msg[0]: u1&), 5, 1, c)
    if s < 0
        return 0
    var cn: ws_conn
    ws_conn_init(&cn, 0)
    var buf[256]: u1
    var fr: ws_frame
    var n: i4 = await ws_recv(&cn, &fr, (&buf[0]: u1&), 256, c)
    if n > 0
        var i: u4 = 0
        while (i: i4) < n
            g_echo[i] = buf[i]
            i = i + 1
        g_echo[n] = 0
    g_n = n
    # 阶段二：PING（服务端自动回 PONG）+ 分片文本 "Hel"+"lo"（服务端重组回显）。
    var ping0: u1 = 0
    var pr: i4 = await send_raw(0x89, (&ping0: u1&), 0, c)   # FIN|PING，空载荷
    var part1[3]: u1
    part1[0] = 'H'
    part1[1] = 'e'
    part1[2] = 'l'
    var f1: i4 = await send_raw(0x01, (&part1[0]: u1&), 3, c) # FIN=0, TEXT 首片
    var part2[2]: u1
    part2[0] = 'l'
    part2[1] = 'o'
    var f2: i4 = await send_raw(0x80, (&part2[0]: u1&), 2, c) # FIN=1, CONTINUATION 末片
    # 客户端 ws_recv 内部忽略服务端自动回的 PONG，取回重组后的 "Hello"。
    var n2: i4 = await ws_recv(&cn, &fr, (&buf[0]: u1&), 256, c)
    if n2 > 0
        var j: u4 = 0
        while (j: i4) < n2
            g_frag[j] = buf[j]
            j = j + 1
        g_frag[n2] = 0
    g_fn = n2
    var clo: i4 = await ws_send(WS_CLOSE, (&msg[0]: u1&), 0, 1, c)
    g_done = 1
    return 0

fnc main: i4
    var fds[2]: i4
    var r: i4 = sock_socketpair(fds)
    if r < 0
        print "socketpair fail\n"
        return 1
    var srv: com& = tcp(fds[0], true, 2, 2)
    var cli: com& = tcp(fds[1], true, 2, 2)
    async_init()
    var fsv: future& = async ws_echo_server(srv)
    var fcl: future& = async ws_echo_client(cli)
    async_loop(nil)
    async_final()
    srv->close()
    cli->close()
    print "cli got \"", (g_echo: char&), "\" n=", g_n, " done=", g_done, "\n"
    print "cli frag \"", (g_frag: char&), "\" fn=", g_fn, "\n"
    if g_done == 1 && g_n == 5 && g_fn == 5
        print "SELFTEST PASS\n"
    else
        print "SELFTEST FAIL\n"
    return 0
