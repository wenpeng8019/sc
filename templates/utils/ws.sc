# ws —— WebSocket 帧/握手层组件（构建在 tcp com 设备 + 异步事件循环之上）
#
# 定位：templates 通用 utils 组件。利用 io.sc 的 tcp() com 设备做传输，crypto.sc 的
#   SHA-1 + Base64 算握手 accept key，把 RFC 6455 的握手 + 帧编解码封成可复用的 async rpc。
#   协议完备性移植自 p2p_server/custom_ws.c（回调框架），适配为 sc 的 await/rpc 拉取模型。
#
# 依赖：inc io.sc（tcp 设备 + com）/ inc crypto.sc（crypto_sha1 + crypto_base64）/ inc async.sc（事件循环）
#   / inc os.sc（os_rand：客户端随机掩码键）。
#   帧/握手报文统一用定长 char&/u1& 缓冲（调用方提供容量）：帧编解码是字节级热路径，
#   定长缓冲零堆分配、零 adt 依赖，比堆 string 更契合 codec 场景（非可见性所迫）。
#   使用方在自己的 async rpc 里 `await ws_handshake(c)` / `await ws_recv(&cn,...)` / `await ws_send(...)`。
#
# RFC 6455 完备特性：
#   · 握手：服务端解析 Sec-WebSocket-Key、算 accept、回 101；客户端发升级请求并读 101。
#   · 帧解析：2/4/10 字节帧头、126/127 扩展长度、掩码键；RSV 位必须为 0；保留 opcode 拒绝。
#   · 掩码角色（§5.3）：服务端收客户端帧必须带掩码、自身发帧不掩码；客户端反之（os_rand 随机键）。
#   · 控制帧（§5.5）：必须 FIN 且 ≤125；PING 自动回 PONG；CLOSE 自动回复并校验状态码 + reason UTF-8。
#   · 分片（§5.4）：CONTINUATION 状态机一致性校验 + 重组到调用方缓冲；容量不足回 1009 并关闭（不流失步）。
#   · UTF-8（§8.1）：TEXT（含跨分片）payload 增量 DFA 校验，FIN 时确保多字节序列不被截断。
#   · close 码（§7.4.1）：1000-1011 不含 1004/1005/1006，或 3000-4999。
#
# 设计要点：
#   · ws_recv 内部循环：透明消化控制帧（PING→PONG、CLOSE 报告）与分片，只向调用方返回完整数据消息。
#   · 每连接协议状态（分片 opcode / 跨片 UTF-8 状态 / 角色 / 关闭）存于 ws_conn，调用方持有并传入。
#   · 重逻辑（解析/codec/建头/UTF-8/close 码）放 sync fnc；async rpc 只做 io + 调 helper + 赋值。

inc io.sc
inc crypto.sc
inc async.sc
inc os.sc

# op.h 恒链接的异步 io 桥接原语（C ABI：future *com_*_async(struct com*, void*, uint32_t)）。
# 声明为 @fnc 即可在 async rpc 里 `await` —— 得到运行时变长的异步读/写。
@fnc com_read_async::  future&, c: com&, buf: &, n: u4
@fnc com_write_async:: future&, c: com&, buf: &, n: u4

# ---------------- WebSocket opcode（RFC 6455 §5.2）----------------
# 枚举成员为全局常量（裸名访问，非 Enum.member），故统一加 WS_ 前缀防冲突。
@def ws_op: [
    WS_CONT  = 0
    WS_TEXT  = 1
    WS_BIN   = 2
    WS_CLOSE = 8
    WS_PING  = 9
    WS_PONG  = 10
] : i4

# 一帧的元信息（ws_recv 出参）
@def ws_frame: {
    opcode: u1      # 操作码（ws_op.*）
    fin:    u1      # FIN 位（1=消息结束帧）
    len:    u4      # 载荷字节数
}

