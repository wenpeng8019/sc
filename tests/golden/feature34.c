/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "feature34_cmac.h"

DEFINE_COUNTER(hits)
DEFINE_COUNTER(miss)
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 28 */
    counter_hits_inc();
    /* line 29 */
    counter_hits_inc();
    /* line 30 */
    counter_hits_inc();
    /* line 31 */
    counter_miss_inc();
    /* line 32 */
    printf("hits=%d miss=%d\n", counter_hits_get(), counter_miss_get());
    /* line 33 */
    printf("counter_hits(direct)=%d\n", counter_hits);
    /* line 34 */
    return 0;
}
