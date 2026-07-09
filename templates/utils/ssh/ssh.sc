# ssh —— SSH 客户端组件（libssh2 + mbedTLS 后端，纯 sc 实现）
#
# 定位：templates 通用 utils 组件，**不是语言基础模块**（区别于 builtins/ssl）。一个可被
#   组装进产品的 SSH 客户端：建连 + 握手 + 认证（口令 / 公钥）+ 远程执行命令并取回输出。
#
# 实现方式：直接按名调用 libssh2 的 C API（sc↔C 互操作，见 syntax.md §9）——
#   `inc "libssh2.h"` 引入其原型后，libssh2_session_init() / libssh2_channel_read() 等
#   即可在 sc 里直接调用；socket 建连/关闭经 sys 模块（sock_connect / sock_close，底层是
#   platform.h 的跨平台 socket 层）。无需任何 C 胶水层。
#
# native 依赖经 sc 的 `add` 指令声明，不在编译器里硬编码（区别于 ssl 的 stem 注入）：
#   · add libssh2.a  —— 自包含静态库 = libssh2 + mbedTLS（密码学后端）。
#                       由同目录 build.sh 从 vendor/libssh2 + vendor/mbedtls 生成
#                       （git 忽略的产物；宿主裸名 libssh2.a，交叉 libssh2.<签名>.a）；
#                       首次使用前先跑一次 build.sh（跨平台：./build.sh --target ...）。
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

inc sys.sc                                     # sock_connect / sock_close（跨平台 socket）
inc mem.sc                                     # chunk / chunk0（动态内存）
inc "../../../vendor/libssh2/include/libssh2.h"   # libssh2 C API 原型（'::' 逃逸到 C 域按名调用）

# 把 native 静态库并入工程（路径相对本 .sc 目录解析）。
add libssh2.a

# ---------------- 连接句柄 ----------------
# 不透明连接：SSH 会话 + 其 TCP 套接字（对调用方以 & 隐藏）。
def ssh_conn: {
    sess: &         # ::LIBSSH2_SESSION*
    sock: i4        # 已连接 TCP 套接字 fd（sock_connect 返回）
}

# libssh2_init 为进程级全局，只跑一次（WSAStartup 由 sock_connect 内部的 sc_net_init 幂等处理）。
var g_ssh_inited: i4 = 0
fnc ssh_init_once: i4
    if g_ssh_inited != 0
        return 0
    if ::libssh2_init(0) != 0
        return -1
    g_ssh_inited = 1
    return 0

# ---------------- 连接生命周期 ----------------
# 建连并完成 SSH 传输层握手。返回不透明 ssh 连接句柄（失败 nil）。
@fnc ssh_connect: &, host: char&, port: i4
    if host == nil
        return nil
    if ssh_init_once() != 0
        return nil
    var fd: i4 = sock_connect(host, port)
    if fd < 0
        return nil
    var sess: & = (::libssh2_session_init(): &)
    if sess == nil
        sock_close(fd)
        return nil
    ::libssh2_session_set_blocking(sess, 1)
    if ::libssh2_session_handshake(sess, fd) != 0
        ::libssh2_session_free(sess)
        sock_close(fd)
        return nil
    var c: ssh_conn& = (chunk0(sizeof(ssh_conn)): ssh_conn&)
    if c == nil
        ::libssh2_session_free(sess)
        sock_close(fd)
        return nil
    c->sess = sess
    c->sock = fd
    return (c: &)

# 取服务器主机密钥的 SHA-256 指纹（写入 out 的 32 字节）。0=成功 / -1=失败。
#   用于主机密钥校验（与已知 known_hosts 指纹比对，防中间人）。
@fnc ssh_hostkey_sha256: i4, h: &, out: &
    var c: ssh_conn& = (h: ssh_conn&)
    if c == nil || out == nil
        return -1
    var fp: & = (::libssh2_hostkey_hash(c->sess, ::LIBSSH2_HOSTKEY_HASH_SHA256): &)
    if fp == nil
        return -1
    ::memcpy(out, fp, 32)
    return 0

