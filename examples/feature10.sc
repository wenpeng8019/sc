# 特性 10：prev/next 上下文关键字 + io 子模块（print / stringify(...) 关键字）
#   - prev/next：链表结构体（def T: ~）成员访问位，等价 _prev/_next
#   - print：C 风格日志输出（inc io.sc），格式串前缀 "X:" 设级别（F/E/W/I/D/V，默认 D）
#     输出 "HH:MM:SS.mmm L| 文本"，自动换行；环境变量 SC_LOG 设过滤级别
#   - stringify(值)：按实参静态类型格式化为 JSON string（inc adt.sc + io.sc），调用方负责 drop；
#     stringify(值, 缓存, 大小)：在给定缓存内构建，返回 char&（无需 drop）
#     成员为子结构体（值）递归展开；结构体指针 → "类型名@0x地址"；标量指针 → "&值"
#   - 选项块 stringify<compact:1>(值)：紧凑单行；默认（无选项）多行美化（2 空格缩进）
inc stdio.h
inc adt.sc
inc io.sc

def color: i1
    Red = 0
    Green
    Blue

def point: {
    x: i4
    y: i4
}

# todo 增加一个 base 关键字，然后 _prev/_next 注入到对象的起始地址。
def node: ~ {
    id: i4
    name[8]: char
    pos: point
    link&: point
    ref&: i4
    score: f8
    ok: bool
    tag&: char
}

fnc main: i4
    # ---- print：级别前缀与过滤 ----
    print("print 基础输出 n=%d s=%s", 42, "hello")
    print("E: 错误级别示例 code=%d", -1)
    print("W: 警告级别示例")
    print("V: 详细级别（默认 SC_LOG=D 下本行不输出）")

    # ---- string(...)：标量 / 结构体 / 指针 / 数组 / 枚举 / 缓存形态 ----
    # 选项块 stringify<compact:1> 紧凑单行，便于嵌入单行日志
    var s: string

    var p: point
    p.x = 3
    p.y = 4
    s = stringify<compact:1>(p)
    print("point 值: %s", s.cstr())
    s.drop()

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
    print("node 值: %s", s.cstr())
    s.drop()

    # 默认（无选项）多行美化：逐层 2 空格缩进，嵌套结构/数组换行展开
    s = stringify(n)
    print("node 美化:\n%s", s.cstr())
    s.drop()

    var pn&: node = &n
    s = stringify<compact:1>(pn)       # 一级指针自动解引用（nil → "nil"）
    print("node 指针: %s", s.cstr())
    s.drop()

    var arr[4]: i4
    var i: i4
    for i = 0; i < 4; i++
        arr[i] = (i + 1) * 10
    s = stringify<compact:1>(arr)
    print("一维数组: %s", s.cstr())
    s.drop()

    var c: color = Green
    s = stringify<compact:1>(c)        # 枚举按整数格式化
    print("枚举: %s", s.cstr())
    s.drop()

    # 缓存形态：在给定缓存内构建，返回 char&（截断保证 NUL 结尾）
    var buf[64]: char
    print("缓存形态: %s", stringify<compact:1>(p, buf, 64))

    # ---- prev/next：链表结构体上下文关键字 ----
    var l: chain
    var t[3]: node
    for i = 0; i < 3; i++
        t[i].id = i
    l.append(&t[0])
    l.append(&t[1])
    l.append(&t[2])

    var it&: node = l.first(): node&
    printf("正向:")
    while it != nil                    # 等价 it->_next
        printf(" %d", it->id)
        it = it->next
    # 首元素 prev 指向尾元素（rear 约定）
    var rear&: node = (l.first(): node&)->prev
    printf(" | rear=%d\n", rear->id)
    return 0
