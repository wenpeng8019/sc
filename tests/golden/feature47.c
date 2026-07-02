/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static sc_thin sc_tok_sensor_level_combine(sc_tok_in *_this);
static sc_token *sc_level = {0};
static sc_token *sc_alert = {0};
static bool sc_dep_0_follow(sc_dep_in *_this);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


static int sc_dep_0_tramp(sc_token **, int, int, void *);

static sc_thin sc_tok_sensor_level_combine(sc_tok_in *_this) {
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

static bool sc_dep_0_follow(sc_dep_in *_this) {
    /* line 31 */
    sc_token *l = _this->toks[0];
    /* line 32 */
    int64_t v = ((int64_t)((sc_token_get(l)).p));
    /* line 33 */
    if (v > 100) {
        /* line 34 */
        sc_token_set(sc_alert, ((sc_thin){(void *)(intptr_t)(1), (int32_t *)0, (void (*)(void *))0}), 0);
    }
    /* line 35 */
    return false;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_level = sc_token_bind("sensor.level", (sc_token_combine)sc_tok_sensor_level_combine);
    sc_alert = sc_token_bind("sensor.alert", NULL);
    { sc_token *_deps0[] = { sc_level }; sc_token_depend(_deps0, 1, 0, sc_dep_0_tramp, NULL); }
    /* line 38 */
    sc_token_form(sc_level, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (sc_token_exec)0);
    /* line 39 */
    sc_token_form(sc_alert, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (sc_token_exec)0);
    /* line 41 */
    sc_token_set(sc_level, ((sc_thin){(void *)(intptr_t)(50), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 42 */
    int64_t lv = ((int64_t)((sc_token_get(sc_level)).p));
    /* line 43 */
    int64_t al = ((int64_t)((sc_token_get(sc_alert)).p));
    /* line 44 */
    printf("after 50:  level=%lld alert=%lld\n", lv, al);
    /* line 46 */
    sc_token_set(sc_level, ((sc_thin){(void *)(intptr_t)(150), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 47 */
    lv = ((int64_t)((sc_token_get(sc_level)).p));
    /* line 48 */
    al = ((int64_t)((sc_token_get(sc_alert)).p));
    /* line 49 */
    printf("after 150: level=%lld alert=%lld\n", lv, al);
    /* line 51 */
    sc_token_set(sc_level, ((sc_thin){(void *)(intptr_t)(30), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 52 */
    lv = ((int64_t)((sc_token_get(sc_level)).p));
    /* line 53 */
    printf("after 30:  level=%lld\n", lv);
    /* line 55 */
    return 0;
}

static int sc_dep_0_tramp(sc_token **_ts, int _n, int _acting, void *_ctx) {
    sc_dep_in _self; _self.toks = _ts; _self.count = _n; _self.active = _acting; _self.ctx = _ctx;
    return (int)sc_dep_0_follow(&_self);
}