# 断开并释放连接（含底层 socket）。
@fnc ssh_free: h: &
    var c: ssh_conn& = (h: ssh_conn&)
    if c == nil
        return
    if c->sess != nil
        ::libssh2_session_disconnect(c->sess, "bye")
        ::libssh2_session_free(c->sess)
    sock_close(c->sock)
    recycle(c)
    return

# ---------------- 用户认证 ----------------
# 口令认证。返回 0=成功 / -1=失败。
@fnc ssh_auth_password: i4, h: &, user: char&, pass: char&
    var c: ssh_conn& = (h: ssh_conn&)
    if c == nil || user == nil || pass == nil
        return -1
    if ::libssh2_userauth_password(c->sess, user, pass) != 0
        return -1
    return 0

# 公钥认证（从密钥文件）。passphrase 可为 nil/""。
#   注意：mbedTLS 后端需显式给出 pubpath（公钥 .pub 文件），不支持由私钥推导（传 nil 会认证失败）。
#   返回 0=成功 / -1=失败。
@fnc ssh_auth_pubkey: i4, h: &, user: char&, pubpath: char&, privpath: char&, passphrase: char&
    var c: ssh_conn& = (h: ssh_conn&)
    if c == nil || user == nil || privpath == nil
        return -1
    var pass: char& = passphrase
    if pass == nil
        pass = ""
    if ::libssh2_userauth_publickey_fromfile(c->sess, user, pubpath, privpath, pass) != 0
        return -1
    return 0

# ---------------- 远程执行 ----------------
# 执行远程命令，最多取回 cap 字节 stdout 到 buf。返回取回字节数（>=0）/ -1 错误。
@fnc ssh_exec: i4, h: &, cmd: char&, buf: &, cap: u4
    var c: ssh_conn& = (h: ssh_conn&)
    if c == nil || cmd == nil || buf == nil
        return -1
    var ch: & = (::libssh2_channel_open_session(c->sess): &)
    if ch == nil
        return -1
    if ::libssh2_channel_exec(ch, cmd) != 0
        ::libssh2_channel_free(ch)
        return -1
    var total: u4 = 0
    var out: char& = (buf: char&)
    while total < cap
        var n: i8 = ::libssh2_channel_read(ch, &out[total], ((cap - total): u8))
        if n > 0
            total = total + (n: u4)
        else
            break                       # n==0 EOF / n<0 错误：返回已取回部分
    ::libssh2_channel_close(ch)
    ::libssh2_channel_free(ch)
    return (total: i4)

# ---------------- 文件传输（SCP，阻塞式） ----------------
# 普通文件拷贝用 SCP 足矣（SFTP 仅用于远端文件系统操作：列目录 / stat / 续传）。
# 二者均要求会话处于阻塞模式（ssh_connect 后默认如此），勿与 ssh_com 异步适配混用同一连接。
# 下载远端文件到本地路径。返回收到字节数（>=0）/ -1。
@fnc ssh_scp_get: i4, h: &, remotepath: char&, localpath: char&
    var c: ssh_conn& = (h: ssh_conn&)
    if c == nil || remotepath == nil || localpath == nil
        return -1
    var st: ::libssh2_struct_stat                 # 由 libssh2 回填远端文件元信息（含 st_size）
    var ch: & = (::libssh2_scp_recv2(c->sess, remotepath, &st): &)
    if ch == nil
        return -1
    var f: & = (::fopen(localpath, "wb"): &)
    if f == nil
        ::libssh2_channel_free(ch)
        return -1
    var total: i8 = (st.st_size: i8)            # 远端文件总字节数（SCP 需按此界定读取）
    var got: i8 = 0
    var chunk[16384]: char
    var ok: i4 = 1
    while got < total
        var want: u8 = 16384
        if (want: i8) > total - got
            want = ((total - got): u8)
        var n: i8 = ::libssh2_channel_read(ch, &chunk[0], want)
        if n <= 0
            if n != 0
                ok = 0                          # n<0 出错；n==0 提前 EOF 视为已尽力
            break
        if (::fwrite(&chunk[0], 1, (n: u8), f): u8) != (n: u8)
            ok = 0
            break
        got = got + n
    ::fclose(f)
    ::libssh2_channel_close(ch)
    ::libssh2_channel_free(ch)
    if ok != 0 && got == total
        return (got: i4)
    return -1

