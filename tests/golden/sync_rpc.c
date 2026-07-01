/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

struct add {
    int32_t _;
    int32_t a;
    int32_t b;
};
static void add_rpc(struct add *_p);
static inline int32_t add(int32_t a, int32_t b) {
    struct add _p = {0};
    _p.a = a;
    _p.b = b;
    add_rpc(&_p);
    return _p._;
}

struct greet {
    int32_t n;
};
static void greet_rpc(struct greet *_p);
static inline void greet(int32_t n) {
    struct greet _p = {0};
    _p.n = n;
    greet_rpc(&_p);
}

struct strlen2 {
    int32_t _;
    char *s;
};
static void strlen2_rpc(struct strlen2 *_p);
static inline int32_t strlen2(char *s) {
    struct strlen2 _p = {0};
    _p.s = s;
    strlen2_rpc(&_p);
    return _p._;
}

struct dot {
    int32_t _;
    int32_t *a;
    size_t a_size;
    int32_t *b;
    size_t b_size;
};
static void dot_rpc(struct dot *_p);
static inline int32_t dot(int32_t a[3], int32_t b[3]) {
    struct dot _p = {0};
    _p.a = a;
    _p.a_size = sizeof(int32_t[3]);
    _p.b = b;
    _p.b_size = sizeof(int32_t[3]);
    dot_rpc(&_p);
    return _p._;
}

struct square {
    int32_t _;
    int32_t x;
};
void square_rpc(struct square *_p);
static inline int32_t square(int32_t x) {
    struct square _p = {0};
    _p.x = x;
    square_rpc(&_p);
    return _p._;
}

typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static void add_rpc(struct add *_p) {
    /* line 4 */
    _p->_ = _p->a + _p->b; return;
}

static void greet_rpc(struct greet *_p) {
    /* line 7 */
    printf("hi %d\n", _p->n);
}

static void strlen2_rpc(struct strlen2 *_p) {
    /* line 10 */
    int32_t n = 0;
    /* line 11 */
    while (_p->s[n] != 0) {
        /* line 12 */
        n++;
    }
    /* line 13 */
    _p->_ = n; return;
}

static void dot_rpc(struct dot *_p) {
    /* line 16 */
    int32_t s = 0;
    /* line 17 */
    int32_t i = 0;
    /* line 18 */
    for (i = 0; i < 3; i++) {
        /* line 19 */
        s = (s + (_p->a[i] * _p->b[i]));
    }
    /* line 20 */
    _p->_ = s; return;
}

void square_rpc(struct square *_p) {
    /* line 23 */
    _p->_ = _p->x * _p->x; return;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 26 */
    int32_t r = add(3, 4);
    /* line 27 */
    printf("add = %d\n", r);
    /* line 28 */
    greet(7);
    /* line 29 */
    printf("len = %d\n", strlen2("abcd"));
    /* line 30 */
    int32_t u[3] = {1, 2, 3};
    /* line 31 */
    int32_t v[3] = {4, 5, 6};
    /* line 32 */
    printf("dot = %d\n", dot(u, v));
    /* line 33 */
    printf("sq = %d\n", square(9));
    /* line 34 */
    return 0;
}
