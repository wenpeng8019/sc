/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct handler handler;

typedef struct handler {
    int32_t tag;
    int32_t (*op)(struct handler *, int32_t x);
} handler;

static int32_t dbl(handler *_this, int32_t x);
static int32_t neg(handler *_this, int32_t x);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static int32_t dbl(handler *_this, int32_t x) {
    /* line 17 */
    return (x * 2) + _this->tag;
}

static int32_t neg(handler *_this, int32_t x) {
    /* line 20 */
    return (0 - x) - _this->tag;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 23 */
    handler a = {0};
    /* line 24 */
    a.tag = 100;
    /* line 25 */
    a.op = dbl;
    /* line 27 */
    handler b = {0};
    /* line 28 */
    b.tag = 1;
    /* line 29 */
    b.op = neg;
    /* line 32 */
    printf("a.op(5) = %d\n", a.op(&a, 5));
    /* line 33 */
    printf("b.op(5) = %d\n", b.op(&b, 5));
    /* line 36 */
    handler *p = &(a);
    /* line 37 */
    printf("p->op(7) = %d\n", p->op(p, 7));
    /* line 40 */
    handler c = {0};
    /* line 41 */
    c.tag = 0;
    /* line 42 */
    if (c.op == NULL) {
        /* line 43 */
        printf("c.op is nil\n");
    }
    /* line 46 */
    c.tag = 10;
    /* line 47 */
    c.op = dbl;
    /* line 48 */
    printf("c.op(5) = %d\n", c.op(&c, 5));
    /* line 49 */
    return 0;
}
