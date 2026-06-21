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
    /* line 27 */
    int8_t a = 5;
    /* line 28 */
    int16_t b = 300;
    /* line 29 */
    uint8_t c = 7u;
    /* line 30 */
    uint16_t d = 100u;
    /* line 31 */
    uint32_t e = 5u;
    /* line 32 */
    int64_t f = 9000000000l;
    /* line 33 */
    printf("suffix: %d %d %u %u %u %lld\n", a, b, c, d, e, f);
}

static int32_t classify(int32_t n) {
    /* line 38 */
    if (n < 0) {
        /* line 39 */
        return -(1);
    }
    /* line 40 */
    if (n == 0) {
        /* line 41 */
        return 0;
    }
    /* line 42 */
    return 1;
}

static void demo_sugar(void) {
    /* line 46 */
    int32_t _sc_ret;
    if (((_sc_ret = classify(-(2)))) != 0) {
        /* line 47 */
        printf("fail branch, $=%d\n", _sc_ret);
    }
    /* line 49 */
    if (((_sc_ret = classify(0))) != 0) {
        /* line 50 */
        printf("never here\n");
    }
    /* line 52 */
    if (((_sc_ret = classify(7)) > 0)) {
        /* line 53 */
        printf("warn branch, $=%d\n", _sc_ret);
    }
    /* line 55 */
    if (((_sc_ret = classify(-(2))) < 0)) {
        /* line 56 */
        printf("err branch, $=%d\n", _sc_ret);
    }
    /* line 58 */
    _sc_ret = classify(0);
    if (_sc_ret != 0) assert(false);
    /* line 59 */
    printf("after assert, $=%d\n", _sc_ret);
}

static int32_t check_pos(int32_t n) {
    /* line 63 */
    if (n < 0) {
        /* line 64 */
        return -(1);
    }
    /* line 65 */
    return 0;
}

static int32_t do_step(int32_t n) {
    /* line 68 */
    int32_t _sc_ret;
    if (((_sc_ret = check_pos(n))) != 0) {
        /* line 69 */
        printf("do_step: check_pos(%d) failed, $=%d, propagate up\n", n, _sc_ret);
        return _sc_ret;
    }
    /* line 70 */
    printf("do_step: ok n=%d\n", n);
    /* line 71 */
    return 0;
}

static int32_t run_pipeline(void) {
    /* line 74 */
    int32_t _sc_ret;
    if (((_sc_ret = do_step(5))) != 0) {
        return _sc_ret;
    }
    /* line 75 */
    if (((_sc_ret = do_step(-(1)))) != 0) {
        return _sc_ret;
    }
    /* line 76 */
    printf("run_pipeline: never reached\n");
    /* line 77 */
    return 0;
}

int32_t main(void) {
    /* line 80 */
    show_suffix();
    /* line 81 */
    demo_sugar();
    /* line 82 */
    int32_t r = run_pipeline();
    /* line 83 */
    printf("pipeline result = %d\n", r);
    /* line 84 */
    return 0;
}
