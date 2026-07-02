/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

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
static void * sc_mb_data(sc_limit *_this);
static sc_limit * sc_mb_alloc(sc_com *_this, uint32_t size, void *ending);
static void sc_mb_free(sc_com *_this, sc_limit *s);
struct sc_handle {
    int32_t _;
    int32_t a;
    int32_t b;
};
static void sc_handle_rpc(struct sc_handle *_p);
static inline int32_t sc_handle(int32_t a, int32_t b) {
    struct sc_handle _p = {0};
    _p.a = a;
    _p.b = b;
    sc_handle_rpc(&_p);
    return _p._;
}

struct sc_handle_buf {
    int32_t _;
    int32_t tag;
    char *buf;
    size_t buf_size;
};
static void sc_handle_buf_rpc(struct sc_handle_buf *_p);
static inline int32_t sc_handle_buf(int32_t tag, char buf[8]) {
    struct sc_handle_buf _p = {0};
    _p.tag = tag;
    _p.buf = buf;
    _p.buf_size = sizeof(char[8]);
    sc_handle_buf_rpc(&_p);
    return _p._;
}

typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;

struct sc_handle_body {
    int32_t _;
    int32_t n;
    struct sc_com__project body;
};
static void sc_handle_body_rpc(struct sc_handle_body *_p);
static inline int32_t sc_handle_body(int32_t n, struct sc_com__project body) {
    struct sc_handle_body _p = {0};
    _p.n = n;
    _p.body = body;
    sc_handle_body_rpc(&_p);
    return _p._;
}


static int32_t sc_mb_write(sc_com *_this, void *buf, uint32_t *size) {
    /* line 19 */
    sc_membuf *m = ((sc_membuf*)(_this->dev));
    /* line 20 */
    char *src = ((char*)(buf));
    /* line 21 */
    uint32_t n = 0;
    /* line 22 */
    while (n < *(size)) {
        /* line 23 */
        m->data[m->wpos] = src[n];
        /* line 24 */
        m->wpos = (m->wpos + 1);
        /* line 25 */
        n = (n + 1);
    }
    /* line 26 */
    return ((int32_t)(n));
}

static int32_t sc_mb_read(sc_com *_this, void *buf, uint32_t *size) {
    /* line 29 */
    sc_membuf *m = ((sc_membuf*)(_this->dev));
    /* line 30 */
    char *dst = ((char*)(buf));
    /* line 31 */
    uint32_t n = 0;
    /* line 32 */
    while (n < *(size)) {
        /* line 33 */
        dst[n] = m->data[m->rpos];
        /* line 34 */
        m->rpos = (m->rpos + 1);
        /* line 35 */
        n = (n + 1);
    }
    /* line 36 */
    *(size) = n;
    /* line 37 */
    return ((int32_t)(n));
}

static void * sc_mb_data(sc_limit *_this) {
    /* line 41 */
    sc_membuf *m = ((sc_membuf*)(_this->_self->dev));
    /* line 42 */
    return &(m->rbuf[0]);
}

static sc_limit * sc_mb_alloc(sc_com *_this, uint32_t size, void *ending) {
    /* line 45 */
    sc_membuf *m = ((sc_membuf*)(_this->dev));
    /* line 46 */
    sc_limit *s = &(m->box);
    /* line 47 */
    s->size = size;
    /* line 48 */
    s->len = 0;
    /* line 49 */
    s->data = sc_mb_data;
    /* line 50 */
    s->ending = ending;
    /* line 51 */
    return s;
}

static void sc_mb_free(sc_com *_this, sc_limit *s) {
    /* line 54 */
    return;
}

static void sc_handle_rpc(struct sc_handle *_p) {
    /* line 58 */
    printf("标量 rpc: a=%d b=%d\n", _p->a, _p->b);
    /* line 59 */
    _p->_ = 0; return;
}

static void sc_handle_buf_rpc(struct sc_handle_buf *_p) {
    /* line 63 */
    printf("数组 rpc: tag=%d buf=%s\n", _p->tag, &(_p->buf[0]));
    /* line 64 */
    _p->_ = 0; return;
}

