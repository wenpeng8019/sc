/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_handler sc_handler;

typedef struct sc_handler {
    int32_t tag;
    int32_t (*op)(struct sc_handler *, int32_t x);
} sc_handler;

static int32_t sc_dbl(sc_handler *_this, int32_t x);
static int32_t sc_neg(sc_handler *_this, int32_t x);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


static int32_t sc_dbl(sc_handler *_this, int32_t x) {
    /* line 17 */
    return (x * 2) + _this->tag;
}

static int32_t sc_neg(sc_handler *_this, int32_t x) {
    /* line 20 */
    return (0 - x) - _this->tag;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 23 */
    sc_handler a = {0};
    /* line 24 */
    a.tag = 100;
    /* line 25 */
    a.op = sc_dbl;
    /* line 27 */
    sc_handler b = {0};
    /* line 28 */
    b.tag = 1;
    /* line 29 */
    b.op = sc_neg;
    /* line 32 */
    printf("a.op(5) = %d\n", a.op(&a, 5));
    /* line 33 */
    printf("b.op(5) = %d\n", b.op(&b, 5));
    /* line 36 */
    sc_handler *p = &(a);
    /* line 37 */
    printf("p->op(7) = %d\n", p->op(p, 7));
    /* line 40 */
    sc_handler c = {0};
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
    c.op = sc_dbl;
    /* line 48 */
    printf("c.op(5) = %d\n", c.op(&c, 5));
    /* line 49 */
    return 0;
}