# 零拷贝转发预留前缀（字节）。约定：转发缓冲 buf 的 [0, WS_HDR_MAX) 为帧头预留区，
# 载荷落在 buf[WS_HDR_MAX..]；转发时把新帧头写进前缀末尾（紧靠载荷），头+载荷连续 → 单次写。
# 取 14 可容纳带掩码客户端帧的最大帧头（2+8+4）；服务端无掩码帧头 ≤10，亦在范围内。
@def ws_hdr_max: [ WS_HDR_MAX = 14 ] : u4

# ---------------- 关闭状态码（RFC 6455 §7.4.1）----------------
@def ws_close_code: [
    WS_CLOSE_NORMAL          = 1000   # 正常关闭
    WS_CLOSE_GOING_AWAY      = 1001   # 端点离开
    WS_CLOSE_PROTOCOL_ERROR  = 1002   # 协议错误
    WS_CLOSE_UNSUPPORTED     = 1003   # 收到无法接受的数据类型
    WS_CLOSE_INVALID_DATA    = 1007   # 载荷与帧类型不符（如 TEXT 非法 UTF-8）
    WS_CLOSE_POLICY          = 1008   # 违反策略
    WS_CLOSE_TOO_BIG         = 1009   # 消息过大
    WS_CLOSE_INTERNAL_ERROR  = 1011   # 内部错误
] : i4

# ws_recv 返回的特殊负码（>=0 为数据消息长度；fr->opcode 标明类型）。
@def ws_recv_ret: [
    WS_RECV_IO    = 0 - 1   # io 错误 / 连接断开
    WS_RECV_PROTO = 0 - 2   # 协议错误（已自动回 close 帧）
] : i4

# UTF-8 DFA 终态（Bjoern Hoehrmann DFA，RFC 6455 §8.1 文本帧必须合法 UTF-8）。
@def ws_utf8_state: [
    WS_UTF8_ACCEPT = 0    # 已完成一个合法序列
    WS_UTF8_REJECT = 12   # 非法
] : u4

# UTF-8 DFA 表：[0..255] 为字节→字符类（0‥11）；[256..363] 为 state×class→下一 state。
# 逐字节移植自 custom_ws.c 的 g_ws_utf8d[364]，保持完全一致。
var g_ws_utf8d[364]: u1 = [
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, 7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,
    0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
    12,0,12,12,12,12,12,0,12,0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
    12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
    12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
    12,36,12,12,12,12,12,12,12,12,12,12
]

# 逐字节推进 UTF-8 DFA，返回新 state（WS_UTF8_REJECT=12 表示非法）。
@fnc ws_utf8_check: u4, st: u4, data: u1&, len: u4
    var s: u4 = st
    var i: u4 = 0
    while i < len
        var t: u4 = (g_ws_utf8d[(data[i]: u4)]: u4)
        s = (g_ws_utf8d[256 + s + t]: u4)
        if s == WS_UTF8_REJECT
            return WS_UTF8_REJECT
        i = i + 1
    return s

# close 状态码合法性（RFC 6455 §7.4.1）：1000-1011 不含 1004/1005/1006，或 3000-4999。
@fnc ws_valid_close_code: i4, c: u4
    if c >= 1000 && c <= 1011 && c != 1004 && c != 1005 && c != 1006
        return 1
    if c >= 3000 && c <= 4999
        return 1
    return 0

# 每连接的 WebSocket 协议状态（分片重组 + 跨片 UTF-8 + 角色）。
@def ws_conn: {
    is_server: u1   # 1=服务端（要求对端帧带掩码、自身发帧不掩码）；0=客户端
    frag_op:   u1   # 进行中分片消息的起始 opcode（0=当前无分片）
    utf8st:    u4   # 跨分片累积的 UTF-8 DFA 状态
    closing:   u1   # 已收到/回复 CLOSE
    code:      u4   # 对端 CLOSE 状态码
}

