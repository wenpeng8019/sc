/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static void show_suffix(void);
static int32_t classify(int32_t n);
static void demo_sugar(void);
static int32_t check_pos(int32_t n);
static int32_t do_step(int32_t n);
static int32_t run_pipeline(void);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static void show_suffix(void) {
    /* line 25 */
    int8_t a = 5;
    /* line 26 */
    int16_t b = 300;
    /* line 27 */
    uint8_t c = 7u;
    /* line 28 */
    uint16_t d = 100u;
    /* line 29 */
    uint32_t e = 5u;
    /* line 30 */
    int64_t f = 9000000000l;
    /* line 31 */
    printf("suffix: %d %d %u %u %u %lld\n", a, b, c, d, e, f);
}

static int32_t classify(int32_t n) {
    /* line 36 */
    if (n < 0) {
        /* line 37 */
        return -(1);
    }
    /* line 38 */
    if (n == 0) {
        /* line 39 */
        return 0;
    }
    /* line 40 */
    return 1;
}

static void demo_sugar(void) {
    /* line 44 */
    int32_t _sc_ret;
    if (((_sc_ret = classify(-(2)))) != 0) {
        /* line 45 */
        printf("fail branch, $=%d\n", _sc_ret);
    }
    /* line 47 */
    if (((_sc_ret = classify(0))) != 0) {
        /* line 48 */
        printf("never here\n");
    }
    /* line 50 */
    if (((_sc_ret = classify(7)) > 0)) {
        /* line 51 */
        printf("warn branch, $=%d\n", _sc_ret);
    }
    /* line 53 */
    if (((_sc_ret = classify(-(2))) < 0)) {
        /* line 54 */
        printf("err branch, $=%d\n", _sc_ret);
    }
    /* line 56 */
    _sc_ret = classify(0);
    if (_sc_ret != 0) assert(false);
    /* line 57 */
    printf("after assert, $=%d\n", _sc_ret);
}

static int32_t check_pos(int32_t n) {
    /* line 61 */
    if (n < 0) {
        /* line 62 */
        return -(1);
    }
    /* line 63 */
    return 0;
}

static int32_t do_step(int32_t n) {
    /* line 66 */
    int32_t _sc_ret;
    if (((_sc_ret = check_pos(n))) != 0) {
        /* line 67 */
        printf("do_step: check_pos(%d) failed, $=%d, propagate up\n", n, _sc_ret);
        return _sc_ret;
    }
    /* line 68 */
    printf("do_step: ok n=%d\n", n);
    /* line 69 */
    return 0;
}

static int32_t run_pipeline(void) {
    /* line 72 */
    int32_t _sc_ret;
    if (((_sc_ret = do_step(5))) != 0) {
        return _sc_ret;
    }
    /* line 73 */
    if (((_sc_ret = do_step(-(1)))) != 0) {
        return _sc_ret;
    }
    /* line 74 */
    printf("run_pipeline: never reached\n");
    /* line 75 */
    return 0;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 78 */
    show_suffix();
    /* line 79 */
    demo_sugar();
    /* line 80 */
    int32_t r = run_pipeline();
    /* line 81 */
    printf("pipeline result = %d\n", r);
    /* line 82 */
    return 0;
}
