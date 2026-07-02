/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_Point sc_Point;
typedef struct sc_Vec_int sc_Vec_int;
typedef struct sc_Vec_dbl sc_Vec_dbl;
typedef struct sc_Vec_pt sc_Vec_pt;

typedef struct sc_Point {
    int32_t x;
    int32_t y;
} sc_Point;

typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;

typedef struct sc_Vec_int {
    int32_t data[8];
    int32_t len;
} sc_Vec_int;

static void sc_Vec_int_push(sc_Vec_int *v, int32_t x);
static int32_t sc_Vec_int_get(sc_Vec_int *v, int32_t i);
typedef struct sc_Vec_dbl {
    double data[8];
    int32_t len;
} sc_Vec_dbl;

static void sc_Vec_dbl_push(sc_Vec_dbl *v, double x);
static double sc_Vec_dbl_get(sc_Vec_dbl *v, int32_t i);
typedef struct sc_Vec_pt {
    sc_Point data[8];
    int32_t len;
} sc_Vec_pt;

static void sc_Vec_pt_push(sc_Vec_pt *v, sc_Point x);
static sc_Point sc_Vec_pt_get(sc_Vec_pt *v, int32_t i);
static int32_t sc_max_i(int32_t a, int32_t b);
static double sc_max_d(double a, double b);

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 45 */
    sc_Vec_int vi = {0};
    /* line 46 */
    vi.len = 0;
    /* line 47 */
    sc_Vec_int_push(&(vi), 10);
    /* line 48 */
    sc_Vec_int_push(&(vi), 20);
    /* line 49 */
    sc_Vec_int_push(&(vi), 30);
    /* line 50 */
    int32_t _sq0 = sc_Vec_int_get(&(vi), 0);
    int32_t _sq1 = sc_Vec_int_get(&(vi), 1);
    int32_t _sq2 = sc_Vec_int_get(&(vi), 2);
    printf("Vec_int: %d %d %d\n", _sq0, _sq1, _sq2);
    /* line 52 */
    sc_Vec_dbl vd = {0};
    /* line 53 */
    vd.len = 0;
    /* line 54 */
    sc_Vec_dbl_push(&(vd), 1.5);
    /* line 55 */
    sc_Vec_dbl_push(&(vd), 2.5);
    /* line 56 */
    double _sq3 = sc_Vec_dbl_get(&(vd), 0);
    double _sq4 = sc_Vec_dbl_get(&(vd), 1);
    printf("Vec_dbl: %g %g\n", _sq3, _sq4);
    /* line 58 */
    sc_Vec_pt vp = {0};
    /* line 59 */
    vp.len = 0;
    /* line 60 */
    sc_Point p0 = {3, 4};
    /* line 61 */
    sc_Vec_pt_push(&(vp), p0);
    /* line 62 */
    sc_Point got = sc_Vec_pt_get(&(vp), 0);
    /* line 63 */
    printf("Vec_pt[0]: (%d, %d)\n", got.x, got.y);
    /* line 65 */
    int32_t _sq5 = sc_max_i(3, 7);
    double _sq6 = sc_max_d(1.5, 0.5);
    printf("max_i(3,7)=%d  max_d(1.5,0.5)=%g\n", _sq5, _sq6);
    /* line 66 */
    return 0;
}

static void sc_Vec_int_push(sc_Vec_int *v, int32_t x) {
    /* line 6 */
    v->data[v->len] = x;
    /* line 7 */
    v->len = (v->len + 1);
}

static int32_t sc_Vec_int_get(sc_Vec_int *v, int32_t i) {
    /* line 9 */
    return v->data[i];
}

static void sc_Vec_dbl_push(sc_Vec_dbl *v, double x) {
    /* line 6 */
    v->data[v->len] = x;
    /* line 7 */
    v->len = (v->len + 1);
}

static double sc_Vec_dbl_get(sc_Vec_dbl *v, int32_t i) {
    /* line 9 */
    return v->data[i];
}

static void sc_Vec_pt_push(sc_Vec_pt *v, sc_Point x) {
    /* line 6 */
    v->data[v->len] = x;
    /* line 7 */
    v->len = (v->len + 1);
}

static sc_Point sc_Vec_pt_get(sc_Vec_pt *v, int32_t i) {
    /* line 9 */
    return v->data[i];
}

static int32_t sc_max_i(int32_t a, int32_t b) {
    /* line 2 */
    if (a > b) {
        /* line 3 */
        return a;
    }
    /* line 4 */
    return b;
}

static double sc_max_d(double a, double b) {
    /* line 2 */
    if (a > b) {
        /* line 3 */
        return a;
    }
    /* line 4 */
    return b;
}
