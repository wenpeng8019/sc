/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static sc_token *sc_raw = {0};
static sc_token *sc_clean = {0};
static sc_token *sc_report = {0};
static bool sc_dep_0_follow(sc_dep_in *_this);
static bool sc_dep_1_follow(sc_dep_in *_this);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


static int sc_dep_0_tramp(sc_token **, int, int, void *);
static int sc_dep_1_tramp(sc_token **, int, int, void *);

static bool sc_dep_0_follow(sc_dep_in *_this) {
    /* line 21 */
    sc_token *r = _this->toks[0];
    /* line 21 */
    sc_token *c = _this->toks[1];
    /* line 26 */
    int64_t v = ((int64_t)((sc_token_get(r)).p));
    /* line 27 */
    if (v < 0) {
        /* line 28 */
        v = 0;
    }
    /* line 29 */
    sc_token_set(c, ((sc_thin){(void *)(intptr_t)(v), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 30 */
    return false;
}

static bool sc_dep_1_follow(sc_dep_in *_this) {
    /* line 33 */
    sc_token *c = _this->toks[0];
    /* line 33 */
    sc_token *o = _this->toks[1];
    /* line 34 */
    int64_t v = ((int64_t)((sc_token_get(c)).p));
    /* line 35 */
    sc_token_set(o, ((sc_thin){(void *)(intptr_t)(v * 2), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 36 */
    return false;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_raw = sc_token_bind("wf.raw", NULL);
    sc_token_set_crit(sc_raw, 1, 0);
    sc_token_set_degree(sc_raw, 0, 1);
    sc_token_set_reach(sc_raw, 2);
    sc_token_set_batch(sc_raw, 1);
    sc_token_set_dom(sc_raw, 1, 2);
    sc_clean = sc_token_bind("wf.clean", NULL);
    sc_token_set_depth(sc_clean, 1);
    sc_token_set_crit(sc_clean, 1, 0);
    sc_token_set_degree(sc_clean, 1, 1);
    sc_token_set_reach(sc_clean, 1);
    sc_token_set_batch(sc_clean, 1);
    sc_token_set_dom(sc_clean, 1, 1);
    sc_report = sc_token_bind("wf.report", NULL);
    sc_token_set_depth(sc_report, 2);
    sc_token_set_crit(sc_report, 1, 0);
    sc_token_set_degree(sc_report, 1, 0);
    sc_token_set_reach(sc_report, 0);
    sc_token_set_batch(sc_report, 1);
    sc_token_set_dom(sc_report, 0, 0);
    { sc_token *_deps0[] = { sc_raw, sc_clean }; sc_token_depend_map(_deps0, 1, 1, 0, sc_dep_0_tramp, NULL); }
    { sc_token *_deps1[] = { sc_clean, sc_report }; sc_token_depend_map(_deps1, 1, 1, 0, sc_dep_1_tramp, NULL); }
    /* line 40 */
    sc_token_form(sc_report, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (sc_token_exec)0);
    /* line 41 */
    sc_token_form(sc_clean, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (sc_token_exec)0);
    /* line 42 */
    sc_token_form(sc_raw, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (sc_token_exec)0);
    /* line 45 */
    int32_t _sq0 = sc_token_depth(sc_raw);
    int32_t _sq1 = sc_token_depth(sc_clean);
    int32_t _sq2 = sc_token_depth(sc_report);
    printf("depth: raw=%d clean=%d report=%d\n", _sq0, _sq1, _sq2);
    /* line 47 */
    sc_token_set(sc_raw, ((sc_thin){(void *)(intptr_t)(0 - 5), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 48 */
    int64_t _sq3 = ((int64_t)((sc_token_get(sc_clean)).p));
    int64_t _sq4 = ((int64_t)((sc_token_get(sc_report)).p));
    printf("raw=-5: clean=%lld report=%lld\n", _sq3, _sq4);
    /* line 50 */
    sc_token_set(sc_raw, ((sc_thin){(void *)(intptr_t)(21), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 51 */
    int64_t _sq5 = ((int64_t)((sc_token_get(sc_clean)).p));
    int64_t _sq6 = ((int64_t)((sc_token_get(sc_report)).p));
    printf("raw=21: clean=%lld report=%lld\n", _sq5, _sq6);
    /* line 53 */
    return 0;
}

static int sc_dep_0_tramp(sc_token **_ts, int _n, int _acting, void *_ctx) {
    sc_dep_in _self; _self.toks = _ts; _self.count = _n; _self.active = _acting; _self.ctx = _ctx;
    return (int)sc_dep_0_follow(&_self);
}
static int sc_dep_1_tramp(sc_token **_ts, int _n, int _acting, void *_ctx) {
    sc_dep_in _self; _self.toks = _ts; _self.count = _n; _self.active = _acting; _self.ctx = _ctx;
    return (int)sc_dep_1_follow(&_self);
}
