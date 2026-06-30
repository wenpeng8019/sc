# RSA 代理（簇 R）端到端测试 —— 需以 -DSCC_SSL_BACKEND=openssl 构建的 scc。
#   inc crypto.sc 提供 @fnc 声明；inc ssl.sc 携带强符号实现覆盖弱桩。
#   无后端构建时 crypto_rsa_backend()==0，正向用例整体跳过（仅断言安全失败）。
# 运行：scc tests/cases/rsa_proxy_test.sc --test

inc crypto.sc
inc ssl.sc

tst "RSA 代理：无后端安全失败 / 有后端全链路"
    if crypto_rsa_backend() == 0
        # 无后端：弱桩安全失败
        var nk: & = crypto_rsa_keygen(2048, 65537)
        assert nk == nil, "无后端 keygen 返回 nil"
        return

    # 有后端（OpenSSL）：keygen -> 签验 -> 加解密 -> 导入导出全链路
    var key: & = crypto_rsa_keygen(2048, 65537)
    assert key != nil, "keygen 成功"

    # 摘要：SHA-256("abc")
    var dg[32]: u1
    crypto_sha256("abc", 3, (&dg[0]: u1&))

    # PKCS1v1.5 签名/验签
    var sig[256]: u1
    var n: i4 = crypto_rsa_sign_pkcs1(key, 4, (&dg[0]: u1&), 32, (&sig[0]: u1&), 256)
    assert n > 0, "pkcs1 签名产出"
    assert crypto_rsa_verify_pkcs1(key, 4, (&dg[0]: u1&), 32, (&sig[0]: u1&), (n: u8)) == 0, "pkcs1 验签通过"
    sig[0] = sig[0] ^ 0x01
    assert crypto_rsa_verify_pkcs1(key, 4, (&dg[0]: u1&), 32, (&sig[0]: u1&), (n: u8)) != 0, "pkcs1 篡改检测"

    # PSS 签名/验签
    var ps[256]: u1
    var m: i4 = crypto_rsa_sign_pss(key, 4, (&dg[0]: u1&), 32, (&ps[0]: u1&), 256)
    assert m > 0, "pss 签名产出"
    assert crypto_rsa_verify_pss(key, 4, (&dg[0]: u1&), 32, (&ps[0]: u1&), (m: u8)) == 0, "pss 验签通过"

    # OAEP 加密/解密回环
    var ct[256]: u1
    var rt[256]: u1
    var c: i4 = crypto_rsa_encrypt_oaep(key, 4, "hello rsa", 9, (&ct[0]: u1&), 256)
    assert c > 0, "oaep 加密产出"
    var d: i4 = crypto_rsa_decrypt_oaep(key, 4, (&ct[0]: u1&), (c: u8), (&rt[0]: u1&), 256)
    assert d == 9, "oaep 解密长度还原"
    rt[d] = 0
    assert sceq((&rt[0]: char&), "hello rsa") == 1, "oaep 明文还原"

    # PKCS1v1.5 加密/解密回环
    var c2: i4 = crypto_rsa_encrypt_pkcs1(key, "legacy enc", 10, (&ct[0]: u1&), 256)
    assert c2 > 0, "pkcs1 加密产出"
    var d2: i4 = crypto_rsa_decrypt_pkcs1(key, (&ct[0]: u1&), (c2: u8), (&rt[0]: u1&), 256)
    assert d2 == 10, "pkcs1 解密长度还原"
    rt[d2] = 0
    assert sceq((&rt[0]: char&), "legacy enc") == 1, "pkcs1 明文还原"

    # 导出公钥（DER）-> 导入 -> 用导入的公钥验签
    var pub[1024]: u1
    var pn: i4 = crypto_rsa_export(key, 0, 0, (&pub[0]: u1&), 1024)
    assert pn > 0, "公钥导出"
    var pk: & = crypto_rsa_import((&pub[0]: u1&), (pn: u8), 0, 0)
    assert pk != nil, "公钥导入"
    var s2[256]: u1
    var sn: i4 = crypto_rsa_sign_pss(key, 4, (&dg[0]: u1&), 32, (&s2[0]: u1&), 256)
    assert crypto_rsa_verify_pss(pk, 4, (&dg[0]: u1&), 32, (&s2[0]: u1&), (sn: u8)) == 0, "导入公钥验签通过"

    crypto_rsa_free(pk)
    crypto_rsa_free(key)

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
