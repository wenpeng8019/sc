/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

#define make_counter(nm) \
    static TLS int32_t cnt_##nm = 0; \
static int32_t bump_##nm(void) { \
        cnt_##nm = (cnt_##nm + 1); \
        return cnt_##nm; \
}
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;

static TLS int32_t cnt_a = 0;
static int32_t bump_a(void);
static TLS int32_t cnt_b = 0;
static int32_t bump_b(void);

int32_t main(void) {
    /* line 16 */
    int32_t _sq0 = bump_a();
    int32_t _sq1 = bump_a();
    int32_t _sq2 = bump_b();
    printf("%d %d %d\n", _sq0, _sq1, _sq2);
    /* line 17 */
    return 0;
}

static int32_t bump_a(void) {
    /* line 3 */
    cnt_a = (cnt_a + 1);
    /* line 4 */
    return cnt_a;
}

static int32_t bump_b(void) {
    /* line 3 */
    cnt_b = (cnt_b + 1);
    /* line 4 */
    return cnt_b;
}
