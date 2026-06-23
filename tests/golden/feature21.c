/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/async/async.h"

typedef struct membuf membuf;

typedef struct membuf {
    char data[256];
    uint32_t wpos;
    uint32_t rpos;
    limit box;
    char rbuf[16];
} membuf;

static int32_t mb_write(com *_this, void *buf, uint32_t *size);
static int32_t mb_read(com *_this, void *buf, uint32_t *size);
static int32_t mb_readable(com *_this, void **id);
static void * mb_data(limit *_this);
static limit * mb_alloc(com *_this, uint32_t size, void *ending);
static void mb_free(com *_this, limit *s);
static void fill_raw(char *dst);
struct greet {
    int32_t _;
    int32_t a;
    int32_t b;
};
static void greet_rpc(struct greet *_p);
static inline int32_t greet(int32_t a, int32_t b) {
    struct greet _p = {0};
    _p.a = a;
    _p.b = b;
    greet_rpc(&_p);
    return _p._;
}

struct greet_buf {
    int32_t _;
    int32_t tag;
    char *buf;
    size_t buf_size;
};
static void greet_buf_rpc(struct greet_buf *_p);
static inline int32_t greet_buf(int32_t tag, char buf[8]) {
    struct greet_buf _p = {0};
    _p.tag = tag;
    _p.buf = buf;
    _p.buf_size = sizeof(char[8]);
    greet_buf_rpc(&_p);
    return _p._;
}

typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;

struct greet_body {
    int32_t _;
    int32_t n;
    struct com__project body;
};
static void greet_body_rpc(struct greet_body *_p);
static inline int32_t greet_body(int32_t n, struct com__project body) {
    struct greet_body _p = {0};
    _p.n = n;
    _p.body = body;
    greet_body_rpc(&_p);
    return _p._;
}

struct session {
    int32_t _;
    future *_ret;
    int _state;
    future *_fut;
    com *c;
    char msg[8];
    int32_t n5;
    char raw[16];
    struct greet *_crpc0;
    struct greet *_crpc1;
    struct greet_buf *_crpc2;
    struct greet_buf *_crpc3;
    struct greet_body *_crpc4;
};
static void session_rpc(struct session *_p);
static future *session__async(com *c);

void sc_mod_async_init(void); void sc_mod_async_drop(void);

static int32_t mb_write(com *_this, void *buf, uint32_t *size) {
    /* line 25 */
    membuf *m = ((membuf*)(_this->dev));
    /* line 26 */
    char *src = ((char*)(buf));
    /* line 27 */
    uint32_t n = 0;
    /* line 28 */
    while (n < *(size)) {
        /* line 29 */
        m->data[m->wpos] = src[n];
        /* line 30 */
        m->wpos = (m->wpos + 1);
        /* line 31 */
        n = (n + 1);
    }
    /* line 32 */
    return ((int32_t)(n));
}

static int32_t mb_read(com *_this, void *buf, uint32_t *size) {
    /* line 35 */
    membuf *m = ((membuf*)(_this->dev));
    /* line 36 */
    char *dst = ((char*)(buf));
    /* line 37 */
    uint32_t n = 0;
    /* line 38 */
    while (n < *(size)) {
        /* line 39 */
        dst[n] = m->data[m->rpos];
        /* line 40 */
        m->rpos = (m->rpos + 1);
        /* line 41 */
        n = (n + 1);
    }
    /* line 42 */
    *(size) = n;
    /* line 43 */
    return ((int32_t)(n));
}

static int32_t mb_readable(com *_this, void **id) {
    /* line 47 */
    *(id) = NULL;
    /* line 48 */
    return 1;
}

static void * mb_data(limit *_this) {
    /* line 52 */
    membuf *m = ((membuf*)(_this->_self->dev));
    /* line 53 */
    return &(m->rbuf[0]);
}

static limit * mb_alloc(com *_this, uint32_t size, void *ending) {
    /* line 56 */
    membuf *m = ((membuf*)(_this->dev));
    /* line 57 */
    limit *s = &(m->box);
    /* line 58 */
    s->size = size;
    /* line 59 */
    s->len = 0;
    /* line 60 */
    s->data = mb_data;
    /* line 61 */
    s->ending = ending;
    /* line 62 */
    return s;
}

