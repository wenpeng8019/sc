/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static sc_token *sc_a = {0};
static sc_token *sc_b = {0};
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
    /* line 19 */
    sc_token *x = _this->toks[0];
    /* line 19 */
    sc_token *y = _this->toks[1];
    /* line 20 */
    int64_t av = ((int64_t)((sc_token_get(x)).p));
    /* line 21 */
    if (av != 0) {
        /* line 22 */
        sc_token_set(y, ((sc_thin){(void *)(intptr_t)(100 / av), (int32_t *)0, (void (*)(void *))0}), 0);
    }
    /* line 23 */
    return false;
}

static bool sc_dep_1_follow(sc_dep_in *_this) {
    /* line 26 */
    sc_token *p = _this->toks[0];
    /* line 26 */
    sc_token *q = _this->toks[1];
    /* line 27 */
    int64_t bv = ((int64_t)((sc_token_get(p)).p));
    /* line 28 */
    int64_t av = ((int64_t)((sc_token_get(q)).p));
    /* line 29 */
    sc_token_set(q, ((sc_thin){(void *)(intptr_t)((av + bv) / 2), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 30 */
    return false;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_a = sc_token_bind("fp.a", NULL);
    sc_token_set_scc(sc_a, 0, 2);
    sc_b = sc_token_bind("fp.b", NULL);
    sc_token_set_scc(sc_b, 0, 2);
    { sc_token *_deps0[] = { sc_a, sc_b }; sc_token_depend_loop(_deps0, 1, 1, 1, sc_dep_0_tramp, NULL); }
    { sc_token *_deps1[] = { sc_b, sc_a }; sc_token_depend_loop(_deps1, 1, 1, 1, sc_dep_1_tramp, NULL); }
    /* line 33 */
    sc_token_form(sc_b, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (sc_token_exec)0);
    /* line 34 */
    sc_token_form(sc_a, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (sc_token_exec)0);
    /* line 36 */
    sc_token_set(sc_a, ((sc_thin){(void *)(intptr_t)(100), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 37 */
    int64_t _sq0 = ((int64_t)((sc_token_get(sc_a)).p));
    int32_t _sq1 = sc_token_scc(sc_a);
    int32_t _sq2 = sc_token_scc_size(sc_a);
    printf("init: a=%lld  scc=%d size=%d\n", _sq0, _sq1, _sq2);
    /* line 40 */
    int32_t rounds = sc_token_loop_run(sc_a, 10);
    /* line 41 */
    int64_t _sq3 = ((int64_t)((sc_token_get(sc_a)).p));
    int64_t _sq4 = ((int64_t)((sc_token_get(sc_b)).p));
    printf("after %d rounds: a=%lld b=%lld (sqrt100=10)\n", rounds, _sq3, _sq4);
    /* line 43 */
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
