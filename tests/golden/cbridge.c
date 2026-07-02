/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

extern int32_t MAX_VIEW;
extern int64_t g_tick;
static int32_t sc_demo(void);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


static int32_t sc_demo(void) {
    /* line 11 */
    printf("v=%d\n", 1);
    /* line 13 */
    int32_t a = abs(-(3));
    /* line 15 */
    int32_t b = sc_MAX_VIEW;
    /* line 17 */
    sc_g_tick = (sc_g_tick + 1);
    /* line 18 */
    return a + b;
}
