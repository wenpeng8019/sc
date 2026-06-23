# 由 scc --emit-sc 从 AST 再生成

def src: {
    text: char&
    pos: i4
}

def ctx: {
    in: src
    box: limit
    buf[64]: char
}

fnc dev_read: ret, _this: com&, data: &, size: u4&
    var c: ctx& = (_this->dev: ctx&)
    var s: src& = &c->in
    var out: char& = (data: char&)
    var n: u4 = 0
    while (n < *size) && (s->text[s->pos] != 0)
        out[n] = s->text[s->pos]
        n = (n + 1)
        s->pos = (s->pos + 1)
    *size = n
    return (n: i4)

fnc lm_data: &, _this: limit&
    var c: ctx& = (_this->_self->dev: ctx&)
    return &c->buf[0]

fnc http_ending: ret, _this: limit&
    var p: char& = (_this->data(): char&)
    var i: u4 = 0
    while (i + 1) < _this->len
        if (p[i] == '\r') && (p[i + 1] == '\n')
            return (i: i4)
        i = (i + 1)
    return -1

fnc com_alloc: limit&, _this: com&, size: u4, ending: &
    var c: ctx& = (_this->dev: ctx&)
    var s: limit& = &c->box
    s->size = size
    s->len = 0
    s->data = lm_data
    s->ending = ending
    return s

fnc com_free: _this: com&, s: limit&
    return

fnc main: i4
    var cctx: ctx
    cctx.in.text = "GET /index\r\nrest..."
    cctx.in.pos = 0
    var c: com
    c.read = dev_read
    c.alloc = com_alloc
    c.free = com_free
    c.dev = &cctx
    var s: com[256, http_ending]
    s = c
    c >> s
    printf("收到一行（%u 字节）: ", s._->len)
    var p: char& = (s._->data(): char&)
    var k: i4 = 0
    while (k: u4) < s._->len
        printf("%c", p[k])
        k = (k + 1)
    printf("\n")
    s = nil
    return 0
