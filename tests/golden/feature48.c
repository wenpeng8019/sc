/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static token *raw = {0};
static token *clean = {0};
static token *report = {0};
static uint8_t __scdep_0_follow(__scdep_in *_this);
static uint8_t __scdep_1_follow(__scdep_in *_this);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static int __scdep_0_tramp(token **, int, int, void *);
static int __scdep_1_tramp(token **, int, int, void *);

static uint8_t __scdep_0_follow(__scdep_in *_this) {
    /* line 21 */
    token *r = _this->toks[0];
    /* line 21 */
    token *c = _this->toks[1];
    /* line 26 */
    int64_t v = ((int64_t)((token_get(r)).p));
    /* line 27 */
    if (v < 0) {
        /* line 28 */
        v = 0;
    }
    /* line 29 */
    token_set(c, ((sc_thin){(void *)(intptr_t)(v), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 30 */
    return false;
}

static uint8_t __scdep_1_follow(__scdep_in *_this) {
    /* line 33 */
    token *c = _this->toks[0];
    /* line 33 */
    token *o = _this->toks[1];
    /* line 34 */
    int64_t v = ((int64_t)((token_get(c)).p));
    /* line 35 */
    token_set(o, ((sc_thin){(void *)(intptr_t)(v * 2), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 36 */
    return false;
}

int32_t main(void) {
    raw = token_bind("wf.raw", NULL);
    token_set_crit(raw, 1, 0);
    token_set_degree(raw, 0, 1);
    token_set_reach(raw, 2);
    token_set_batch(raw, 1);
    token_set_dom(raw, 1, 2);
    clean = token_bind("wf.clean", NULL);
    token_set_depth(clean, 1);
    token_set_crit(clean, 1, 0);
    token_set_degree(clean, 1, 1);
    token_set_reach(clean, 1);
    token_set_batch(clean, 1);
    token_set_dom(clean, 1, 1);
    report = token_bind("wf.report", NULL);
    token_set_depth(report, 2);
    token_set_crit(report, 1, 0);
    token_set_degree(report, 1, 0);
    token_set_reach(report, 0);
    token_set_batch(report, 1);
    token_set_dom(report, 0, 0);
    { token *_deps0[] = { raw, clean }; token_depend_map(_deps0, 1, 1, 0, __scdep_0_tramp, NULL); }
    { token *_deps1[] = { clean, report }; token_depend_map(_deps1, 1, 1, 0, __scdep_1_tramp, NULL); }
    /* line 40 */
    token_form(report, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (token_exec)0);
    /* line 41 */
    token_form(clean, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (token_exec)0);
    /* line 42 */
    token_form(raw, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (token_exec)0);
    /* line 45 */
    printf("depth: raw=%d clean=%d report=%d\n", token_depth(raw), token_depth(clean), token_depth(report));
    /* line 47 */
    token_set(raw, ((sc_thin){(void *)(intptr_t)(0 - 5), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 48 */
    printf("raw=-5: clean=%lld report=%lld\n", ((int64_t)((token_get(clean)).p)), ((int64_t)((token_get(report)).p)));
    /* line 50 */
    token_set(raw, ((sc_thin){(void *)(intptr_t)(21), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 51 */
    printf("raw=21: clean=%lld report=%lld\n", ((int64_t)((token_get(clean)).p)), ((int64_t)((token_get(report)).p)));
    /* line 53 */
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
