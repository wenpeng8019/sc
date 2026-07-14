# io 语法糖单元测试：<< 写字符串内容 + >> com[0] 读全部（seek 求全长）。
#   目的：让程序直接用 com 完成整读整写，无需封装 read_file/write_file 之类函数。
# 运行：scc tests/cases/io_sugar_test.sc --test
#
# 被测：codegen emitComChain 的 << 字符串糖 + builtins/io/io_impl.c 的 read-all 哨兵。

inc io.sc
inc adt.sc

tst "<< 写 const char* / char* 内容（写 strlen 字节，非指针）"
    var buf[32]: u1
    var i: u4 = 0
    while i < 32
        buf[i] = 0
        i = i + 1

    var c: com& = stream((&buf[0]: &), 32, 0, 1)     # 内存只写设备
    c << "hello"                                       # 字面量：写 5 字节内容
    var tail: char& = "!!"
    c << tail                                          # char* 变量：写 2 字节内容

    assert buf[0] == 'h', "buf[0]=h"
    assert buf[4] == 'o', "buf[4]=o"
    assert buf[5] == '!', "buf[5]=!"
    assert buf[6] == '!', "buf[6]=!"
    assert buf[7] == 0, "buf[7] 未写"
    assert c->close() == 0, "close 成功"

tst "<< 写 string 内容（写 size 字节，取 data）"
    var buf[32]: u1
    var i: u4 = 0
    while i < 32
        buf[i] = 0
        i = i + 1

    var c: com& = stream((&buf[0]: &), 32, 0, 1)
    var str: string& = string("sc-str")               # size=6
    c << str
    assert buf[0] == 's', "buf[0]=s"
    assert buf[5] == 'r', "buf[5]=r"
    assert buf[6] == 0, "buf[6] 未写"
    str->drop()
    assert c->close() == 0, "close 成功"

tst ">> com[0] 读全部（哨兵 size=0 → seek 求全长一次读满）"
    var src[12]: u1
    var txt: char& = "hello world!"                    # 12 字节
    var i: u4 = 0
    while i < 12
        src[i] = (txt[i]: u1)
        i = i + 1

    var c: com& = stream((&src[0]: &), 12, 1, 0)       # 内存只读设备
    var s: com[0]                                       # read-all 句柄（无 ending）
    s = c
    c >> s
    assert s._->len == 12, "读满 12 字节"
    var p: char& = (s._->data(): char&)
    assert p[0] == 'h', "p[0]=h"
    assert p[11] == '!', "p[11]=!"
    s = nil
    assert c->close() == 0, "close 成功"
