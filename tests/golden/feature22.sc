# 由 scc --emit-sc 从 AST 再生成

inc io.sc

inc async.sc

rpc on_pair: i4, a: i4, b: i4
    ::printf("  rpc 反序列化: a=%d b=%d\n", a, b)
    return 0

fnc sync_roundtrip: i4, path: char&
    var wc: com& = file(path, false, 0, 1)
    if wc == nil
        ::printf("E: 打开写端口失败\n")
        return 1
    wc << on_pair(7, 9)
    var tag: i4 = 42
    wc << tag
    var msg[8]: char = "hi-file"
    wc << msg
    var rc: com& = file(path, false, 1, 0)
    if rc == nil
        ::printf("E: 打开读端口失败\n")
        return 1
    rc >> on_pair
    var t: i4 = 0
    rc >> t
    var buf[8]: char
    rc >> buf
    ::printf("  同步读回: tag=%d msg=%s\n", t, &buf[0])
    wc->close()
    rc->close()
    return 0

rpc async_read: ret, rc: com&
    var n: i4 = 0
    rc >> n
    ::printf("  异步读回: n=%d\n", n)
    return 0

fnc main: i4
    var path: char& = "/tmp/sc_feature22.bin"
    ::printf("== 同步往返 ==\n")
    if sync_roundtrip(path) != 0
        return 1
    ::printf("== 异步读 ==\n")
    var awc: com& = file(path, false, 0, 1)
    var v: i4 = 2026
    awc << v
    var arc: com& = file(path, false, 2, 0)
    async_init()
    async async_read(arc)
    async_loop(nil)
    async_final()
    awc->close()
    arc->close()
    ::printf("done\n")
    return 0