# 初始化连接状态。is_server!=0 表示作为服务端（收掩码帧、发非掩码帧）。
@fnc ws_conn_init: i4, cn: ws_conn&, is_server: i4
    cn->is_server = (is_server != 0 ? 1 : 0)
    cn->frag_op = 0
    cn->utf8st = WS_UTF8_ACCEPT
    cn->closing = 0
    cn->code = 0
    return 0

# ================================ 握手 ================================

# 子串查找：在 hay[0..haylen) 找 needle[0..nlen)，返回首次出现下标，未找到 -1。
@fnc ws_find: i4, hay: char&, haylen: u4, needle: char&, nlen: u4
    var i: u4 = 0
    while i + nlen <= haylen
        var j: u4 = 0
        while j < nlen && hay[i + j] == needle[j]
            j = j + 1
        if j == nlen
            return (i: i4)
        i = i + 1
    return -1

# 把以 0 结尾的 src 追加到 dst[off..]，返回新偏移（零 adt 的字符串拼接）。
@fnc ws_puts: u4, dst: char&, off: u4, src: char&
    var i: u4 = 0
    while src[i] != 0
        dst[off + i] = src[i]
        i = i + 1
    return off + i

# 由 key[0..keylen) 计算 Sec-WebSocket-Accept 写入 out（>=32 字节），返回长度。
# accept = Base64( SHA1( key + WS_GUID ) )，WS_GUID 见 RFC 6455 §1.3。SHA1/Base64 由 crypto 内置提供。
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
    crypto_sha1((&buf[0]: &), (n: u8), (&digest[0]: u1&))
    return crypto_base64((&digest[0]: &), 20, out)

# 从 HTTP 请求里解析 Sec-WebSocket-Key 的值，拷进 keyout（调用方提供 >=64 字节），返回长度 / -1。
@fnc ws_parse_key: i4, req: char&, reqlen: u4, keyout: char&
    var name: char& = "Sec-WebSocket-Key:"
    var pos: i4 = ws_find(req, reqlen, name, 18)
    if pos < 0
        return -1
    var i: u4 = (pos: u4) + 18
    while i < reqlen && req[i] == ' '
        i = i + 1
    var n: i4 = 0
    while i < reqlen && req[i] != '\r' && req[i] != '\n'
        keyout[n] = req[i]
        n = n + 1
        i = i + 1
    return n

# 服务端握手：逐字节读 HTTP 升级请求至 \r\n\r\n → 解析 key → 算 accept → 回 101 响应。
# 返回 0 / <0 错。（逐字节读避免越界吞入后续帧；请求短小，足够通用。）
@rpc ws_handshake: i4, c: com&
    var req[2048]: char
    var pos: u4 = 0
    var found: i4 = 0
    while found == 0 && pos < 2047
        var r: i4 = await com_read_async(c, (&req[pos]: &), 1)
        if r <= 0
            return -1
        pos = pos + 1
        if pos >= 4 && req[pos - 4] == '\r' && req[pos - 3] == '\n' && req[pos - 2] == '\r' && req[pos - 1] == '\n'
            found = 1
    if found == 0
        return -1
    var key[128]: char
    var klen: i4 = ws_parse_key((&req[0]: char&), pos, (&key[0]: char&))
    if klen <= 0
        return -1
    key[klen] = 0
    var accept[64]: char
    var alen: i4 = ws_accept_buf((&key[0]: char&), (klen: u4), (&accept[0]: char&))
    accept[alen] = 0
    var resp[256]: char
    var ro: u4 = 0
    ro = ws_puts((&resp[0]: char&), ro, "HTTP/1.1 101 Switching Protocols\r\n")
    ro = ws_puts((&resp[0]: char&), ro, "Upgrade: websocket\r\n")
    ro = ws_puts((&resp[0]: char&), ro, "Connection: Upgrade\r\n")
    ro = ws_puts((&resp[0]: char&), ro, "Sec-WebSocket-Accept: ")
    ro = ws_puts((&resp[0]: char&), ro, (&accept[0]: char&))
    ro = ws_puts((&resp[0]: char&), ro, "\r\n\r\n")
    var w: i4 = await com_write_async(c, (&resp[0]: &), ro)
    if w < 0
        return -1
    return 0

