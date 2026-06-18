# 特性 21：com << / >> 异步形态的【E】【F】（rpc 内自动套用 await 状态机）
#
# 承接特性 19（同步【B】【C】）与特性 17（异步【D】）。rpc 内 << / >> 为异步收发：
#   ·【F】rpc 序列化：com << rpc(实参) / com >> rpc，逐参数字段一个 await 让出点，
#                    发起 com_write_async / com_read_async，io 就绪后由事件循环恢复；
#                    收端读齐参数再触发本地 worker（不取返回）。
#   ·【E】com[...] 句柄：序列化收端遇 com[...] 参数 → 绑定句柄到本 com，走框架确定
#                    读流程 com_limit_read_async（遇 again 挂起、设备就绪后续读，命中
#                    ending/定长再兑现）。
# 含 << / >> 的 rpc 因此被编译为状态机：跨收发存活的局部/参数提升到帧，rpc 参数槽
# 堆分配挂在帧 _crpcN（跨 await 存活）。用内存回环 com 验证 round-trip。

inc stdio.h
inc async.sc                            # 事件循环运行时（async_init/loop/final + future）

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

# 读就绪查询：本设备不支持多路复用（*id=nil），且始终就绪（返回 1）。
fnc mb_readable: ret, _this: com&, id: &&
    *id = nil
    return 1

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

# 16 字节裸缓冲填充 "OK".............."（异步 rpc 体为直线状态机，循环放在同步辅助里）。
fnc fill_raw: dst: char&
    var i: i4 = 0
    while i < 16
        dst[i] = '.'
        i = i + 1
    dst[0] = 'O'
    dst[1] = 'K'
    return

#-------------- 收端触发的 rpc：标量参数 ----------------------------------------
rpc greet: i4, a: i4, b: i4
    printf("  标量 rpc: a=%d b=%d\n", a, b)
    return 0

#-------------- 收端触发的 rpc：数组参数 ----------------------------------------
rpc greet_buf: i4, tag: i4, buf[8]: char
    printf("  数组 rpc: tag=%d buf=%s\n", tag, &buf[0])
    return 0

#-------------- 收端触发的 rpc：com[...] 句柄参数（定长 16）---------------------
rpc greet_body: i4, n: i4, body: com[16, nil]
    var p: char& = (body._->data(): char&)
    printf("  句柄 rpc: n=%d body(%u)=", n, body._->len)
    var k: i4 = 0
    while (k: u4) < body._->len
        printf("%c", p[k])
        k = k + 1
    printf("\n")
    return 0

#-- 异步会话 rpc：含 com >> / << ⇒ 自动状态机（标量 / 数组 / 句柄三类序列化）------
rpc session: ret, c: com&
    # 【F】标量：发参数 → 收并触发（每字段一个 await 让出点）
    c << greet(7, 9)
    c >> greet                          #   标量 rpc: a=7 b=9

    # 【F】数组：发参数 → 收并触发
    var msg[8]: char
    msg[0] = 'h'
    msg[1] = 'i'
    msg[2] = 0
    c << greet_buf(42, msg)
    c >> greet_buf                      #   数组 rpc: tag=42 buf=hi

    # 【E】+【F】句柄：n 经普通字段，body 内容经裸字节流写入（句柄不可序列化）；
    # 收端把 n 读入，body 绑定本 com 走 com_limit_read_async 异步框架读 16 字节。
    var n5: i4 = 5
    var raw[16]: char
    fill_raw(&raw[0])
    c << n5
    c << raw
    c >> greet_body                     #   句柄 rpc: n=5 body(16)=OK..............
    return 0

fnc main: i4
    async_init()                        # 建立当前线程事件循环

    var mb: membuf
    mb.wpos = 0
    mb.rpos = 0

    var c: com
    c.read     = mb_read
    c.write    = mb_write
    c.alloc    = mb_alloc
    c.free     = mb_free
    c.readable = mb_readable
    c.dev      = &mb

    var f: future& = async session(&c)  # 挂起式启动 rpc，立即返回 future
    async_loop(nil)                     # 驱动事件循环，推进状态机直到完成

    printf("done\n")
    async_final()                       # 销毁事件循环
    return 0
