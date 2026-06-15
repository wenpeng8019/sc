# 由 scc --emit-sc 从 AST 再生成

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
    link&: point
    ref&: i4
    score: f8
    ok: bool
    tag&: char
}

fnc main: i4
    print("print 基础输出 n=%d s=%s", 42, "hello")
    print("E: 错误级别示例 code=%d", -1)
    print("W: 警告级别示例")
    print("V: 详细级别（默认 SC_LOG=D 下本行不输出）")
    var s: string
    var p: point
    p.x = 3
    p.y = 4
    s = stringify(p)
    print("point 值: %s", s.cstr())
    s.drop()
    var n: node
    n.id = 1
    n.name[0] = 'A'
    n.name[1] = 'B'
    n.pos = p
    n.link = &p
    n.ref = &n.id
    n.score = 9.5
    n.ok = true
    n.tag = "hot"
    s = stringify(n)
    print("node 值: %s", s.cstr())
    s.drop()
    s = stringify(n)
    print("node 美化:\n%s", s.cstr())
    s.drop()
    var pn&: node = &n
    s = stringify(pn)
    print("node 指针: %s", s.cstr())
    s.drop()
    var arr[4]: i4
    var i: i4
    for i = 0; i < 4; i++
        arr[i] = ((i + 1) * 10)
    s = stringify(arr)
    print("一维数组: %s", s.cstr())
    s.drop()
    var c: color = Green
    s = stringify(c)
    print("枚举: %s", s.cstr())
    s.drop()
    var buf[64]: char
    print("缓存形态: %s", stringify(p, buf, 64))
    var l: chain
    var t[3]: node
    for i = 0; i < 3; i++
        t[i].id = i
    l.append(&t[0])
    l.append(&t[1])
    l.append(&t[2])
    var it&: node = (l.first(): node&)
    printf("正向:")
    while it != nil
        printf(" %d", it->id)
        it = it->next
    var rear&: node = (l.first(): node&)->prev
    printf(" | rear=%d\n", rear->id)
    return 0
