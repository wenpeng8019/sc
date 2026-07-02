/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

int32_t sc_puts_wrap(uint8_t *s);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


int32_t sc_puts_wrap(uint8_t *s) {
    /* line 6 */
    return puts(s);
}
