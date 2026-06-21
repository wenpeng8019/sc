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
    /* line 12 */
    *(dst) = *(src);
}

int32_t main(void) {
    /* line 15 */
    volatile int32_t a = 5;
    /* line 16 */
    volatile uint32_t *reg = NULL;
    /* line 17 */
    const volatile uint32_t *x = NULL;
    /* line 18 */
    const node *p = NULL;
    /* line 19 */
    node *const q = NULL;
    /* line 20 */
    const node *const r = NULL;
    /* line 21 */
    const int32_t n = 7;
    /* line 22 */
    int32_t src = 11;
    /* line 23 */
    int32_t dst = 0;
    /* line 24 */
    copy(&(dst), &(src));
    /* line 25 */
    return dst + n;
}
