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
} obj;

static int32_t obj_add(obj *o, int32_t x, int32_t y);
static int32_t sq(int32_t x);
static void my_printf(char *fmt, ...);

static int32_t clamp(int32_t v, int32_t lo, int32_t hi) {
    /* line 24 */
    if (v < lo) {
        /* line 25 */
        return lo;
    }
    /* line 26 */
    if (v > hi) {
        /* line 27 */
        return hi;
    }
    /* line 28 */
    return v;
}

static int32_t area(rect *r) {
    /* line 33 */
    int32_t w = r->rb.x - r->lt.x;
    /* line 34 */
    int32_t h = r->rb.y - r->lt.y;
    /* line 35 */
    return w * h;
}

static int32_t add1(int32_t a, int32_t b) {
    /* line 42 */
    return a + b;
}

static int32_t add2(int32_t a, int32_t b) {
    /* line 44 */
    return a + (2 * b);
}

static void dec(int32_t v) {
    /* line 48 */
    --(v);
}

static int32_t add3(int32_t a, int32_t b, int32_t c) {
    /* line 53 */
    return (a + b) + c;
}

static int32_t desc(char *s, point pt) {
    /* line 56 */
    if (s == NULL) {
        /* line 57 */
        return pt.x + pt.y;
    }
    /* line 58 */
    return 100;
}

static int32_t obj_add(obj *o, int32_t x, int32_t y) {
    /* line 69 */
    return (o->abc + x) + y;
}

static int32_t sq(int32_t x) {
    /* line 73 */
    return x * x;
}

static void my_printf(char *fmt, ...) {
    /* line 84 */
    va_list ap;
    /* line 85 */
    va_start(ap, fmt);
    /* line 86 */
    vprintf(fmt, ap);
    /* line 87 */
    va_end(ap);
}

int32_t main(void) {
    /* line 94 */
    printf("clamp(42,0,10) = %d\n", clamp(42, 0, 10));
    /* line 97 */
    rect r = {0};
    /* line 98 */
    (r.lt.x = 0) , (r.lt.y = 0);
    /* line 99 */
    (r.rb.x = 10) , (r.rb.y = 5);
    /* line 100 */
    printf("area = %d\n", area(&(r)));
    /* line 103 */
    printf("add1(3,4) = %d\n", add1(3, 4));
    /* line 104 */
    printf("add2(3,4) = %d\n", add2(3, 4));
    /* line 109 */
    int32_t (*cb)(int32_t x);
    /* line 110 */
    cb = sq;
    /* line 111 */
    printf("cb(7) = %d\n", cb(7));
    /* line 114 */
    obj o = {0};
    /* line 115 */
    o.abc = 10;
    /* line 116 */
    if (o.func1 == NULL) {
        /* line 117 */
        printf("func1 is nil\n");
    }
    /* line 118 */
    o.func1 = obj_add;
    /* line 119 */
    printf("o.func1(2,3) = %d\n", o.func1(&(o), 2, 3));
    /* line 120 */
    printf("po->func1(4,5) = %d\n", o.func1(&(o), 4, 5));
    /* line 125 */
    my_printf("sc says: %s %d\n", "hello", 42);
    /* line 135 */
    printf("add3(7) = %d\n", add3(7, 0, 0));
    /* line 136 */
    printf("add3(1,2) = %d\n", add3(1, 2, 0));
    /* line 137 */
    printf("desc() = %d\n", desc(NULL, (point){0}));
    /* line 140 */
    printf("o.func1(&o) = %d\n", o.func1(&(o), 0, 0));
    /* line 141 */
    printf("cb() = %d\n", cb(0));
    /* line 144 */
    return 0;
}
