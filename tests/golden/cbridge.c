/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

extern int32_t MAX_VIEW;
extern int64_t g_tick;
static int32_t demo(void);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static int32_t demo(void) {
    /* line 11 */
    printf("v=%d\n", 1);
    /* line 13 */
    int32_t a = abs(-(3));
    /* line 15 */
    int32_t b = MAX_VIEW;
    /* line 17 */
    g_tick = (g_tick + 1);
    /* line 18 */
    return a + b;
}
