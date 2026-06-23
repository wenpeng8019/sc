# 由 scc --emit-sc 从 AST 再生成

fnc dev_read: ret, _this: com&, data: &, size: u4&
    var p: char& = (data: char&)
    var i: u4 = 0
    while i < *size
        p[i] = ((i: char) + 'A')
        i = (i + 1)
    return (*size: i4)

fnc dev_write: ret, _this: com&, buf: &, size: u4&
    var p: char& = (buf: char&)
    printf("写出 %u 字节: ", *size)
    var i: u4 = 0
    while i < *size
        printf("%c", p[i])
        i = (i + 1)
    printf("\n")
    return (*size: i4)

fnc main: i4
    var c: com
    c.read = dev_read
    c.write = dev_write
    var msg[4]: char
    msg[0] = 'H'
    msg[1] = 'i'
    msg[2] = '!'
    msg[3] = 0
    c << msg
    var buf[6]: char
    c >> buf
    buf[5] = 0
    printf("读入: %s\n", (buf: char&))
    var p: com& = &c
    var a[3]: char
    var b[3]: char
    (p >> a) >> b
    a[2] = 0
    b[2] = 0
    printf("a=%c%c b=%c%c\n", a[0], a[1], b[0], b[1])
    return 0
