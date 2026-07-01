# 特性 8：io 子模块 —— print / stringify(...) 关键字

inc adt.sc
inc io.sc

def color: [
    Red = 0
    Green
    Blue
] : i1

def point: {
    x: i4
    y: i4
}

def node: ~ {
    id: i4
    name[8]: char
    pos: point
    link: point&
    ref: i4&
    score: f8
    ok: bool
    tag: char&
}

fnc main: i4

    #-------------- print：拼接糖 / 级别通道 / 格式覆盖 ------
    # 无括号 print ...：字符串字面量=纯文本；非字面量按静态类型自动补说明符（i4→%d, char&→%s …）
    # 有括号 print(...)：C printf 兼容模式，首参为格式串，实参原样传递
    # <chn> = 级别/通道（见 op.sc def log）：0=普通 stdout（默认）；1..6=F/E/W/I/D/V，按级别着色
    # 级别过滤 SC_LOG=F/E/W/I/D/V（默认 D）；SC_LOG_SYS 非空 → 额外写系统日志

    var nn: i4 = 42
    var nm: char& = "hello"
    print "print 基础输出 n=", nn, " s=", nm   # 拼接糖：i4→%d, char&→%s 自动拼接
    print<E>("错误级别示例 code=%d", -1)         # <E> 括号 = printf 兼容模式（红色）
    print<W> "警告级别示例"                       # <W> 警告（黄色）
    print<V> "详细级别（默认 SC_LOG=D 下本行不输出）"  # <V> 详尽（灰色）
    print<D> "调试级别示例"                        # <D> 调试（青色）
    var pi: f8 = 3.14159
    print "默认浮点=", pi, " 定点=", (pi: "%.2f")  # (expr: "%fmt") 显式格式覆盖

    #-------------- stringify(...)：按类型格式化为 JSON ---------
    # stringify<compact:1>(值) 紧凑单行；默认多行美化（2 空格缩进）
    # 结构体指针 → "类型名@0x地址"；标量指针 → "&值"

    var s: string&                            # 堆专属：每次 stringify 返回新堆指针，用后即 drop

    var p: point
    p.x = 3
    p.y = 4
    s = stringify<compact:1>(p)
    print "point 值: ", s->cstr()             # s->cstr() 返回 char& → %s
    s->drop()

    var n: node
    n.id = 1
    n.name[0] = 'A'
    n.name[1] = 'B'
    n.pos = p
    n.link = &p                        # 结构体指针成员 → "point@0x地址"
    n.ref = &n.id                      # 标量指针成员 → "&值"
    n.score = 9.5
    n.ok = true
    n.tag = "hot"
    s = stringify<compact:1>(n)
    print "node 值: ", s->cstr()
    s->drop()

    # 默认（无选项）多行美化
    s = stringify(n)
    print "node 美化:\n", s->cstr()
    s->drop()

    var pn: node& = &n
    s = stringify<compact:1>(pn)       # 一级指针自动解引用
    print "node 指针: ", s->cstr()
    s->drop()

    # 数组 / 枚举 stringify
    var arr[4]: i4
    var i: i4
    for i = 0; i < 4; i++
        arr[i] = (i + 1) * 10
    s = stringify<compact:1>(arr)
    print "一维数组: ", s->cstr()
    s->drop()

    var c: color = Green
    s = stringify<compact:1>(c)        # 枚举按整数格式化
    print "枚举: ", s->cstr()
    s->drop()

    # 缓存形态：在给定缓存内构建，返回 char&（截断保证 NUL 结尾）
    var buf[64]: char
    print "缓存形态: ", stringify<compact:1>(p, buf, 64)   # 返回 char& → %s

    return 0
