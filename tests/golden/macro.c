/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

#define sc_TAG 7
#define sc_decl_pair(pfx) \
    static int32_t sc_pfx##_lo = 0; \
    static int32_t sc_pfx##_hi = 1;
#define sc_show(x) \
    printf("%s = %d\n", #x, x);
#define sc_sumprint(fmt, ...) \
    printf(fmt, __VA_ARGS__);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;

static int32_t sc_g_lo = 0;
static int32_t sc_g_hi = 1;

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 23 */
    int32_t count = sc_TAG;
    /* line 24 */
    sc_show(count)
    /* line 25 */
    sc_sumprint("sum=%d\n", count)
    /* line 26 */
    return count;
}
