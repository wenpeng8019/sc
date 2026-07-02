/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_node sc_node;

typedef struct sc_node {
    int32_t v;
} sc_node;

int32_t sc_sum(int32_t *arr, int32_t n);
int32_t sc_deref(int32_t *p);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


int32_t sc_sum(int32_t *arr, int32_t n) {
    /* line 9 */
    int32_t s = 0;
    /* line 10 */
    int32_t i = 0;
    /* line 11 */
    while (i < n) {
        /* line 12 */
        s = (s + SC_PTRCHK(arr, "指针下标@ptr_check.sc:12")[i]);
        /* line 13 */
        i = (i + 1);
    }
    /* line 14 */
    return s;
}

int32_t sc_deref(int32_t *p) {
    /* line 17 */
    return *SC_PTRCHK(p, "解引用@ptr_check.sc:17");
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 20 */
    int32_t arr[3] = {10, 20, 30};
    /* line 21 */
    int32_t total = sc_sum(&(arr[SC_BOUNDCHK(0, 3, "数组下标@ptr_check.sc:21")]), 3);
    /* line 22 */
    int32_t one = arr[SC_BOUNDCHK(0, 3, "数组下标@ptr_check.sc:22")];
    /* line 23 */
    sc_node x = {0};
    /* line 24 */
    x.v = 7;
    /* line 25 */
    sc_node *p = &(x);
    /* line 26 */
    int32_t d = sc_deref(&(one));
    /* line 27 */
    return (total + SC_PTRCHK(p, "成员访问@ptr_check.sc:27")->v) + d;
}
