/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_point sc_point;

typedef struct sc_point {
    int32_t x;
    int32_t y;
} sc_point;

static int32_t sc_copy_n(int32_t *restrict dst, const int32_t *restrict src, int32_t n);
static int32_t sc_sum_point(const sc_point *p);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


static int32_t sc_copy_n(int32_t *restrict dst, const int32_t *restrict src, int32_t n) {
    /* line 17 */
    int32_t i = 0;
    /* line 18 */
    while (i < n) {
        /* line 19 */
        dst[i] = src[i];
        /* line 20 */
        i = (i + 1);
    }
    /* line 21 */
    return 0;
}

static int32_t sc_sum_point(const sc_point *p) {
    /* line 25 */
    return p->x + p->y;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 29 */
    int32_t src[3] = {10, 20, 30};
    /* line 30 */
    int32_t dst[3] = {0, 0, 0};
    /* line 31 */
    sc_copy_n(&(dst[0]), &(src[0]), 3);
    /* line 32 */
    printf("copy:      %d %d %d\n", dst[0], dst[1], dst[2]);
    /* line 35 */
    sc_point pt = {.x = 9, .y = 11};
    /* line 36 */
    printf("sum_point: %d\n", sc_sum_point(&(pt)));
    /* line 39 */
    volatile int32_t flag = 1;
    /* line 40 */
    printf("flag:      %d\n", flag);
    /* line 43 */
    sc_point *const q = &(pt);
    /* line 44 */
    q->x = 100;
    /* line 45 */
    printf("q->x:      %d\n", q->x);
    /* line 48 */
    int32_t *raw = &(pt.x);
    /* line 49 */
    const int32_t *const ro = ((const int32_t*)(raw));
    /* line 50 */
    printf("ro:        %d\n", *(ro));
    /* line 51 */
    volatile int32_t *vp = ((volatile int32_t*)(&(flag)));
    /* line 52 */
    printf("vp:        %d\n", *(vp));
    /* line 54 */
    return 0;
}
