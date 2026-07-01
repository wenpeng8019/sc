# sec_demo —— securechn 自通讯加密信道示例 + socketpair 单进程自测
#
# 用 templates/utils/securechn.sc 组件，在单进程里用 os.sc 的 net_socketpair 同时跑
# 「发起方 + 响应方」两个 async rpc，事件循环驱动一次完整闭环：
#   握手（PSK 或 PSK+X25519 临时）→ 双向加密收发明文 → 篡改帧被拒。
#
# 运行：scc templates/demo/sec_demo.sc
#   期望：两种模式各自 srv 收到 "ping from initiator"、cli 收到 "pong from responder"，
#   且篡改的密文被 SEC_EAUTH 拒绝 → 末行 SELFTEST PASS。
#
# 真实部署时把 net_socketpair 换成 listen/accept（或 connect）得到的已连接 fd，对端各
# `async sec_responder(tcp(fd, true, 2, 2), ...)` / `async sec_initiator(...)` 即可。

inc async.sc
inc io.sc
inc os.sc
inc crypto.sc
inc ../utils/securechn.sc

var g_srv_msg[64]: u1     # 响应方收到的明文
var g_srv_n: i4 = -1
var g_cli_msg[64]: u1     # 发起方收到的明文
var g_cli_n: i4 = -1
var g_tamper: i4 = -1     # 篡改帧被拒（响应方第二次 recv 的返回码）
var g_srv_hs: i4 = -1     # 响应方握手返回码
var g_cli_hs: i4 = -1     # 发起方握手返回码

# 共享的预共享密钥（双方一致）。真实使用时来自安全配置/口令派生，切勿硬编码进源码。
var g_psk[16]: u1 = [0x53, 0x65, 0x63, 0x72, 0x65, 0x74, 0x50, 0x53, 0x4b, 0x21, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35]

# 发起方：握手 → 发 "ping from initiator" → 收响应方的明文回复。
rpc sec_initiator: ret, mode: i4, c: com&
    var ch: sec_chn
    sec_chn_init(&ch)
    var hs: i4 = await sec_handshake(&ch, (&g_psk[0]: &), 16, 1, mode, c)
    g_cli_hs = hs
    if hs < 0
        return 0
    var msg: char& = "ping from initiator"
    var mlen: u4 = 0
    while msg[mlen] != 0
        mlen = mlen + 1
    var s: i4 = await sec_send(&ch, (msg: &), mlen, c)
    if s < 0
        return 0
    var buf[64]: u1
    var n: i4 = await sec_recv(&ch, (&buf[0]: u1&), 64, c)
    if n > 0
        var i: u4 = 0
        while (i: i4) < n
            g_cli_msg[i] = buf[i]
            i = i + 1
        g_cli_msg[n] = 0
    g_cli_n = n
    return 0

# 响应方（正常闭环）：握手 → 收发起方明文 → 回 "pong from responder"。
rpc sec_responder: ret, mode: i4, c: com&
    var ch: sec_chn
    sec_chn_init(&ch)
    var hs: i4 = await sec_handshake(&ch, (&g_psk[0]: &), 16, 0, mode, c)
    g_srv_hs = hs
    if hs < 0
        return 0
    var buf[64]: u1
    var n: i4 = await sec_recv(&ch, (&buf[0]: u1&), 64, c)
    if n > 0
        var i: u4 = 0
        while (i: i4) < n
            g_srv_msg[i] = buf[i]
            i = i + 1
        g_srv_msg[n] = 0
    g_srv_n = n
    var reply: char& = "pong from responder"
    var rlen: u4 = 0
    while reply[rlen] != 0
        rlen = rlen + 1
    var s: i4 = await sec_send(&ch, (reply: &), rlen, c)
    return 0

# 响应方（篡改测试）：握手 → 收第一帧 → 回 pong → 再收一帧（被篡改 → 应认证失败）。
rpc sec_responder_tamper: ret, mode: i4, c: com&
    var ch: sec_chn
    sec_chn_init(&ch)
    var hs: i4 = await sec_handshake(&ch, (&g_psk[0]: &), 16, 0, mode, c)
    g_srv_hs = hs
    if hs < 0
        return 0
    var buf[64]: u1
    var n: i4 = await sec_recv(&ch, (&buf[0]: u1&), 64, c)
    g_srv_n = n
    var reply: char& = "pong from responder"
    var s: i4 = await sec_send(&ch, (reply: &), 19, c)
    if s < 0
        return 0
    # 第二次接收：发起方发来一帧被翻转 1 字节的密文，应认证失败。
    var n2: i4 = await sec_recv(&ch, (&buf[0]: u1&), 64, c)
    g_tamper = n2
    return 0

