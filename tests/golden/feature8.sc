# 由 scc --emit-sc 从 AST 再生成

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
    var nn: i4 = 42
    var nm: char& = "hello"
    print "print 基础输出 n=", nn, " s=", nm
    print("E: 错误级别示例 code=%d", -1)
    print "W: 警告级别示例"
    print "V: 详细级别（默认 SC_LOG=D 下本行不输出）"
    print<7> "通道 7：自定义日志通道"
    var pi: f8 = 3.14159
    print "默认浮点=", pi, " 定点=", (pi: "%.2f")
    var s: string&
    var p: point
    p.x = 3
    p.y = 4
    s = stringify(p)
    print "point 值: ", s->cstr()
    s->drop()
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
    print "node 值: ", s->cstr()
    s->drop()
    s = stringify(n)
    print "node 美化:\n", s->cstr()
    s->drop()
    var pn: node& = &n
    s = stringify(pn)
    print "node 指针: ", s->cstr()
    s->drop()
    var arr[4]: i4
    var i: i4
    for i = 0; i < 4; i++
        arr[i] = ((i + 1) * 10)
    s = stringify(arr)
    print "一维数组: ", s->cstr()
    s->drop()
    var c: color = Green
    s = stringify(c)
    print "枚举: ", s->cstr()
    s->drop()
    var buf[64]: char
    print "缓存形态: ", stringify(p, buf, 64)
    return 0
