/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

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
static void * mb_data(limit *_this);
static limit * mb_alloc(com *_this, uint32_t size, void *ending);
static void mb_free(com *_this, limit *s);
struct handle {
    int32_t _;
    int32_t a;
    int32_t b;
};
static void handle_rpc(struct handle *_p);
static inline int32_t handle(int32_t a, int32_t b) {
    struct handle _p = {0};
    _p.a = a;
    _p.b = b;
    handle_rpc(&_p);
    return _p._;
}

struct handle_buf {
    int32_t _;
    int32_t tag;
    char *buf;
    size_t buf_size;
};
static void handle_buf_rpc(struct handle_buf *_p);
static inline int32_t handle_buf(int32_t tag, char buf[8]) {
    struct handle_buf _p = {0};
    _p.tag = tag;
    _p.buf = buf;
    _p.buf_size = sizeof(char[8]);
    handle_buf_rpc(&_p);
    return _p._;
}

typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;

struct handle_body {
    int32_t _;
    int32_t n;
    struct com__project body;
};
static void handle_body_rpc(struct handle_body *_p);
static inline int32_t handle_body(int32_t n, struct com__project body) {
    struct handle_body _p = {0};
    _p.n = n;
    _p.body = body;
    handle_body_rpc(&_p);
    return _p._;
}


static int32_t mb_write(com *_this, void *buf, uint32_t *size) {
    /* line 21 */
    membuf *m = ((membuf*)(_this->dev));
    /* line 22 */
    char *src = ((char*)(buf));
    /* line 23 */
    uint32_t n = 0;
    /* line 24 */
    while (n < *(size)) {
        /* line 25 */
        m->data[m->wpos] = src[n];
        /* line 26 */
        m->wpos = (m->wpos + 1);
        /* line 27 */
        n = (n + 1);
    }
    /* line 28 */
    return ((int32_t)(n));
}

static int32_t mb_read(com *_this, void *buf, uint32_t *size) {
    /* line 31 */
    membuf *m = ((membuf*)(_this->dev));
    /* line 32 */
    char *dst = ((char*)(buf));
    /* line 33 */
    uint32_t n = 0;
    /* line 34 */
    while (n < *(size)) {
        /* line 35 */
        dst[n] = m->data[m->rpos];
        /* line 36 */
        m->rpos = (m->rpos + 1);
        /* line 37 */
        n = (n + 1);
    }
    /* line 38 */
    *(size) = n;
    /* line 39 */
    return ((int32_t)(n));
}

static void * mb_data(limit *_this) {
    /* line 43 */
    membuf *m = ((membuf*)(_this->_self->dev));
    /* line 44 */
    return &(m->rbuf[0]);
}

static limit * mb_alloc(com *_this, uint32_t size, void *ending) {
    /* line 47 */
    membuf *m = ((membuf*)(_this->dev));
    /* line 48 */
    limit *s = &(m->box);
    /* line 49 */
    s->size = size;
    /* line 50 */
    s->len = 0;
    /* line 51 */
    s->data = mb_data;
    /* line 52 */
    s->ending = ending;
    /* line 53 */
    return s;
}

static void mb_free(com *_this, limit *s) {
    /* line 56 */
    return;
}

static void handle_rpc(struct handle *_p) {
    /* line 60 */
    printf("标量 rpc: a=%d b=%d\n", _p->a, _p->b);
    /* line 61 */
    _p->_ = 0; return;
}

static void handle_buf_rpc(struct handle_buf *_p) {
    /* line 65 */
    printf("数组 rpc: tag=%d buf=%s\n", _p->tag, &(_p->buf[0]));
    /* line 66 */
    _p->_ = 0; return;
}

static void handle_body_rpc(struct handle_body *_p) {
    /* line 70 */
    char *p = ((char*)(_p->body._->data(_p->body._)));
    /* line 71 */
    printf("句柄 rpc: n=%d body(%u)=", _p->n, _p->body._->len);
    /* line 72 */
    int32_t k = 0;
    /* line 73 */
    while (((uint32_t)(k)) < _p->body._->len) {
        /* line 74 */
        printf("%c", p[k]);
        /* line 75 */
        k = (k + 1);
    }
    /* line 76 */
    printf("\n");
    /* line 77 */
    _p->_ = 0; return;
}

