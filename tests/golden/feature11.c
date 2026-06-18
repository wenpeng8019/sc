/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static void demo_scalar(void);
static void bump(int32_t *p);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static void demo_scalar(void) {
    /* line 13 */
    int32_t x = 0;
    /* line 14 */
    sc_set(&(x), 42);
    /* line 15 */
    int32_t y = sc_get(&(x));
    /* line 16 */
    printf("scalar: set(42) get()=%d\n", y);
}

static void bump(int32_t *p) {
    /* line 20 */
    int32_t cur = sc_get_acq((p));
    /* line 21 */
    sc_set_rel((p), cur + 1);
}

int32_t main(void) {
    /* line 24 */
    demo_scalar();
    /* line 26 */
    int32_t n = 10;
    /* line 27 */
    bump(&(n));
    /* line 28 */
    bump(&(n));
    /* line 29 */
    printf("pointer: bump x2 -> %d\n", sc_get(&(n)));
    /* line 32 */
    double f = 1.5;
    /* line 33 */
    sc_set(&(f), 3.25);
    /* line 34 */
    printf("f8: set(3.25) get()=%.2f\n", sc_get(&(f)));
    /* line 35 */
    return 0;
}