static void mb_free(com *_this, limit *s) {
    /* line 65 */
    return;
}

static void fill_raw(char *dst) {
    /* line 69 */
    int32_t i = 0;
    /* line 70 */
    while (i < 16) {
        /* line 71 */
        dst[i] = '.';
        /* line 72 */
        i = (i + 1);
    }
    /* line 73 */
    dst[0] = 'O';
    /* line 74 */
    dst[1] = 'K';
    /* line 75 */
    return;
}

static void greet_rpc(struct greet *_p) {
    /* line 79 */
    printf("  标量 rpc: a=%d b=%d\n", _p->a, _p->b);
    /* line 80 */
    _p->_ = 0; return;
}

static void greet_buf_rpc(struct greet_buf *_p) {
    /* line 84 */
    printf("  数组 rpc: tag=%d buf=%s\n", _p->tag, &(_p->buf[0]));
    /* line 85 */
    _p->_ = 0; return;
}

static void greet_body_rpc(struct greet_body *_p) {
    /* line 89 */
    char *p = ((char*)(_p->body._->data(_p->body._)));
    /* line 90 */
    printf("  句柄 rpc: n=%d body(%u)=", _p->n, _p->body._->len);
    /* line 91 */
    int32_t k = 0;
    /* line 92 */
    while (((uint32_t)(k)) < _p->body._->len) {
        /* line 93 */
        printf("%c", p[k]);
        /* line 94 */
        k = (k + 1);
    }
    /* line 95 */
    printf("\n");
    /* line 96 */
    _p->_ = 0; return;
}

static future *session__async(com *c) {
    struct session *_p = (struct session *)calloc(1, sizeof(struct session));
    _p->_state = 0;
    _p->_ret = future_new();
    _p->c = c;
    session_rpc(_p);
    return _p->_ret;
}

