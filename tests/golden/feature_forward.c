/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_node_a sc_node_a;
typedef struct sc_node_b sc_node_b;

typedef struct sc_node_a {
    sc_node_b *pb;
} sc_node_a;

typedef struct sc_node_b {
    sc_node_a *pa;
} sc_node_b;

bool sc_is_even(int32_t n);
bool sc_is_odd(int32_t n);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


bool sc_is_even(int32_t n) {
    /* line 14 */
    if (n == 0) {
        /* line 15 */
        return true;
    }
    /* line 16 */
    return sc_is_odd(n - 1);
}

bool sc_is_odd(int32_t n) {
    /* line 19 */
    if (n == 0) {
        /* line 20 */
        return false;
    }
    /* line 21 */
    return sc_is_even(n - 1);
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 24 */
    sc_node_a x = {0};
    /* line 25 */
    sc_node_b y = {0};
    /* line 26 */
    x.pb = &(y);
    /* line 27 */
    y.pa = &(x);
    /* line 28 */
    bool _sq0 = sc_is_even(10);
    bool _sq1 = sc_is_odd(7);
    printf("even(10)=%d odd(7)=%d\n", _sq0, _sq1);
    /* line 29 */
    return 0;
}
