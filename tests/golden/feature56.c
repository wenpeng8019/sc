/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static token *sig = {0};
static sc_thin __sctok_cd_gauge_combine(__sctok_in *_this);
static token *gauge = {0};
static token *hits = {0};
static token *ghits = {0};
static uint8_t __scdep_0_follow(__scdep_in *_this);
static uint8_t __scdep_1_follow(__scdep_in *_this);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static int __scdep_0_tramp(token **, int, int, void *);
static int __scdep_1_tramp(token **, int, int, void *);

static sc_thin __sctok_cd_gauge_combine(__sctok_in *_this) {
    /* line 19 */
    int64_t i = ((int64_t)((_this->input).p));
    /* line 20 */
    if (i == 0) {
        /* line 21 */
        return tok_modified();
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

static uint8_t __scdep_0_follow(__scdep_in *_this) {
    /* line 32 */
    token *s = _this->toks[0];
    /* line 33 */
    if (_this->active >= 0) {
        /* line 34 */
        int64_t h = ((int64_t)((token_get(hits)).p));
        /* line 35 */
        token_set(hits, ((sc_thin){(void *)(intptr_t)(h + 1), (int32_t *)0, (void (*)(void *))0}), 0);
    }
    /* line 36 */
    return false;
}

static uint8_t __scdep_1_follow(__scdep_in *_this) {
    /* line 39 */
    token *g = _this->toks[0];
    /* line 40 */
    if (_this->active >= 0) {
        /* line 41 */
        int64_t c = ((int64_t)((token_get(ghits)).p));
        /* line 42 */
        token_set(ghits, ((sc_thin){(void *)(intptr_t)(c + 1), (int32_t *)0, (void (*)(void *))0}), 0);
    }
    /* line 43 */
    return false;
}

int32_t main(void) {
    sig = token_bind("cd.sig", NULL);
    gauge = token_bind("cd.gauge", (token_combine)__sctok_cd_gauge_combine);
    hits = token_bind("cd.hits", NULL);
    ghits = token_bind("cd.ghits", NULL);
    { token *_deps0[] = { sig }; token_depend(_deps0, 1, 0, __scdep_0_tramp, NULL); }
    { token *_deps1[] = { gauge }; token_depend(_deps1, 1, 0, __scdep_1_tramp, NULL); }
    /* line 46 */
    token_form(sig, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (token_exec)0);
    /* line 47 */
    token_form(gauge, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (token_exec)0);
    /* line 48 */
    token_form(hits, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (token_exec)0);
    /* line 49 */
    token_form(ghits, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (token_exec)0);
    /* line 52 */
    token_set(sig, ((sc_thin){(void *)(intptr_t)(5), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 53 */
    token_set(sig, ((sc_thin){(void *)(intptr_t)(5), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 54 */
    token_set(sig, ((sc_thin){(void *)(intptr_t)(5), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 55 */
    int64_t h1 = ((int64_t)((token_get(hits)).p));
    /* line 56 */
    printf("直赋 set(5) x3:   hits=%lld (期望 1)\n", h1);
    /* line 58 */
    token_set(sig, ((sc_thin){(void *)(intptr_t)(7), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 59 */
    int64_t h2 = ((int64_t)((token_get(hits)).p));
    /* line 60 */
    printf("直赋 set(7):      hits=%lld (期望 2)\n", h2);
    /* line 63 */
    token_set(gauge, ((sc_thin){(void *)(intptr_t)(10), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 64 */
    token_set(gauge, ((sc_thin){(void *)(intptr_t)(3), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 65 */
    int64_t g1 = ((int64_t)((token_get(ghits)).p));
    /* line 66 */
    printf("combine 取峰抑制:  ghits=%lld (期望 1)\n", g1);
    /* line 69 */
    token_set(gauge, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 70 */
    int64_t g2 = ((int64_t)((token_get(ghits)).p));
    /* line 71 */
    printf("modified 强制刷新: ghits=%lld (期望 2)\n", g2);
    /* line 73 */
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
