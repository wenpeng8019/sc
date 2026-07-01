/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static token *root = {0};
static token *m1 = {0};
static token *m2 = {0};
static token *leaf1 = {0};
static token *leaf2 = {0};
static uint8_t __scdep_0_follow(__scdep_in *_this);
static uint8_t __scdep_1_follow(__scdep_in *_this);
static uint8_t __scdep_2_follow(__scdep_in *_this);
static uint8_t __scdep_3_follow(__scdep_in *_this);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static int __scdep_0_tramp(token **, int, int, void *);
static int __scdep_1_tramp(token **, int, int, void *);
static int __scdep_2_tramp(token **, int, int, void *);
static int __scdep_3_tramp(token **, int, int, void *);

static uint8_t __scdep_0_follow(__scdep_in *_this) {
    /* line 18 */
    token *s = _this->toks[0];
    /* line 18 */
    token *t = _this->toks[1];
    /* line 19 */
    return false;
}

static uint8_t __scdep_1_follow(__scdep_in *_this) {
    /* line 20 */
    token *s = _this->toks[0];
    /* line 20 */
    token *t = _this->toks[1];
    /* line 21 */
    return false;
}

static uint8_t __scdep_2_follow(__scdep_in *_this) {
    /* line 22 */
    token *s = _this->toks[0];
    /* line 22 */
    token *t = _this->toks[1];
    /* line 23 */
    return false;
}

static uint8_t __scdep_3_follow(__scdep_in *_this) {
    /* line 24 */
    token *s = _this->toks[0];
    /* line 24 */
    token *t = _this->toks[1];
    /* line 25 */
    return false;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    root = token_bind("rch.root", NULL);
    token_set_crit(root, 1, 0);
    token_set_degree(root, 0, 2);
    token_set_reach(root, 4);
    token_set_batch(root, 1);
    token_set_dom(root, 1, 4);
    m1 = token_bind("rch.m1", NULL);
    token_set_depth(m1, 1);
    token_set_crit(m1, 1, 0);
    token_set_degree(m1, 1, 1);
    token_set_reach(m1, 1);
    token_set_batch(m1, 2);
    token_set_dom(m1, 1, 1);
    m2 = token_bind("rch.m2", NULL);
    token_set_depth(m2, 1);
    token_set_crit(m2, 1, 0);
    token_set_degree(m2, 1, 1);
    token_set_reach(m2, 1);
    token_set_batch(m2, 2);
    token_set_dom(m2, 1, 1);
    leaf1 = token_bind("rch.leaf1", NULL);
    token_set_depth(leaf1, 2);
    token_set_crit(leaf1, 1, 0);
    token_set_degree(leaf1, 1, 0);
    token_set_reach(leaf1, 0);
    token_set_batch(leaf1, 2);
    token_set_dom(leaf1, 0, 0);
    leaf2 = token_bind("rch.leaf2", NULL);
    token_set_depth(leaf2, 2);
    token_set_crit(leaf2, 1, 0);
    token_set_degree(leaf2, 1, 0);
    token_set_reach(leaf2, 0);
    token_set_batch(leaf2, 2);
    token_set_dom(leaf2, 0, 0);
    { token *_deps0[] = { root, m1 }; token_depend_map(_deps0, 1, 1, 1, __scdep_0_tramp, NULL); }
    { token *_deps1[] = { root, m2 }; token_depend_map(_deps1, 1, 1, 1, __scdep_1_tramp, NULL); }
    { token *_deps2[] = { m1, leaf1 }; token_depend_map(_deps2, 1, 1, 1, __scdep_2_tramp, NULL); }
    { token *_deps3[] = { m2, leaf2 }; token_depend_map(_deps3, 1, 1, 1, __scdep_3_tramp, NULL); }
    /* line 28 */
    printf("node     reach\n");
    /* line 29 */
    printf("root     %5d\n", token_reach(root));
    /* line 30 */
    printf("m1       %5d\n", token_reach(m1));
    /* line 31 */
    printf("m2       %5d\n", token_reach(m2));
    /* line 32 */
    printf("leaf1    %5d\n", token_reach(leaf1));
    /* line 33 */
    printf("leaf2    %5d\n", token_reach(leaf2));
    /* line 34 */
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
