/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct box box;

typedef struct box {
    int32_t v;
} box;

typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


int32_t main(void) {
    /* line 15 */
    int64_t big = 1000;
    /* line 16 */
    int32_t n = ((int32_t)(big));
    /* line 17 */
    printf("assign=%d\n", n);
    /* line 20 */
    box b = {0};
    /* line 21 */
    b.v = 42;
    /* line 22 */
    void *pv = &(b);
    /* line 23 */
    printf("deref=%d\n", ((box*)(pv))->v);
    /* line 26 */
    void *raw = malloc(8);
    /* line 27 */
    box *pb = ((box*)(raw));
    /* line 28 */
    pb->v = 7;
    /* line 29 */
    printf("heap=%d\n", pb->v);
    /* line 30 */
    free(((void*)(raw)));
    /* line 33 */
    box *sp = &(b);
    /* line 34 */
    box **ppb = &(sp);
    /* line 35 */
    box **qq = ((box**)(ppb));
    /* line 36 */
    printf("pp=%d\n", qq[0]->v);
    /* line 37 */
    return 0;
}
