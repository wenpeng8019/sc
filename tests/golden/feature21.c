/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/async/async.h"

typedef struct sc_membuf sc_membuf;

typedef struct sc_membuf {
    char data[256];
    uint32_t wpos;
    uint32_t rpos;
    sc_limit box;
    char rbuf[16];
} sc_membuf;

static int32_t sc_mb_write(sc_com *_this, void *buf, uint32_t *size);
static int32_t sc_mb_read(sc_com *_this, void *buf, uint32_t *size);
static int32_t sc_mb_readable(sc_com *_this, void **id);
static void * sc_mb_data(sc_limit *_this);
static sc_limit * sc_mb_alloc(sc_com *_this, uint32_t size, void *ending);
static void sc_mb_free(sc_com *_this, sc_limit *s);
static void sc_fill_raw(char *dst);
struct sc_greet {
    int32_t _;
    int32_t a;
    int32_t b;
};
static void sc_greet_rpc(struct sc_greet *_p);
static inline int32_t sc_greet(int32_t a, int32_t b) {
    struct sc_greet _p = {0};
    _p.a = a;
    _p.b = b;
    sc_greet_rpc(&_p);
    return _p._;
}

struct sc_greet_buf {
    int32_t _;
    int32_t tag;
    char *buf;
    size_t buf_size;
};
static void sc_greet_buf_rpc(struct sc_greet_buf *_p);
static inline int32_t sc_greet_buf(int32_t tag, char buf[8]) {
    struct sc_greet_buf _p = {0};
    _p.tag = tag;
    _p.buf = buf;
    _p.buf_size = sizeof(char[8]);
    sc_greet_buf_rpc(&_p);
    return _p._;
}

typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;

struct sc_greet_body {
    int32_t _;
    int32_t n;
    struct sc_com__project body;
};
static void sc_greet_body_rpc(struct sc_greet_body *_p);
static inline int32_t sc_greet_body(int32_t n, struct sc_com__project body) {
    struct sc_greet_body _p = {0};
    _p.n = n;
    _p.body = body;
    sc_greet_body_rpc(&_p);
    return _p._;
}

struct sc_serve {
    int32_t _;
    sc_future *_ret;
    int _state;
    sc_future *_fut;
    sc_com *c;
    char msg[8];
    int32_t n5;
    char raw[16];
    struct sc_greet *_crpc0;
    struct sc_greet *_crpc1;
    struct sc_greet_buf *_crpc2;
    struct sc_greet_buf *_crpc3;
    struct sc_greet_body *_crpc4;
};
static void sc_serve_rpc(struct sc_serve *_p);
static sc_future *sc_serve__async(sc_com *c);

void sc_mod_async_init(void); void sc_mod_async_drop(void);

