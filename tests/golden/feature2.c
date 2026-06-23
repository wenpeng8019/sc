/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct point point;
typedef struct rect rect;
typedef struct obj obj;

typedef struct point {
    int32_t x;
    int32_t y;
} point;

typedef struct rect {
    point lt;
    point rb;
} rect;

static int32_t clamp(int32_t v, int32_t lo, int32_t hi);
static int32_t area(rect *r);
typedef int32_t (*add_f)(int32_t a, int32_t b);

static int32_t add1(int32_t a, int32_t b);
static int32_t add2(int32_t a, int32_t b);
typedef void (*dec_f)(int32_t v);

static void dec(int32_t v);
static int32_t add3(int32_t a, int32_t b, int32_t c);
static int32_t desc(char *s, point pt);
typedef struct obj {
    int32_t abc;
    int32_t (*func1)(obj *o, int32_t x, int32_t y);
    add_f func2;
    int32_t (*scale)(struct obj *, int32_t k);
} obj;

static int32_t obj_add(obj *o, int32_t x, int32_t y);
static int32_t obj_scale(obj *_this, int32_t k);
static int32_t sq(int32_t x);
static void my_printf(char *fmt, ...);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static int32_t clamp(int32_t v, int32_t lo, int32_t hi) {
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

static int32_t area(rect *r) {
    /* line 32 */
    int32_t w = r->rb.x - r->lt.x;
    /* line 33 */
    int32_t h = r->rb.y - r->lt.y;
    /* line 34 */
    return w * h;
}

static int32_t add1(int32_t a, int32_t b) {
    /* line 41 */
    return a + b;
}

static int32_t add2(int32_t a, int32_t b) {
    /* line 43 */
    return a + (2 * b);
}

static void dec(int32_t v) {
    /* line 47 */
    --(v);
}

static int32_t add3(int32_t a, int32_t b, int32_t c) {
    /* line 52 */
    return (a + b) + c;
}

static int32_t desc(char *s, point pt) {
    /* line 55 */
    if (s == NULL) {
        /* line 56 */
        return pt.x + pt.y;
    }
    /* line 57 */
    return 100;
}

static int32_t obj_add(obj *o, int32_t x, int32_t y) {
    /* line 69 */
    return (o->abc + x) + y;
}

static int32_t obj_scale(obj *_this, int32_t k) {
    /* line 73 */
    return _this->abc * k;
}

static int32_t sq(int32_t x) {
    /* line 77 */
    return x * x;
}

static void my_printf(char *fmt, ...) {
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
    /* line 98 */
    printf("clamp(42,0,10) = %d\n", clamp(42, 0, 10));
    /* line 101 */
    rect r = {0};
    /* line 102 */
    (r.lt.x = 0) , (r.lt.y = 0);
    /* line 103 */
    (r.rb.x = 10) , (r.rb.y = 5);
    /* line 104 */
    printf("area = %d\n", area(&(r)));
    /* line 107 */
    printf("add1(3,4) = %d\n", add1(3, 4));
    /* line 108 */
    printf("add2(3,4) = %d\n", add2(3, 4));
    /* line 113 */
    int32_t (*cb)(int32_t x);
    /* line 114 */
    cb = sq;
    /* line 115 */
    printf("cb(7) = %d\n", cb(7));
    /* line 118 */
    obj o = {0};
    /* line 119 */
    o.abc = 10;
    /* line 120 */
    if (o.func1 == NULL) {
        /* line 121 */
        printf("func1 is nil\n");
    }
    /* line 122 */
    o.func1 = obj_add;
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
    o.scale = obj_scale;
    /* line 133 */
    printf("o.scale(3) = %d\n", o.scale(&o, 3));
    /* line 134 */
    obj *po = &(o);
    /* line 135 */
    printf("po->scale(4) = %d\n", po->scale(po, 4));
    /* line 140 */
    my_printf("sc says: %s %d\n", "hello", 42);
    /* line 150 */
    printf("add3(7) = %d\n", add3(7, 0, 0));
    /* line 151 */
    printf("add3(1,2) = %d\n", add3(1, 2, 0));
    /* line 152 */
    printf("desc() = %d\n", desc(NULL, (point){0}));
    /* line 155 */
    printf("o.func1(&o) = %d\n", o.func1(&(o), 0, 0));
    /* line 156 */
    printf("cb() = %d\n", cb(0));
    /* line 159 */
    return 0;
}