# 客户端握手：发升级请求（含固定 key）→ 读并丢弃 101 响应。返回 0 / <0 错。
# （仅供自测客户端使用；固定 key 对应 accept 由服务端按 RFC 计算，无需校验即可联通。）
@rpc ws_client_handshake: i4, c: com&
    var req: char& = "GET / HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n"
    var rlen: u4 = 0
    while req[rlen] != 0
        rlen = rlen + 1
    var w: i4 = await com_write_async(c, (req: &), rlen)
    if w < 0
        return -1
    var resp[2048]: char
    var pos: u4 = 0
    var found: i4 = 0
    while found == 0 && pos < 2047
        var r: i4 = await com_read_async(c, (&resp[pos]: &), 1)
        if r <= 0
            return -1
        pos = pos + 1
        if pos >= 4 && resp[pos - 4] == '\r' && resp[pos - 3] == '\n' && resp[pos - 2] == '\r' && resp[pos - 1] == '\n'
            found = 1
    if found == 0
        return -1
    return 0

# ================================ 收帧 ================================

# 解掩码：payload[0..plen) 逐字节 XOR mask[i&3]（mask 为 4 字节）。
@fnc ws_unmask: i4, payload: u1&, plen: u4, mask: u1&
    var i: u4 = 0
    while i < plen
        payload[i] = payload[i] ^ mask[i & 3]
        i = i + 1
    return 0

# 写帧头到 hdr（调用方提供 >=14 字节），返回头长度。mask!=0 时置 MASK 位并写 4 字节掩码键。
@fnc ws_build_header: i4, hdr: u1&, opcode: u1, plen: u4, mask: i4, maskkey: u1&
    hdr[0] = 0x80 | opcode
    var hlen: i4 = 0
    var maskbit: u1 = 0
    if mask != 0
        maskbit = 0x80
    if plen < 126
        hdr[1] = maskbit | (plen: u1)
        hlen = 2
    else if plen < 65536
        hdr[1] = maskbit | 126
        hdr[2] = ((plen >> 8) & 0xFF: u1)
        hdr[3] = (plen & 0xFF: u1)
        hlen = 4
    else
        hdr[1] = maskbit | 127
        hdr[2] = 0
        hdr[3] = 0
        hdr[4] = 0
        hdr[5] = 0
        hdr[6] = ((plen >> 24) & 0xFF: u1)
        hdr[7] = ((plen >> 16) & 0xFF: u1)
        hdr[8] = ((plen >> 8) & 0xFF: u1)
        hdr[9] = (plen & 0xFF: u1)
        hlen = 10
    if mask != 0
        hdr[hlen + 0] = maskkey[0]
        hdr[hlen + 1] = maskkey[1]
        hdr[hlen + 2] = maskkey[2]
        hdr[hlen + 3] = maskkey[3]
        hlen = hlen + 4
    return hlen

# 内部：发送一帧控制/数据（pl[0..plen)）。服务端不加掩码；客户端用 os_rand 随机掩码键
# 就地掩码 pl（会修改 pl 缓冲）。返回 0 / <0 错。
@rpc ws_emit: i4, cn: ws_conn&, opcode: u1, pl: u1&, plen: u4, c: com&
    var hdr[14]: u1
    var mk[4]: u1
    var domask: i4 = 0
    if cn->is_server == 0
        domask = 1
        os_rand((&mk[0]: &), 4)
    var hlen: i4 = ws_build_header((&hdr[0]: u1&), opcode, plen, domask, (&mk[0]: u1&))
    if domask != 0 && plen > 0
        ws_unmask(pl, plen, (&mk[0]: u1&))
    var w1: i4 = await com_write_async(c, (&hdr[0]: &), (hlen: u4))
    if w1 < 0
        return -1
    if plen > 0
        var w2: i4 = await com_write_async(c, (pl: &), plen)
        if w2 < 0
            return -1
    return 0

