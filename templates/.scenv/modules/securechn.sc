# securechn —— 自通讯加密信道组件（两端都是自己的 sc 程序，无需 PKI）
#
# 定位：templates 通用 utils 组件。把 crypto.sc 的算法积木组合成一条「自己和自己」通讯用的
#   端到端加密信道：比 TLS 轻（不需要证书/CA/握手协商套件），因为约定双方都是使用者自己的
#   程序、共享一个预共享密钥（PSK）。在任意已连接的 com 设备（tcp / socketpair / stream）之上
#   叠一层认证加密分帧，调用方只管 send/recv 明文。
#
# 依赖：inc crypto.sc（x25519 / hkdf_sha256 / sha256 / aead_seal/open=ChaCha20-Poly1305）
#   / inc io.sc（com 设备）/ inc async.sc（事件循环）。随机数经 op 层 rand_bytes（CSPRNG，默认导入，无需 inc）。
#
# 两种握手模式（sec_mode）：
#   · SEC_PSK_ONLY  —— 仅用 PSK 快速派生会话密钥（一个 RTT，无非对称运算）。无前向保密。
#   · SEC_EPHEMERAL —— 在 PSK 之上叠加 X25519 临时密钥交换（前向保密）：即便日后 PSK 泄露，
#                      也无法解密历史会话。PSK 仍参与密钥派生 → 同时起到认证作用，挡住中间人。
#
# 密钥架构（HKDF-SHA256）：
#   salt = 发起方随机数(32) ‖ 响应方随机数(32)
#   ikm  = PSK ‖ ECDH 共享密钥(临时模式才有)
#   key_c2s = HKDF(salt, ikm, "sc-secure/v1 c2s", 32)   # 发起→响应 方向
#   key_s2c = HKDF(salt, ikm, "sc-secure/v1 s2c", 32)   # 响应→发起 方向
#   双向独立密钥 → 各方向用各自单调序号当 nonce，永不复用。
#
# 握手确认（Finished）：双方各用「己方发送密钥 + 序号 0」AEAD 封装 transcript 哈希
#   th=SHA256(发起方 hello ‖ 响应方 hello)，互发并校验。PSK/握手不符 → 解封失败即拒绝，
#   且对端拿不到任何可被当作解密谕示器的明文。确认占用序号 0，数据帧从序号 1 起。
#
# 数据分帧（每帧）：4 字节大端明文长度 L ‖ 密文(L) ‖ Poly1305 标签(16)。
#   nonce = 0x00000000 ‖ 大端 64 位方向序号；长度前缀作为 AAD 一并认证。
#   单帧明文上限 SEC_MAXFRAME（4096）；更大载荷由调用方自行切块。

inc crypto.sc
inc io.sc
inc async.sc

# op.h 恒链接的异步 io 桥接原语（与 ws.sc 同）。声明为 @fnc 即可 await。
@fnc com_read_async::  future&, c: com&, buf: &, n: u4
@fnc com_write_async:: future&, c: com&, buf: &, n: u4

# ---------------- 握手模式 ----------------
@def sec_mode: [
    SEC_PSK_ONLY  = 0    # 仅 PSK（快路，无前向保密）
    SEC_EPHEMERAL = 1    # PSK + X25519 临时握手（前向保密）
] : i4

# ---------------- 返回码 ----------------
@def sec_ret: [
    SEC_OK     = 0
    SEC_EIO    = 0 - 1   # io 错误 / 连接断开
    SEC_EAUTH  = 0 - 2   # 认证/解密失败（PSK 不符 / 被篡改 / 双方模式不一致）
    SEC_EFRAME = 0 - 3   # 帧过大 / 接收容量不足
] : i4

# ---------------- 协议常量 ----------------
@def sec_param: [
    SEC_VERSION  = 1      # 协议版本字节
    SEC_MAXFRAME = 4096   # 单帧明文上限（字节）
] : i4

# 信道状态：调用方持有，握手后供 send/recv 使用。
@def sec_chn: {
    key_tx[32]:   u1   # 本端发送方向会话密钥
    key_rx[32]:   u1   # 本端接收方向会话密钥
    seq_tx:       u8   # 发送方向单调序号
    seq_rx:       u8   # 接收方向单调序号
    is_initiator: u1   # 1=发起方
    established:  u1    # 1=握手已完成
}

