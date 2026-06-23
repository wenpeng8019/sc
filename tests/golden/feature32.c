/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct Point Point;

#define def_max(T, N) \
static T max_##N(T a, T b) { \
        if (a > b) { \
            return a; \
        } \
        return b; \
}
def_max(int32_t, i)
def_max(double, d)
#define def_vec(T, N) \
    typedef struct Vec_##N { \
        T *data; \
        int32_t len; \
        int32_t cap; \
    } Vec_##N; \
static void Vec_##N##_push(Vec_##N *v, T x) { \
        v->data[v->len] = x; \
        v->len = (v->len + 1); \
} \
static T Vec_##N##_get(Vec_##N *v, int32_t i) { \
        return v->data[i]; \
}
def_vec(int32_t, int)
def_vec(double, dbl)
typedef struct Point {
    int32_t x;
    int32_t y;
} Point;

def_vec(Point, pt)
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


int32_t main(void) {
    /* line 52 */
    printf("max_i(3,7)=%d  max_d(1.5,0.5)=%g\n", max_i(3, 7), max_d(1.5, 0.5));
    /* line 54 */
    int32_t ibuf[4];
    /* line 55 */
    Vec_int iv = {ibuf, 0, 4};
    /* line 56 */
    Vec_int_push(&(iv), 10);
    /* line 57 */
    Vec_int_push(&(iv), 20);
    /* line 58 */
    printf("Vec_int: %d %d\n", Vec_int_get(&(iv), 0), Vec_int_get(&(iv), 1));
    /* line 60 */
    double dbuf[4];
    /* line 61 */
    Vec_dbl dv = {dbuf, 0, 4};
    /* line 62 */
    Vec_dbl_push(&(dv), 1.5);
    /* line 63 */
    printf("Vec_dbl: %g\n", Vec_dbl_get(&(dv), 0));
    /* line 65 */
    Point pbuf[4] = {0};
    /* line 66 */
    Vec_pt pv = {pbuf, 0, 4};
    /* line 67 */
    Point p0 = {3, 4};
    /* line 68 */
    Vec_pt_push(&(pv), p0);
    /* line 69 */
    Point p = Vec_pt_get(&(pv), 0);
    /* line 70 */
    printf("Vec_pt: %d %d\n", p.x, p.y);
    /* line 71 */
    return 0;
}