int32_t main(void) {
    /* line 80 */
    membuf mb = {0};
    /* line 81 */
    mb.wpos = 0;
    /* line 82 */
    mb.rpos = 0;
    /* line 84 */
    com c = {0};
    /* line 85 */
    c.read = mb_read;
    /* line 86 */
    c.write = mb_write;
    /* line 87 */
    c.alloc = mb_alloc;
    /* line 88 */
    c.free = mb_free;
    /* line 89 */
    c.dev = &(mb);
    /* line 92 */
    {
        {
            struct handle _rp = {0};
            _rp.a = 7;
            _rp.b = 9;
            uint32_t _scsz;
            _scsz = sizeof(_rp.a); c.write(&(c), (void *)&(_rp.a), &_scsz);
            _scsz = sizeof(_rp.b); c.write(&(c), (void *)&(_rp.b), &_scsz);
        }
    }
    /* line 93 */
    {
        {
            struct handle _rp = {0};
            uint32_t _scsz;
            _scsz = sizeof(_rp.a); c.read(&(c), (void *)&(_rp.a), &_scsz);
            _scsz = sizeof(_rp.b); c.read(&(c), (void *)&(_rp.b), &_scsz);
            handle_rpc(&_rp);
        }
    }
    /* line 96 */
    char msg[8];
    /* line 97 */
    msg[0] = 'h';
    /* line 98 */
    msg[1] = 'i';
    /* line 99 */
    msg[2] = 0;
    /* line 100 */
    {
        {
            struct handle_buf _rp = {0};
            _rp.tag = 42;
            _rp.buf = msg;
            _rp.buf_size = sizeof(char[8]);
            uint32_t _scsz;
            _scsz = sizeof(_rp.tag); c.write(&(c), (void *)&(_rp.tag), &_scsz);
            _scsz = (uint32_t)_rp.buf_size; c.write(&(c), (void *)(_rp.buf), &_scsz);
        }
    }
    /* line 101 */
    {
        {
            struct handle_buf _rp = {0};
            char _rp_buf[8];
            _rp.buf = _rp_buf;
            _rp.buf_size = sizeof(char[8]);
            uint32_t _scsz;
            _scsz = sizeof(_rp.tag); c.read(&(c), (void *)&(_rp.tag), &_scsz);
            _scsz = (uint32_t)_rp.buf_size; c.read(&(c), (void *)(_rp.buf), &_scsz);
            handle_buf_rpc(&_rp);
        }
    }
    /* line 105 */
    int32_t n5 = 5;
    /* line 106 */
    char raw[16];
    /* line 107 */
    int32_t i = 0;
    /* line 108 */
    while (i < 16) {
        /* line 109 */
        raw[i] = '.';
        /* line 110 */
        i = (i + 1);
    }
    /* line 111 */
    raw[0] = 'O';
    /* line 112 */
    raw[1] = 'K';
    /* line 113 */
    {
        uint32_t _scsz;
        _scsz = sizeof(n5); c.write(&(c), (void *)&(n5), &_scsz);
    }
    /* line 114 */
    {
        uint32_t _scsz;
        _scsz = sizeof(raw); c.write(&(c), (void *)&(raw), &_scsz);
    }
    /* line 115 */
    {
        {
            struct handle_body _rp = {0};
            _rp.body.size = 16;
            _rp.body.ending = NULL;
            uint32_t _scsz;
            _scsz = sizeof(_rp.n); c.read(&(c), (void *)&(_rp.n), &_scsz);
            _rp.body._ = c.alloc(&(c), _rp.body.size, _rp.body.ending);
            _rp.body._->_self = &(c);
            limit_read(&(c), _rp.body._);
            handle_body_rpc(&_rp);
        }
    }
    /* line 116 */
    return 0;
}
