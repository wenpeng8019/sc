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

uint8_t is_even(int32_t n) {
    /* line 15 */
    if (n == 0) {
        /* line 16 */
        return true;
    }
    /* line 17 */
    return is_odd(n - 1);
}

uint8_t is_odd(int32_t n) {
    /* line 20 */
    if (n == 0) {
        /* line 21 */
        return false;
    }
    /* line 22 */
    return is_even(n - 1);
}

int32_t main(void) {
    /* line 25 */
    node_a x = {0};
    /* line 26 */
    node_b y = {0};
    /* line 27 */
    x.pb = &(y);
    /* line 28 */
    y.pa = &(x);
    /* line 29 */
    printf("even(10)=%d odd(7)=%d\n", is_even(10), is_odd(7));
    /* line 30 */
    return 0;
}
