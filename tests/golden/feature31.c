/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

#define CAP 4
#define dump(x) \
    printf("  %s = %d\n", #x, x);
#define tally(tag) \
    static int32_t tag##_n = 0; \
    tag##_n = (tag##_n + CAP); \
    printf("  %s_n = %d\n", #tag, tag##_n);
#define logf(fmt, ...) \
    printf(fmt, __VA_ARGS__);
#define gpair(pfx) \
    extern int32_t pfx##_lo; \
    int32_t pfx##_lo = 10; \
    static int32_t pfx##_hi = 20;
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;

int32_t cfg_lo = 10;
static int32_t cfg_hi = 20;

int32_t main(void) {
    /* line 43 */
    int32_t count = CAP;
    /* line 44 */
    printf("object macro: CAP=%d\n", CAP);
    /* line 46 */
    printf("stringify:\n");
    /* line 47 */
    dump(count)
    /* line 49 */
    printf("paste:\n");
    /* line 50 */
    tally(item)
    /* line 52 */
    printf("variadic + macro globals:\n");
    /* line 53 */
    logf("  sum=%d range=[%d,%d]\n", count + cfg_lo, cfg_lo, cfg_hi)
    /* line 55 */
    return 0;
}
