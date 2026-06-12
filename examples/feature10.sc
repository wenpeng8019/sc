# 特性 10：prev/next 上下文关键字 + io 子模块（print / string_of 关键字）
#   - prev/next：链表结构体（def T: ~）成员访问位，等价 _prev/_next
#   - print：C 风格日志输出（inc io.sc），格式串前缀 "X:" 设级别（F/E/W/I/D/V，默认 D）
#     输出 "HH:MM:SS.mmm L| 文本"，自动换行；环境变量 SC_LOG 设过滤级别
#   - string_of：按实参静态类型格式化为 string（inc adt.sc），调用方负责 drop
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

def node: ~ {
    id: i4
    name[8]: char
    pos: point
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

    # ---- string_of：标量 / 结构体 / 指针 / 数组 / 枚举 / string ----
    var s: string

    var p: point
    p.x = 3
    p.y = 4
    s = string_of(p)
    print("point 值: %s", s.cstr())
    s.drop()

    var n: node
    n.id = 1
    n.name[0] = 'A'
    n.name[1] = 'B'
    n.pos = p
    n.score = 9.5
    n.ok = true
    n.tag = "hot"
    s = string_of(n)
    print("node 值: %s", s.cstr())
    s.drop()

    var pn&: node = &n
    s = string_of(pn)                  # 一级指针自动解引用（nil → "nil"）
    print("node 指针: %s", s.cstr())
    s.drop()

    var arr[4]: i4
    var i: i4
    for i = 0; i < 4; i++
        arr[i] = (i + 1) * 10
    s = string_of(arr)
    print("一维数组: %s", s.cstr())
    s.drop()

    var c: color = Green
    s = string_of(c)                   # 枚举按整数格式化
    print("枚举: %s", s.cstr())
    s.drop()

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