# 上传本地文件到远端路径，mode 为 POSIX 权限（0 => 0644）。返回发送字节数（>=0）/ -1。
@fnc ssh_scp_put: i4, h: &, localpath: char&, remotepath: char&, mode: i4
    var c: ssh_conn& = (h: ssh_conn&)
    if c == nil || localpath == nil || remotepath == nil
        return -1
    var f: & = (::fopen(localpath, "rb"): &)
    if f == nil
        return -1
    if ::fseek(f, 0, ::SEEK_END) != 0
        ::fclose(f)
        return -1
    var sz: i8 = (::ftell(f): i8)
    if sz < 0
        ::fclose(f)
        return -1
    ::rewind(f)
    var m: i4 = 420                             # 0644
    if mode != 0
        m = mode & 511                          # 0777
    var ch: & = (::libssh2_scp_send64(c->sess, remotepath, m, sz, 0, 0): &)
    if ch == nil
        ::fclose(f)
        return -1
    var chunk[16384]: char
    var sent: i8 = 0
    var ok: i4 = 1
    while ok != 0
        var r: u8 = (::fread(&chunk[0], 1, 16384, f): u8)
        if r == 0
            break
        var off: u8 = 0
        while off < r
            var n: i8 = ::libssh2_channel_write(ch, &chunk[off], r - off)
            if n < 0
                ok = 0
                break
            off = off + (n: u8)
            sent = sent + n
    ::fclose(f)
    ::libssh2_channel_send_eof(ch)
    ::libssh2_channel_wait_eof(ch)
    ::libssh2_channel_wait_closed(ch)
    ::libssh2_channel_free(ch)
    if ok != 0
        return (sent: i4)
    return -1

# ---------------- 异步 com 适配 ----------------
# 把一个 SSH exec 通道封装为 op.h 的 com 设备：远程命令的 stdout/stdin 即可经 sc 异步
# I/O（com >> v / com << v）由 async_io 事件循环驱动。ssh_com 打开并 exec 通道后把会话
# 切为非阻塞；自此勿再用阻塞式 ssh_exec / ssh_scp_* / 认证。owns != 0 时关闭该 com 会一并释放连接。
#
# readable/writable 向多路复用后端暴露底层 TCP 套接字 fd：libssh2 在该 fd 上复用自身协议，
# 对简单 stdout 流式已足够；双向/频繁 rekey 场景可另加 libssh2_session_block_directions()。

# 设备结构：com 端点（com.dev 回指本设备）+ 读/写队列 + 通道/连接句柄。
def ssh_chan_dev: {
    com:  com       # 端点
    rq:   ioq       # 读队列（异步读）
    wq:   ioq       # 写队列（异步写）
    ch:   &         # ::LIBSSH2_CHANNEL*
    conn: &         # ssh_conn*
    owns: i4        # 关闭时是否一并释放连接
}

# limit 缓冲紧随 limit 结构之后分配，data() 返回其首址。
fnc sc_ssh_chan_data: &, _this: limit&
    return ((_this: char&) + sizeof(limit): &)

# com.alloc（每对象 MethodPtr）：构造 limit，把 size、ending 透传写入。
fnc sc_ssh_chan_alloc: limit&, _this: com&, size: u4, ending: &
    var nbytes: u8 = sizeof(limit)
    if size != 0
        nbytes = nbytes + (size: u8)
    else
        nbytes = nbytes + 1
    var s: limit& = (chunk0(nbytes): limit&)
    if s == nil
        return nil
    s->size = size
    s->len = 0
    s->data = sc_ssh_chan_data
    s->ending = ending
    return s

# com.free（每对象 MethodPtr）：释放 limit。
fnc sc_ssh_chan_free: _this: com&, s: limit&
    recycle(s)
    return

