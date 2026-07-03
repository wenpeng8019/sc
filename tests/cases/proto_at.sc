# proto 协议解析/转换构件回归用例：覆盖
#   1. FILO（栈）——feed 入、drain 顶出，逆序消费 + depth/is_empty/back；
#   2. FIFO（队列）——feed 入、drain 头出，顺序消费；
#   3. 类型化 push（i4/f8/str/bool，带自定义 format）+ build 内置格式化重组（带分隔符）；
#   4. push_blob + 自定义 xform 回调，经 build 触发协议转换；
#   5. 跨块存储（小 chunk_sz 强制多块）+ clear 复用；
#   6. peek 不消费、drain 消费。
inc proto.sc
inc mem.sc     # build 结果缓冲的 recycle

# 自定义 blob 转换：把 4 字节小端整数渲染为 "#<n>"
@fnc blob_hex: i4, tag: u4, data: &, size: u4, out: &, cap: u4, ctx: &
    var v: i4 = 0
    if size >= 4
        var p: i4& = (data: i4&)
        v = p[0]
    # 测长趟：out 为空只回报所需字节
    var tmp[32]: char
    var n: i4 = ::snprintf(tmp, 32, "#%d", v)
    if !out
        return n
    var need: u4 = (n: u4)
    if need > cap
        need = cap
    ::memcpy(out, tmp, need)
    return n

@fnc main: i4
    # ---------- A：FILO 栈语义 ----------
    var a: proto
    a.init(PROTO_FILO, 0, 4)
    a.feed(1, ("alpha": const &), 5)
    a.feed(2, ("beta": const &), 4)
    a.feed(3, ("gamma": const &), 5)
    ::printf("A depth=%llu empty=%d\n", a.depth(), (a.is_empty(): i4))

    var tag: u4 = 0
    var data: & = nil
    while !a.is_empty()
        var len: i4 = a.drain(&tag, &data)
        ::printf("  drain tag=%u len=%d data=%.*s\n", tag, len, len, (data: char&))
    a.drop()

    # ---------- B：FIFO 队列语义 ----------
    var b: proto
    b.init(PROTO_FIFO, 0, 4)
    b.feed(10, ("one": const &), 3)
    b.feed(20, ("two": const &), 3)
    b.feed(30, ("three": const &), 5)
    ::printf("B order:")
    while !b.is_empty()
        var len2: i4 = b.drain(&tag, &data)
        ::printf(" [%u]%.*s", tag, len2, (data: char&))
    ::printf("\n")
    b.drop()

    # ---------- C：类型化 push + build 格式化 ----------
    var c: proto
    c.init(PROTO_FIFO, 0, 4)
    c.push_str("id", nil)
    c.push_i4(42, nil)
    c.push_str("ratio", nil)
    c.push_f8(3.14159, "%.2f")
    c.push_str("ok", nil)
    c.push_b(true, nil)
    var s: char& = c.build(", ")
    ::printf("C build: %s\n", s)
    recycle(s)
    c.drop()

    # ---------- D：blob + 自定义 xform ----------
    var d: proto
    d.init(PROTO_FIFO, 0, 4)
    var n1: i4 = 100
    var n2: i4 = 255
    d.push_blob((&n1: const &), 4, blob_hex)
    d.push_blob((&n2: const &), 4, blob_hex)
    var s2: char& = d.build(" ")
    ::printf("D build: %s\n", s2)
    recycle(s2)
    d.drop()

    # ---------- E：跨块 + peek + back + clear ----------
    var e: proto
    e.init(PROTO_FILO, 64, 8)             # 极小块 → 强制多块
    var i: i4 = 0
    while i < 20
        e.feed((i: u4), ("payload_data": const &), 12)
        i += 1
    ::printf("E depth=%llu\n", e.depth())
    e.peek(&tag, &data)                    # 顶（最后 feed，tag=19）
    ::printf("E peek tag=%u\n", tag)
    e.back(5)                              # 丢 5 条（顶部）
    ::printf("E after back(5) depth=%llu\n", e.depth())
    var pk: i4 = e.peek(&tag, &data)
    ::printf("E peek tag=%u (still %d bytes)\n", tag, pk)
    e.clear()                              # 清空复用缓存
    ::printf("E after clear depth=%llu empty=%d\n", e.depth(), (e.is_empty(): i4))
    e.feed(999, ("reuse": const &), 5)     # 复用缓存块
    ::printf("E reuse depth=%llu\n", e.depth())
    e.drop()

    return 0
