/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static sc_thin __sctok_sensor_level_combine(__sctok_in *_this);
static token *level = {0};
static token *alert = {0};
static uint8_t __scdep_0_follow(__scdep_in *_this);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static int __scdep_0_tramp(token **, int, int, void *);

static sc_thin __sctok_sensor_level_combine(__sctok_in *_this) {
    /* line 20 */
    int64_t b = ((int64_t)((_this->base).p));
    /* line 21 */
    int64_t i = ((int64_t)((_this->input).p));
    /* line 22 */
    int64_t m = b;
    /* line 23 */
    if (i > b) {
        /* line 24 */
        m = i;
    }
    /* line 25 */
    return ((sc_thin){(void *)(intptr_t)(m), (int32_t *)0, (void (*)(void *))0});
}

static uint8_t __scdep_0_follow(__scdep_in *_this) {
    /* line 31 */
    token *l = _this->toks[0];
    /* line 32 */
    int64_t v = ((int64_t)((token_get(l)).p));
    /* line 33 */
    if (v > 100) {
        /* line 34 */
        token_set(alert, ((sc_thin){(void *)(intptr_t)(1), (int32_t *)0, (void (*)(void *))0}), 0);
    }
    /* line 35 */
    return false;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    level = token_bind("sensor.level", (token_combine)__sctok_sensor_level_combine);
    alert = token_bind("sensor.alert", NULL);
    { token *_deps0[] = { level }; token_depend(_deps0, 1, 0, __scdep_0_tramp, NULL); }
    /* line 38 */
    token_form(level, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (token_exec)0);
    /* line 39 */
    token_form(alert, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (token_exec)0);
    /* line 41 */
    token_set(level, ((sc_thin){(void *)(intptr_t)(50), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 42 */
    int64_t lv = ((int64_t)((token_get(level)).p));
    /* line 43 */
    int64_t al = ((int64_t)((token_get(alert)).p));
    /* line 44 */
    printf("after 50:  level=%lld alert=%lld\n", lv, al);
    /* line 46 */
    token_set(level, ((sc_thin){(void *)(intptr_t)(150), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 47 */
    lv = ((int64_t)((token_get(level)).p));
    /* line 48 */
    al = ((int64_t)((token_get(alert)).p));
    /* line 49 */
    printf("after 150: level=%lld alert=%lld\n", lv, al);
    /* line 51 */
    token_set(level, ((sc_thin){(void *)(intptr_t)(30), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 52 */
    lv = ((int64_t)((token_get(level)).p));
    /* line 53 */
    printf("after 30:  level=%lld\n", lv);
    /* line 55 */
    return 0;
}

static int __scdep_0_tramp(token **_ts, int _n, int _acting, void *_ctx) {
    __scdep_in _self; _self.toks = _ts; _self.count = _n; _self.active = _acting; _self.ctx = _ctx;
    return (int)__scdep_0_follow(&_self);
}