# 内部：构造并发送 close 帧（携带 2 字节状态码）。返回 0 / <0 错。
@rpc ws_send_close: i4, cn: ws_conn&, code: u4, c: com&
    var pl[2]: u1
    pl[0] = ((code >> 8) & 0xFF: u1)
    pl[1] = (code & 0xFF: u1)
    var r: i4 = await ws_emit(cn, (WS_CLOSE: u1), (&pl[0]: u1&), 2, c)
    cn->closing = 1
    return r

# 收一条完整数据消息到 out（调用方提供 cap 字节缓冲），元信息填 fr。
# 透明处理：RSV/保留 opcode/掩码角色校验、控制帧（PING 自动回 PONG、CLOSE 自动回复并报告）、
# 分片重组（CONTINUATION 状态机 + 跨片 UTF-8 增量校验）。返回：
#   >=0  一条完整数据消息长度（fr->opcode=TEXT/BIN，fr->fin=1，fr->len=该长度）；
#        若 fr->opcode=WS_CLOSE 表示收到对端关闭（返回 0，cn->code 为状态码，已自动回复）。
#   WS_RECV_IO(-1)    io 错误 / 对端断开。
#   WS_RECV_PROTO(-2) 协议违规（已自动回 close 帧）。
@rpc ws_recv: i4, cn: ws_conn&, fr: ws_frame&, out: u1&, cap: u4, c: com&
    var msg_len: u4 = 0          # 已重组的分片消息总长
    var ctl[125]: u1             # 控制帧载荷（≤125）独立缓冲，不扰乱分片重组
    while 1 == 1
        # ---- 基础帧头（2 字节）----
        var hdr[2]: u1
        var r0: i4 = await com_read_async(c, (&hdr[0]: &), 2)
        if r0 <= 0
            return WS_RECV_IO
        var fin: u1 = (hdr[0] >> 7) & 0x01
        var opcode: u1 = hdr[0] & 0x0F
        var masked: u1 = (hdr[1] >> 7) & 0x01
        var len7: u4 = (hdr[1] & 0x7F: u4)
        # RFC §5.2：RSV1/2/3 必须为 0（未协商扩展）
        if (hdr[0] & 0x70) != 0
            var e1: i4 = await ws_send_close(cn, WS_CLOSE_PROTOCOL_ERROR, c)
            return WS_RECV_PROTO
        # 掩码角色校验（RFC §5.3）：服务端收客户端帧必须带掩码；客户端收服务端帧不得带掩码
        if (cn->is_server == 1 && masked == 0) || (cn->is_server == 0 && masked == 1)
            var e2: i4 = await ws_send_close(cn, WS_CLOSE_PROTOCOL_ERROR, c)
            return WS_RECV_PROTO
        # RFC §5.2：保留 opcode（0x3-0x7 数据帧、0xB-0xF 控制帧）必须拒绝
        if ((opcode: i4) >= 3 && (opcode: i4) <= 7) || (opcode: i4) >= 11
            var e3: i4 = await ws_send_close(cn, WS_CLOSE_PROTOCOL_ERROR, c)
            return WS_RECV_PROTO
        # ---- 扩展长度 ----
        var plen: u4 = len7
        if len7 == 126
            var ext[2]: u1
            var re: i4 = await com_read_async(c, (&ext[0]: &), 2)
            if re <= 0
                return WS_RECV_IO
            plen = ((ext[0]: u4) << 8) | (ext[1]: u4)
        else if len7 == 127
            var ext8[8]: u1
            var re8: i4 = await com_read_async(c, (&ext8[0]: &), 8)
            if re8 <= 0
                return WS_RECV_IO
            # 仅取低 32 位（应用层 cap 限制，远小于 4GB）
            plen = ((ext8[4]: u4) << 24) | ((ext8[5]: u4) << 16) | ((ext8[6]: u4) << 8) | (ext8[7]: u4)
        # ---- 掩码键 ----
        var mask[4]: u1
        if masked == 1
            var rm: i4 = await com_read_async(c, (&mask[0]: &), 4)
            if rm <= 0
                return WS_RECV_IO
        # ---- 控制帧（CLOSE/PING/PONG）----
        if (opcode: i4) >= WS_CLOSE
            # RFC §5.5：控制帧必须 FIN，且 payload ≤ 125
            if fin == 0 || plen > 125
                var e4: i4 = await ws_send_close(cn, WS_CLOSE_PROTOCOL_ERROR, c)
                return WS_RECV_PROTO
            if plen > 0
                var rc: i4 = await com_read_async(c, (&ctl[0]: &), plen)
                if rc <= 0
                    return WS_RECV_IO
                if masked == 1
                    ws_unmask((&ctl[0]: u1&), plen, (&mask[0]: u1&))
            if opcode == WS_CLOSE
                var code: u4 = (WS_CLOSE_NORMAL: u4)
                if plen >= 2
                    code = ((ctl[0]: u4) << 8) | (ctl[1]: u4)
                    if ws_valid_close_code(code) == 0
                        var ec: i4 = await ws_send_close(cn, WS_CLOSE_PROTOCOL_ERROR, c)
                        return WS_RECV_PROTO
                    if plen > 2
                        if ws_utf8_check(WS_UTF8_ACCEPT, (&ctl[2]: u1&), plen - 2) != WS_UTF8_ACCEPT
                            var eu: i4 = await ws_send_close(cn, WS_CLOSE_PROTOCOL_ERROR, c)
                            return WS_RECV_PROTO
                cn->code = code
                cn->closing = 1
                # 自动回复 close（回显对端状态码）
                var er: i4 = await ws_send_close(cn, code, c)
                fr->opcode = (WS_CLOSE: u1)
                fr->fin = 1
                fr->len = 0
                return 0
            else if opcode == WS_PING
                # 自动回 PONG，携带相同 payload
                var ep: i4 = await ws_emit(cn, (WS_PONG: u1), (&ctl[0]: u1&), plen, c)
                if ep < 0
                    return WS_RECV_IO
                # 继续读下一帧
            # PONG：忽略，继续读下一帧
        else
            # ---- 数据帧（TEXT/BIN/CONTINUATION）----
            # RFC §5.4：分片状态机一致性
            if cn->frag_op != 0
                if opcode == WS_TEXT || opcode == WS_BIN
                    var e5: i4 = await ws_send_close(cn, WS_CLOSE_PROTOCOL_ERROR, c)
                    return WS_RECV_PROTO
            else
                if opcode == WS_CONT
                    var e6: i4 = await ws_send_close(cn, WS_CLOSE_PROTOCOL_ERROR, c)
                    return WS_RECV_PROTO
            # 容量校验：放不下则关闭（1009 消息过大），避免半读导致流失步
            if msg_len + plen > cap
                var e7: i4 = await ws_send_close(cn, WS_CLOSE_TOO_BIG, c)
                return WS_RECV_PROTO
            if plen > 0
                var rp: i4 = await com_read_async(c, (&out[msg_len]: &), plen)
                if rp <= 0
                    return WS_RECV_IO
                if masked == 1
                    ws_unmask((&out[msg_len]: u1&), plen, (&mask[0]: u1&))
            # RFC §8.1：TEXT 消息（含分片）payload 必须合法 UTF-8，逐片增量校验
            var eff_text: i4 = 0
            if opcode == WS_TEXT
                cn->utf8st = WS_UTF8_ACCEPT       # 新消息重置 DFA
                eff_text = 1
            else if opcode == WS_CONT && cn->frag_op == WS_TEXT
                eff_text = 1
            if eff_text == 1
                cn->utf8st = ws_utf8_check(cn->utf8st, (&out[msg_len]: u1&), plen)
                if cn->utf8st == WS_UTF8_REJECT
                    var e8: i4 = await ws_send_close(cn, WS_CLOSE_INVALID_DATA, c)
                    return WS_RECV_PROTO
                if fin == 1 && cn->utf8st != WS_UTF8_ACCEPT
                    # FIN 时多字节序列被截断 → 非法
                    var e9: i4 = await ws_send_close(cn, WS_CLOSE_INVALID_DATA, c)
                    return WS_RECV_PROTO
            # 分片记账
            if opcode != WS_CONT && fin == 0
                cn->frag_op = opcode              # 分片序列起始
            msg_len = msg_len + plen
            if fin == 1
                fr->opcode = (cn->frag_op != 0 ? cn->frag_op : opcode)
                fr->fin = 1
                fr->len = msg_len
                cn->frag_op = 0
                return (msg_len: i4)
            # 非 FIN：继续读后续分片
    return WS_RECV_IO

