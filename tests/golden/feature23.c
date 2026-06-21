/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct point point;

typedef struct point {
    int32_t x;
    int32_t y;
} point;

static int32_t copy_n(int32_t *restrict dst, const int32_t *restrict src, int32_t n);
static int32_t sum_point(const point *p);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static int32_t copy_n(int32_t *restrict dst, const int32_t *restrict src, int32_t n) {
    /* line 19 */
    int32_t i = 0;
    /* line 20 */
    while (i < n) {
        /* line 21 */
        dst[i] = src[i];
        /* line 22 */
        i = (i + 1);
    }
    /* line 23 */
    return 0;
}

static int32_t sum_point(const point *p) {
    /* line 27 */
    return p->x + p->y;
}

int32_t main(void) {
    /* line 31 */
    int32_t src[3] = {10, 20, 30};
    /* line 32 */
    int32_t dst[3] = {0, 0, 0};
    /* line 33 */
    copy_n(&(dst[0]), &(src[0]), 3);
    /* line 34 */
    printf("copy:      %d %d %d\n", dst[0], dst[1], dst[2]);
    /* line 37 */
    point pt = {.x = 9, .y = 11};
    /* line 38 */
    printf("sum_point: %d\n", sum_point(&(pt)));
    /* line 41 */
    volatile int32_t flag = 1;
    /* line 42 */
    printf("flag:      %d\n", flag);
    /* line 45 */
    point *const q = &(pt);
    /* line 46 */
    q->x = 100;
    /* line 47 */
    printf("q->x:      %d\n", q->x);
    /* line 50 */
    int32_t *raw = &(pt.x);
    /* line 51 */
    const int32_t *const ro = ((const int32_t*)(raw));
    /* line 52 */
    printf("ro:        %d\n", *(ro));
    /* line 53 */
    volatile int32_t *vp = ((volatile int32_t*)(&(flag)));
    /* line 54 */
    printf("vp:        %d\n", *(vp));
    /* line 56 */
    return 0;
}
