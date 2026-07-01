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
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;

static int32_t g_lo = 0;
static int32_t g_hi = 1;

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 23 */
    int32_t count = TAG;
    /* line 24 */
    show(count)
    /* line 25 */
    sumprint("sum=%d\n", count)
    /* line 26 */
    return count;
}
