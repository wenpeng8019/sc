# 由 scc --emit-sc 从 AST 再生成

def membuf: {
    data[256]: char
    wpos: u4
    rpos: u4
    box: limit
    rbuf[16]: char
}

fnc mb_write: ret, _this: com&, buf: &, size: u4&
    var m: membuf& = (_this->dev: membuf&)
    var src: char& = (buf: char&)
    var n: u4 = 0
    while n < *size
        m->data[m->wpos] = src[n]
        m->wpos = (m->wpos + 1)
        n = (n + 1)
    return (n: i4)

fnc mb_read: ret, _this: com&, buf: &, size: u4&
    var m: membuf& = (_this->dev: membuf&)
    var dst: char& = (buf: char&)
    var n: u4 = 0
    while n < *size
        dst[n] = m->data[m->rpos]
        m->rpos = (m->rpos + 1)
        n = (n + 1)
    *size = n
    return (n: i4)

fnc mb_data: &, _this: limit&
    var m: membuf& = (_this->_self->dev: membuf&)
    return &m->rbuf[0]

fnc mb_alloc: limit&, _this: com&, size: u4, ending: &
    var m: membuf& = (_this->dev: membuf&)
    var s: limit& = &m->box
    s->size = size
    s->len = 0
    s->data = mb_data
    s->ending = ending
    return s

fnc mb_free: _this: com&, s: limit&
    return

rpc handle: i4, a: i4, b: i4
    printf("标量 rpc: a=%d b=%d\n", a, b)
    return 0

rpc handle_buf: i4, tag: i4, buf[8]: char
    printf("数组 rpc: tag=%d buf=%s\n", tag, &buf[0])
    return 0

rpc handle_body: i4, n: i4, body: com[16, nil]
    var p: char& = (body._->data(): char&)
    printf("句柄 rpc: n=%d body(%u)=", n, body._->len)
    var k: i4 = 0
    while (k: u4) < body._->len
        printf("%c", p[k])
        k = (k + 1)
    printf("\n")
    return 0

fnc main: i4
    var mb: membuf
    mb.wpos = 0
    mb.rpos = 0
    var c: com
    c.read = mb_read
    c.write = mb_write
    c.alloc = mb_alloc
    c.free = mb_free
    c.dev = &mb
    c << handle(7, 9)
    c >> handle
    var msg[8]: char
    msg[0] = 'h'
    msg[1] = 'i'
    msg[2] = 0
    c << handle_buf(42, msg)
    c >> handle_buf
    var n5: i4 = 5
    var raw[16]: char
    var i: i4 = 0
    while i < 16
        raw[i] = '.'
        i = (i + 1)
    raw[0] = 'O'
    raw[1] = 'K'
    c << n5
    c << raw
    c >> handle_body
    return 0
