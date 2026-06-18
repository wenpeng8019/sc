/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct src src;
typedef struct ctx ctx;

typedef struct src {
    char *text;
    int32_t pos;
} src;

typedef struct ctx {
    src in;
    limit box;
    char buf[64];
} ctx;

static int32_t dev_read(com *_this, void *data, uint32_t *size);
static void * lm_data(limit *_this);
static int32_t http_ending(limit *_this);
static limit * com_alloc(com *_this, uint32_t size, void *ending);
static void com_free(com *_this, limit *s);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static int32_t dev_read(com *_this, void *data, uint32_t *size) {
    /* line 26 */
    ctx *c = ((ctx*)(_this->dev));
    /* line 27 */
    src *s = &(c->in);
    /* line 28 */
    char *out = ((char*)(data));
    /* line 29 */
    uint32_t n = 0;
    /* line 30 */
    while ((n < *(size)) && (s->text[s->pos] != 0)) {
        /* line 31 */
        out[n] = s->text[s->pos];
        /* line 32 */
        n = (n + 1);
        /* line 33 */
        s->pos = (s->pos + 1);
    }
    /* line 34 */
    *(size) = n;
    /* line 35 */
    return ((int32_t)(n));
}

static void * lm_data(limit *_this) {
    /* line 39 */
    ctx *c = ((ctx*)(_this->_self->dev));
    /* line 40 */
    return &(c->buf[0]);
}

static int32_t http_ending(limit *_this) {
    /* line 45 */
    char *p = ((char*)(_this->data(_this)));
    /* line 46 */
    uint32_t i = 0;
    /* line 47 */
    while ((i + 1) < _this->len) {
        /* line 48 */
        if ((p[i] == '\r') && (p[i + 1] == '\n')) {
            /* line 49 */
            return ((int32_t)(i));
        }
        /* line 50 */
        i = (i + 1);
    }
    /* line 51 */
    return -(1);
}

static limit * com_alloc(com *_this, uint32_t size, void *ending) {
    /* line 55 */
    ctx *c = ((ctx*)(_this->dev));
    /* line 56 */
    limit *s = &(c->box);
    /* line 57 */
    s->size = size;
    /* line 58 */
    s->len = 0;
    /* line 59 */
    s->data = lm_data;
    /* line 60 */
    s->ending = ending;
    /* line 61 */
    return s;
}

static void com_free(com *_this, limit *s) {
    /* line 65 */
    return;
}

int32_t main(void) {
    /* line 68 */
    ctx cctx = {0};
    /* line 69 */
    cctx.in.text = "GET /index\r\nrest...";
    /* line 70 */
    cctx.in.pos = 0;
    /* line 72 */
    com c = {0};
    /* line 73 */
    c.read = dev_read;
    /* line 74 */
    c.alloc = com_alloc;
    /* line 75 */
    c.free = com_free;
    /* line 76 */
    c.dev = &(cctx);
    /* line 78 */
    struct com__project s = {256, http_ending, NULL};
    /* line 79 */
    s._ = c.alloc(&c, s.size, s.ending);
    s._->_self = &c;
    /* line 80 */
    {
        limit_read(&(c), s._);
    }
    /* line 82 */
    printf("收到一行（%u 字节）: ", s._->len);
    /* line 83 */
    char *p = ((char*)(s._->data(s._)));
    /* line 84 */
    int32_t k = 0;
    /* line 85 */
    while (((uint32_t)(k)) < s._->len) {
        /* line 86 */
        printf("%c", p[k]);
        /* line 87 */
        k = (k + 1);
    }
    /* line 88 */
    printf("\n");
    /* line 90 */
    if (s._) { s._->_self->free(s._->_self, s._); s._ = NULL; }
    /* line 91 */
    return 0;
}
