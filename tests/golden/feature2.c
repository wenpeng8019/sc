/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_point sc_point;
typedef struct sc_rect sc_rect;
typedef struct sc_obj sc_obj;

typedef struct sc_point {
    int32_t x;
    int32_t y;
} sc_point;

typedef struct sc_rect {
    sc_point lt;
    sc_point rb;
} sc_rect;

static int32_t sc_clamp(int32_t v, int32_t lo, int32_t hi);
static int32_t sc_area(sc_rect *r);
typedef int32_t (*sc_add_f)(int32_t a, int32_t b);

static int32_t sc_add1(int32_t a, int32_t b);
static int32_t sc_add2(int32_t a, int32_t b);
typedef void (*sc_dec_f)(int32_t v);

static void sc_dec(int32_t v);
static int32_t sc_add3(int32_t a, int32_t b, int32_t c);
static int32_t sc_desc(char *s, sc_point pt);
typedef struct sc_obj {
    int32_t abc;
    int32_t (*func1)(sc_obj *o, int32_t x, int32_t y);
    sc_add_f func2;
    int32_t (*scale)(struct sc_obj *, int32_t k);
} sc_obj;

static int32_t sc_obj_add(sc_obj *o, int32_t x, int32_t y);
static int32_t sc_obj_scale(sc_obj *_this, int32_t k);
static int32_t sc_sq(int32_t x);
static void sc_my_printf(char *fmt, ...);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


static int32_t sc_clamp(int32_t v, int32_t lo, int32_t hi) {
    /* line 23 */
    if (v < lo) {
        /* line 24 */
        return lo;
    }
    /* line 25 */
    if (v > hi) {
        /* line 26 */
        return hi;
    }
    /* line 27 */
    return v;
}

static int32_t sc_area(sc_rect *r) {
    /* line 32 */
    int32_t w = r->rb.x - r->lt.x;
    /* line 33 */
    int32_t h = r->rb.y - r->lt.y;
    /* line 34 */
    return w * h;
}

static int32_t sc_add1(int32_t a, int32_t b) {
    /* line 41 */
    return a + b;
}

static int32_t sc_add2(int32_t a, int32_t b) {
    /* line 43 */
    return a + (2 * b);
}

static void sc_dec(int32_t v) {
    /* line 47 */
    --(v);
}

static int32_t sc_add3(int32_t a, int32_t b, int32_t c) {
    /* line 52 */
    return (a + b) + c;
}

static int32_t sc_desc(char *s, sc_point pt) {
    /* line 55 */
    if (s == NULL) {
        /* line 56 */
        return pt.x + pt.y;
    }
    /* line 57 */
    return 100;
}

static int32_t sc_obj_add(sc_obj *o, int32_t x, int32_t y) {
    /* line 69 */
    return (o->abc + x) + y;
}

static int32_t sc_obj_scale(sc_obj *_this, int32_t k) {
    /* line 73 */
    return _this->abc * k;
}

static int32_t sc_sq(int32_t x) {
    /* line 77 */
    return x * x;
}

static void sc_my_printf(char *fmt, ...) {
    /* line 88 */
    va_list ap;
    /* line 89 */
    va_start(ap, fmt);
    /* line 90 */
    vprintf(fmt, ap);
    /* line 91 */
    va_end(ap);
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 98 */
    printf("clamp(42,0,10) = %d\n", sc_clamp(42, 0, 10));
    /* line 101 */
    sc_rect r = {0};
    /* line 102 */
    (r.lt.x = 0) , (r.lt.y = 0);
    /* line 103 */
    (r.rb.x = 10) , (r.rb.y = 5);
    /* line 104 */
    printf("area = %d\n", sc_area(&(r)));
    /* line 107 */
    printf("add1(3,4) = %d\n", sc_add1(3, 4));
    /* line 108 */
    printf("add2(3,4) = %d\n", sc_add2(3, 4));
    /* line 113 */
    int32_t (*cb)(int32_t x);
    /* line 114 */
    cb = sc_sq;
    /* line 115 */
    printf("cb(7) = %d\n", cb(7));
    /* line 118 */
    sc_obj o = {0};
    /* line 119 */
    o.abc = 10;
    /* line 120 */
    if (o.func1 == NULL) {
        /* line 121 */
        printf("func1 is nil\n");
    }
    /* line 122 */
    o.func1 = sc_obj_add;
    /* line 123 */
    printf("o.func1(2,3) = %d\n", o.func1(&(o), 2, 3));
    /* line 124 */
    printf("po->func1(4,5) = %d\n", o.func1(&(o), 4, 5));
    /* line 130 */
    if (o.scale == NULL) {
        /* line 131 */
        printf("scale is nil\n");
    }
    /* line 132 */
    o.scale = sc_obj_scale;
    /* line 133 */
    printf("o.scale(3) = %d\n", o.scale(&o, 3));
    /* line 134 */
    sc_obj *po = &(o);
    /* line 135 */
    printf("po->scale(4) = %d\n", po->scale(po, 4));
    /* line 140 */
    sc_my_printf("sc says: %s %d\n", "hello", 42);
    /* line 150 */
    printf("add3(7) = %d\n", sc_add3(7, 0, 0));
    /* line 151 */
    printf("add3(1,2) = %d\n", sc_add3(1, 2, 0));
    /* line 152 */
    printf("desc() = %d\n", sc_desc(NULL, (sc_point){0}));
    /* line 155 */
    printf("o.func1(&o) = %d\n", o.func1(&(o), 0, 0));
    /* line 156 */
    printf("cb() = %d\n", cb(0));
    /* line 159 */
    return 0;
}