# 初始化信道（清零）。握手前调用一次。
@fnc sec_chn_init: i4, ch: sec_chn&
    ch->seq_tx = 0
    ch->seq_rx = 0
    ch->is_initiator = 0
    ch->established = 0
    var i: u4 = 0
    while i < 32
        ch->key_tx[i] = 0
        ch->key_rx[i] = 0
        i = i + 1
    return 0

# ================================ 小工具（同步） ================================

# 把 src[0..n) 拷到 dst[off..]，返回新偏移。
@fnc sec_cat: u4, dst: u1&, off: u4, src: u1&, n: u4
    var i: u4 = 0
    while i < n
        dst[off + i] = src[i]
        i = i + 1
    return off + i

# 常量时间比较 a[0..n) 与 b[0..n)：相等返回 1，否则 0。
@fnc sec_cteq: i4, a: u1&, b: u1&, n: u4
    var d: u1 = 0
    var i: u4 = 0
    while i < n
        d = d | (a[i] ^ b[i])
        i = i + 1
    if d == 0
        return 1
    return 0

# 构造 12 字节 ChaCha20-Poly1305 nonce：前 4 字节 0，后 8 字节大端序号。
@fnc sec_nonce: i4, out: u1&, seq: u8
    out[0] = 0
    out[1] = 0
    out[2] = 0
    out[3] = 0
    out[4] = ((seq >> 56) & 0xFF: u1)
    out[5] = ((seq >> 48) & 0xFF: u1)
    out[6] = ((seq >> 40) & 0xFF: u1)
    out[7] = ((seq >> 32) & 0xFF: u1)
    out[8] = ((seq >> 24) & 0xFF: u1)
    out[9] = ((seq >> 16) & 0xFF: u1)
    out[10] = ((seq >> 8) & 0xFF: u1)
    out[11] = (seq & 0xFF: u1)
    return 0

# 密钥派生：salt=ni‖nr，ikm=PSK‖ECDH；按角色把 c2s/s2c 装入 ch 的 tx/rx。
@fnc sec_derive: i4, ch: sec_chn&, is_init: i4, ni: u1&, nr: u1&, psk: &, psklen: u8, ecdh: u1&, ecdhlen: u8
    var salt[64]: u1
    var k: u4 = 0
    while k < 32
        salt[k] = ni[k]
        salt[32 + k] = nr[k]
        k = k + 1
    var ikm[160]: u1
    var p: u1& = (psk: u1&)
    var il: u4 = 0
    var i: u4 = 0
    while (i: u8) < psklen
        ikm[il] = p[i]
        il = il + 1
        i = i + 1
    var j: u4 = 0
    while (j: u8) < ecdhlen
        ikm[il] = ecdh[j]
        il = il + 1
        j = j + 1
    var kc2s[32]: u1
    var ks2c[32]: u1
    crypto_hkdf_sha256((&salt[0]: &), 64, (&ikm[0]: &), (il: u8), "sc-secure/v1 c2s", 16, (&kc2s[0]: u1&), 32)
    crypto_hkdf_sha256((&salt[0]: &), 64, (&ikm[0]: &), (il: u8), "sc-secure/v1 s2c", 16, (&ks2c[0]: u1&), 32)
    var t: u4 = 0
    while t < 32
        if is_init != 0
            ch->key_tx[t] = kc2s[t]
            ch->key_rx[t] = ks2c[t]
        else
            ch->key_tx[t] = ks2c[t]
            ch->key_rx[t] = kc2s[t]
        t = t + 1
    return 0

# ================================ io 辅助（异步） ================================

# 读满 n 字节到 buf：循环直到读够；对端断开/出错返回 SEC_EIO，成功返回 0。
@rpc sec_read_full: i4, buf: &, n: u4, c: com&
    var p: u1& = (buf: u1&)
    var got: u4 = 0
    while got < n
        var r: i4 = await com_read_async(c, (&p[got]: &), n - got)
        if r <= 0
            return SEC_EIO
        got = got + (r: u4)
    return 0

# 写满 n 字节：循环直到写完；出错返回 SEC_EIO，成功返回 0。
@rpc sec_write_full: i4, buf: &, n: u4, c: com&
    var p: u1& = (buf: u1&)
    var sent: u4 = 0
    while sent < n
        var r: i4 = await com_write_async(c, (&p[sent]: &), n - sent)
        if r <= 0
            return SEC_EIO
        sent = sent + (r: u4)
    return 0

