/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static sc_token *sc_root = {0};
static sc_token *sc_m1 = {0};
static sc_token *sc_m2 = {0};
static sc_token *sc_leaf1 = {0};
static sc_token *sc_leaf2 = {0};
static bool sc_dep_0_follow(sc_dep_in *_this);
static bool sc_dep_1_follow(sc_dep_in *_this);
static bool sc_dep_2_follow(sc_dep_in *_this);
static bool sc_dep_3_follow(sc_dep_in *_this);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


static int sc_dep_0_tramp(sc_token **, int, int, void *);
static int sc_dep_1_tramp(sc_token **, int, int, void *);
static int sc_dep_2_tramp(sc_token **, int, int, void *);
static int sc_dep_3_tramp(sc_token **, int, int, void *);

static bool sc_dep_0_follow(sc_dep_in *_this) {
    /* line 18 */
    sc_token *s = _this->toks[0];
    /* line 18 */
    sc_token *t = _this->toks[1];
    /* line 19 */
    return false;
}

static bool sc_dep_1_follow(sc_dep_in *_this) {
    /* line 20 */
    sc_token *s = _this->toks[0];
    /* line 20 */
    sc_token *t = _this->toks[1];
    /* line 21 */
    return false;
}

static bool sc_dep_2_follow(sc_dep_in *_this) {
    /* line 22 */
    sc_token *s = _this->toks[0];
    /* line 22 */
    sc_token *t = _this->toks[1];
    /* line 23 */
    return false;
}

static bool sc_dep_3_follow(sc_dep_in *_this) {
    /* line 24 */
    sc_token *s = _this->toks[0];
    /* line 24 */
    sc_token *t = _this->toks[1];
    /* line 25 */
    return false;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_root = sc_token_bind("rch.root", NULL);
    sc_token_set_crit(sc_root, 1, 0);
    sc_token_set_degree(sc_root, 0, 2);
    sc_token_set_reach(sc_root, 4);
    sc_token_set_batch(sc_root, 1);
    sc_token_set_dom(sc_root, 1, 4);
    sc_m1 = sc_token_bind("rch.m1", NULL);
    sc_token_set_depth(sc_m1, 1);
    sc_token_set_crit(sc_m1, 1, 0);
    sc_token_set_degree(sc_m1, 1, 1);
    sc_token_set_reach(sc_m1, 1);
    sc_token_set_batch(sc_m1, 2);
    sc_token_set_dom(sc_m1, 1, 1);
    sc_m2 = sc_token_bind("rch.m2", NULL);
    sc_token_set_depth(sc_m2, 1);
    sc_token_set_crit(sc_m2, 1, 0);
    sc_token_set_degree(sc_m2, 1, 1);
    sc_token_set_reach(sc_m2, 1);
    sc_token_set_batch(sc_m2, 2);
    sc_token_set_dom(sc_m2, 1, 1);
    sc_leaf1 = sc_token_bind("rch.leaf1", NULL);
    sc_token_set_depth(sc_leaf1, 2);
    sc_token_set_crit(sc_leaf1, 1, 0);
    sc_token_set_degree(sc_leaf1, 1, 0);
    sc_token_set_reach(sc_leaf1, 0);
    sc_token_set_batch(sc_leaf1, 2);
    sc_token_set_dom(sc_leaf1, 0, 0);
    sc_leaf2 = sc_token_bind("rch.leaf2", NULL);
    sc_token_set_depth(sc_leaf2, 2);
    sc_token_set_crit(sc_leaf2, 1, 0);
    sc_token_set_degree(sc_leaf2, 1, 0);
    sc_token_set_reach(sc_leaf2, 0);
    sc_token_set_batch(sc_leaf2, 2);
    sc_token_set_dom(sc_leaf2, 0, 0);
    { sc_token *_deps0[] = { sc_root, sc_m1 }; sc_token_depend_map(_deps0, 1, 1, 1, sc_dep_0_tramp, NULL); }
    { sc_token *_deps1[] = { sc_root, sc_m2 }; sc_token_depend_map(_deps1, 1, 1, 1, sc_dep_1_tramp, NULL); }
    { sc_token *_deps2[] = { sc_m1, sc_leaf1 }; sc_token_depend_map(_deps2, 1, 1, 1, sc_dep_2_tramp, NULL); }
    { sc_token *_deps3[] = { sc_m2, sc_leaf2 }; sc_token_depend_map(_deps3, 1, 1, 1, sc_dep_3_tramp, NULL); }
    /* line 28 */
    printf("node     reach\n");
    /* line 29 */
    printf("root     %5d\n", sc_token_reach(sc_root));
    /* line 30 */
    printf("m1       %5d\n", sc_token_reach(sc_m1));
    /* line 31 */
    printf("m2       %5d\n", sc_token_reach(sc_m2));
    /* line 32 */
    printf("leaf1    %5d\n", sc_token_reach(sc_leaf1));
    /* line 33 */
    printf("leaf2    %5d\n", sc_token_reach(sc_leaf2));
    /* line 34 */
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
static int sc_dep_2_tramp(sc_token **_ts, int _n, int _acting, void *_ctx) {
    sc_dep_in _self; _self.toks = _ts; _self.count = _n; _self.active = _acting; _self.ctx = _ctx;
    return (int)sc_dep_2_follow(&_self);
}
static int sc_dep_3_tramp(sc_token **_ts, int _n, int _acting, void *_ctx) {
    sc_dep_in _self; _self.toks = _ts; _self.count = _n; _self.active = _acting; _self.ctx = _ctx;
    return (int)sc_dep_3_follow(&_self);
}
