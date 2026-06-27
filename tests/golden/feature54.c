/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static token *s = {0};
static token *a = {0};
static token *b = {0};
static token *c = {0};
static token *t = {0};
static uint8_t __scdep_0_follow(__scdep_in *_this);
static uint8_t __scdep_1_follow(__scdep_in *_this);
static uint8_t __scdep_2_follow(__scdep_in *_this);
static uint8_t __scdep_3_follow(__scdep_in *_this);
static uint8_t __scdep_4_follow(__scdep_in *_this);
static uint8_t __scdep_5_follow(__scdep_in *_this);
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
static int __scdep_5_tramp(token **, int, int, void *);

static uint8_t __scdep_0_follow(__scdep_in *_this) {
    /* line 19 */
    token *s = _this->toks[0];
    /* line 19 */
    token *t = _this->toks[1];
    /* line 20 */
    return false;
}

static uint8_t __scdep_1_follow(__scdep_in *_this) {
    /* line 21 */
    token *s = _this->toks[0];
    /* line 21 */
    token *t = _this->toks[1];
    /* line 22 */
    return false;
}

static uint8_t __scdep_2_follow(__scdep_in *_this) {
    /* line 23 */
    token *s = _this->toks[0];
    /* line 23 */
    token *t = _this->toks[1];
    /* line 24 */
    return false;
}

static uint8_t __scdep_3_follow(__scdep_in *_this) {
    /* line 25 */
    token *s = _this->toks[0];
    /* line 25 */
    token *t = _this->toks[1];
    /* line 26 */
    return false;
}

static uint8_t __scdep_4_follow(__scdep_in *_this) {
    /* line 27 */
    token *s = _this->toks[0];
    /* line 27 */
    token *t = _this->toks[1];
    /* line 28 */
    return false;
}

static uint8_t __scdep_5_follow(__scdep_in *_this) {
    /* line 29 */
    token *s = _this->toks[0];
    /* line 29 */
    token *t = _this->toks[1];
    /* line 30 */
    return false;
}

int32_t main(void) {
    s = token_bind("bat.s", NULL);
    token_set_crit(s, 1, 0);
    token_set_degree(s, 0, 3);
    token_set_reach(s, 4);
    token_set_batch(s, 1);
    token_set_dom(s, 1, 4);
    a = token_bind("bat.a", NULL);
    token_set_depth(a, 1);
    token_set_crit(a, 1, 0);
    token_set_degree(a, 1, 1);
    token_set_reach(a, 1);
    token_set_batch(a, 3);
    token_set_dom(a, 0, 0);
    b = token_bind("bat.b", NULL);
    token_set_depth(b, 1);
    token_set_crit(b, 1, 0);
    token_set_degree(b, 1, 1);
    token_set_reach(b, 1);
    token_set_batch(b, 3);
    token_set_dom(b, 0, 0);
    c = token_bind("bat.c", NULL);
    token_set_depth(c, 1);
    token_set_crit(c, 1, 0);
    token_set_degree(c, 1, 1);
    token_set_reach(c, 1);
    token_set_batch(c, 3);
    token_set_dom(c, 0, 0);
    t = token_bind("bat.t", NULL);
    token_set_depth(t, 2);
    token_set_crit(t, 1, 0);
    token_set_degree(t, 3, 0);
    token_set_reach(t, 0);
    token_set_batch(t, 1);
    token_set_dom(t, 0, 0);
    { token *_deps0[] = { s, a }; token_depend_map(_deps0, 1, 1, 1, __scdep_0_tramp, NULL); }
    { token *_deps1[] = { s, b }; token_depend_map(_deps1, 1, 1, 1, __scdep_1_tramp, NULL); }
    { token *_deps2[] = { s, c }; token_depend_map(_deps2, 1, 1, 1, __scdep_2_tramp, NULL); }
    { token *_deps3[] = { a, t }; token_depend_map(_deps3, 1, 1, 1, __scdep_3_tramp, NULL); }
    { token *_deps4[] = { b, t }; token_depend_map(_deps4, 1, 1, 1, __scdep_4_tramp, NULL); }
    { token *_deps5[] = { c, t }; token_depend_map(_deps5, 1, 1, 1, __scdep_5_tramp, NULL); }
    /* line 33 */
    printf("node     batch width\n");
    /* line 34 */
    printf("s        %5d %5d\n", token_batch(s), token_batch_width(s));
    /* line 35 */
    printf("a        %5d %5d\n", token_batch(a), token_batch_width(a));
    /* line 36 */
    printf("b        %5d %5d\n", token_batch(b), token_batch_width(b));
    /* line 37 */
    printf("c        %5d %5d\n", token_batch(c), token_batch_width(c));
    /* line 38 */
    printf("t        %5d %5d\n", token_batch(t), token_batch_width(t));
    /* line 39 */
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
static int __scdep_2_tramp(token **_ts, int _n, int _acting, void *_ctx) {
    __scdep_in _self; _self.toks = _ts; _self.count = _n; _self.active = _acting; _self.ctx = _ctx;
    return (int)__scdep_2_follow(&_self);
}
static int __scdep_3_tramp(token **_ts, int _n, int _acting, void *_ctx) {
    __scdep_in _self; _self.toks = _ts; _self.count = _n; _self.active = _acting; _self.ctx = _ctx;
    return (int)__scdep_3_follow(&_self);
}
static int __scdep_4_tramp(token **_ts, int _n, int _acting, void *_ctx) {
    __scdep_in _self; _self.toks = _ts; _self.count = _n; _self.active = _acting; _self.ctx = _ctx;
    return (int)__scdep_4_follow(&_self);
}
static int __scdep_5_tramp(token **_ts, int _n, int _acting, void *_ctx) {
    __scdep_in _self; _self.toks = _ts; _self.count = _n; _self.active = _acting; _self.ctx = _ctx;
    return (int)__scdep_5_follow(&_self);
}