static int32_t sc_mb_write(sc_com *_this, void *buf, uint32_t *size) {
    /* line 25 */
    sc_membuf *m = ((sc_membuf*)(_this->dev));
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

static int32_t sc_mb_read(sc_com *_this, void *buf, uint32_t *size) {
    /* line 35 */
    sc_membuf *m = ((sc_membuf*)(_this->dev));
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

static int32_t sc_mb_readable(sc_com *_this, void **id) {
    /* line 47 */
    *(id) = NULL;
    /* line 48 */
    return 1;
}

static void * sc_mb_data(sc_limit *_this) {
    /* line 52 */
    sc_membuf *m = ((sc_membuf*)(_this->_self->dev));
    /* line 53 */
    return &(m->rbuf[0]);
}

static sc_limit * sc_mb_alloc(sc_com *_this, uint32_t size, void *ending) {
    /* line 56 */
    sc_membuf *m = ((sc_membuf*)(_this->dev));
    /* line 57 */
    sc_limit *s = &(m->box);
    /* line 58 */
    s->size = size;
    /* line 59 */
    s->len = 0;
    /* line 60 */
    s->data = sc_mb_data;
    /* line 61 */
    s->ending = ending;
    /* line 62 */
    return s;
}

static void sc_mb_free(sc_com *_this, sc_limit *s) {
    /* line 65 */
    return;
}

static void sc_fill_raw(char *dst) {
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

static void sc_greet_rpc(struct sc_greet *_p) {
    /* line 79 */
    printf("  标量 rpc: a=%d b=%d\n", _p->a, _p->b);
    /* line 80 */
    _p->_ = 0; return;
}

static void sc_greet_buf_rpc(struct sc_greet_buf *_p) {
    /* line 84 */
    printf("  数组 rpc: tag=%d buf=%s\n", _p->tag, &(_p->buf[0]));
    /* line 85 */
    _p->_ = 0; return;
}

static void sc_greet_body_rpc(struct sc_greet_body *_p) {
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

static sc_future *sc_serve__async(sc_com *c) {
    struct sc_serve *_p = (struct sc_serve *)calloc(1, sizeof(struct sc_serve));
    _p->_state = 0;
    _p->_ret = sc_future_new();
    _p->c = c;
    sc_serve_rpc(_p);
    return _p->_ret;
}

static void sc_serve_rpc(struct sc_serve *_p) {
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
    _p->_crpc0 = (struct sc_greet *)calloc(1, sizeof(struct sc_greet));
    _p->_crpc0->a = 7;
    _p->_crpc0->b = 9;
    _p->_fut = sc_com_write_async(_p->c, (void *)&(_p->_crpc0->a), sizeof(_p->_crpc0->a));
    if (sc_future_await(_p->_fut, _p, (void (*)(void *))sc_serve_rpc)) goto _s1;
    _p->_state = 1; return;
    _s1: ;
    _p->_fut = sc_com_write_async(_p->c, (void *)&(_p->_crpc0->b), sizeof(_p->_crpc0->b));
    if (sc_future_await(_p->_fut, _p, (void (*)(void *))sc_serve_rpc)) goto _s2;
    _p->_state = 2; return;
    _s2: ;
    free(_p->_crpc0);
    _p->_crpc1 = (struct sc_greet *)calloc(1, sizeof(struct sc_greet));
    _p->_fut = sc_com_read_async(_p->c, (void *)&(_p->_crpc1->a), sizeof(_p->_crpc1->a));
    if (sc_future_await(_p->_fut, _p, (void (*)(void *))sc_serve_rpc)) goto _s3;
    _p->_state = 3; return;
    _s3: ;
    _p->_fut = sc_com_read_async(_p->c, (void *)&(_p->_crpc1->b), sizeof(_p->_crpc1->b));
    if (sc_future_await(_p->_fut, _p, (void (*)(void *))sc_serve_rpc)) goto _s4;
    _p->_state = 4; return;
    _s4: ;
    sc_greet_rpc(_p->_crpc1);
    free(_p->_crpc1);
    _p->msg[0] = 'h';
    _p->msg[1] = 'i';
    _p->msg[2] = 0;
    _p->_crpc2 = (struct sc_greet_buf *)calloc(1, sizeof(struct sc_greet_buf));
    _p->_crpc2->tag = 42;
    _p->_crpc2->buf = _p->msg;
    _p->_crpc2->buf_size = sizeof(char[8]);
    _p->_fut = sc_com_write_async(_p->c, (void *)&(_p->_crpc2->tag), sizeof(_p->_crpc2->tag));
    if (sc_future_await(_p->_fut, _p, (void (*)(void *))sc_serve_rpc)) goto _s5;
    _p->_state = 5; return;
    _s5: ;
    _p->_fut = sc_com_write_async(_p->c, (void *)(_p->_crpc2->buf), (uint32_t)_p->_crpc2->buf_size);
    if (sc_future_await(_p->_fut, _p, (void (*)(void *))sc_serve_rpc)) goto _s6;
    _p->_state = 6; return;
    _s6: ;
    free(_p->_crpc2);
    _p->_crpc3 = (struct sc_greet_buf *)calloc(1, sizeof(struct sc_greet_buf));
    _p->_crpc3->buf = calloc(1, sizeof(char[8]));
    _p->_crpc3->buf_size = sizeof(char[8]);
    _p->_fut = sc_com_read_async(_p->c, (void *)&(_p->_crpc3->tag), sizeof(_p->_crpc3->tag));
    if (sc_future_await(_p->_fut, _p, (void (*)(void *))sc_serve_rpc)) goto _s7;
    _p->_state = 7; return;
    _s7: ;
    _p->_fut = sc_com_read_async(_p->c, (void *)(_p->_crpc3->buf), (uint32_t)_p->_crpc3->buf_size);
    if (sc_future_await(_p->_fut, _p, (void (*)(void *))sc_serve_rpc)) goto _s8;
    _p->_state = 8; return;
    _s8: ;
    sc_greet_buf_rpc(_p->_crpc3);
    free(_p->_crpc3->buf);
    free(_p->_crpc3);
    _p->n5 = 5;
    sc_fill_raw(&(_p->raw[0]));
    _p->_fut = sc_com_write_async(_p->c, (void *)&(_p->n5), sizeof(_p->n5));
    if (sc_future_await(_p->_fut, _p, (void (*)(void *))sc_serve_rpc)) goto _s9;
    _p->_state = 9; return;
    _s9: ;
    _p->_fut = sc_com_write_async(_p->c, (void *)&(_p->raw), sizeof(_p->raw));
    if (sc_future_await(_p->_fut, _p, (void (*)(void *))sc_serve_rpc)) goto _s10;
    _p->_state = 10; return;
    _s10: ;
    _p->_crpc4 = (struct sc_greet_body *)calloc(1, sizeof(struct sc_greet_body));
    _p->_crpc4->body.size = 16;
    _p->_crpc4->body.ending = NULL;
    _p->_fut = sc_com_read_async(_p->c, (void *)&(_p->_crpc4->n), sizeof(_p->_crpc4->n));
    if (sc_future_await(_p->_fut, _p, (void (*)(void *))sc_serve_rpc)) goto _s11;
    _p->_state = 11; return;
    _s11: ;
    _p->_crpc4->body._ = _p->c->alloc(_p->c, _p->_crpc4->body.size, _p->_crpc4->body.ending);
    _p->_crpc4->body._->_self = _p->c;
    _p->_fut = sc_com_limit_read_async(_p->c, _p->_crpc4->body._);
    if (sc_future_await(_p->_fut, _p, (void (*)(void *))sc_serve_rpc)) goto _s12;
    _p->_state = 12; return;
    _s12: ;
    sc_greet_body_rpc(_p->_crpc4);
    free(_p->_crpc4);
    _p->_ = 0;
    { sc_future *_r = _p->_ret; void *_res = (void *)(intptr_t)(_p->_); free(_p); sc_future_done(_r, _res); return; }
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_async_init();
    /* line 124 */
    sc_async_init();
    /* line 126 */
    sc_membuf mb = {0};
    /* line 127 */
    mb.wpos = 0;
    /* line 128 */
    mb.rpos = 0;
    /* line 130 */
    sc_com c = {0};
    /* line 131 */
    c.read = sc_mb_read;
    /* line 132 */
    c.write = sc_mb_write;
    /* line 133 */
    c.alloc = sc_mb_alloc;
    /* line 134 */
    c.free = sc_mb_free;
    /* line 135 */
    c.readable = sc_mb_readable;
    /* line 136 */
    c.dev = &(mb);
    /* line 138 */
    sc_future *f = sc_serve__async(&(c));
    /* line 139 */
    sc_async_loop(NULL);
    /* line 141 */
    printf("done\n");
    /* line 142 */
    sc_async_final();
    /* line 143 */
    {
        int32_t _ret = 0;
        sc_mod_async_drop();
        return _ret;
    }
}
