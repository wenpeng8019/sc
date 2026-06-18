# 特性 18：com 有界读框架（com[size, ending] + c >> s）
#
# 最小内核：框架只驱动确定的读流程（limit_read），缓存/边界策略全在用户实现：
#   - com.alloc/free：分配/释放 limit（把 size、ending 透传写入 limit）
#   - limit.data()：返回缓冲基址
#   - limit.ending（可选）：动态截止；nil 则按 size 定长
#   - com.read：设备读
# 本例用 HTTP 行结束符 "\r\n" 做动态截止：c >> s 自动跑框架读循环。

inc stdio.h

#-------------- 模拟字节流设备：dev 指向带读游标的源 + limit 存储 + 缓冲 --------
def src: {
    text: char&
    pos:  i4
}

def ctx: {
    in:  src                            # 输入游标
    box: limit                          # limit 存储（由 alloc 返回其地址）
    buf[64]: char                       # 读缓冲
}

# 设备读（每对象 MethodPtr）：从源游标最多读 *size 字节，回写实际读到字节数。
fnc dev_read: ret, _this: com&, data: &, size: u4&
    var c:   ctx&  = (_this->dev: ctx&)
    var s:   src&  = &c->in
    var out: char& = (data: char&)
    var n: u4 = 0
    while n < *size && s->text[s->pos] != 0
        out[n] = s->text[s->pos]
        n = n + 1
        s->pos = s->pos + 1
    *size = n                           # 回写实际读到字节数（in/out）
    return (n: i4)

# limit.data()：返回缓冲基址（用户实现，从 _self->dev 取上下文）。
fnc lm_data: &, _this: limit&
    var c: ctx& = (_this->_self->dev: ctx&)
    return &c->buf[0]

# 动态截止：扫描 \r\n，命中返回其前数据长度，否则 -1 继续读。
# 基址/已读长度经 this 自取（self.data() / self.len）。
fnc http_ending: ret, _this: limit&
    var p: char& = (_this->data(): char&)
    var i: u4 = 0
    while i + 1 < _this->len
        if p[i] == '\r' && p[i + 1] == '\n'
            return (i: i4)              # 保留 \r\n 之前的数据
        i = i + 1
    return -1                           # 未到结尾

# com.alloc（每对象 MethodPtr）：构造 limit，把 size、ending 透传写入。
fnc com_alloc: limit&, _this: com&, size: u4, ending: &
    var c: ctx& = (_this->dev: ctx&)
    var s: limit& = &c->box
    s->size   = size
    s->len    = 0
    s->data   = lm_data
    s->ending = ending                  # 透传用户的动态截止判定
    return s

# com.free（每对象 MethodPtr）：本例 limit 内嵌于 ctx，无需释放。
fnc com_free: _this: com&, s: limit&
    return

fnc main: i4
    var cctx: ctx
    cctx.in.text = "GET /index\r\nrest..."     # 含一行，以 \r\n 截止
    cctx.in.pos  = 0

    var c: com
    c.read  = dev_read
    c.alloc = com_alloc
    c.free  = com_free
    c.dev   = &cctx

    var s: com[256, http_ending]        # 句柄：size=256（最大 chunk），ending=动态截止
    s = c                               # 分身构造：c.alloc(&c, 256, http_ending)
    c >> s                              # 框架读流程：limit_read(&c, s._)

    printf("收到一行（%u 字节）: ", s._->len)
    var p: char& = (s._->data(): char&)
    var k: i4 = 0
    while (k: u4) < s._->len
        printf("%c", p[k])
        k = k + 1
    printf("\n")                        # GET /index

    s = nil                             # 分身析构：c.free(&c, s._)
    return 0
