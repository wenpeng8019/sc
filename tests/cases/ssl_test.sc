# ssl 模块冒烟测试（后端无关）：验证 inc ssl.sc 拼接 ssl_impl.c 并链接，API 可调用。
#   后端由构建 scc 时 CMake SCC_SSL_BACKEND 固化：none(0)/openssl(2)/mbedtls(1)。
#   none    ：建连返回 nil（安全失败）。
#   openssl/mbedtls：建连返回非 nil（随即释放）。
# 真实 TLS 握手/收发的端到端验证需传输层（socket/com），属 ssl com 适配层（后续路线）。
# 运行：scc tests/cases/ssl_test.sc --test

inc ssl.sc

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

tst "后端标识与名字自洽"
    var b: i4 = ssl_backend()
    assert b == 0 || b == 1 || b == 2, "backend ∈ {none,mbedtls,openssl}"
    if b == 0
        assert sceq(ssl_backend_name(), "none") == 1, "name == 'none'"
    if b == 1
        assert sceq(ssl_backend_name(), "mbedtls") == 1, "name == 'mbedtls'"
    if b == 2
        assert sceq(ssl_backend_name(), "openssl") == 1, "name == 'openssl'"

tst "建连行为与后端一致"
    var s: & = ssl_client_new("example.com", 1)
    if ssl_backend() == 0
        assert s == nil, "none：建连返回 nil"
    if ssl_backend() != 0
        assert s != nil, "有后端：建连成功"
        ssl_free(s)