# ================================ 握手 ================================

# 在已连接的 com 设备 c 上完成端到端加密握手。
#   ch       —— 已 sec_chn_init 的信道状态（出参，成功后承载会话密钥/序号）。
#   psk/psklen —— 预共享密钥（双方必须一致）。
#   is_init  —— 1=发起方，0=响应方（双方必须取相反角色）。
#   mode     —— SEC_PSK_ONLY 或 SEC_EPHEMERAL（双方必须一致，否则握手失败）。
# 返回 SEC_OK(0) / SEC_EIO(-1) / SEC_EAUTH(-2)。
@rpc sec_handshake: i4, ch: sec_chn&, psk: &, psklen: u8, is_init: i4, mode: i4, c: com&
    var use_eph: i4 = 0
    if mode == SEC_EPHEMERAL
        use_eph = 1
    # ---- 构造并发送本端 hello：版本(1) ‖ flags(1) ‖ 随机数(32) ‖ [临时公钥(32)] ----
    var my_hello[66]: u1
    var my_priv[32]: u1
    var my_pub[32]: u1
    my_hello[0] = (SEC_VERSION: u1)
    my_hello[1] = (use_eph: u1)
    rand_bytes((&my_hello[2]: &), 32)
    var hello_len: u4 = 34
    if use_eph != 0
        rand_bytes((&my_priv[0]: &), 32)
        crypto_x25519_base((&my_pub[0]: u1&), (&my_priv[0]: u1&))
        var b: u4 = 0
        while b < 32
            my_hello[34 + b] = my_pub[b]
            b = b + 1
        hello_len = 66
    var w: i4 = await sec_write_full((&my_hello[0]: &), hello_len, c)
    if w < 0
        return SEC_EIO
    # ---- 读对端 hello（先固定头 34 字节，临时模式再读公钥 32 字节）----
    var pr_hello[66]: u1
    var r1: i4 = await sec_read_full((&pr_hello[0]: &), 34, c)
    if r1 < 0
        return SEC_EIO
    if pr_hello[0] != (SEC_VERSION: u1)
        return SEC_EAUTH
    var peer_eph: i4 = 0
    if pr_hello[1] != 0
        peer_eph = 1
    if peer_eph != use_eph
        return SEC_EAUTH
    if use_eph != 0
        var r2: i4 = await sec_read_full((&pr_hello[34]: &), 32, c)
        if r2 < 0
            return SEC_EIO
    # ---- ECDH（临时模式）----
    var ecdh[32]: u1
    var ecdhlen: u8 = 0
    if use_eph != 0
        crypto_x25519((&ecdh[0]: u1&), (&my_priv[0]: u1&), (&pr_hello[34]: u1&))
        ecdhlen = 32
    # ---- 派生会话密钥（salt 按角色固定为 发起方随机数‖响应方随机数）----
    if is_init != 0
        sec_derive(ch, is_init, (&my_hello[2]: u1&), (&pr_hello[2]: u1&), psk, psklen, (&ecdh[0]: u1&), ecdhlen)
    else
        sec_derive(ch, is_init, (&pr_hello[2]: u1&), (&my_hello[2]: u1&), psk, psklen, (&ecdh[0]: u1&), ecdhlen)
    # ---- transcript 哈希 th=SHA256(发起方 hello ‖ 响应方 hello) ----
    var tbuf[132]: u1
    var tl: u4 = 0
    if is_init != 0
        tl = sec_cat((&tbuf[0]: u1&), 0, (&my_hello[0]: u1&), hello_len)
        tl = sec_cat((&tbuf[0]: u1&), tl, (&pr_hello[0]: u1&), hello_len)
    else
        tl = sec_cat((&tbuf[0]: u1&), 0, (&pr_hello[0]: u1&), hello_len)
        tl = sec_cat((&tbuf[0]: u1&), tl, (&my_hello[0]: u1&), hello_len)
    var th[32]: u1
    crypto_sha256((&tbuf[0]: &), (tl: u8), (&th[0]: u1&))
    # ---- Finished 互发：己方发送密钥 + 序号 0 封装 th，互校验 ----
    var nonce0[12]: u1
    sec_nonce((&nonce0[0]: u1&), 0)
    var fin_out[48]: u1
    crypto_aead_seal((&ch->key_tx[0]: u1&), (&nonce0[0]: u1&), "", 0, (&th[0]: &), 32, (&fin_out[0]: u1&), (&fin_out[32]: u1&))
    var wf: i4 = await sec_write_full((&fin_out[0]: &), 48, c)
    if wf < 0
        return SEC_EIO
    var fin_in[48]: u1
    var rf: i4 = await sec_read_full((&fin_in[0]: &), 48, c)
    if rf < 0
        return SEC_EIO
    var th2[32]: u1
    var ok: i4 = crypto_aead_open((&ch->key_rx[0]: u1&), (&nonce0[0]: u1&), "", 0, (&fin_in[0]: &), 32, (&fin_in[32]: u1&), (&th2[0]: u1&))
    if ok < 0
        return SEC_EAUTH
    if sec_cteq((&th[0]: u1&), (&th2[0]: u1&), 32) == 0
        return SEC_EAUTH
    ch->seq_tx = 1
    ch->seq_rx = 1
    if is_init != 0
        ch->is_initiator = 1
    ch->established = 1
    return SEC_OK

