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
    /* line 24 */
    ctx *c = ((ctx*)(_this->dev));
    /* line 25 */
    src *s = &(c->in);
    /* line 26 */
    char *out = ((char*)(data));
    /* line 27 */
    uint32_t n = 0;
    /* line 28 */
    while ((n < *(size)) && (s->text[s->pos] != 0)) {
        /* line 29 */
        out[n] = s->text[s->pos];
        /* line 30 */
        n = (n + 1);
        /* line 31 */
        s->pos = (s->pos + 1);
    }
    /* line 32 */
    *(size) = n;
    /* line 33 */
    return ((int32_t)(n));
}

static void * lm_data(limit *_this) {
    /* line 37 */
    ctx *c = ((ctx*)(_this->_self->dev));
    /* line 38 */
    return &(c->buf[0]);
}

static int32_t http_ending(limit *_this) {
    /* line 43 */
    char *p = ((char*)(_this->data(_this)));
    /* line 44 */
    uint32_t i = 0;
    /* line 45 */
    while ((i + 1) < _this->len) {
        /* line 46 */
        if ((p[i] == '\r') && (p[i + 1] == '\n')) {
            /* line 47 */
            return ((int32_t)(i));
        }
        /* line 48 */
        i = (i + 1);
    }
    /* line 49 */
    return -(1);
}

static limit * com_alloc(com *_this, uint32_t size, void *ending) {
    /* line 53 */
    ctx *c = ((ctx*)(_this->dev));
    /* line 54 */
    limit *s = &(c->box);
    /* line 55 */
    s->size = size;
    /* line 56 */
    s->len = 0;
    /* line 57 */
    s->data = lm_data;
    /* line 58 */
    s->ending = ending;
    /* line 59 */
    return s;
}

static void com_free(com *_this, limit *s) {
    /* line 63 */
    return;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 66 */
    ctx cctx = {0};
    /* line 67 */
    cctx.in.text = "GET /index\r\nrest...";
    /* line 68 */
    cctx.in.pos = 0;
    /* line 70 */
    com c = {0};
    /* line 71 */
    c.read = dev_read;
    /* line 72 */
    c.alloc = com_alloc;
    /* line 73 */
    c.free = com_free;
    /* line 74 */
    c.dev = &(cctx);
    /* line 76 */
    struct com__project s = {256, http_ending, NULL};
    /* line 77 */
    s._ = c.alloc(&c, s.size, s.ending);
    s._->_self = &c;
    /* line 78 */
    {
        limit_read(&(c), s._);
    }
    /* line 80 */
    printf("收到一行（%u 字节）: ", s._->len);
    /* line 81 */
    char *p = ((char*)(s._->data(s._)));
    /* line 82 */
    int32_t k = 0;
    /* line 83 */
    while (((uint32_t)(k)) < s._->len) {
        /* line 84 */
        printf("%c", p[k]);
        /* line 85 */
        k = (k + 1);
    }
    /* line 86 */
    printf("\n");
    /* line 88 */
    if (s._) { s._->_self->free(s._->_self, s._); s._ = NULL; }
    /* line 89 */
    return 0;
}
