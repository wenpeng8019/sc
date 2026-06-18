# 特性 22：file 文件 com 设备（io 模块）—— 同步/异步读写端点
#
# file(name, txt, read, write) 打开文件构造一个 com 通讯端点：
#   - txt：true=文本 / false=二进制（"b" 后缀）
#   - read/write：0=禁用该方向 / 1=同步 / 2=异步（自动初始化 ioq 队列）
#   - 返回 com&（失败 nil）；无缓冲，写入立即落盘 → 同进程内另一端口可即时读到
# 覆盖：同步写 << / 读 >>（标量·数组·rpc 序列化）+ 异步读（auto-ioq，事件循环驱动）。

inc io.sc
inc async.sc

#-------------- 收端触发的 rpc：标量参数（同步反序列化触发）-------------------
rpc on_pair: i4, a: i4, b: i4
    printf("  rpc 反序列化: a=%d b=%d\n", a, b)
    return 0

#-------------- 同步往返：写端口序列化写入，读端口反序列化读回 ----------------
fnc sync_roundtrip: i4, path: char&
    var wc: com& = file(path, false, 0, 1)        # 二进制·同步·只写
    if wc == nil
        printf("E: 打开写端口失败\n")
        return 1
    wc << on_pair(7, 9)                           # rpc 序列化：逐字段写出参数
    var tag: i4 = 42
    wc << tag                                     # 标量
    var msg[8]: char = "hi-file"
    wc << msg                                     # 数组

    var rc: com& = file(path, false, 1, 0)        # 二进制·同步·只读
    if rc == nil
        printf("E: 打开读端口失败\n")
        return 1
    rc >> on_pair                                 # rpc 反序列化：读齐参数后触发
    var t: i4 = 0
    rc >> t
    var buf[8]: char
    rc >> buf
    printf("  同步读回: tag=%d msg=%s\n", t, &buf[0])
    wc->close()                                   # 关闭写端口：fclose + 释放设备
    rc->close()                                   # 关闭读端口
    return 0

#-------------- 异步读会话：从异步读端口（read==2，auto-ioq）读回 -------------
rpc async_read: ret, rc: com&
    var n: i4 = 0
    rc >> n                                       # 异步读标量（事件循环就绪后兑现）
    printf("  异步读回: n=%d\n", n)
    return 0

fnc main: i4
    var path: char& = "/tmp/sc_feature22.bin"
    printf("== 同步往返 ==\n")
    if sync_roundtrip(path) != 0
        return 1

    printf("== 异步读 ==\n")
    var awc: com& = file(path, false, 0, 1)       # 同步写入一个标量供异步读
    var v: i4 = 2026
    awc << v
    var arc: com& = file(path, false, 2, 0)       # 异步只读（自动建 ioq）
    async_init()
    async async_read(arc)
    async_loop(nil)
    async_final()
    awc->close()                                  # 关闭异步写/读端口
    arc->close()
    printf("done\n")
    return 0
