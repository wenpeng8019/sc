# ssh —— SSH 客户端组件（libssh2 + mbedTLS 后端）
#
# 定位：templates 通用 utils 组件，**不是语言基础模块**（区别于 builtins/ssl）。一个可被
#   组装进产品的 SSH 客户端：建连 + 握手 + 认证（口令 / 公钥）+ 远程执行命令并取回输出。
#
# native 依赖经 sc 的 `add` 指令声明，不在编译器里硬编码（区别于 ssl 的 stem 注入）：
#   · add ssh_glue.c —— C 胶水层（封装 libssh2 调用），由编译器现场编译并链接。
#   · add libssh2.a  —— 自包含静态库 = libssh2 + mbedTLS（密码学后端）。
#                       由同目录 build_libssh2.sh 从 vendor/libssh2 + vendor/mbedtls 生成
#                       （host 构建，git 忽略的产物）；首次使用前先跑一次该脚本。
#   libssh2 自身不含密码学，全权委托 vendored mbedTLS（与默认 TLS 后端同源，零系统依赖）。
#
# 已知限制：libssh2 的 mbedTLS 后端**不协商 ssh-ed25519 主机密钥**（只提供 ECDSA / RSA）。
#   连接仅配置 ed25519 host key 的服务器会握手失败（"no matching host key type"）。
#   需要时令服务器额外提供 ecdsa / rsa host key 即可。
#
# 用法：inc ssh.sc，然后：
#   var c: & = ssh_connect("example.com", 22)        # TCP + SSH 握手
#   if ssh_auth_password(c, "user", "pass") == 0      # 或 ssh_auth_pubkey(...)
#       var buf[4096]: u1
#       var n: i4 = ssh_exec(c, "uname -a", &buf, 4096)
#   ssh_free(c)
#
# 范围：阻塞式同步客户端（建连 / 认证 / exec / SCP）+ 可选异步 com 适配（ssh_com），
#   POSIX（macOS/Linux）+ Windows（winsock，源码就绪未实测），无 zlib 压缩。SFTP 暂缓。

# 把 native 实现与库并入工程（路径相对本 .sc 目录解析）。
add ssh_glue.c
add libssh2.a

# ---------------- 连接生命周期 ----------------
# 建连并完成 SSH 传输层握手。返回不透明 ssh 连接句柄（失败 nil）。
@fnc ssh_connect:: &, host: char&, port: i4
# 取服务器主机密钥的 SHA-256 指纹（写入 out 的 32 字节）。0=成功 / -1=失败。
#   用于主机密钥校验（与已知 known_hosts 指纹比对，防中间人）。
@fnc ssh_hostkey_sha256:: i4, h: &, out: &
# 断开并释放连接（含底层 socket）。
@fnc ssh_free:: h: &

# ---------------- 用户认证 ----------------
# 口令认证。返回 0=成功 / -1=失败。
@fnc ssh_auth_password:: i4, h: &, user: char&, pass: char&
# 公钥认证（从密钥文件）。passphrase 可为 nil/""。
#   注意：mbedTLS 后端需显式给出 pubpath（公钥 .pub 文件），不支持由私钥推导（传 nil 会认证失败）。
#   返回 0=成功 / -1=失败。
@fnc ssh_auth_pubkey:: i4, h: &, user: char&, pubpath: char&, privpath: char&, passphrase: char&

# ---------------- 远程执行 ----------------
# 执行远程命令，最多取回 cap 字节 stdout 到 buf。返回取回字节数（>=0）/ -1 错误。
@fnc ssh_exec:: i4, h: &, cmd: char&, buf: &, cap: u4

# ---------------- 文件传输（SCP，阻塞式） ----------------
# 普通文件拷贝用 SCP 足矣（SFTP 仅用于远端文件系统操作：列目录 / stat / 续传）。
# 二者均要求会话处于阻塞模式（ssh_connect 后默认如此），勿与 ssh_com 异步适配混用同一连接。
# 下载远端文件到本地路径。返回收到字节数（>=0）/ -1。
@fnc ssh_scp_get:: i4, h: &, remotepath: char&, localpath: char&
# 上传本地文件到远端路径，mode 为 POSIX 权限（0 => 0644）。返回发送字节数（>=0）/ -1。
@fnc ssh_scp_put:: i4, h: &, localpath: char&, remotepath: char&, mode: i4

# ---------------- 异步 com 适配 ----------------
# 打开一个 exec 通道并封装为 op.h 的 com 设备：远程命令的 stdout/stdin 即可经 sc 异步
# I/O（com >> v / com << v）由 async_io 事件循环驱动。调用后会话切非阻塞，自此勿再用
# 阻塞式 ssh_exec / ssh_scp_* / 认证。owns != 0 时关闭该 com 会一并释放连接。返回 com&（失败 nil）。
@fnc ssh_com:: com&, h: &, cmd: char&, owns: i4
