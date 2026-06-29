# ============================================================================
# codec —— WebSocket 握手所需的纯 sc 编解码原语（base64 + SHA1）
#
# 定位：templates 通用 utils 基础组件，零 C 边车、零 adt 依赖（全部用 sc 位运算/
#   定长 char 缓冲实现）。SHA1 摘要 / base64 是字节级运算，定长 char& 缓冲（调用方
#   提供容量）零堆分配，比堆 string 更契合 codec 场景，故一律用 char& 缓冲接口。
# WebSocket 握手 accept key（RFC 6455 §4.2.2）：
#   accept = base64( SHA1( client_key + WS_GUID ) )
#   WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
# ============================================================================

# 32 位循环左移（SHA1 核心原语；sc 无内建，按定义合成）
@fnc rotl32: u4, x: u4, n: u4
    return (x << n) | (x >> (32 - n))

# ---------------- SHA1（FIPS 180-1）----------------
#
# 不物化整块填充缓冲：按需取「填充后消息」第 i 字节（原文 + 0x80 + 0 + 64bit 长度）。
# 这样对任意长度输入都无需动态分配，契合 WS 短输入场景。

# 填充后总长度（64 字节对齐）：原文 len + 1(0x80) + 8(长度) 向上取整到 64 的倍数
@fnc sha1_total: u8, len: u8
    return ((len + 9 + 63) / 64) * 64

# 取「填充后消息」第 i 字节
@fnc sha1_byte: u1, msg: char&, len: u8, total: u8, i: u8
    if i < len
        return msg[i]
    if i == len
        return 0x80
    if i + 8 >= total
        # 末 8 字节：原文比特长度，大端
        var bitlen: u8 = len * 8
        var shift: u8 = (total - 1 - i) * 8
        return (bitlen >> shift) & 0xFF
    return 0

# 大端写 u4 到 out[off..off+4)
@fnc sha1_put: i4, out: char&, off: u8, v: u4
    out[off + 0] = (v >> 24) & 0xFF
    out[off + 1] = (v >> 16) & 0xFF
    out[off + 2] = (v >> 8) & 0xFF
    out[off + 3] = v & 0xFF
    return 0

# 计算 SHA1，把 20 字节摘要写入 out（调用方提供 >=20 字节缓冲）
@fnc sha1: i4, msg: char&, len: u8, out: char&
    var h0: u4 = 0x67452301
    var h1: u4 = 0xEFCDAB89
    var h2: u4 = 0x98BADCFE
    var h3: u4 = 0x10325476
    var h4: u4 = 0xC3D2E1F0
    var total: u8 = sha1_total(len)
    var w[80]: u4
    var chunk: u8 = 0
    for chunk = 0; chunk < total; chunk = chunk + 64
        var t: u8 = 0
        for t = 0; t < 16; t = t + 1
            var b0: u4 = sha1_byte(msg, len, total, chunk + t * 4 + 0)
            var b1: u4 = sha1_byte(msg, len, total, chunk + t * 4 + 1)
            var b2: u4 = sha1_byte(msg, len, total, chunk + t * 4 + 2)
            var b3: u4 = sha1_byte(msg, len, total, chunk + t * 4 + 3)
            w[t] = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3
        for t = 16; t < 80; t = t + 1
            var x: u4 = w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16]
            w[t] = rotl32(x, 1)
        var a: u4 = h0
        var b: u4 = h1
        var c: u4 = h2
        var d: u4 = h3
        var e: u4 = h4
        for t = 0; t < 80; t = t + 1
            var f: u4 = 0
            var k: u4 = 0
            if t < 20
                f = (b & c) | ((~b) & d)
                k = 0x5A827999
            else if t < 40
                f = b ^ c ^ d
                k = 0x6ED9EBA1
            else if t < 60
                f = (b & c) | (b & d) | (c & d)
                k = 0x8F1BBCDC
            else
                f = b ^ c ^ d
                k = 0xCA62C1D6
            var tmp: u4 = rotl32(a, 5) + f + e + k + w[t]
            e = d
            d = c
            c = rotl32(b, 30)
            b = a
            a = tmp
        h0 = h0 + a
        h1 = h1 + b
        h2 = h2 + c
        h3 = h3 + d
        h4 = h4 + e
    sha1_put(out, 0, h0)
    sha1_put(out, 4, h1)
    sha1_put(out, 8, h2)
    sha1_put(out, 12, h3)
    sha1_put(out, 16, h4)
    return 0

# ---------------- base64（RFC 4648）----------------

# 取 base64 字母表第 i 个字符（0..63）
@fnc b64char: u1, i: u4
    var tab: char& = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
    return tab[i]

# ---------------- WebSocket accept key（纯 char& 缓冲，零 adt 依赖）----------------

# 把 data[0..len) 的 base64 写入 out（调用方保证 >=((len+2)/3)*4 字节），返回写入字符数。
@fnc base64_buf: i4, data: char&, len: u8, out: char&
    var i: u8 = 0
    var o: i4 = 0
    for i = 0; i + 3 <= len; i = i + 3
        var n: u4 = (((data[i]: u4) & 0xFF) << 16) | (((data[i + 1]: u4) & 0xFF) << 8) | ((data[i + 2]: u4) & 0xFF)
        out[o + 0] = (b64char((n >> 18) & 0x3F): char)
        out[o + 1] = (b64char((n >> 12) & 0x3F): char)
        out[o + 2] = (b64char((n >> 6) & 0x3F): char)
        out[o + 3] = (b64char(n & 0x3F): char)
        o = o + 4
    var rem: u8 = len - i
    if rem == 1
        var n: u4 = ((data[i]: u4) & 0xFF) << 16
        out[o + 0] = (b64char((n >> 18) & 0x3F): char)
        out[o + 1] = (b64char((n >> 12) & 0x3F): char)
        out[o + 2] = '='
        out[o + 3] = '='
        o = o + 4
    else if rem == 2
        var n: u4 = (((data[i]: u4) & 0xFF) << 16) | (((data[i + 1]: u4) & 0xFF) << 8)
        out[o + 0] = (b64char((n >> 18) & 0x3F): char)
        out[o + 1] = (b64char((n >> 12) & 0x3F): char)
        out[o + 2] = (b64char((n >> 6) & 0x3F): char)
        out[o + 3] = '='
        o = o + 4
    return o

# 由 key[0..keylen) 计算 Sec-WebSocket-Accept 写入 out（>=32 字节），返回长度。零 adt 依赖。
@fnc ws_accept_buf: i4, key: char&, keylen: u4, out: char&
    var buf[256]: char
    var n: u4 = 0
    while n < keylen
        buf[n] = key[n]
        n = n + 1
    var guid: char& = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
    var g: u4 = 0
    while guid[g] != 0
        buf[n] = guid[g]
        n = n + 1
        g = g + 1
    var digest[20]: u1
    sha1((&buf[0]: char&), (n: u8), (&digest[0]: char&))
    return base64_buf((&digest[0]: char&), 20, out)
