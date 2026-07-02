# 由 scc --emit-sc 从 AST 再生成

inc async.sc

fnc dev_read: ret, _this: com&, data: &, size: u4&
    var p: char& = (data: char&)
    var i: u4 = 0
    while i < *size
        p[i] = ((i: char) + 'a')
        i = (i + 1)
    return (*size: i4)

fnc dev_write: ret, _this: com&, buf: &, size: u4&
    var p: char& = (buf: char&)
    ::printf("  写出: ")
    var i: u4 = 0
    while i < *size
        ::printf("%c", p[i])
        i = (i + 1)
    ::printf("\n")
    return (*size: i4)

rpc handler: ret, c: com&
    var buf[4]: char
    c >> buf
    buf[3] = 0
    ::printf("  读入: %s\n", (buf: char&))
    var msg[3]: char
    msg[0] = 'O'
    msg[1] = 'K'
    msg[2] = 0
    c << msg
    return 0

fnc main: i4
    async_init()
    var c: com
    c.read = dev_read
    c.write = dev_write
    var f: future& = async handler(&c)
    async_loop(nil)
    ::printf("done\n")
    async_final()
    return 0
