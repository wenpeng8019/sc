/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static token *a = {0};
static token *b = {0};
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
    /* line 19 */
    token *x = _this->toks[0];
    /* line 19 */
    token *y = _this->toks[1];
    /* line 20 */
    int64_t av = ((int64_t)((token_get(x)).p));
    /* line 21 */
    if (av != 0) {
        /* line 22 */
        token_set(y, ((sc_thin){(void *)(intptr_t)(100 / av), (int32_t *)0, (void (*)(void *))0}), 0);
    }
    /* line 23 */
    return false;
}

static uint8_t __scdep_1_follow(__scdep_in *_this) {
    /* line 26 */
    token *p = _this->toks[0];
    /* line 26 */
    token *q = _this->toks[1];
    /* line 27 */
    int64_t bv = ((int64_t)((token_get(p)).p));
    /* line 28 */
    int64_t av = ((int64_t)((token_get(q)).p));
    /* line 29 */
    token_set(q, ((sc_thin){(void *)(intptr_t)((av + bv) / 2), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 30 */
    return false;
}

int32_t main(void) {
    a = token_bind("fp.a", NULL);
    token_set_scc(a, 0, 2);
    b = token_bind("fp.b", NULL);
    token_set_scc(b, 0, 2);
    { token *_deps0[] = { a, b }; token_depend_loop(_deps0, 1, 1, 1, __scdep_0_tramp, NULL); }
    { token *_deps1[] = { b, a }; token_depend_loop(_deps1, 1, 1, 1, __scdep_1_tramp, NULL); }
    /* line 33 */
    token_form(b, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (token_exec)0);
    /* line 34 */
    token_form(a, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)0, (token_exec)0);
    /* line 36 */
    token_set(a, ((sc_thin){(void *)(intptr_t)(100), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 37 */
    int64_t _sq0 = ((int64_t)((token_get(a)).p));
    int32_t _sq1 = token_scc(a);
    int32_t _sq2 = token_scc_size(a);
    printf("init: a=%lld  scc=%d size=%d\n", _sq0, _sq1, _sq2);
    /* line 40 */
    int32_t rounds = token_loop_run(a, 10);
    /* line 41 */
    int64_t _sq3 = ((int64_t)((token_get(a)).p));
    int64_t _sq4 = ((int64_t)((token_get(b)).p));
    printf("after %d rounds: a=%lld b=%lld (sqrt100=10)\n", rounds, _sq3, _sq4);
    /* line 43 */
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
