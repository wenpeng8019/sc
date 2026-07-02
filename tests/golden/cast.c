/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_box sc_box;

typedef struct sc_box {
    int32_t v;
} sc_box;

typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 13 */
    int64_t big = 1000;
    /* line 14 */
    int32_t n = ((int32_t)(big));
    /* line 15 */
    printf("assign=%d\n", n);
    /* line 18 */
    sc_box b = {0};
    /* line 19 */
    b.v = 42;
    /* line 20 */
    void *pv = &(b);
    /* line 21 */
    printf("deref=%d\n", ((sc_box*)(pv))->v);
    /* line 24 */
    void *raw = malloc(8);
    /* line 25 */
    sc_box *pb = ((sc_box*)(raw));
    /* line 26 */
    pb->v = 7;
    /* line 27 */
    printf("heap=%d\n", pb->v);
    /* line 28 */
    free(((void*)(raw)));
    /* line 31 */
    sc_box *sp = &(b);
    /* line 32 */
    sc_box **ppb = &(sp);
    /* line 33 */
    sc_box **qq = ((sc_box**)(ppb));
    /* line 34 */
    printf("pp=%d\n", qq[0]->v);
    /* line 35 */
    return 0;
}
