/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct node node;

typedef struct node {
    int32_t v;
} node;

void copy(int32_t *restrict dst, const int32_t *restrict src);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


void copy(int32_t *restrict dst, const int32_t *restrict src) {
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
    const node *p = NULL;
    /* line 18 */
    node *const q = NULL;
    /* line 19 */
    const node *const r = NULL;
    /* line 20 */
    const int32_t n = 7;
    /* line 21 */
    int32_t src = 11;
    /* line 22 */
    int32_t dst = 0;
    /* line 23 */
    copy(&(dst), &(src));
    /* line 24 */
    return dst + n;
}
