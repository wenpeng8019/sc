# crypto 单元测试：比对 SHA-256 / HMAC-SHA256 / HKDF-SHA256 官方 KAT 向量
#   SHA-256: FIPS 180-4 / NIST 示例
#   HMAC-SHA256: RFC 4231 测试用例
#   HKDF-SHA256: RFC 5869 测试用例
# 运行：scc tests/cases/crypto_test.sc --test
#
# 被测组件 builtins/crypto/crypto.sc 经默认 builtins 搜索路径 inc（自动链接 crypto_impl.c）。

inc crypto.sc

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

# digest[0..n) 转小写十六进制写入 out（须 >= 2n+1 字节，含结尾 0）。
fnc hexn: i4, digest: u1&, n: u4, out: char&
    var tab: char& = "0123456789abcdef"
    var i: u4 = 0
    while i < n
        out[i * 2 + 0] = tab[(digest[i] >> 4) & 0x0F]
        out[i * 2 + 1] = tab[digest[i] & 0x0F]
        i = i + 1
    out[n * 2] = 0
    return 0

tst "SHA-256 FIPS 180-4 向量"
    var d[32]: u1
    var hx[80]: char
    crypto_sha256("abc", 3, (&d[0]: u1&))
    hexn((&d[0]: u1&), 32, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 1, "sha256('abc')"
    crypto_sha256("", 0, (&d[0]: u1&))
    hexn((&d[0]: u1&), 32, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 1, "sha256('')"
    crypto_sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, (&d[0]: u1&))
    hexn((&d[0]: u1&), 32, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 1, "sha256(448-bit)"

tst "HMAC-SHA256 RFC 4231 向量"
    var d[32]: u1
    var hx[80]: char
    # 测试用例 2：key='Jefe'，data='what do ya want for nothing?'
    crypto_hmac_sha256("Jefe", 4, "what do ya want for nothing?", 28, (&d[0]: u1&))
    hexn((&d[0]: u1&), 32, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843") == 1, "hmac case2"

tst "HKDF-SHA256 RFC 5869 测试用例 1"
    var salt[13]: u1 = [0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c]
    var ikm[22]: u1
    var info[10]: u1 = [0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9]
    var okm[42]: u1
    var hx[96]: char
    var i: u4 = 0
    while i < 22
        ikm[i] = 0x0b
        i = i + 1
    crypto_hkdf_sha256((&salt[0]: u1&), 13, (&ikm[0]: u1&), 22, (&info[0]: u1&), 10, (&okm[0]: u1&), 42)
    hexn((&okm[0]: u1&), 42, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865") == 1, "hkdf case1"

tst "SHA-1 FIPS 180-1 向量（遗留协议互通）"
    var d[20]: u1
    var hx[48]: char
    crypto_sha1("abc", 3, (&d[0]: u1&))
    hexn((&d[0]: u1&), 20, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "a9993e364706816aba3e25717850c26c9cd0d89d") == 1, "sha1('abc')"
    crypto_sha1("", 0, (&d[0]: u1&))
    hexn((&d[0]: u1&), 20, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "da39a3ee5e6b4b0d3255bfef95601890afd80709") == 1, "sha1('')"
    crypto_sha1("The quick brown fox jumps over the lazy dog", 43, (&d[0]: u1&))
    hexn((&d[0]: u1&), 20, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12") == 1, "sha1('fox')"

tst "Base64 RFC 4648 向量"
    var buf[128]: char
    var n: i4 = 0
    n = crypto_base64("", 0, (&buf[0]: char&))
    buf[n] = 0
    assert sceq((&buf[0]: char&), "") == 1, "b64('')"
    n = crypto_base64("f", 1, (&buf[0]: char&))
    buf[n] = 0
    assert sceq((&buf[0]: char&), "Zg==") == 1, "b64('f')"
    n = crypto_base64("fo", 2, (&buf[0]: char&))
    buf[n] = 0
    assert sceq((&buf[0]: char&), "Zm8=") == 1, "b64('fo')"
    n = crypto_base64("foo", 3, (&buf[0]: char&))
    buf[n] = 0
    assert sceq((&buf[0]: char&), "Zm9v") == 1, "b64('foo')"
    n = crypto_base64("foobar", 6, (&buf[0]: char&))
    buf[n] = 0
    assert sceq((&buf[0]: char&), "Zm9vYmFy") == 1, "b64('foobar')"