# ================================ 数据收发 ================================

# 加密并发送一条明文消息 data[0..len)。len 须 <= SEC_MAXFRAME。
# 返回 SEC_OK(0) / SEC_EIO(-1) / SEC_EFRAME(-3)。
@rpc sec_send: i4, ch: sec_chn&, data: &, len: u4, c: com&
    if len > (SEC_MAXFRAME: u4)
        return SEC_EFRAME
    var hdr[4]: u1
    hdr[0] = ((len >> 24) & 0xFF: u1)
    hdr[1] = ((len >> 16) & 0xFF: u1)
    hdr[2] = ((len >> 8) & 0xFF: u1)
    hdr[3] = (len & 0xFF: u1)
    var nonce[12]: u1
    sec_nonce((&nonce[0]: u1&), ch->seq_tx)
    var cipher[4096]: u1
    var tag[16]: u1
    crypto_aead_seal((&ch->key_tx[0]: u1&), (&nonce[0]: u1&), (&hdr[0]: &), 4, data, (len: u8), (&cipher[0]: u1&), (&tag[0]: u1&))
    var w1: i4 = await sec_write_full((&hdr[0]: &), 4, c)
    if w1 < 0
        return SEC_EIO
    if len > 0
        var w2: i4 = await sec_write_full((&cipher[0]: &), len, c)
        if w2 < 0
            return SEC_EIO
    var w3: i4 = await sec_write_full((&tag[0]: &), 16, c)
    if w3 < 0
        return SEC_EIO
    ch->seq_tx = ch->seq_tx + 1
    return SEC_OK

# 接收并解密一条消息到 out（调用方提供 cap 字节缓冲）。
# 返回 >=0 明文长度；SEC_EIO(-1) io 错误；SEC_EAUTH(-2) 认证失败（已篡改/密钥不符）；
#   SEC_EFRAME(-3) 帧过大或超出 cap。校验失败时不向 out 释放明文。
@rpc sec_recv: i4, ch: sec_chn&, out: u1&, cap: u4, c: com&
    var hdr[4]: u1
    var r1: i4 = await sec_read_full((&hdr[0]: &), 4, c)
    if r1 < 0
        return SEC_EIO
    var len: u4 = (((hdr[0]): u4) << 24) | (((hdr[1]): u4) << 16) | (((hdr[2]): u4) << 8) | ((hdr[3]): u4)
    if len > (SEC_MAXFRAME: u4)
        return SEC_EFRAME
    if len > cap
        return SEC_EFRAME
    var cipher[4096]: u1
    if len > 0
        var r2: i4 = await sec_read_full((&cipher[0]: &), len, c)
        if r2 < 0
            return SEC_EIO
    var tag[16]: u1
    var r3: i4 = await sec_read_full((&tag[0]: &), 16, c)
    if r3 < 0
        return SEC_EIO
    var nonce[12]: u1
    sec_nonce((&nonce[0]: u1&), ch->seq_rx)
    var ok: i4 = crypto_aead_open((&ch->key_rx[0]: u1&), (&nonce[0]: u1&), (&hdr[0]: &), 4, (&cipher[0]: &), (len: u8), (&tag[0]: u1&), out)
    if ok < 0
        return SEC_EAUTH
    ch->seq_rx = ch->seq_rx + 1
    return (len: i4)