static void sc_handle_body_rpc(struct sc_handle_body *_p) {
    /* line 68 */
    char *p = ((char*)(_p->body._->data(_p->body._)));
    /* line 69 */
    printf("句柄 rpc: n=%d body(%u)=", _p->n, _p->body._->len);
    /* line 70 */
    int32_t k = 0;
    /* line 71 */
    while (((uint32_t)(k)) < _p->body._->len) {
        /* line 72 */
        printf("%c", p[k]);
        /* line 73 */
        k = (k + 1);
    }
    /* line 74 */
    printf("\n");
    /* line 75 */
    _p->_ = 0; return;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 78 */
    sc_membuf mb = {0};
    /* line 79 */
    mb.wpos = 0;
    /* line 80 */
    mb.rpos = 0;
    /* line 82 */
    sc_com c = {0};
    /* line 83 */
    c.read = sc_mb_read;
    /* line 84 */
    c.write = sc_mb_write;
    /* line 85 */
    c.alloc = sc_mb_alloc;
    /* line 86 */
    c.free = sc_mb_free;
    /* line 87 */
    c.dev = &(mb);
    /* line 90 */
    {
        {
            struct sc_handle _rp = {0};
            _rp.a = 7;
            _rp.b = 9;
            uint32_t _scsz;
            _scsz = sizeof(_rp.a); c.write(&(c), (void *)&(_rp.a), &_scsz);
            _scsz = sizeof(_rp.b); c.write(&(c), (void *)&(_rp.b), &_scsz);
        }
    }
    /* line 91 */
    {
        {
            struct sc_handle _rp = {0};
            uint32_t _scsz;
            _scsz = sizeof(_rp.a); c.read(&(c), (void *)&(_rp.a), &_scsz);
            _scsz = sizeof(_rp.b); c.read(&(c), (void *)&(_rp.b), &_scsz);
            sc_handle_rpc(&_rp);
        }
    }
    /* line 94 */
    char msg[8];
    /* line 95 */
    msg[0] = 'h';
    /* line 96 */
    msg[1] = 'i';
    /* line 97 */
    msg[2] = 0;
    /* line 98 */
    {
        {
            struct sc_handle_buf _rp = {0};
            _rp.tag = 42;
            _rp.buf = msg;
            _rp.buf_size = sizeof(char[8]);
            uint32_t _scsz;
            _scsz = sizeof(_rp.tag); c.write(&(c), (void *)&(_rp.tag), &_scsz);
            _scsz = (uint32_t)_rp.buf_size; c.write(&(c), (void *)(_rp.buf), &_scsz);
        }
    }
    /* line 99 */
    {
        {
            struct sc_handle_buf _rp = {0};
            char _rp_buf[8];
            _rp.buf = _rp_buf;
            _rp.buf_size = sizeof(char[8]);
            uint32_t _scsz;
            _scsz = sizeof(_rp.tag); c.read(&(c), (void *)&(_rp.tag), &_scsz);
            _scsz = (uint32_t)_rp.buf_size; c.read(&(c), (void *)(_rp.buf), &_scsz);
            sc_handle_buf_rpc(&_rp);
        }
    }
    /* line 103 */
    int32_t n5 = 5;
    /* line 104 */
    char raw[16];
    /* line 105 */
    int32_t i = 0;
    /* line 106 */
    while (i < 16) {
        /* line 107 */
        raw[i] = '.';
        /* line 108 */
        i = (i + 1);
    }
    /* line 109 */
    raw[0] = 'O';
    /* line 110 */
    raw[1] = 'K';
    /* line 111 */
    {
        uint32_t _scsz;
        _scsz = sizeof(n5); c.write(&(c), (void *)&(n5), &_scsz);
    }
    /* line 112 */
    {
        uint32_t _scsz;
        _scsz = sizeof(raw); c.write(&(c), (void *)&(raw), &_scsz);
    }
    /* line 113 */
    {
        {
            struct sc_handle_body _rp = {0};
            _rp.body.size = 16;
            _rp.body.ending = NULL;
            uint32_t _scsz;
            _scsz = sizeof(_rp.n); c.read(&(c), (void *)&(_rp.n), &_scsz);
            _rp.body._ = c.alloc(&(c), _rp.body.size, _rp.body.ending);
            _rp.body._->_self = &(c);
            sc_limit_read(&(c), _rp.body._);
            sc_handle_body_rpc(&_rp);
        }
    }
    /* line 114 */
    return 0;
}
