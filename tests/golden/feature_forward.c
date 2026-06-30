/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct node_a node_a;
typedef struct node_b node_b;

typedef struct node_a {
    node_b *pb;
} node_a;

typedef struct node_b {
    node_a *pa;
} node_b;

uint8_t is_even(int32_t n);
uint8_t is_odd(int32_t n);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


uint8_t is_even(int32_t n) {
    /* line 14 */
    if (n == 0) {
        /* line 15 */
        return true;
    }
    /* line 16 */
    return is_odd(n - 1);
}

uint8_t is_odd(int32_t n) {
    /* line 19 */
    if (n == 0) {
        /* line 20 */
        return false;
    }
    /* line 21 */
    return is_even(n - 1);
}

int32_t main(void) {
    /* line 24 */
    node_a x = {0};
    /* line 25 */
    node_b y = {0};
    /* line 26 */
    x.pb = &(y);
    /* line 27 */
    y.pa = &(x);
    /* line 28 */
    uint8_t _sq0 = is_even(10);
    uint8_t _sq1 = is_odd(7);
    printf("even(10)=%d odd(7)=%d\n", _sq0, _sq1);
    /* line 29 */
    return 0;
}
