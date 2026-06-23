/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

#define TAG 7
#define decl_pair(pfx) \
    static int32_t pfx##_lo = 0; \
    static int32_t pfx##_hi = 1;
#define show(x) \
    printf("%s = %d\n", #x, x);
#define sumprint(fmt, ...) \
    printf(fmt, __VA_ARGS__);
decl_pair(g)
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


int32_t main(void) {
    /* line 24 */
    int32_t count = TAG;
    /* line 25 */
    show(count)
    /* line 26 */
    sumprint("sum=%d\n", count)
    /* line 27 */
    return count;
}
