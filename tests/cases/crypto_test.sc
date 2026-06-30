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

# 单个十六进制字符 -> 数值（0..15）。
fnc hexval: i4, c: i4
    if c >= 48 && c <= 57
        return c - 48
    if c >= 97 && c <= 102
        return c - 87
    if c >= 65 && c <= 70
        return c - 55
    return 0

# 十六进制串 s -> 字节缓冲 out，返回字节数。
fnc unhex: i4, s: char&, out: u1&
    var i: u4 = 0
    while s[i * 2] != 0
        out[i] = (((hexval((s[i * 2]: i4)) << 4) | hexval((s[i * 2 + 1]: i4))): u1)
        i = i + 1
    return (i: i4)

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

# ===== 第二期 · 批1：ChaCha20-Poly1305 AEAD / X25519 =====

tst "Poly1305 RFC 8439 §2.5.2 向量"
    var key[32]: u1
    var d[16]: u1
    var hx[48]: char
    unhex("85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b", (&key[0]: u1&))
    crypto_poly1305((&key[0]: u1&), "Cryptographic Forum Research Group", 34, (&d[0]: u1&))
    hexn((&d[0]: u1&), 16, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "a8061dc1305136c6c22b8baf0c0127a9") == 1, "poly1305 tag"

tst "ChaCha20-Poly1305 AEAD RFC 8439 §2.8.2 向量"
    var key[32]: u1
    var nonce[12]: u1
    var aad[12]: u1
    var ct[114]: u1
    var dec[114]: u1
    var tag[16]: u1
    var hx[256]: char
    var ok: i4 = 0
    unhex("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f", (&key[0]: u1&))
    unhex("070000004041424344454647", (&nonce[0]: u1&))
    unhex("50515253c0c1c2c3c4c5c6c7", (&aad[0]: u1&))
    crypto_aead_seal((&key[0]: u1&), (&nonce[0]: u1&), (&aad[0]: u1&), 12, "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.", 114, (&ct[0]: u1&), (&tag[0]: u1&))
    hexn((&ct[0]: u1&), 114, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d63dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b3692ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc3ff4def08e4b7a9de576d26586cec64b6116") == 1, "aead ciphertext"
    hexn((&tag[0]: u1&), 16, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "1ae10b594f09e26a7e902ecbd0600691") == 1, "aead tag"
    # 解封往返：未篡改应成功
    ok = crypto_aead_open((&key[0]: u1&), (&nonce[0]: u1&), (&aad[0]: u1&), 12, (&ct[0]: u1&), 114, (&tag[0]: u1&), (&dec[0]: u1&))
    assert ok == 0, "aead open ok"
    # 篡改 tag 应被拒
    tag[0] = ((tag[0] ^ 1): u1)
    ok = crypto_aead_open((&key[0]: u1&), (&nonce[0]: u1&), (&aad[0]: u1&), 12, (&ct[0]: u1&), 114, (&tag[0]: u1&), (&dec[0]: u1&))
    assert ok == -1, "aead open 篡改被拒"

tst "X25519 RFC 7748 §5.2 标量乘"
    var k[32]: u1
    var u[32]: u1
    var out[32]: u1
    var hx[80]: char
    unhex("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4", (&k[0]: u1&))
    unhex("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c", (&u[0]: u1&))
    crypto_x25519((&out[0]: u1&), (&k[0]: u1&), (&u[0]: u1&))
    hexn((&out[0]: u1&), 32, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552") == 1, "x25519 标量乘"

tst "X25519 RFC 7748 §6.1 DH 密钥交换"
    var apriv[32]: u1
    var bpriv[32]: u1
    var apub[32]: u1
    var bpub[32]: u1
    var sh1[32]: u1
    var sh2[32]: u1
    var hx[80]: char
    unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", (&apriv[0]: u1&))
    unhex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", (&bpriv[0]: u1&))
    crypto_x25519_base((&apub[0]: u1&), (&apriv[0]: u1&))
    hexn((&apub[0]: u1&), 32, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a") == 1, "alice 公钥"
    crypto_x25519_base((&bpub[0]: u1&), (&bpriv[0]: u1&))
    crypto_x25519((&sh1[0]: u1&), (&apriv[0]: u1&), (&bpub[0]: u1&))
    crypto_x25519((&sh2[0]: u1&), (&bpriv[0]: u1&), (&apub[0]: u1&))
    hexn((&sh1[0]: u1&), 32, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742") == 1, "共享密钥"
    hexn((&sh2[0]: u1&), 32, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742") == 1, "共享密钥对称一致"

# ===== 第二期 · 批2：SHA-512 家族 / PBKDF2 / SHA-3 =====

tst "SHA-512 FIPS 180-4 向量"
    var d[64]: u1
    var hx[160]: char
    crypto_sha512("abc", 3, (&d[0]: u1&))
    hexn((&d[0]: u1&), 64, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f") == 1, "sha512('abc')"
    crypto_sha512("", 0, (&d[0]: u1&))
    hexn((&d[0]: u1&), 64, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e") == 1, "sha512('')"

tst "SHA-384 FIPS 180-4 向量"
    var d[48]: u1
    var hx[112]: char
    crypto_sha384("abc", 3, (&d[0]: u1&))
    hexn((&d[0]: u1&), 48, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7") == 1, "sha384('abc')"

tst "HMAC-SHA512 RFC 4231 向量"
    var d[64]: u1
    var hx[160]: char
    crypto_hmac_sha512("Jefe", 4, "what do ya want for nothing?", 28, (&d[0]: u1&))
    hexn((&d[0]: u1&), 64, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea2505549758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737") == 1, "hmac-sha512 case2"

tst "PBKDF2-HMAC-SHA256 公开 KAT"
    var d[32]: u1
    var hx[80]: char
    crypto_pbkdf2_sha256("password", 8, "salt", 4, 1, (&d[0]: u1&), 32)
    hexn((&d[0]: u1&), 32, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b") == 1, "pbkdf2 c=1"
    crypto_pbkdf2_sha256("password", 8, "salt", 4, 2, (&d[0]: u1&), 32)
    hexn((&d[0]: u1&), 32, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43") == 1, "pbkdf2 c=2"
    crypto_pbkdf2_sha256("password", 8, "salt", 4, 4096, (&d[0]: u1&), 32)
    hexn((&d[0]: u1&), 32, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a") == 1, "pbkdf2 c=4096"

tst "SHA-3 FIPS 202 向量"
    var d[64]: u1
    var hx[160]: char
    crypto_sha3_256("", 0, (&d[0]: u1&))
    hexn((&d[0]: u1&), 32, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a") == 1, "sha3-256('')"
    crypto_sha3_256("abc", 3, (&d[0]: u1&))
    hexn((&d[0]: u1&), 32, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532") == 1, "sha3-256('abc')"
    crypto_sha3_512("abc", 3, (&d[0]: u1&))
    hexn((&d[0]: u1&), 64, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "b751850b1a57168a5693cd924b6b096e08f621827444f70d884f5d0240d2712e10e116e9192af3c91a7ec57647e3934057340b4cf408d5a56592f8274eec53f0") == 1, "sha3-512('abc')"

tst "SHAKE FIPS 202 向量"
    var d[64]: u1
    var hx[160]: char
    crypto_shake128("", 0, (&d[0]: u1&), 32)
    hexn((&d[0]: u1&), 32, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "7f9c2ba4e88f827d616045507605853ed73b8093f6efbc88eb1a6eacfa66ef26") == 1, "shake128('')"
    crypto_shake256("", 0, (&d[0]: u1&), 32)
    hexn((&d[0]: u1&), 32, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "46b9dd2b0ba88d13233b3feb743eeb243fcd52ea62b81b82b50c27646ed5762f") == 1, "shake256('')"

# ---------------- 第二期 · 批3：AES / Ed25519 ----------------

tst "AES-128-CTR SP 800-38A F.5.1"
    var key[16]: u1
    var iv[16]: u1
    var pt[64]: u1
    var ct[64]: u1
    var hx[160]: char
    unhex("2b7e151628aed2a6abf7158809cf4f3c", (&key[0]: u1&))
    unhex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", (&iv[0]: u1&))
    unhex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710", (&pt[0]: u1&))
    crypto_aes_ctr((&key[0]: u1&), 128, (&iv[0]: u1&), (&pt[0]: u1&), 64, (&ct[0]: u1&))
    hexn((&ct[0]: u1&), 64, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff5ae4df3edbd5d35e5b4f09020db03eab1e031dda2fbe03d1792170a0f3009cee") == 1, "aes-128-ctr"

tst "AES-256-CTR SP 800-38A F.5.5"
    var key[32]: u1
    var iv[16]: u1
    var pt[64]: u1
    var ct[64]: u1
    var hx[160]: char
    unhex("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", (&key[0]: u1&))
    unhex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", (&iv[0]: u1&))
    unhex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710", (&pt[0]: u1&))
    crypto_aes_ctr((&key[0]: u1&), 256, (&iv[0]: u1&), (&pt[0]: u1&), 64, (&ct[0]: u1&))
    hexn((&ct[0]: u1&), 64, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "601ec313775789a5b7a7f504bbf3d228f443e3ca4d62b59aca84e990cacaf5c52b0930daa23de94ce87017ba2d84988ddfc9c58db67aada613c2dd08457941a6") == 1, "aes-256-ctr"

tst "AES-128-CBC SP 800-38A F.2.1 + 解密还原"
    var key[16]: u1
    var iv[16]: u1
    var pt[64]: u1
    var ct[64]: u1
    var dec[64]: u1
    var hx[160]: char
    var i: u4 = 0
    var same: i4 = 1
    unhex("2b7e151628aed2a6abf7158809cf4f3c", (&key[0]: u1&))
    unhex("000102030405060708090a0b0c0d0e0f", (&iv[0]: u1&))
    unhex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710", (&pt[0]: u1&))
    crypto_aes_cbc_encrypt((&key[0]: u1&), 128, (&iv[0]: u1&), (&pt[0]: u1&), 64, (&ct[0]: u1&))
    hexn((&ct[0]: u1&), 64, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b273bed6b8e3c1743b7116e69e222295163ff1caa1681fac09120eca307586e1a7") == 1, "aes-128-cbc 加密"
    crypto_aes_cbc_decrypt((&key[0]: u1&), 128, (&iv[0]: u1&), (&ct[0]: u1&), 64, (&dec[0]: u1&))
    while i < 64
        if dec[i] != pt[i]
            same = 0
        i = i + 1
    assert same == 1, "aes-128-cbc 解密还原"

tst "AES-128-GCM（McGrew TC4，含 AAD）+ 篡改检测"
    var key[16]: u1
    var iv[12]: u1
    var aad[20]: u1
    var pt[60]: u1
    var ct[60]: u1
    var dec[60]: u1
    var tag[16]: u1
    var hx[160]: char
    var ok: i4 = 0
    unhex("feffe9928665731c6d6a8f9467308308", (&key[0]: u1&))
    unhex("cafebabefacedbaddecaf888", (&iv[0]: u1&))
    unhex("feedfacedeadbeeffeedfacedeadbeefabaddad2", (&aad[0]: u1&))
    unhex("d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39", (&pt[0]: u1&))
    crypto_aes_gcm_seal((&key[0]: u1&), 128, (&iv[0]: u1&), 12, (&aad[0]: u1&), 20, (&pt[0]: u1&), 60, (&ct[0]: u1&), (&tag[0]: u1&))
    hexn((&ct[0]: u1&), 60, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091") == 1, "gcm 密文"
    hexn((&tag[0]: u1&), 16, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "5bc94fbc3221a5db94fae95ae7121a47") == 1, "gcm 标签"
    ok = crypto_aes_gcm_open((&key[0]: u1&), 128, (&iv[0]: u1&), 12, (&aad[0]: u1&), 20, (&ct[0]: u1&), 60, (&tag[0]: u1&), (&dec[0]: u1&))
    assert ok == 0, "gcm 解封成功"
    tag[0] = ((tag[0] ^ 1): u1)
    ok = crypto_aes_gcm_open((&key[0]: u1&), 128, (&iv[0]: u1&), 12, (&aad[0]: u1&), 20, (&ct[0]: u1&), 60, (&tag[0]: u1&), (&dec[0]: u1&))
    assert ok == -1, "gcm 篡改被拒"

tst "Ed25519 RFC 8032 Test 1（空消息）"
    var seed[32]: u1
    var pub[32]: u1
    var sig[64]: u1
    var hx[160]: char
    var ok: i4 = 0
    unhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", (&seed[0]: u1&))
    crypto_ed25519_pubkey((&pub[0]: u1&), (&seed[0]: u1&))
    hexn((&pub[0]: u1&), 32, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a") == 1, "ed25519 公钥"
    crypto_ed25519_sign((&sig[0]: u1&), "", 0, (&seed[0]: u1&), (&pub[0]: u1&))
    hexn((&sig[0]: u1&), 64, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b") == 1, "ed25519 签名"
    ok = crypto_ed25519_verify((&sig[0]: u1&), "", 0, (&pub[0]: u1&))
    assert ok == 0, "ed25519 验签通过"

tst "Ed25519 RFC 8032 Test 2 + 篡改检测"
    var seed[32]: u1
    var pub[32]: u1
    var sig[64]: u1
    var hx[160]: char
    var ok: i4 = 0
    unhex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb", (&seed[0]: u1&))
    crypto_ed25519_pubkey((&pub[0]: u1&), (&seed[0]: u1&))
    hexn((&pub[0]: u1&), 32, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c") == 1, "ed25519 test2 公钥"
    crypto_ed25519_sign((&sig[0]: u1&), "r", 1, (&seed[0]: u1&), (&pub[0]: u1&))
    hexn((&sig[0]: u1&), 64, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00") == 1, "ed25519 test2 签名"
    ok = crypto_ed25519_verify((&sig[0]: u1&), "r", 1, (&pub[0]: u1&))
    assert ok == 0, "ed25519 test2 验签通过"
    sig[0] = ((sig[0] ^ 1): u1)
    ok = crypto_ed25519_verify((&sig[0]: u1&), "r", 1, (&pub[0]: u1&))
    assert ok == -1, "ed25519 篡改被拒"

# ===== 第二期 · 批4：遗留 / 弱算法 KAT（值经 openssl 交叉验证）=====

tst "MD5 RFC 1321（abc / 空串）"
    var d[16]: u1
    var hx[40]: char
    crypto_md5("abc", 3, (&d[0]: u1&))
    hexn((&d[0]: u1&), 16, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "900150983cd24fb0d6963f7d28e17f72") == 1, "md5 abc"
    crypto_md5("", 0, (&d[0]: u1&))
    hexn((&d[0]: u1&), 16, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "d41d8cd98f00b204e9800998ecf8427e") == 1, "md5 空串"

tst "RIPEMD-160（abc / 空串）"
    var d[20]: u1
    var hx[48]: char
    crypto_ripemd160("abc", 3, (&d[0]: u1&))
    hexn((&d[0]: u1&), 20, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "8eb208f7e05d987a9b044a8e98c6b087f15a0bfc") == 1, "ripemd160 abc"
    crypto_ripemd160("", 0, (&d[0]: u1&))
    hexn((&d[0]: u1&), 20, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "9c1185a5c5e9fc54612808977ee8f548b2258d31") == 1, "ripemd160 空串"

tst "DES-ECB 经典向量 + 解密回环"
    var key[8]: u1
    var pt[8]: u1
    var ct[8]: u1
    var rt[8]: u1
    var hx[24]: char
    unhex("133457799bbcdff1", (&key[0]: u1&))
    unhex("0123456789abcdef", (&pt[0]: u1&))
    crypto_des_ecb_encrypt((&key[0]: u1&), (&pt[0]: u1&), 8, (&ct[0]: u1&))
    hexn((&ct[0]: u1&), 8, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "85e813540f0ab405") == 1, "des-ecb 密文"
    crypto_des_ecb_decrypt((&key[0]: u1&), (&ct[0]: u1&), 8, (&rt[0]: u1&))
    hexn((&rt[0]: u1&), 8, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "0123456789abcdef") == 1, "des-ecb 解密回环"

tst "DES-CBC 加解密回环（2 分组）"
    var key[8]: u1
    var iv[8]: u1
    var pt[16]: u1
    var ct[16]: u1
    var rt[16]: u1
    var hx[40]: char
    unhex("133457799bbcdff1", (&key[0]: u1&))
    unhex("0011223344556677", (&iv[0]: u1&))
    unhex("0123456789abcdeffedcba9876543210", (&pt[0]: u1&))
    crypto_des_cbc_encrypt((&key[0]: u1&), (&iv[0]: u1&), (&pt[0]: u1&), 16, (&ct[0]: u1&))
    crypto_des_cbc_decrypt((&key[0]: u1&), (&iv[0]: u1&), (&ct[0]: u1&), 16, (&rt[0]: u1&))
    hexn((&rt[0]: u1&), 16, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "0123456789abcdeffedcba9876543210") == 1, "des-cbc 回环"

tst "3DES-ECB：单密钥退化 + 三密钥向量 + 解密回环"
    var k1[24]: u1
    var k3[24]: u1
    var pt[8]: u1
    var pt3[8]: u1
    var ct[8]: u1
    var rt[8]: u1
    var hx[24]: char
    # K1=K2=K3 时应退化为单 DES（与上面 des-ecb 同向量）
    unhex("133457799bbcdff1133457799bbcdff1133457799bbcdff1", (&k1[0]: u1&))
    unhex("0123456789abcdef", (&pt[0]: u1&))
    crypto_des3_ecb_encrypt((&k1[0]: u1&), (&pt[0]: u1&), 8, (&ct[0]: u1&))
    hexn((&ct[0]: u1&), 8, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "85e813540f0ab405") == 1, "3des 退化单 DES"
    # 真三密钥向量
    unhex("0123456789abcdef23456789abcdef01456789abcdef0123", (&k3[0]: u1&))
    unhex("0123456789abcdef", (&pt3[0]: u1&))
    crypto_des3_ecb_encrypt((&k3[0]: u1&), (&pt3[0]: u1&), 8, (&ct[0]: u1&))
    hexn((&ct[0]: u1&), 8, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "f2afd84ee809e2b5") == 1, "3des 三密钥密文"
    crypto_des3_ecb_decrypt((&k3[0]: u1&), (&ct[0]: u1&), 8, (&rt[0]: u1&))
    hexn((&rt[0]: u1&), 8, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "0123456789abcdef") == 1, "3des 解密回环"

tst "3DES-CBC 加解密回环（2 分组）"
    var key[24]: u1
    var iv[8]: u1
    var pt[16]: u1
    var ct[16]: u1
    var rt[16]: u1
    var hx[40]: char
    unhex("0123456789abcdef23456789abcdef01456789abcdef0123", (&key[0]: u1&))
    unhex("8899aabbccddeeff", (&iv[0]: u1&))
    unhex("0123456789abcdeffedcba9876543210", (&pt[0]: u1&))
    crypto_des3_cbc_encrypt((&key[0]: u1&), (&iv[0]: u1&), (&pt[0]: u1&), 16, (&ct[0]: u1&))
    crypto_des3_cbc_decrypt((&key[0]: u1&), (&iv[0]: u1&), (&ct[0]: u1&), 16, (&rt[0]: u1&))
    hexn((&rt[0]: u1&), 16, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "0123456789abcdeffedcba9876543210") == 1, "3des-cbc 回环"

tst "AES-128-ECB（SP 800-38A 向量）+ 解密回环"
    var key[16]: u1
    var pt[16]: u1
    var ct[16]: u1
    var rt[16]: u1
    var hx[40]: char
    unhex("2b7e151628aed2a6abf7158809cf4f3c", (&key[0]: u1&))
    unhex("6bc1bee22e409f96e93d7e117393172a", (&pt[0]: u1&))
    crypto_aes_ecb_encrypt((&key[0]: u1&), 128, (&pt[0]: u1&), 16, (&ct[0]: u1&))
    hexn((&ct[0]: u1&), 16, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "3ad77bb40d7a3660a89ecaf32466ef97") == 1, "aes-ecb 密文"
    crypto_aes_ecb_decrypt((&key[0]: u1&), 128, (&ct[0]: u1&), 16, (&rt[0]: u1&))
    hexn((&rt[0]: u1&), 16, (&hx[0]: char&))
    assert sceq((&hx[0]: char&), "6bc1bee22e409f96e93d7e117393172a") == 1, "aes-ecb 解密回环"


