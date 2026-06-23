# 特性 17：com << / >> 的「异步形态」（rpc 内自动套用 await 状态机）
#
# << / >> 上下文分形（承接特性 16 的同步形态）：
#   · fnc 内  → 同步收发（直接调 write/read，立即返回）——见特性 16；
#   · rpc 内  → 异步收发：编译器把每个 com >> v / com << v 自动展开为一个 await 点，
#               发起 com_read_async / com_write_async（产出 future）→ 登记本帧为
#               waiter → 让出；io 就绪后由事件循环恢复 rpc 状态机。
# 含 com 收发的 rpc 因此被编译为状态机（同特性 13 的 await）：跨收发存活的局部与参数
# 提升到帧结构体，函数体按收发点切段。com& 通讯通道参数须位于 rpc 参数表末位。
#
# 当前异步驱动为「立即完成」桥接（com_*_async 同步调一次 read/write 后即兑现 future），
# 用于打通端到端语义；真实驱动改为入队 rq/wq、io 完成回调里延迟兑现（ioq 循环缓冲已就位）。

inc async.sc                       # 引入事件循环运行时（async_init/loop/final + future）

#-------------- 设备 io 实现（每对象绑定，MethodPtr）-----------------------
# 设备读：把 size 字节填成自增字母数据（a、b、c ...），返回实际字节数。
fnc dev_read: ret, _this: com&, data: &, size: u4&
    var p: char& = (data: char&)
    var i: u4 = 0
    while i < *size
        p[i] = (i: char) + 'a'
        i = i + 1
    return (*size: i4)

# 设备写：打印收到的字节，返回写出字节数。
fnc dev_write: ret, _this: com&, buf: &, size: u4&
    var p: char& = (buf: char&)
    printf("  写出: ")
    var i: u4 = 0
    while i < *size
        printf("%c", p[i])
        i = i + 1
    printf("\n")
    return (*size: i4)

#-- 异步收发 rpc：含 com >> / << ⇒ 自动状态机；buf/msg 跨收发存活 ⇒ 提升到帧 ----
rpc handler: ret, c: com&
    var buf[4]: char
    c >> buf                       # 异步接收（await com_read_async）
    buf[3] = 0
    printf("  读入: %s\n", (buf: char&))

    var msg[3]: char
    msg[0] = 'O'
    msg[1] = 'K'
    msg[2] = 0
    c << msg                       # 异步发送（await com_write_async）
    return 0

fnc main: i4
    async_init()                   # 建立当前线程事件循环

    var c: com
    c.read = dev_read              # 绑定每对象 io 实现
    c.write = dev_write

    var f: future& = async handler(&c)   # 挂起式启动 rpc，立即返回 future
    async_loop(nil)                # 驱动事件循环，推进 rpc 直到完成

    printf("done\n")
    async_final()                  # 销毁事件循环
    return 0
