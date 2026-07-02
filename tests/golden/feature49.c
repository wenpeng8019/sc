/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static sc_token *sc_x = {0};
static sc_token *sc_y = {0};
static sc_token *sc_loss = {0};
static sc_token *sc_gx = {0};
static sc_token *sc_gy = {0};
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
    /* line 23 */
    sc_token *a = _this->toks[0];
    /* line 23 */
    sc_token *b = _this->toks[1];
    /* line 24 */
    if (_this->active == (0 - 4)) {
        /* line 25 */
        int64_t g = ((int64_t)((sc_token_get(sc_gy)).p));
        /* line 26 */
        sc_token_set(sc_gx, ((sc_thin){(void *)(intptr_t)(g * 2), (int32_t *)0, (void (*)(void *))0}), 0);
        /* line 27 */
        return false;
    }
    /* line 28 */
    int64_t v = ((int64_t)((sc_token_get(a)).p));
    /* line 29 */
    sc_token_set(b, ((sc_thin){(void *)(intptr_t)(v * 2), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 30 */
    return false;
}

static bool sc_dep_1_follow(sc_dep_in *_this) {
    /* line 33 */
    sc_token *c = _this->toks[0];
    /* line 33 */
    sc_token *o = _this->toks[1];
    /* line 34 */
    if (_this->active == (0 - 4)) {
        /* line 35 */
        int64_t gloss = ((int64_t)((sc_token_get(o)).p));
        /* line 36 */
        sc_token_set(sc_gy, ((sc_thin){(void *)(intptr_t)(gloss), (int32_t *)0, (void (*)(void *))0}), 0);
        /* line 37 */
        return false;
    }
    /* line 38 */
    int64_t v = ((int64_t)((sc_token_get(c)).p));
    /* line 39 */
    sc_token_set(o, ((sc_thin){(void *)(intptr_t)(v + 5), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 40 */
    return false;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_x = sc_token_bind("nn.x", NULL);
    sc_token_set_crit(sc_x, 1, 0);
    sc_token_set_degree(sc_x, 0, 1);
    sc_token_set_reach(sc_x, 2);
    sc_token_set_batch(sc_x, 1);
    sc_token_set_dom(sc_x, 1, 2);
    sc_y = sc_token_bind("nn.y", NULL);
    sc_token_set_depth(sc_y, 1);
    sc_token_set_crit(sc_y, 1, 0);
    sc_token_set_degree(sc_y, 1, 1);
    sc_token_set_reach(sc_y, 1);
    sc_token_set_batch(sc_y, 1);
    sc_token_set_dom(sc_y, 1, 1);
    sc_loss = sc_token_bind("nn.loss", NULL);
    sc_token_set_depth(sc_loss, 2);
    sc_token_set_crit(sc_loss, 1, 0);
    sc_token_set_degree(sc_loss, 1, 0);
    sc_token_set_reach(sc_loss, 0);
    sc_token_set_batch(sc_loss, 1);
    sc_token_set_dom(sc_loss, 0, 0);
    sc_gx = sc_token_bind("nn.gx", NULL);
    sc_gy = sc_token_bind("nn.gy", NULL);
    { sc_token *_deps0[] = { sc_x, sc_y }; sc_token_depend_map(_deps0, 1, 1, 0, sc_dep_0_tramp, NULL); }
    { sc_token *_deps1[] = { sc_y, sc_loss }; sc_token_depend_map(_deps1, 1, 1, 0, sc_dep_1_tramp, NULL); }
    /* line 44 */
    sc_token_form(sc_gx, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (sc_token_exec)0);
    /* line 45 */
    sc_token_form(sc_gy, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (sc_token_exec)0);
    /* line 46 */
    sc_token_form(sc_loss, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (sc_token_exec)0);
    /* line 47 */
    sc_token_form(sc_y, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (sc_token_exec)0);
    /* line 48 */
    sc_token_form(sc_x, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (sc_token_exec)0);
    /* line 51 */
    sc_token_set(sc_x, ((sc_thin){(void *)(intptr_t)(4), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 52 */
    int64_t _sq0 = ((int64_t)((sc_token_get(sc_x)).p));
    int64_t _sq1 = ((int64_t)((sc_token_get(sc_y)).p));
    int64_t _sq2 = ((int64_t)((sc_token_get(sc_loss)).p));
    printf("forward: x=%lld y=%lld loss=%lld\n", _sq0, _sq1, _sq2);
    /* line 55 */
    sc_token_back(sc_loss, ((sc_thin){(void *)(intptr_t)(1), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 56 */
    int64_t _sq3 = ((int64_t)((sc_token_get(sc_gy)).p));
    int64_t _sq4 = ((int64_t)((sc_token_get(sc_gx)).p));
    printf("backward(seed=1): gy=%lld gx=%lld\n", _sq3, _sq4);
    /* line 58 */
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
