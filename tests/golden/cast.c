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
    /* line 13 */
    int64_t big = 1000;
    /* line 14 */
    int32_t n = ((int32_t)(big));
    /* line 15 */
    printf("assign=%d\n", n);
    /* line 18 */
    box b = {0};
    /* line 19 */
    b.v = 42;
    /* line 20 */
    void *pv = &(b);
    /* line 21 */
    printf("deref=%d\n", ((box*)(pv))->v);
    /* line 24 */
    void *raw = malloc(8);
    /* line 25 */
    box *pb = ((box*)(raw));
    /* line 26 */
    pb->v = 7;
    /* line 27 */
    printf("heap=%d\n", pb->v);
    /* line 28 */
    free(((void*)(raw)));
    /* line 31 */
    box *sp = &(b);
    /* line 32 */
    box **ppb = &(sp);
    /* line 33 */
    box **qq = ((box**)(ppb));
    /* line 34 */
    printf("pp=%d\n", qq[0]->v);
    /* line 35 */
    return 0;
}
