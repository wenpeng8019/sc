/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static sc_token *sc_sig = {0};
static sc_thin sc_tok_cd_gauge_combine(sc_tok_in *_this);
static sc_token *sc_gauge = {0};
static sc_token *sc_hits = {0};
static sc_token *sc_ghits = {0};
static bool sc_dep_0_follow(sc_dep_in *_this);
static bool sc_dep_1_follow(sc_dep_in *_this);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


static int sc_dep_0_tramp(sc_token **, int, int, void *);
static int sc_dep_1_tramp(sc_token **, int, int, void *);

static sc_thin sc_tok_cd_gauge_combine(sc_tok_in *_this) {
    /* line 19 */
    int64_t i = ((int64_t)((_this->input).p));
    /* line 20 */
    if (i == 0) {
        /* line 21 */
        return sc_tok_modified();
    }
    /* line 22 */
    int64_t b = ((int64_t)((_this->base).p));
    /* line 23 */
    if (i > b) {
        /* line 24 */
        return ((sc_thin){(void *)(intptr_t)(i), (int32_t *)0, (void (*)(void *))0});
    }
    /* line 25 */
    return ((sc_thin){(void *)(intptr_t)(b), (int32_t *)0, (void (*)(void *))0});
}

static bool sc_dep_0_follow(sc_dep_in *_this) {
    /* line 32 */
    sc_token *s = _this->toks[0];
    /* line 33 */
    if (_this->active >= 0) {
        /* line 34 */
        int64_t h = ((int64_t)((sc_token_get(sc_hits)).p));
        /* line 35 */
        sc_token_set(sc_hits, ((sc_thin){(void *)(intptr_t)(h + 1), (int32_t *)0, (void (*)(void *))0}), 0);
    }
    /* line 36 */
    return false;
}

static bool sc_dep_1_follow(sc_dep_in *_this) {
    /* line 39 */
    sc_token *g = _this->toks[0];
    /* line 40 */
    if (_this->active >= 0) {
        /* line 41 */
        int64_t c = ((int64_t)((sc_token_get(sc_ghits)).p));
        /* line 42 */
        sc_token_set(sc_ghits, ((sc_thin){(void *)(intptr_t)(c + 1), (int32_t *)0, (void (*)(void *))0}), 0);
    }
    /* line 43 */
    return false;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_sig = sc_token_bind("cd.sig", NULL);
    sc_gauge = sc_token_bind("cd.gauge", (sc_token_combine)sc_tok_cd_gauge_combine);
    sc_hits = sc_token_bind("cd.hits", NULL);
    sc_ghits = sc_token_bind("cd.ghits", NULL);
    { sc_token *_deps0[] = { sc_sig }; sc_token_depend(_deps0, 1, 0, sc_dep_0_tramp, NULL); }
    { sc_token *_deps1[] = { sc_gauge }; sc_token_depend(_deps1, 1, 0, sc_dep_1_tramp, NULL); }
    /* line 46 */
    sc_token_form(sc_sig, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (sc_token_exec)0);
    /* line 47 */
    sc_token_form(sc_gauge, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (sc_token_exec)0);
    /* line 48 */
    sc_token_form(sc_hits, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (sc_token_exec)0);
    /* line 49 */
    sc_token_form(sc_ghits, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (sc_token_exec)0);
    /* line 52 */
    sc_token_set(sc_sig, ((sc_thin){(void *)(intptr_t)(5), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 53 */
    sc_token_set(sc_sig, ((sc_thin){(void *)(intptr_t)(5), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 54 */
    sc_token_set(sc_sig, ((sc_thin){(void *)(intptr_t)(5), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 55 */
    int64_t h1 = ((int64_t)((sc_token_get(sc_hits)).p));
    /* line 56 */
    printf("直赋 set(5) x3:   hits=%lld (期望 1)\n", h1);
    /* line 58 */
    sc_token_set(sc_sig, ((sc_thin){(void *)(intptr_t)(7), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 59 */
    int64_t h2 = ((int64_t)((sc_token_get(sc_hits)).p));
    /* line 60 */
    printf("直赋 set(7):      hits=%lld (期望 2)\n", h2);
    /* line 63 */
    sc_token_set(sc_gauge, ((sc_thin){(void *)(intptr_t)(10), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 64 */
    sc_token_set(sc_gauge, ((sc_thin){(void *)(intptr_t)(3), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 65 */
    int64_t g1 = ((int64_t)((sc_token_get(sc_ghits)).p));
    /* line 66 */
    printf("combine 取峰抑制:  ghits=%lld (期望 1)\n", g1);
    /* line 69 */
    sc_token_set(sc_gauge, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 70 */
    int64_t g2 = ((int64_t)((sc_token_get(sc_ghits)).p));
    /* line 71 */
    printf("modified 强制刷新: ghits=%lld (期望 2)\n", g2);
    /* line 73 */
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
