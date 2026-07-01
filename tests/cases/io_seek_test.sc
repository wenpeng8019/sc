# com seek 机制单元测试：stream 内存设备（可寻址）
#   验证 SEEK_SET(0) / SEEK_CUR(1) / SEEK_END(2) 三种基准、取当前位置、
#   读定位、写定位（回填 header 场景）、越界与非法 whence 的错误返回。
# 运行：scc tests/cases/io_seek_test.sc --test
#
# 被测：builtins/op.sc @def com 的 seek 方法 + builtins/io/io_impl.c stream 实现。

inc io.sc

tst "stream seek：绝对/相对/从尾定位 + 读定位"
    var buf[16]: u1
    var i: u4 = 0
    while i < 16
        buf[i] = (i: u1)                 # 0,1,2,...,15
        i = i + 1

    var c: com& = stream((&buf[0]: &), 16, 1, 0)   # 只读、同步

    # seek(0, 1) 取当前位置 —— 初始为 0
    var pos: i8 = c->seek(0, 1)
    assert pos == 0, "初始位置为 0"

    # 绝对定位到 10（SEEK_SET）
    pos = c->seek(10, 0)
    assert pos == 10, "SEEK_SET 到 10"
    var v: u1 = 0
    c >> v
    assert v == 10, "读到 buf[10]=10"
    assert c->seek(0, 1) == 11, "读后当前位置 11"

    # 相对回退 5（SEEK_CUR）到 6
    pos = c->seek(-5, 1)
    assert pos == 6, "SEEK_CUR -5 到 6"
    c >> v
    assert v == 6, "读到 buf[6]=6"

    # 从尾定位（SEEK_END）：末尾 -1 = 15
    pos = c->seek(-1, 2)
    assert pos == 15, "SEEK_END -1 到 15"
    c >> v
    assert v == 15, "读到 buf[15]=15"

    # 越界返回 <0
    assert c->seek(100, 0) < 0, "越界 seek 返回 <0"
    # 非法 whence 返回 <0
    assert c->seek(0, 9) < 0, "非法 whence 返回 <0"

    assert c->close() == 0, "close 成功"

tst "stream seek：写定位（回填 header 场景）"
    var buf[8]: u1
    var i: u4 = 0
    while i < 8
        buf[i] = 0
        i = i + 1

    var c: com& = stream((&buf[0]: &), 8, 0, 1)   # 只写

    var a: u1 = 0xAA
    c << a                                          # buf[0]=0xAA，wpos=1
    assert c->seek(4, 0) == 4, "写 SEEK_SET 到 4"
    var b: u1 = 0xBB
    c << b                                           # buf[4]=0xBB
    assert c->seek(0, 1) == 5, "写后位置 5"

    assert buf[0] == 0xAA, "buf[0]=0xAA"
    assert buf[4] == 0xBB, "buf[4]=0xBB"
    assert buf[1] == 0, "buf[1] 仍为 0（未覆盖）"

    assert c->close() == 0, "写端 close 成功"
