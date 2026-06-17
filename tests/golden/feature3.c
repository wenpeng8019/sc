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


static void add_rpc(struct add *_p) {
    /* line 13 */
    _p->_ = _p->a + _p->b; return;
}

static void greet_rpc(struct greet *_p) {
    /* line 17 */
    printf("hello rpc x%d\n", _p->n);
}

static void strlen2_rpc(struct strlen2 *_p) {
    /* line 21 */
    int32_t n = 0;
    /* line 22 */
    while (_p->s[n] != 0) {
        /* line 23 */
        n++;
    }
    /* line 24 */
    _p->_ = n * 2; return;
}

static void dot_rpc(struct dot *_p) {
    /* line 30 */
    int32_t s = 0;
    /* line 31 */
    int32_t i = 0;
    /* line 32 */
    for (i = 0; i < 3; i++) {
        /* line 33 */
        s = (s + (_p->a[i] * _p->b[i]));
    }
    /* line 34 */
    _p->_ = s; return;
}

void square_rpc(struct square *_p) {
    /* line 38 */
    _p->_ = _p->x * _p->x; return;
}

int32_t main(void) {
    /* line 41 */
    printf("add(3,4) = %d\n", add(3, 4));
    /* line 42 */
    greet(2);
    /* line 43 */
    printf("strlen2 = %d\n", strlen2("abc"));
    /* line 44 */
    int32_t u[3] = {1, 2, 3};
    /* line 45 */
    int32_t v[3] = {4, 5, 6};
    /* line 46 */
    printf("dot = %d\n", dot(u, v));
    /* line 47 */
    printf("square(9) = %d\n", square(9));
    /* line 48 */
    return 0;
}
