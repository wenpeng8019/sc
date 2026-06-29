# codec 单元测试：比对 SHA1 / base64 / WS accept 已知向量
#   （RFC 6455 §1.3 / RFC 3174 / RFC 4648）。
# 被测组件 codec.sc 是 templates 通用 utils 基础件，故经相对路径 inc。
# 运行：scc tests/cases/codec_test.sc --test

inc ../../templates/utils/codec.sc

# C 风格 strcmp（相等返回 1）。
fnc sceq: i4, a: char&, b: char&
    var i: u4 = 0
    while a[i] != 0 && b[i] != 0
        if a[i] != b[i]
            return 0
        i = i + 1
    if a[i] == b[i]
        return 1
    return 0

# digest[0..20) 转小写十六进制写入 out（>=41 字节，含结尾 0）。
fnc hex20: i4, digest: u1&, out: char&
    var tab: char& = "0123456789abcdef"
    var i: u4 = 0
    while i < 20
        out[i * 2 + 0] = tab[(digest[i] >> 4) & 0x0F]
        out[i * 2 + 1] = tab[digest[i] & 0x0F]
        i = i + 1
    out[40] = 0
    return 0

# 计算 base64 写入 out 并补 0。
fnc b64: i4, data: char&, len: u8, out: char&
    var n: i4 = base64_buf(data, len, out)
    out[n] = 0
    return n

tst "base64 RFC 4648 向量"
    var buf[128]: char
    b64("", 0, (&buf[0]: char&))
    assert sceq((&buf[0]: char&), "") == 1, "b64('')"
    b64("f", 1, (&buf[0]: char&))
    assert sceq((&buf[0]: char&), "Zg==") == 1, "b64('f')"
    b64("fo", 2, (&buf[0]: char&))
    assert sceq((&buf[0]: char&), "Zm8=") == 1, "b64('fo')"
    b64("foo", 3, (&buf[0]: char&))
    assert sceq((&buf[0]: char&), "Zm9v") == 1, "b64('foo')"
    b64("foobar", 6, (&buf[0]: char&))
    assert sceq((&buf[0]: char&), "Zm9vYmFy") == 1, "b64('foobar')"

tst "SHA1 FIPS 180-1 向量"
    var d[20]: u1
    var hx[48]: char
    sha1("abc", 3, (&d[0]: char&))
    hex20((&d[0]: u1&), (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "a9993e364706816aba3e25717850c26c9cd0d89d") == 1, "sha1('abc')"
    sha1("", 0, (&d[0]: char&))
    hex20((&d[0]: u1&), (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "da39a3ee5e6b4b0d3255bfef95601890afd80709") == 1, "sha1('')"
    sha1("The quick brown fox jumps over the lazy dog", 43, (&d[0]: char&))
    hex20((&d[0]: u1&), (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12") == 1, "sha1('fox')"

tst "WebSocket accept key（RFC 6455 §1.3）"
    # key = "dGhlIHNhbXBsZSBub25jZQ==" -> "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="
    var acc[64]: char
    var alen: i4 = ws_accept_buf("dGhlIHNhbXBsZSBub25jZQ==", 24, (&acc[0]: char&))
    acc[alen] = 0
    assert sceq((&acc[0]: char&), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 1, "ws_accept"
