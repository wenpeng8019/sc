/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct pair pair;

typedef struct pair {
    int32_t a;
    int32_t b;
} pair;


int32_t main(void) {
    /* line 10 */
    pair p = {0};
    /* line 11 */
    p.a = 3;
    /* line 12 */
    p.b = 4;
    /* line 15 */
    printf("sz_expr=%lld sz_type=%lld\n", ((int64_t)(sizeof(p))), ((int64_t)(sizeof(pair))));
    /* line 18 */
    printf("off_a=%lld off_b=%lld\n", ((int64_t)(offsetof(pair, a))), ((int64_t)(offsetof(pair, b))));
    /* line 21 */
    int32_t m = (p.a > p.b) ? p.a : p.b;
    /* line 22 */
    printf("max=%d\n", m);
    /* line 25 */
    int32_t arr[3];
    /* line 26 */
    arr[0] = p.a;
    /* line 27 */
    arr[1] = p.b;
    /* line 28 */
    arr[2] = m;
    /* line 29 */
    printf("sum=%d\n", (arr[0] + arr[1]) + arr[2]);
    /* line 30 */
    return 0;
}
