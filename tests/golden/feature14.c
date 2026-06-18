/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct handler handler;

typedef struct handler {
    int32_t tag;
    int32_t (*op)(struct handler *, int32_t x);
} handler;

static int32_t dbl(handler *_this, int32_t x);
static int32_t neg(handler *_this, int32_t x);

static int32_t dbl(handler *_this, int32_t x) {
    /* line 19 */
    return (x * 2) + _this->tag;
}

static int32_t neg(handler *_this, int32_t x) {
    /* line 22 */
    return (0 - x) - _this->tag;
}

int32_t main(void) {
    /* line 25 */
    handler a = {0};
    /* line 26 */
    a.tag = 100;
    /* line 27 */
    a.op = dbl;
    /* line 29 */
    handler b = {0};
    /* line 30 */
    b.tag = 1;
    /* line 31 */
    b.op = neg;
    /* line 34 */
    printf("a.op(5) = %d\n", a.op(&a, 5));
    /* line 35 */
    printf("b.op(5) = %d\n", b.op(&b, 5));
    /* line 38 */
    handler *p = &(a);
    /* line 39 */
    printf("p->op(7) = %d\n", p->op(p, 7));
    /* line 42 */
    handler c = {0};
    /* line 43 */
    c.tag = 0;
    /* line 44 */
    if (c.op == NULL) {
        /* line 45 */
        printf("c.op is nil\n");
    }
    /* line 48 */
    c.tag = 10;
    /* line 49 */
    c.op = dbl;
    /* line 50 */
    printf("c.op(5) = %d\n", c.op(&c, 5));
    /* line 51 */
    return 0;
}
