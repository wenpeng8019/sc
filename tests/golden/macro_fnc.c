/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

#define sc_make_counter(nm) \
    static TLS int32_t sc_cnt_##nm = 0; \
static int32_t bump_##nm(void) { \
        sc_cnt_##nm = (sc_cnt_##nm + 1); \
        return sc_cnt_##nm; \
}
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;

static TLS int32_t sc_cnt_a = 0;
static int32_t sc_bump_a(void);
static TLS int32_t sc_cnt_b = 0;
static int32_t sc_bump_b(void);

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 16 */
    int32_t _sq0 = sc_bump_a();
    int32_t _sq1 = sc_bump_a();
    int32_t _sq2 = sc_bump_b();
    printf("%d %d %d\n", _sq0, _sq1, _sq2);
    /* line 17 */
    return 0;
}

static int32_t sc_bump_a(void) {
    /* line 3 */
    sc_cnt_a = (sc_cnt_a + 1);
    /* line 4 */
    return sc_cnt_a;
}

static int32_t sc_bump_b(void) {
    /* line 3 */
    sc_cnt_b = (sc_cnt_b + 1);
    /* line 4 */
    return sc_cnt_b;
}
