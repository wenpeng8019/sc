# com@1 半自动指针单元测试：设备句柄退域自动 close（fclose 刷盘 + 回收结构）。
#   目的：调用方 `var c: com@1 = file(...)` 后无需手动 c->close()，退域/重新赋值自动收尾，
#   从语法上让 io「不封装」——不必再写 sa_write_file/sa_read_file 之类的收尾包装。
# 运行：scc tests/cases/io_auto_test.sc --test
#
# 被测：codegen 半自动指针 T@1 对内建 com 的 close 特化（emitAutoFreeDestroy/Assign）。

inc io.sc
inc mem.sc

# 辅助：com@1 写整文件，退域自动 close（返回即触发，无显式收尾）。
fnc wr_auto: i4, path: const char&, text: const char&
    var c: com@1 = file(path, true, 0, 1)
    if c == nil
        return -1
    c << text
    return 0                                  # 退域自动 close（fclose 刷盘）

# 辅助：com@1 读整文件长度（seek 求全长），退域自动 close。
fnc rd_len: i4, path: const char&
    var c: com@1 = file(path, true, 1, 0)
    if c == nil
        return -1
    var n: i4 = (c->seek(0, 2): i4)           # SEEK_END = 全长
    return n                                  # 退域自动 close

# 辅助：com@1 + com[0] 整读，返回读得字节数。
#   全程无手动收尾：s（com[...] 句柄）退域自动 free、c（com@1）退域自动 close。
fnc rd_all_auto: i4, path: const char&
    var c: com@1 = file(path, true, 1, 0)
    if c == nil
        return -1
    var s: com[0]
    s = c
    c >> s
    return (s._->len: i4)                      # 退域：先 free s（c 尚存活），再 close c

# 辅助：com@1 + com[0] 整读并 take 取走缓冲（零拷贝转移所有权），返回缓冲（调用方 recycle）。
#   取走后 s 退域只回收元数据（缓冲已摘除，不双释）；c 退域自动 close。
fnc rd_take: char&, path: const char&
    var c: com@1 = file(path, true, 1, 0)
    if c == nil
        return nil
    var s: com[0]
    s = c
    c >> s
    return (s.take(): char&)                   # 句柄糖：com.take(com, s._) 转移缓冲所有权

tst "com@1 写文件退域自动 close：内容已刷盘可读回"
    assert wr_auto("/tmp/sc_auto_a.txt", "auto-closed!") == 0, "写成功"
    # 若退域未自动 close，fclose 未执行→未刷盘，长度读回将不足 12。
    assert rd_len("/tmp/sc_auto_a.txt") == 12, "读回全长 12（证明已 close 刷盘）"

tst "com@1 重新赋值：旧句柄自动 close（旧文件亦刷盘）"
    var c: com@1 = file("/tmp/sc_auto_b1.txt", true, 0, 1)
    assert c != nil, "开 B1"
    c << "1111"
    c = file("/tmp/sc_auto_b2.txt", true, 0, 1)   # 赋新值→旧 B1 自动 close
    assert c != nil, "开 B2"
    c << "22"
    # c(B2) 退域自动 close
    assert rd_len("/tmp/sc_auto_b1.txt") == 4, "B1 已 close 刷盘（4 字节）"

tst "com@1 读全部：>> com[0] + 退域自动 close"
    assert wr_auto("/tmp/sc_auto_c.txt", "hello world!") == 0, "预写"
    var c: com@1 = file("/tmp/sc_auto_c.txt", true, 1, 0)
    assert c != nil, "开读"
    var s: com[0]
    s = c
    c >> s
    assert s._->len == 12, "读满 12"
    var p: char& = (s._->data(): char&)
    assert p[0] == 'h', "p[0]=h"
    assert p[11] == '!', "p[11]=!"
    s = nil
    # c 退域自动 close

tst "com[...] 句柄退域自动 free：无手动 s = nil，句柄+设备均自动收尾"
    assert wr_auto("/tmp/sc_auto_d.txt", "hello world!") == 0, "预写"
    # rd_all_auto 内既不 s = nil 也不 c->close()，全靠退域自动收尾。
    assert rd_all_auto("/tmp/sc_auto_d.txt") == 12, "整读 12（句柄自动 free 未崩）"
    # 再次整读同一文件：若上次退域 free/close 有误（双重释放/句柄泄漏），此处会崩或读错。
    assert rd_all_auto("/tmp/sc_auto_d.txt") == 12, "复读仍 12（证明上次收尾正确）"

tst "com[0] + take：零拷贝取走缓冲（NUL 结尾），调用方 recycle 释放"
    assert wr_auto("/tmp/sc_auto_e.txt", "take-zero-copy!") == 0, "预写 15 字节"
    var buf: char& = rd_take("/tmp/sc_auto_e.txt")
    assert buf != nil, "取到缓冲"
    assert buf[0] == 't', "buf[0]=t"
    assert buf[14] == '!', "buf[14]=!"
    assert buf[15] == 0, "buf[15]=NUL（多分配 1 字节，天然 C 字符串）"
    assert (::strlen((buf: const char&)): i4) == 15, "strlen=15（当 C 字符串用）"
    recycle((buf: &))                          # 池缓冲：mem.sc recycle 释放（转移后由调用方负责）
    # 再取一次：证明 take 摘除缓冲后 s 退域 free 未双释、设备可复用
    var buf2: char& = rd_take("/tmp/sc_auto_e.txt")
    assert buf2 != nil, "复取到缓冲"
    assert (::strlen((buf2: const char&)): i4) == 15, "复取 strlen=15"
    recycle((buf2: &))


