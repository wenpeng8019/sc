/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static sc_afat __sctok_sensor_level_combine(__sctok_in *_this);
static token *level = {0};
static token *alert = {0};
static uint8_t __scdep_0_follow(__scdep_in *_this);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static int __scdep_0_tramp(token **, int, int, void *);

static sc_afat __sctok_sensor_level_combine(__sctok_in *_this) {
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
    return ((sc_afat){(void *)(intptr_t)(m), (int32_t *)0, SC_OWN_RAW, (void (*)(void *))0});
}

static uint8_t __scdep_0_follow(__scdep_in *_this) {
    /* line 31 */
    token *l = _this->toks[0];
    /* line 32 */
    int64_t v = ((int64_t)((token_get(l)).p));
    /* line 33 */
    if (v > 100) {
        /* line 34 */
        token_set(alert, ((sc_afat){(void *)(intptr_t)(1), (int32_t *)0, SC_OWN_RAW, (void (*)(void *))0}), 0);
    }
    /* line 35 */
    return false;
}

int32_t main(void) {
    level = token_bind("sensor.level", (token_combine)__sctok_sensor_level_combine);
    alert = token_bind("sensor.alert", NULL);
    { token *_deps0[] = { level }; token_depend(_deps0, 1, 0, __scdep_0_tramp, NULL); }
    /* line 38 */
    token_form(level, ((sc_afat){(void *)(intptr_t)(0), (int32_t *)0, SC_OWN_RAW, (void (*)(void *))0}), 0);
    /* line 40 */
    token_set(level, ((sc_afat){(void *)(intptr_t)(50), (int32_t *)0, SC_OWN_RAW, (void (*)(void *))0}), 0);
    /* line 41 */
    int64_t lv = ((int64_t)((token_get(level)).p));
    /* line 42 */
    int64_t al = ((int64_t)((token_get(alert)).p));
    /* line 43 */
    printf("after 50:  level=%lld alert=%lld\n", lv, al);
    /* line 45 */
    token_set(level, ((sc_afat){(void *)(intptr_t)(150), (int32_t *)0, SC_OWN_RAW, (void (*)(void *))0}), 0);
    /* line 46 */
    lv = ((int64_t)((token_get(level)).p));
    /* line 47 */
    al = ((int64_t)((token_get(alert)).p));
    /* line 48 */
    printf("after 150: level=%lld alert=%lld\n", lv, al);
    /* line 50 */
    token_set(level, ((sc_afat){(void *)(intptr_t)(30), (int32_t *)0, SC_OWN_RAW, (void (*)(void *))0}), 0);
    /* line 51 */
    lv = ((int64_t)((token_get(level)).p));
    /* line 52 */
    printf("after 30:  level=%lld\n", lv);
    /* line 54 */
    return 0;
}

static int __scdep_0_tramp(token **_ts, int _n, int _acting, void *_ctx) {
    (void)_ctx;
    __scdep_in _self; _self.toks = _ts; _self.count = _n; _self.active = _acting;
    return (int)__scdep_0_follow(&_self);
}
