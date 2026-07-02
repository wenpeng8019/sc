# ssh_demo —— SSH 客户端示例：建连 + 握手 + 打印服务器主机密钥指纹。
# 验证 sc → libssh2 → mbedTLS → 网络 整条链路（ssh.sc 为纯 sc 实现，直接调 libssh2）。
# 需联网。首次运行前先生成自包含静态库：
#   sh templates/utils/build_libssh2.sh
# 运行：scc templates/demo/ssh_demo.sc
#
# 连 github.com:22（真实 SSH 服务器，仅握手、无需认证）。

inc ../utils/ssh.sc

fnc main: i4
    var host: char& = "github.com"
    var c: & = ssh_connect(host, 22)
    if c == nil
        print "FAIL: ssh_connect/握手 %s:22\n", host
        return 1

    var fp[32]: u1
    if ssh_hostkey_sha256(c, &fp) != 0
        print "FAIL: 取主机密钥指纹\n"
        ssh_free(c)
        return 1

    ::printf("SSH 握手成功，主机密钥 SHA256: ")
    var i: i4 = 0
    while i < 32
        ::printf("%02x", fp[i])
        i = i + 1
    ::printf("\n")

    ssh_free(c)
    return 0