# ================================ 发帧 ================================

# 发一帧。mask!=0（客户端）：用 os_rand 生成随机掩码键并就地掩码 payload（会修改缓冲）。
# RFC §5.3 要求客户端掩码键不可预测，故每帧重新随机。返回 0 / <0 错。
@rpc ws_send: i4, opcode: u1, payload: u1&, plen: u4, mask: i4, c: com&
    var hdr[14]: u1
    var maskkey[4]: u1
    if mask != 0
        os_rand((&maskkey[0]: &), 4)
    var hlen: i4 = ws_build_header((&hdr[0]: u1&), opcode, plen, mask, (&maskkey[0]: u1&))
    if mask != 0 && plen > 0
        ws_unmask(payload, plen, (&maskkey[0]: u1&))
    var w1: i4 = await com_write_async(c, (&hdr[0]: &), (hlen: u4))
    if w1 < 0
        return -1
    if plen > 0
        var w2: i4 = await com_write_async(c, (payload: &), plen)
        if w2 < 0
            return -1
    return 0

# 零拷贝转发：把一条已收到的消息整块转发到 dst，载荷不拷贝、单次 syscall（移植自
# custom_ws.c 的「预留前缀 + 原地建头 + 连续写」）。
#   buf 必须有 WS_HDR_MAX 字节预留前缀，载荷在 buf[WS_HDR_MAX .. WS_HDR_MAX+plen)
#   —— 即调用方收帧时用 `ws_recv(&cn, &fr, (&buf[WS_HDR_MAX]: u1&), cap, dst_com)`。
# 据 cn->is_server 决定是否掩码：服务端不掩码（纯零拷贝）；客户端就地掩码载荷后仍单次连续写。
# 返回 0 / <0 错。
@rpc ws_forward: i4, cn: ws_conn&, opcode: u1, buf: u1&, plen: u4, dst: com&
    var hdrtmp[14]: u1
    var mk[4]: u1
    var domask: i4 = 0
    if cn->is_server == 0
        domask = 1
        os_rand((&mk[0]: &), 4)
    var hlen: i4 = ws_build_header((&hdrtmp[0]: u1&), opcode, plen, domask, (&mk[0]: u1&))
    # 把帧头写进预留前缀末尾（紧靠载荷），头+载荷连续；仅拷贝 ≤14 字节小头，载荷不动。
    var off: u4 = (WS_HDR_MAX: u4) - (hlen: u4)
    var i: u4 = 0
    while (i: i4) < hlen
        buf[off + i] = hdrtmp[i]
        i = i + 1
    # 客户端需掩码：就地 XOR 载荷（mask key 已在帧头末尾、紧靠载荷）。
    if domask != 0 && plen > 0
        ws_unmask((&buf[WS_HDR_MAX]: u1&), plen, (&mk[0]: u1&))
    # 单次连续写：帧头 + 载荷一起发出，零载荷拷贝、单 syscall。
    var w: i4 = await com_write_async(dst, (&buf[off]: &), (hlen: u4) + plen)
    if w < 0
        return -1
    return 0
