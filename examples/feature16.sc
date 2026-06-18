# 特性 16：com 设备通讯端点的 << / >> 同步收发语法糖
#
# com 是默认导入（op.sc）的设备通讯端点（机制框架）：具体 io 依赖设备，由每对象方法指针
# read/write（MethodPtr，fnc 前置、无函数体）实现。语言层提供 << / >> 语法糖：
#   com << v   发送：write(&com, &v, &sizeof v)
#   com >> v   接收：read (&com, &v, &sizeof v)
# 接收者按值/指针自动取址注入（与方法指针调用约定一致）；size 为收发字节数（u4&，in/out）。
# 左结合可连续链接：com >> a >> b 依次两次接收。本特性为「同步形态」（fnc 内直接收发）。

inc stdio.h

#-------------- 设备 io 实现（每对象绑定）-----------------------
# 设备读：把 size 字节填成自增字母数据（A、B、C ...），返回实际字节数。
fnc dev_read: ret, _this: com&, data: &, size: u4&
    var p: char& = (data: char&)
    var i: u4 = 0
    while i < *size
        p[i] = (i: char) + 'A'
        i = i + 1
    return (*size: i4)

# 设备写：打印收到的字节，返回写出字节数。
fnc dev_write: ret, _this: com&, buf: &, size: u4&
    var p: char& = (buf: char&)
    printf("写出 %u 字节: ", *size)
    var i: u4 = 0
    while i < *size
        printf("%c", p[i])
        i = i + 1
    printf("\n")
    return (*size: i4)

fnc main: i4
    var c: com
    c.read = dev_read              # 绑定每对象 io 实现
    c.write = dev_write

    #-- << 发送（值接收者，自动 &c）---------------------------
    var msg[4]: char
    msg[0] = 'H'
    msg[1] = 'i'
    msg[2] = '!'
    msg[3] = 0
    c << msg                       # write(&c, &msg, sizeof msg = 4)

    #-- >> 接收（值接收者）-----------------------------------
    var buf[6]: char
    c >> buf                       # read(&c, &buf, sizeof buf = 6)
    buf[5] = 0
    printf("读入: %s\n", (buf: char&))

    #-- 指针接收者 + 连续链 ----------------------------------
    var p: com& = &c
    var a[3]: char
    var b[3]: char
    p >> a >> b                    # 依次两次接收（指针接收者自动直传 p）
    a[2] = 0
    b[2] = 0
    printf("a=%c%c b=%c%c\n", a[0], a[1], b[0], b[1])
    return 0
