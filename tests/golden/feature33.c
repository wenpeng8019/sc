/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct Point Point;
typedef struct Vec_int Vec_int;
typedef struct Vec_dbl Vec_dbl;
typedef struct Vec_pt Vec_pt;

typedef struct Point {
    int32_t x;
    int32_t y;
} Point;

typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;

typedef struct Vec_int {
    int32_t data[8];
    int32_t len;
} Vec_int;

static void Vec_int_push(Vec_int *v, int32_t x);
static int32_t Vec_int_get(Vec_int *v, int32_t i);
typedef struct Vec_dbl {
    double data[8];
    int32_t len;
} Vec_dbl;

static void Vec_dbl_push(Vec_dbl *v, double x);
static double Vec_dbl_get(Vec_dbl *v, int32_t i);
typedef struct Vec_pt {
    Point data[8];
    int32_t len;
} Vec_pt;

static void Vec_pt_push(Vec_pt *v, Point x);
static Point Vec_pt_get(Vec_pt *v, int32_t i);
static int32_t max_i(int32_t a, int32_t b);
static double max_d(double a, double b);

int32_t main(void) {
    /* line 45 */
    Vec_int vi = {0};
    /* line 46 */
    vi.len = 0;
    /* line 47 */
    Vec_int_push(&(vi), 10);
    /* line 48 */
    Vec_int_push(&(vi), 20);
    /* line 49 */
    Vec_int_push(&(vi), 30);
    /* line 50 */
    int32_t _sq0 = Vec_int_get(&(vi), 0);
    int32_t _sq1 = Vec_int_get(&(vi), 1);
    int32_t _sq2 = Vec_int_get(&(vi), 2);
    printf("Vec_int: %d %d %d\n", _sq0, _sq1, _sq2);
    /* line 52 */
    Vec_dbl vd = {0};
    /* line 53 */
    vd.len = 0;
    /* line 54 */
    Vec_dbl_push(&(vd), 1.5);
    /* line 55 */
    Vec_dbl_push(&(vd), 2.5);
    /* line 56 */
    double _sq3 = Vec_dbl_get(&(vd), 0);
    double _sq4 = Vec_dbl_get(&(vd), 1);
    printf("Vec_dbl: %g %g\n", _sq3, _sq4);
    /* line 58 */
    Vec_pt vp = {0};
    /* line 59 */
    vp.len = 0;
    /* line 60 */
    Point p0 = {3, 4};
    /* line 61 */
    Vec_pt_push(&(vp), p0);
    /* line 62 */
    Point got = Vec_pt_get(&(vp), 0);
    /* line 63 */
    printf("Vec_pt[0]: (%d, %d)\n", got.x, got.y);
    /* line 65 */
    int32_t _sq5 = max_i(3, 7);
    double _sq6 = max_d(1.5, 0.5);
    printf("max_i(3,7)=%d  max_d(1.5,0.5)=%g\n", _sq5, _sq6);
    /* line 66 */
    return 0;
}

static void Vec_int_push(Vec_int *v, int32_t x) {
    /* line 6 */
    v->data[v->len] = x;
    /* line 7 */
    v->len = (v->len + 1);
}

static int32_t Vec_int_get(Vec_int *v, int32_t i) {
    /* line 9 */
    return v->data[i];
}

static void Vec_dbl_push(Vec_dbl *v, double x) {
    /* line 6 */
    v->data[v->len] = x;
    /* line 7 */
    v->len = (v->len + 1);
}

static double Vec_dbl_get(Vec_dbl *v, int32_t i) {
    /* line 9 */
    return v->data[i];
}

static void Vec_pt_push(Vec_pt *v, Point x) {
    /* line 6 */
    v->data[v->len] = x;
    /* line 7 */
    v->len = (v->len + 1);
}

static Point Vec_pt_get(Vec_pt *v, int32_t i) {
    /* line 9 */
    return v->data[i];
}

static int32_t max_i(int32_t a, int32_t b) {
    /* line 2 */
    if (a > b) {
        /* line 3 */
        return a;
    }
    /* line 4 */
    return b;
}

static double max_d(double a, double b) {
    /* line 2 */
    if (a > b) {
        /* line 3 */
        return a;
    }
    /* line 4 */
    return b;
}
