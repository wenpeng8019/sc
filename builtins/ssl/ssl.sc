# ssl —— sc 的 TLS 流加密内置模块（双后端，编译期可选）
# 唯一事实源：C ABI 契约见同目录 ssl.h，默认实现见 ssl_impl.c。
# 定位：在「传输 com（socket/管道）」之上叠一层 TLS 记录层，对接 wss:// 与 HTTPS。
#   不自造密码学（区别于 crypto：那是零外链自实现的算法积木）；TLS 协议栈体量大、
#   安全敏感，故走成熟实现的双后端：
#     · mbedTLS —— vendor 源码内置（嵌入式/可裁剪/静态链接）
#     · OpenSSL —— 仅用系统预装库的头文件 + 链接（-lssl -lcrypto）
#   二择一在「构建 scc 时」经 CMake 选项 SCC_SSL_BACKEND 固化（参考 async 的 SCC_WITH_UV）。
#   未配置后端时为 none：本模块所有调用安全失败（ssl_backend()==0）。
#
# 用法：inc ssl.sc
#
# 设计：传输回调缝（ssl_set_transport）—— 密文搬运交给调用方提供的 send/recv，
#   故本层与具体传输解耦（mbedtls_ssl_set_bio / OpenSSL BIO 同构）。
#   sc 侧 com 设备适配层（把 send/recv 桥到底层 com.write/com.read）见后续路线。

# ---------------- 后端探测 ----------------
# 当前编译进 scc 的后端：0=none / 1=mbedtls / 2=openssl。
@fnc ssl_backend:: i4
# 后端名字（"none" / "mbedtls" / "openssl"）。
@fnc ssl_backend_name:: char&

# ---------------- 连接生命周期 ----------------
# 新建 TLS 客户端连接（尚未握手）。host 用于 SNI 与证书域名校验；verify!=0 校验对端证书。
# 返回不透明 ssl_conn*（失败 nil）。
@fnc ssl_client_new:: &, host: char&, verify: i4
# 注入传输回调：send(ctx,buf,len)/recv(ctx,buf,len) 把密文写到/读自底层（返回字节数，<0 错误，0=暂无/EOF）。
@fnc ssl_set_transport:: s: &, send: &, recv: &, ctx: &
# 释放连接（含底层 TLS 上下文）。
@fnc ssl_free:: s: &

# ---------------- 握手与明文收发 ----------------
# 驱动握手：0=完成 / 1=需更多 io（重试）/ <0=失败。
@fnc ssl_handshake:: i4, s: &
# 明文读：自动经 TLS 记录解密。返回 >=0 字节 / 1=需更多 io / <0 错误。
@fnc ssl_read:: i4, s: &, buf: &, len: u4
# 明文写：自动经 TLS 记录加密。返回 >=0 字节 / 1=需更多 io / <0 错误。
@fnc ssl_write:: i4, s: &, buf: &, len: u4

# ---------------- com 设备适配层 ----------------
# 把 TLS 包成 com 设备：叠在底层传输 com（如 tcp(fd,false,1,1) 同步 com）之上。
# host=SNI/校验域名；verify!=0 校验对端证书。构造时同步驱动握手（底层须为阻塞传输），
# 返回 TLS com&（握手失败/无后端返回 nil）。底层 com 不被拥有（close 不关底层）。
@fnc ssl_com:: com&, transport: com&, host: char&, verify: i4
