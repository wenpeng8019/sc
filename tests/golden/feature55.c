/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static token *tin = {0};
static token *l = {0};
static token *r = {0};
static token *mid = {0};
static token *tout = {0};
static uint8_t __scdep_0_follow(__scdep_in *_this);
static uint8_t __scdep_1_follow(__scdep_in *_this);
static uint8_t __scdep_2_follow(__scdep_in *_this);
static uint8_t __scdep_3_follow(__scdep_in *_this);
static uint8_t __scdep_4_follow(__scdep_in *_this);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static int __scdep_0_tramp(token **, int, int, void *);
static int __scdep_1_tramp(token **, int, int, void *);
static int __scdep_2_tramp(token **, int, int, void *);
static int __scdep_3_tramp(token **, int, int, void *);
static int __scdep_4_tramp(token **, int, int, void *);

static uint8_t __scdep_0_follow(__scdep_in *_this) {
    /* line 23 */
    token *s = _this->toks[0];
    /* line 23 */
    token *t = _this->toks[1];
    /* line 24 */
    return false;
}

static uint8_t __scdep_1_follow(__scdep_in *_this) {
    /* line 25 */
    token *s = _this->toks[0];
    /* line 25 */
    token *t = _this->toks[1];
    /* line 26 */
    return false;
}

static uint8_t __scdep_2_follow(__scdep_in *_this) {
    /* line 27 */
    token *s = _this->toks[0];
    /* line 27 */
    token *t = _this->toks[1];
    /* line 28 */
    return false;
}

static uint8_t __scdep_3_follow(__scdep_in *_this) {
    /* line 29 */
    token *s = _this->toks[0];
    /* line 29 */
    token *t = _this->toks[1];
    /* line 30 */
    return false;
}

static uint8_t __scdep_4_follow(__scdep_in *_this) {
    /* line 31 */
    token *s = _this->toks[0];
    /* line 31 */
    token *t = _this->toks[1];
    /* line 32 */
    return false;
}

int32_t main(void) {
    tin = token_bind("dom.in", NULL);
    token_set_crit(tin, 1, 0);
    token_set_degree(tin, 0, 2);
    token_set_reach(tin, 4);
    token_set_batch(tin, 1);
    token_set_dom(tin, 1, 4);
    l = token_bind("dom.l", NULL);
    token_set_depth(l, 1);
    token_set_crit(l, 1, 0);
    token_set_degree(l, 1, 1);
    token_set_reach(l, 2);
    token_set_batch(l, 2);
    token_set_dom(l, 0, 0);
    r = token_bind("dom.r", NULL);
    token_set_depth(r, 1);
    token_set_crit(r, 1, 0);
    token_set_degree(r, 1, 1);
    token_set_reach(r, 2);
    token_set_batch(r, 2);
    token_set_dom(r, 0, 0);
    mid = token_bind("dom.mid", NULL);
    token_set_depth(mid, 2);
    token_set_crit(mid, 1, 0);
    token_set_degree(mid, 2, 1);
    token_set_reach(mid, 1);
    token_set_batch(mid, 1);
    token_set_dom(mid, 1, 1);
    tout = token_bind("dom.out", NULL);
    token_set_depth(tout, 3);
    token_set_crit(tout, 1, 0);
    token_set_degree(tout, 1, 0);
    token_set_reach(tout, 0);
    token_set_batch(tout, 1);
    token_set_dom(tout, 0, 0);
    { token *_deps0[] = { tin, l }; token_depend_map(_deps0, 1, 1, 1, __scdep_0_tramp, NULL); }
    { token *_deps1[] = { tin, r }; token_depend_map(_deps1, 1, 1, 1, __scdep_1_tramp, NULL); }
    { token *_deps2[] = { l, mid }; token_depend_map(_deps2, 1, 1, 1, __scdep_2_tramp, NULL); }
    { token *_deps3[] = { r, mid }; token_depend_map(_deps3, 1, 1, 1, __scdep_3_tramp, NULL); }
    { token *_deps4[] = { mid, tout }; token_depend_map(_deps4, 1, 1, 1, __scdep_4_tramp, NULL); }
    /* line 35 */
    printf("node     checkpoint dom_size\n");
    /* line 36 */
    printf("in       %10d %8d\n", token_checkpoint(tin), token_dom_size(tin));
    /* line 37 */
    printf("l        %10d %8d\n", token_checkpoint(l), token_dom_size(l));
    /* line 38 */
    printf("r        %10d %8d\n", token_checkpoint(r), token_dom_size(r));
    /* line 39 */
    printf("mid      %10d %8d\n", token_checkpoint(mid), token_dom_size(mid));
    /* line 40 */
    printf("out      %10d %8d\n", token_checkpoint(tout), token_dom_size(tout));
    /* line 41 */
    return 0;
}

static int __scdep_0_tramp(token **_ts, int _n, int _acting, void *_ctx) {
    (void)_ctx;
    __scdep_in _self; _self.toks = _ts; _self.count = _n; _self.active = _acting;
    return (int)__scdep_0_follow(&_self);
}
static int __scdep_1_tramp(token **_ts, int _n, int _acting, void *_ctx) {
    (void)_ctx;
    __scdep_in _self; _self.toks = _ts; _self.count = _n; _self.active = _acting;
    return (int)__scdep_1_follow(&_self);
}
static int __scdep_2_tramp(token **_ts, int _n, int _acting, void *_ctx) {
    (void)_ctx;
    __scdep_in _self; _self.toks = _ts; _self.count = _n; _self.active = _acting;
    return (int)__scdep_2_follow(&_self);
}
static int __scdep_3_tramp(token **_ts, int _n, int _acting, void *_ctx) {
    (void)_ctx;
    __scdep_in _self; _self.toks = _ts; _self.count = _n; _self.active = _acting;
    return (int)__scdep_3_follow(&_self);
}
static int __scdep_4_tramp(token **_ts, int _n, int _acting, void *_ctx) {
    (void)_ctx;
    __scdep_in _self; _self.toks = _ts; _self.count = _n; _self.active = _acting;
    return (int)__scdep_4_follow(&_self);
}