# 设备读：channel_read 至多 *size 字节，回写实读字节数。
#   n>0 → 0 / n==0 → sc_eof / EAGAIN → sc_again / 其它 → <0。
fnc sc_ssh_chan_read: ret, _this: com&, data: &, size: u4&
    var d: ssh_chan_dev& = (_this->dev: ssh_chan_dev&)
    if d->ch == nil || size == nil
        return -1
    var want: u4 = *size
    *size = 0
    var n: i8 = ::libssh2_channel_read(d->ch, (data: char&), (want: u8))
    if n > 0
        *size = (n: u4)
        return 0
    if n == 0
        return eof
    if n == ::LIBSSH2_ERROR_EAGAIN
        return again
    return -1

# 设备写：channel_write 至多 *size 字节，回写实写字节数。
#   全部写出 → 0 / 部分或 EAGAIN → sc_again / 其它 → <0。
fnc sc_ssh_chan_write: ret, _this: com&, buf: &, size: u4&
    var d: ssh_chan_dev& = (_this->dev: ssh_chan_dev&)
    if d->ch == nil || size == nil
        return -1
    var want: u4 = *size
    *size = 0
    var n: i8 = ::libssh2_channel_write(d->ch, (buf: char&), (want: u8))
    if n >= 0
        *size = (n: u4)
        if (n: u4) == want
            return 0
        return again
    if n == ::LIBSSH2_ERROR_EAGAIN
        return again
    return -1

fnc sc_ssh_chan_error: ret, _this: com&
    return 0

# 读就绪查询：回填 *id=底层 TCP 套接字 fd，交多路复用后端监听。
fnc sc_ssh_chan_readable: ret, _this: com&, id: &&
    var d: ssh_chan_dev& = (_this->dev: ssh_chan_dev&)
    var cn: ssh_conn& = (d->conn: ssh_conn&)
    *id = ((cn->sock: i8): &)
    if cn->sock < 0
        return -1
    return 1

fnc sc_ssh_chan_writable: ret, _this: com&, id: &&
    var d: ssh_chan_dev& = (_this->dev: ssh_chan_dev&)
    var cn: ssh_conn& = (d->conn: ssh_conn&)
    *id = ((cn->sock: i8): &)
    if cn->sock < 0
        return -1
    return 1

# 关闭设备：关闭通道；owns 则一并释放连接；再回收设备内存。
fnc sc_ssh_chan_close: ret, _this: com&
    var d: ssh_chan_dev& = (_this->dev: ssh_chan_dev&)
    if d->ch != nil
        ::libssh2_channel_close(d->ch)
        ::libssh2_channel_free(d->ch)
        d->ch = nil
    if d->owns != 0 && d->conn != nil
        ssh_free(d->conn)
    recycle(d)
    return 0

# 打开一个 exec 通道并封装为异步 com 设备。返回 com&（失败 nil）。
# owns != 0 时关闭该 com 会一并释放连接。
@fnc ssh_com: com&, h: &, cmd: char&, owns: i4
    var c: ssh_conn& = (h: ssh_conn&)
    if c == nil || cmd == nil
        return nil
    var ch: & = (::libssh2_channel_open_session(c->sess): &)
    if ch == nil
        return nil
    if ::libssh2_channel_exec(ch, cmd) != 0
        ::libssh2_channel_free(ch)
        return nil
    var d: ssh_chan_dev& = (chunk0(sizeof(ssh_chan_dev)): ssh_chan_dev&)
    if d == nil
        ::libssh2_channel_free(ch)
        return nil
    d->ch = ch
    d->conn = (c: &)
    d->owns = owns
    # 自此异步流式：会话切非阻塞
    ::libssh2_session_set_blocking(c->sess, 0)
    d->com.dev = (d: &)
    d->com.alloc = sc_ssh_chan_alloc
    d->com.free = sc_ssh_chan_free
    d->com.read = sc_ssh_chan_read
    d->com.write = sc_ssh_chan_write
    d->com.error = sc_ssh_chan_error
    d->com.readable = sc_ssh_chan_readable
    d->com.writable = sc_ssh_chan_writable
    d->com.close = sc_ssh_chan_close
    d->rq.com = &d->com
    d->com.rq = &d->rq                          # 异步读
    d->wq.com = &d->com
    d->com.wq = &d->wq                          # 异步写
    return (d: com&)                            # com 为首字段：&d->com 即 d（避免取局部地址）
