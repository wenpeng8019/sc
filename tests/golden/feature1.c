/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct point point;
typedef struct rect rect;
typedef union value value;

typedef enum { /* base: int8_t */
    Red = 0,
    Green,
    Blue
} color;

typedef struct point {
    int32_t x;
    int32_t y;
} point;

typedef struct rect {
    point lt;
    point rb;
} rect;

typedef union value {
    int32_t i;
    float f;
} value;

typedef uint8_t byte;

typedef int32_t add_f(int32_t a, int32_t b);

static int32_t add1(int32_t a, int32_t b);
static int32_t add2(int32_t a, int32_t b);
typedef void dec_f(int32_t v);

static void dec(int32_t v);
static int32_t clamp(int32_t v, int32_t lo, int32_t hi);
static int32_t area(rect *r);
static const int32_t MAX = 100;
static int32_t counter = 0;

static int32_t add1(int32_t a, int32_t b) {
    /* line 34 */
    return a + b;
}

static int32_t add2(int32_t a, int32_t b) {
    /* line 36 */
    return a + (2 * b);
}

static void dec(int32_t v) {
    /* line 41 */
    --(v);
}

static int32_t clamp(int32_t v, int32_t lo, int32_t hi) {
    /* line 45 */
    if (v < lo) {
        /* line 46 */
        return lo;
    }
    /* line 47 */
    if (v > hi) {
        /* line 48 */
        return hi;
    }
    /* line 49 */
    return v;
}

static int32_t area(rect *r) {
    /* line 56 */
    int32_t w = r->rb.x - r->lt.x;
    /* line 57 */
    int32_t h = r->rb.y - r->lt.y;
    /* line 58 */
    return w * h;
}

int32_t main(void) {
    /* line 66 */
    point p = {0};
    /* line 67 */
    p.x = 3;
    /* line 68 */
    p.y = 4;
    /* line 70 */
    rect r = {0};
    /* line 71 */
    (r.lt.x = 0) , (r.lt.y = 0);
    /* line 72 */
    (r.rb.x = 10) , (r.rb.y = 5);
    /* line 74 */
    rect *r_ptr = &(r);
    /* line 77 */
    char *msg = "hello sc";
    /* line 78 */
    printf("%s\n", msg);
    /* line 79 */
    printf("add1(3,4) = %d\n", add1(p.x, p.y));
    /* line 80 */
    printf("add2(3,4) = %d\n", add2(p.x, p.y));
    /* line 81 */
    printf("area = %d\n", area(&(r)));
    /* line 82 */
    printf("clamp(42,0,10) = %d\n", clamp(42, 0, 10));
    /* line 85 */
    if ((p.x > 1) && (p.y < 10)) {
        /* line 88 */
        printf("cond ok\n");
    } else {
        /* line 90 */
        printf("cond fail\n");
    }
    /* line 93 */
    int32_t i;
    /* line 94 */
    for (i = 0; i < 3; i++) {
        /* line 95 */
        counter += i;
    }
    /* line 96 */
    printf("counter = %d\n", counter);
    /* line 98 */
    while (counter > 0) {
        /* line 99 */
        counter--;
        /* line 100 */
        if (counter == 1) {
            /* line 101 */
            break;
        }
    }
    /* line 103 */
    color c = Green;
    /* line 104 */
    printf("color = %d, MAX = %d\n", c, MAX);
    /* line 105 */
    return 0;
}
