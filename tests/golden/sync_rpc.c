/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

struct sc_add {
    int32_t _;
    int32_t a;
    int32_t b;
};
static void sc_add_rpc(struct sc_add *_p);
static inline int32_t sc_add(int32_t a, int32_t b) {
    struct sc_add _p = {0};
    _p.a = a;
    _p.b = b;
    sc_add_rpc(&_p);
    return _p._;
}

struct sc_greet {
    int32_t n;
};
static void sc_greet_rpc(struct sc_greet *_p);
static inline void sc_greet(int32_t n) {
    struct sc_greet _p = {0};
    _p.n = n;
    sc_greet_rpc(&_p);
}

struct sc_strlen2 {
    int32_t _;
    char *s;
};
static void sc_strlen2_rpc(struct sc_strlen2 *_p);
static inline int32_t sc_strlen2(char *s) {
    struct sc_strlen2 _p = {0};
    _p.s = s;
    sc_strlen2_rpc(&_p);
    return _p._;
}

struct sc_dot {
    int32_t _;
    int32_t *a;
    size_t a_size;
    int32_t *b;
    size_t b_size;
};
static void sc_dot_rpc(struct sc_dot *_p);
static inline int32_t sc_dot(int32_t a[3], int32_t b[3]) {
    struct sc_dot _p = {0};
    _p.a = a;
    _p.a_size = sizeof(int32_t[3]);
    _p.b = b;
    _p.b_size = sizeof(int32_t[3]);
    sc_dot_rpc(&_p);
    return _p._;
}

struct sc_square {
    int32_t _;
    int32_t x;
};
void sc_square_rpc(struct sc_square *_p);
static inline int32_t sc_square(int32_t x) {
    struct sc_square _p = {0};
    _p.x = x;
    sc_square_rpc(&_p);
    return _p._;
}

typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


static void sc_add_rpc(struct sc_add *_p) {
    /* line 4 */
    _p->_ = _p->a + _p->b; return;
}

static void sc_greet_rpc(struct sc_greet *_p) {
    /* line 7 */
    printf("hi %d\n", _p->n);
}

static void sc_strlen2_rpc(struct sc_strlen2 *_p) {
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

static void sc_dot_rpc(struct sc_dot *_p) {
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

void sc_square_rpc(struct sc_square *_p) {
    /* line 23 */
    _p->_ = _p->x * _p->x; return;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 26 */
    int32_t r = sc_add(3, 4);
    /* line 27 */
    printf("add = %d\n", r);
    /* line 28 */
    sc_greet(7);
    /* line 29 */
    printf("len = %d\n", sc_strlen2("abcd"));
    /* line 30 */
    int32_t u[3] = {1, 2, 3};
    /* line 31 */
    int32_t v[3] = {4, 5, 6};
    /* line 32 */
    printf("dot = %d\n", sc_dot(u, v));
    /* line 33 */
    printf("sq = %d\n", sc_square(9));
    /* line 34 */
    return 0;
}
