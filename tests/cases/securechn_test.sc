# securechn 单元测试：自通讯加密信道端到端闭环 + codec 辅助
#   在单进程里用 sock_socketpair 跑「发起方 + 响应方」两个 async rpc，事件循环驱动：
#     · PSK 快路握手 → 双向加密收发 → 明文一致
#     · PSK + X25519 临时握手（前向保密）→ 同上
#     · 被篡改的密文帧被 AEAD 认证拒绝（SEC_EAUTH）
#   再单测纯同步 codec 辅助（nonce 构造 / 常量时间比较）。
# 运行：scc tests/cases/securechn_test.sc --test
#
# 被测组件 templates/utils/securechn.sc 经默认搜索路径 inc（连带 crypto/io/async/os）。

inc async.sc
inc io.sc
inc sys.sc
inc crypto.sc
inc ../../templates/utils/securechn.sc

# 预共享密钥（双方一致）。
var t_psk[16]: u1 = [0x53, 0x65, 0x63, 0x72, 0x65, 0x74, 0x50, 0x53, 0x4b, 0x21, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35]

var t_srv_msg[64]: u1
var t_srv_n: i4 = -1
var t_cli_msg[64]: u1
var t_cli_n: i4 = -1
var t_srv_hs: i4 = -1
var t_cli_hs: i4 = -1
var t_tamper: i4 = -1

# C 风格 strcmp（相等返回 1）。
fnc tceq: i4, a: char&, b: char&
    var i: u4 = 0
    while a[i] != 0 && b[i] != 0
        if a[i] != b[i]
            return 0
        i = i + 1
    if a[i] == b[i]
        return 1
    return 0

# 发起方：握手 → 发 "ping" → 收回复。
rpc t_initiator: ret, mode: i4, c: com&
    var ch: sec_chn
    sec_chn_init(&ch)
    var hs: i4 = await sec_handshake(&ch, (&t_psk[0]: &), 16, 1, mode, c)
    t_cli_hs = hs
    if hs < 0
        return 0
    var s: i4 = await sec_send(&ch, ("ping": &), 4, c)
    if s < 0
        return 0
    var buf[64]: u1
    var n: i4 = await sec_recv(&ch, (&buf[0]: u1&), 64, c)
    if n > 0
        var i: u4 = 0
        while (i: i4) < n
            t_cli_msg[i] = buf[i]
            i = i + 1
        t_cli_msg[n] = 0
    t_cli_n = n
    return 0

# 响应方：握手 → 收 ping → 回 "pong"。
rpc t_responder: ret, mode: i4, c: com&
    var ch: sec_chn
    sec_chn_init(&ch)
    var hs: i4 = await sec_handshake(&ch, (&t_psk[0]: &), 16, 0, mode, c)
    t_srv_hs = hs
    if hs < 0
        return 0
    var buf[64]: u1
    var n: i4 = await sec_recv(&ch, (&buf[0]: u1&), 64, c)
    if n > 0
        var i: u4 = 0
        while (i: i4) < n
            t_srv_msg[i] = buf[i]
            i = i + 1
        t_srv_msg[n] = 0
    t_srv_n = n
    var s: i4 = await sec_send(&ch, ("pong": &), 4, c)
    return 0

# 篡改测试响应方：握手 → 收第一帧 → 回 pong → 再收一帧（应认证失败）。
rpc t_responder_tamper: ret, mode: i4, c: com&
    var ch: sec_chn
    sec_chn_init(&ch)
    var hs: i4 = await sec_handshake(&ch, (&t_psk[0]: &), 16, 0, mode, c)
    t_srv_hs = hs
    if hs < 0
        return 0
    var buf[64]: u1
    var n: i4 = await sec_recv(&ch, (&buf[0]: u1&), 64, c)
    var s: i4 = await sec_send(&ch, ("pong": &), 4, c)
    if s < 0
        return 0
    var n2: i4 = await sec_recv(&ch, (&buf[0]: u1&), 64, c)
    t_tamper = n2
    return 0

# 篡改测试发起方：正常握手 + 发一帧（让响应方第一次 recv 成功），再手工封一帧坏密文。
rpc t_initiator_tamper: ret, mode: i4, c: com&
    var ch: sec_chn
    sec_chn_init(&ch)
    var hs: i4 = await sec_handshake(&ch, (&t_psk[0]: &), 16, 1, mode, c)
    t_cli_hs = hs
    if hs < 0
        return 0
    var s: i4 = await sec_send(&ch, ("ping": &), 4, c)
    var buf[64]: u1
    var n: i4 = await sec_recv(&ch, (&buf[0]: u1&), 64, c)
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

# 跑一种模式的正常闭环：返回 1=全部断言通过。
fnc run_loop: i4, mode: i4
    t_srv_n = -1
    t_cli_n = -1
    t_srv_hs = -1
    t_cli_hs = -1
    var fds[2]: i4
    if sock_socketpair(fds) < 0
        return 0
    var srv: com& = tcp(fds[0], true, 2, 2)
    var cli: com& = tcp(fds[1], true, 2, 2)
    async_init()
    var fsv: future& = async t_responder(mode, srv)
    var fin: future& = async t_initiator(mode, cli)
    async_loop(nil)
    async_final()
    srv->close()
    cli->close()
    if t_srv_hs != 0 || t_cli_hs != 0
        return 0
    if t_srv_n != 4 || t_cli_n != 4
        return 0
    if tceq((&t_srv_msg[0]: char&), "ping") != 1
        return 0
    if tceq((&t_cli_msg[0]: char&), "pong") != 1
        return 0
    return 1

tst "PSK 快路握手端到端加密收发"
    assert run_loop(SEC_PSK_ONLY) == 1, "PSK 双向加密闭环"

tst "PSK + X25519 临时握手端到端（前向保密）"
    assert run_loop(SEC_EPHEMERAL) == 1, "X25519 双向加密闭环"

tst "篡改密文帧被 AEAD 认证拒绝"
    t_tamper = -100
    var fds[2]: i4
    assert sock_socketpair(fds) == 0, "socketpair"
    var srv: com& = tcp(fds[0], true, 2, 2)
    var cli: com& = tcp(fds[1], true, 2, 2)
    async_init()
    var fsv: future& = async t_responder_tamper(SEC_EPHEMERAL, srv)
    var fin: future& = async t_initiator_tamper(SEC_EPHEMERAL, cli)
    async_loop(nil)
    async_final()
    srv->close()
    cli->close()
    assert t_tamper == SEC_EAUTH, "坏帧应得 SEC_EAUTH"

tst "nonce 构造（前4字节0 + 大端64位序号）"
    var nn[12]: u1
    sec_nonce((&nn[0]: u1&), 1)
    assert nn[0] == 0 && nn[1] == 0 && nn[2] == 0 && nn[3] == 0, "前4字节为0"
    assert nn[4] == 0 && nn[10] == 0 && nn[11] == 1, "序号1的大端编码"
    sec_nonce((&nn[0]: u1&), 0x0102030405060708)
    assert nn[4] == 0x01 && nn[5] == 0x02 && nn[11] == 0x08, "大端多字节序号"

tst "常量时间比较 sec_cteq"
    var a[4]: u1 = [1, 2, 3, 4]
    var b[4]: u1 = [1, 2, 3, 4]
    var d[4]: u1 = [1, 2, 3, 5]
    assert sec_cteq((&a[0]: u1&), (&b[0]: u1&), 4) == 1, "相等返回1"
    assert sec_cteq((&a[0]: u1&), (&d[0]: u1&), 4) == 0, "不等返回0"
