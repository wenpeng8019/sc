/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

#define sc_CAP 4
#define sc_dump(x) \
    printf("  %s = %d\n", #x, x);
#define sc_tally(tag) \
    static int32_t sc_tag##_n = 0; \
    sc_tag##_n = (sc_tag##_n + sc_CAP); \
    printf("  %s_n = %d\n", #tag, sc_tag##_n);
#define sc_logf(fmt, ...) \
    printf(fmt, __VA_ARGS__);
#define sc_gpair(pfx) \
    extern int32_t sc_pfx##_lo; \
    int32_t sc_pfx##_lo = 10; \
    static int32_t sc_pfx##_hi = 20;
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;

int32_t sc_cfg_lo = 10;
static int32_t sc_cfg_hi = 20;

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 41 */
    int32_t count = sc_CAP;
    /* line 42 */
    printf("object macro: CAP=%d\n", sc_CAP);
    /* line 44 */
    printf("stringify:\n");
    /* line 45 */
    sc_dump(count)
    /* line 47 */
    printf("paste:\n");
    /* line 48 */
    sc_tally(item)
    /* line 50 */
    printf("variadic + macro globals:\n");
    /* line 51 */
    sc_logf("  sum=%d range=[%d,%d]\n", count + sc_cfg_lo, sc_cfg_lo, sc_cfg_hi)
    /* line 53 */
    return 0;
}
