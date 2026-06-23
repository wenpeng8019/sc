/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

#define make_counter(nm) \
    static TLS int32_t cnt_##nm = 0; \
static int32_t bump_##nm(void) { \
        cnt_##nm = (cnt_##nm + 1); \
        return cnt_##nm; \
}
make_counter(a)
make_counter(b)
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


int32_t main(void) {
    /* line 17 */
    printf("%d %d %d\n", bump_a(), bump_a(), bump_b());
    /* line 18 */
    return 0;
}