# 跑一种握手模式的完整闭环，返回 1=全部断言通过。
fnc run_mode: i4, mode: i4, label: char&
    g_srv_n = -1
    g_cli_n = -1
    g_tamper = -1
    g_srv_hs = -1
    g_cli_hs = -1
    var fds[2]: i4
    var r: i4 = net_socketpair(fds)
    if r < 0
        print "socketpair fail\n"
        return 0
    var srv: com& = tcp(fds[0], true, 2, 2)
    var cli: com& = tcp(fds[1], true, 2, 2)
    async_init()
    var fsv: future& = async sec_responder(mode, srv)
    var fin: future& = async sec_initiator(mode, cli)
    async_loop(nil)
    async_final()
    srv->close()
    cli->close()
    print label, ": srv hs=", g_srv_hs, " got \"", (g_srv_msg: char&), "\" (", g_srv_n, ")"
    print " | cli hs=", g_cli_hs, " got \"", (g_cli_msg: char&), "\" (", g_cli_n, ")\n"
    var ok: i4 = 1
    if g_srv_hs != 0 || g_cli_hs != 0
        ok = 0
    if g_srv_n != 19 || g_cli_n != 19
        ok = 0
    return ok

# 单独验证「篡改帧被拒」：发起方完成握手后发一帧坏密文，响应方第二次 recv 应得 SEC_EAUTH。
fnc run_tamper: i4, mode: i4
    g_srv_n = -1
    g_tamper = -1
    g_srv_hs = -1
    var fds[2]: i4
    var r: i4 = net_socketpair(fds)
    if r < 0
        return 0
    var srv: com& = tcp(fds[0], true, 2, 2)
    var cli: com& = tcp(fds[1], true, 2, 2)
    async_init()
    var fsv: future& = async sec_responder_tamper(mode, srv)
    var fin: future& = async sec_initiator_then_tamper(mode, cli)
    async_loop(nil)
    async_final()
    srv->close()
    cli->close()
    print "tamper: 第二帧 recv 返回 ", g_tamper, "（期望 ", (SEC_EAUTH: i4), "）\n"
    if g_tamper == SEC_EAUTH
        return 1
    return 0

# 配合 run_tamper 的发起方：先正常握手 + 发一帧让响应方第一次 recv 成功，再发一帧坏密文。
rpc sec_initiator_then_tamper: ret, mode: i4, c: com&
    var ch: sec_chn
    sec_chn_init(&ch)
    var hs: i4 = await sec_handshake(&ch, (&g_psk[0]: &), 16, 1, mode, c)
    g_cli_hs = hs
    if hs < 0
        return 0
    var msg: char& = "ping from initiator"
    var s: i4 = await sec_send(&ch, (msg: &), 19, c)
    # 收响应方的回复（消化掉，使序列推进）。
    var buf[64]: u1
    var n: i4 = await sec_recv(&ch, (&buf[0]: u1&), 64, c)
    # 第二帧：手工封 "x" 后翻转密文 → 坏帧。
    var pt[1]: u1
    pt[0] = ('x': u1)
    var hdr[4]: u1
    hdr[0] = 0
    hdr[1] = 0
    hdr[2] = 0
    hdr[3] = 1
    var nonce[12]: u1
    sec_nonce((&nonce[0]: u1&), ch.seq_tx)
    var cipher[1]: u1
    var tag[16]: u1
    crypto_aead_seal((&ch.key_tx[0]: u1&), (&nonce[0]: u1&), (&hdr[0]: &), 4, (&pt[0]: &), 1, (&cipher[0]: u1&), (&tag[0]: u1&))
    cipher[0] = cipher[0] ^ 0xFF
    var w1: i4 = await sec_write_full((&hdr[0]: &), 4, c)
    var w2: i4 = await sec_write_full((&cipher[0]: &), 1, c)
    var w3: i4 = await sec_write_full((&tag[0]: &), 16, c)
    return 0

fnc main: i4
    var ok1: i4 = run_mode(SEC_PSK_ONLY, "PSK    ")
    var ok2: i4 = run_mode(SEC_EPHEMERAL, "EPHEM  ")
    var ok3: i4 = run_tamper(SEC_EPHEMERAL)
    if ok1 == 1 && ok2 == 1 && ok3 == 1
        print "SELFTEST PASS\n"
    else
        print "SELFTEST FAIL\n"
    return 0
