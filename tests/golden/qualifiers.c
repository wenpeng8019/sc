/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_node sc_node;

typedef struct sc_node {
    int32_t v;
} sc_node;

void sc_copy(int32_t *restrict dst, const int32_t *restrict src);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_copy(int32_t *restrict dst, const int32_t *restrict src) {
    /* line 11 */
    *(dst) = *(src);
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 14 */
    volatile int32_t a = 5;
    /* line 15 */
    volatile uint32_t *reg = NULL;
    /* line 16 */
    const volatile uint32_t *x = NULL;
    /* line 17 */
    const sc_node *p = NULL;
    /* line 18 */
    sc_node *const q = NULL;
    /* line 19 */
    const sc_node *const r = NULL;
    /* line 20 */
    const int32_t n = 7;
    /* line 21 */
    int32_t src = 11;
    /* line 22 */
    int32_t dst = 0;
    /* line 23 */
    sc_copy(&(dst), &(src));
    /* line 24 */
    return dst + n;
}
