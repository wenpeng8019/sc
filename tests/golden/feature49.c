/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static token *x = {0};
static token *y = {0};
static token *loss = {0};
static token *gx = {0};
static token *gy = {0};
static bool __scdep_0_follow(__scdep_in *_this);
static bool __scdep_1_follow(__scdep_in *_this);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static int __scdep_0_tramp(token **, int, int, void *);
static int __scdep_1_tramp(token **, int, int, void *);

static bool __scdep_0_follow(__scdep_in *_this) {
    /* line 23 */
    token *a = _this->toks[0];
    /* line 23 */
    token *b = _this->toks[1];
    /* line 24 */
    if (_this->active == (0 - 4)) {
        /* line 25 */
        int64_t g = ((int64_t)((token_get(gy)).p));
        /* line 26 */
        token_set(gx, ((sc_thin){(void *)(intptr_t)(g * 2), (int32_t *)0, (void (*)(void *))0}), 0);
        /* line 27 */
        return false;
    }
    /* line 28 */
    int64_t v = ((int64_t)((token_get(a)).p));
    /* line 29 */
    token_set(b, ((sc_thin){(void *)(intptr_t)(v * 2), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 30 */
    return false;
}

static bool __scdep_1_follow(__scdep_in *_this) {
    /* line 33 */
    token *c = _this->toks[0];
    /* line 33 */
    token *o = _this->toks[1];
    /* line 34 */
    if (_this->active == (0 - 4)) {
        /* line 35 */
        int64_t gloss = ((int64_t)((token_get(o)).p));
        /* line 36 */
        token_set(gy, ((sc_thin){(void *)(intptr_t)(gloss), (int32_t *)0, (void (*)(void *))0}), 0);
        /* line 37 */
        return false;
    }
    /* line 38 */
    int64_t v = ((int64_t)((token_get(c)).p));
    /* line 39 */
    token_set(o, ((sc_thin){(void *)(intptr_t)(v + 5), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 40 */
    return false;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    x = token_bind("nn.x", NULL);
    token_set_crit(x, 1, 0);
    token_set_degree(x, 0, 1);
    token_set_reach(x, 2);
    token_set_batch(x, 1);
    token_set_dom(x, 1, 2);
    y = token_bind("nn.y", NULL);
    token_set_depth(y, 1);
    token_set_crit(y, 1, 0);
    token_set_degree(y, 1, 1);
    token_set_reach(y, 1);
    token_set_batch(y, 1);
    token_set_dom(y, 1, 1);
    loss = token_bind("nn.loss", NULL);
    token_set_depth(loss, 2);
    token_set_crit(loss, 1, 0);
    token_set_degree(loss, 1, 0);
    token_set_reach(loss, 0);
    token_set_batch(loss, 1);
    token_set_dom(loss, 0, 0);
    gx = token_bind("nn.gx", NULL);
    gy = token_bind("nn.gy", NULL);
    { token *_deps0[] = { x, y }; token_depend_map(_deps0, 1, 1, 0, __scdep_0_tramp, NULL); }
    { token *_deps1[] = { y, loss }; token_depend_map(_deps1, 1, 1, 0, __scdep_1_tramp, NULL); }
    /* line 44 */
    token_form(gx, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (token_exec)0);
    /* line 45 */
    token_form(gy, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (token_exec)0);
    /* line 46 */
    token_form(loss, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (token_exec)0);
    /* line 47 */
    token_form(y, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (token_exec)0);
    /* line 48 */
    token_form(x, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (token_exec)0);
    /* line 51 */
    token_set(x, ((sc_thin){(void *)(intptr_t)(4), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 52 */
    int64_t _sq0 = ((int64_t)((token_get(x)).p));
    int64_t _sq1 = ((int64_t)((token_get(y)).p));
    int64_t _sq2 = ((int64_t)((token_get(loss)).p));
    printf("forward: x=%lld y=%lld loss=%lld\n", _sq0, _sq1, _sq2);
    /* line 55 */
    token_back(loss, ((sc_thin){(void *)(intptr_t)(1), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 56 */
    int64_t _sq3 = ((int64_t)((token_get(gy)).p));
    int64_t _sq4 = ((int64_t)((token_get(gx)).p));
    printf("backward(seed=1): gy=%lld gx=%lld\n", _sq3, _sq4);
    /* line 58 */
    return 0;
}

static int __scdep_0_tramp(token **_ts, int _n, int _acting, void *_ctx) {
    __scdep_in _self; _self.toks = _ts; _self.count = _n; _self.active = _acting; _self.ctx = _ctx;
    return (int)__scdep_0_follow(&_self);
}
static int __scdep_1_tramp(token **_ts, int _n, int _acting, void *_ctx) {
    __scdep_in _self; _self.toks = _ts; _self.count = _n; _self.active = _acting; _self.ctx = _ctx;
    return (int)__scdep_1_follow(&_self);
}