static void session_rpc(struct session *_p) {
    switch (_p->_state) {
        case 0: goto _s0;
        case 1: goto _s1;
        case 2: goto _s2;
        case 3: goto _s3;
        case 4: goto _s4;
        case 5: goto _s5;
        case 6: goto _s6;
        case 7: goto _s7;
        case 8: goto _s8;
        case 9: goto _s9;
        case 10: goto _s10;
        case 11: goto _s11;
        case 12: goto _s12;
    }
    _s0: ;
    _p->_crpc0 = (struct greet *)calloc(1, sizeof(struct greet));
    _p->_crpc0->a = 7;
    _p->_crpc0->b = 9;
    _p->_fut = com_write_async(_p->c, (void *)&(_p->_crpc0->a), sizeof(_p->_crpc0->a));
    if (future_await(_p->_fut, _p, (void (*)(void *))session_rpc)) goto _s1;
    _p->_state = 1; return;
    _s1: ;
    _p->_fut = com_write_async(_p->c, (void *)&(_p->_crpc0->b), sizeof(_p->_crpc0->b));
    if (future_await(_p->_fut, _p, (void (*)(void *))session_rpc)) goto _s2;
    _p->_state = 2; return;
    _s2: ;
    free(_p->_crpc0);
    _p->_crpc1 = (struct greet *)calloc(1, sizeof(struct greet));
    _p->_fut = com_read_async(_p->c, (void *)&(_p->_crpc1->a), sizeof(_p->_crpc1->a));
    if (future_await(_p->_fut, _p, (void (*)(void *))session_rpc)) goto _s3;
    _p->_state = 3; return;
    _s3: ;
    _p->_fut = com_read_async(_p->c, (void *)&(_p->_crpc1->b), sizeof(_p->_crpc1->b));
    if (future_await(_p->_fut, _p, (void (*)(void *))session_rpc)) goto _s4;
    _p->_state = 4; return;
    _s4: ;
    greet_rpc(_p->_crpc1);
    free(_p->_crpc1);
    _p->msg[0] = 'h';
    _p->msg[1] = 'i';
    _p->msg[2] = 0;
    _p->_crpc2 = (struct greet_buf *)calloc(1, sizeof(struct greet_buf));
    _p->_crpc2->tag = 42;
    _p->_crpc2->buf = _p->msg;
    _p->_crpc2->buf_size = sizeof(char[8]);
    _p->_fut = com_write_async(_p->c, (void *)&(_p->_crpc2->tag), sizeof(_p->_crpc2->tag));
    if (future_await(_p->_fut, _p, (void (*)(void *))session_rpc)) goto _s5;
    _p->_state = 5; return;
    _s5: ;
    _p->_fut = com_write_async(_p->c, (void *)(_p->_crpc2->buf), (uint32_t)_p->_crpc2->buf_size);
    if (future_await(_p->_fut, _p, (void (*)(void *))session_rpc)) goto _s6;
    _p->_state = 6; return;
    _s6: ;
    free(_p->_crpc2);
    _p->_crpc3 = (struct greet_buf *)calloc(1, sizeof(struct greet_buf));
    _p->_crpc3->buf = calloc(1, sizeof(char[8]));
    _p->_crpc3->buf_size = sizeof(char[8]);
    _p->_fut = com_read_async(_p->c, (void *)&(_p->_crpc3->tag), sizeof(_p->_crpc3->tag));
    if (future_await(_p->_fut, _p, (void (*)(void *))session_rpc)) goto _s7;
    _p->_state = 7; return;
    _s7: ;
    _p->_fut = com_read_async(_p->c, (void *)(_p->_crpc3->buf), (uint32_t)_p->_crpc3->buf_size);
    if (future_await(_p->_fut, _p, (void (*)(void *))session_rpc)) goto _s8;
    _p->_state = 8; return;
    _s8: ;
    greet_buf_rpc(_p->_crpc3);
    free(_p->_crpc3->buf);
    free(_p->_crpc3);
    _p->n5 = 5;
    fill_raw(&(_p->raw[0]));
    _p->_fut = com_write_async(_p->c, (void *)&(_p->n5), sizeof(_p->n5));
    if (future_await(_p->_fut, _p, (void (*)(void *))session_rpc)) goto _s9;
    _p->_state = 9; return;
    _s9: ;
    _p->_fut = com_write_async(_p->c, (void *)&(_p->raw), sizeof(_p->raw));
    if (future_await(_p->_fut, _p, (void (*)(void *))session_rpc)) goto _s10;
    _p->_state = 10; return;
    _s10: ;
    _p->_crpc4 = (struct greet_body *)calloc(1, sizeof(struct greet_body));
    _p->_crpc4->body.size = 16;
    _p->_crpc4->body.ending = NULL;
    _p->_fut = com_read_async(_p->c, (void *)&(_p->_crpc4->n), sizeof(_p->_crpc4->n));
    if (future_await(_p->_fut, _p, (void (*)(void *))session_rpc)) goto _s11;
    _p->_state = 11; return;
    _s11: ;
    _p->_crpc4->body._ = _p->c->alloc(_p->c, _p->_crpc4->body.size, _p->_crpc4->body.ending);
    _p->_crpc4->body._->_self = _p->c;
    _p->_fut = com_limit_read_async(_p->c, _p->_crpc4->body._);
    if (future_await(_p->_fut, _p, (void (*)(void *))session_rpc)) goto _s12;
    _p->_state = 12; return;
    _s12: ;
    greet_body_rpc(_p->_crpc4);
    free(_p->_crpc4);
    _p->_ = 0;
    { future *_r = _p->_ret; void *_res = (void *)(intptr_t)(_p->_); free(_p); future_done(_r, _res); return; }
}

int32_t main(void) {
    sc_mod_async_init();
    /* line 123 */
    async_init();
    /* line 125 */
    membuf mb = {0};
    /* line 126 */
    mb.wpos = 0;
    /* line 127 */
    mb.rpos = 0;
    /* line 129 */
    com c = {0};
    /* line 130 */
    c.read = mb_read;
    /* line 131 */
    c.write = mb_write;
    /* line 132 */
    c.alloc = mb_alloc;
    /* line 133 */
    c.free = mb_free;
    /* line 134 */
    c.readable = mb_readable;
    /* line 135 */
    c.dev = &(mb);
    /* line 137 */
    future *f = session__async(&(c));
    /* line 138 */
    async_loop(NULL);
    /* line 140 */
    printf("done\n");
    /* line 141 */
    async_final();
    /* line 142 */
    {
        int32_t _ret = 0;
        sc_mod_async_drop();
        return _ret;
    }
}
