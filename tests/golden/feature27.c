/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static void show_suffix(void);
static int32_t classify(int32_t n);
static void demo_sugar(void);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static void show_suffix(void) {
    /* line 24 */
    int8_t a = 5;
    /* line 25 */
    int16_t b = 300;
    /* line 26 */
    uint8_t c = 7u;
    /* line 27 */
    uint16_t d = 100u;
    /* line 28 */
    uint32_t e = 5u;
    /* line 29 */
    int64_t f = 9000000000l;
    /* line 30 */
    printf("suffix: %d %d %u %u %u %lld\n", a, b, c, d, e, f);
}

static int32_t classify(int32_t n) {
    /* line 35 */
    if (n < 0) {
        /* line 36 */
        return -(1);
    }
    /* line 37 */
    if (n == 0) {
        /* line 38 */
        return 0;
    }
    /* line 39 */
    return 1;
}

static void demo_sugar(void) {
    /* line 43 */
    int32_t _sc_ret;
    if (!((_sc_ret = classify(0)))) {
        /* line 44 */
        printf("ok branch, $=%d\n", _sc_ret);
    }
    /* line 46 */
    if (((_sc_ret = classify(7)) > 0)) {
        /* line 47 */
        printf("warn branch, $=%d\n", _sc_ret);
    }
    /* line 49 */
    if (((_sc_ret = classify(-(2))) < 0)) {
        /* line 50 */
        printf("err branch, $=%d\n", _sc_ret);
    }
    /* line 52 */
    _sc_ret = classify(0);
    if (_sc_ret != 0) assert(false);
    /* line 53 */
    printf("after assert, $=%d\n", _sc_ret);
}

int32_t main(void) {
    /* line 56 */
    show_suffix();
    /* line 57 */
    demo_sugar();
    /* line 58 */
    return 0;
}
