# 特性 19：com 同步收发 rpc 形态（com << rpc(参数) / com >> rpc）
#
# << rpc(args)：把 rpc 调用的参数序列化写入 com（不触发本地调用），跳过返回槽 _；
#              com[...] 句柄参数无法序列化 → 编译报错（须由裸字节流承载）。
# >> rpc      ：从 com 读出参数结构体，然后触发 rpc（参数来自 com）；
#              com[...] 句柄参数走框架读流程 limit_read，从同一 com 读入。
# 覆盖：标量、数组、com[...] 句柄三类参数。用内存回环 com 验证 round-trip。

#-------------- 内存回环 com 设备：write 追加到缓冲，read 从缓冲消费 -------------
def membuf: {
    data[256]: char                     # 收发共享缓冲
    wpos: u4
    rpos: u4
    box: limit                          # com[...] 句柄的 limit 存储
    rbuf[16]: char                      # com[...] 读目标缓冲
}

fnc mb_write: ret, _this: com&, buf: &, size: u4&
    var m:   membuf& = (_this->dev: membuf&)
    var src: char&   = (buf: char&)
    var n: u4 = 0
    while n < *size
        m->data[m->wpos] = src[n]
        m->wpos = m->wpos + 1
        n = n + 1
    return (n: i4)

fnc mb_read: ret, _this: com&, buf: &, size: u4&
    var m:   membuf& = (_this->dev: membuf&)
    var dst: char&   = (buf: char&)
    var n: u4 = 0
    while n < *size
        dst[n] = m->data[m->rpos]
        m->rpos = m->rpos + 1
        n = n + 1
    *size = n
    return (n: i4)

# com[...] 句柄支撑：limit.data() 返回读目标缓冲；alloc 把 size/ending 写入 limit。
fnc mb_data: &, _this: limit&
    var m: membuf& = (_this->_self->dev: membuf&)
    return &m->rbuf[0]

fnc mb_alloc: limit&, _this: com&, size: u4, ending: &
    var m: membuf& = (_this->dev: membuf&)
    var s: limit& = &m->box
    s->size   = size
    s->len    = 0
    s->data   = mb_data
    s->ending = ending
    return s

fnc mb_free: _this: com&, s: limit&
    return

#-------------- 收端触发的 rpc：标量参数 ----------------------------------------
rpc handle: i4, a: i4, b: i4
    ::printf("标量 rpc: a=%d b=%d\n", a, b)
    return 0

#-------------- 收端触发的 rpc：数组参数 ----------------------------------------
rpc handle_buf: i4, tag: i4, buf[8]: char
    ::printf("数组 rpc: tag=%d buf=%s\n", tag, &buf[0])
    return 0

#-------------- 收端触发的 rpc：com[...] 句柄参数（定长 16）---------------------
rpc handle_body: i4, n: i4, body: com[16, nil]
    var p: char& = (body._->data(): char&)
    ::printf("句柄 rpc: n=%d body(%u)=", n, body._->len)
    var k: i4 = 0
    while (k: u4) < body._->len
        ::printf("%c", p[k])
        k = k + 1
    ::printf("\n")
    return 0

fnc main: i4
    var mb: membuf
    mb.wpos = 0
    mb.rpos = 0

    var c: com
    c.read  = mb_read
    c.write = mb_write
    c.alloc = mb_alloc
    c.free  = mb_free
    c.dev   = &mb

    # 标量：发参数 → 收并触发
    c << handle(7, 9)
    c >> handle                          # 标量 rpc: a=7 b=9

    # 数组：发参数 → 收并触发
    var msg[8]: char
    msg[0] = 'h'
    msg[1] = 'i'
    msg[2] = 0
    c << handle_buf(42, msg)
    c >> handle_buf                      # 数组 rpc: tag=42 buf=hi

    # com[...] 句柄：n 经普通字段，body 内容经裸字节流写入（句柄不可序列化）；
    # 收端 c >> handle_body 把 n 读入，body 走 limit_read 从同一 com 读 16 字节。
    var n5: i4 = 5
    var raw[16]: char
    var i: i4 = 0
    while i < 16
        raw[i] = '.'
        i = i + 1
    raw[0] = 'O'
    raw[1] = 'K'
    c << n5
    c << raw
    c >> handle_body                     # 句柄 rpc: n=5 body(16)=OK..............
    return 0
