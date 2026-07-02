/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static sc_token *sc_tin = {0};
static sc_token *sc_l = {0};
static sc_token *sc_r = {0};
static sc_token *sc_mid = {0};
static sc_token *sc_tout = {0};
static bool sc_dep_0_follow(sc_dep_in *_this);
static bool sc_dep_1_follow(sc_dep_in *_this);
static bool sc_dep_2_follow(sc_dep_in *_this);
static bool sc_dep_3_follow(sc_dep_in *_this);
static bool sc_dep_4_follow(sc_dep_in *_this);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


static int sc_dep_0_tramp(sc_token **, int, int, void *);
static int sc_dep_1_tramp(sc_token **, int, int, void *);
static int sc_dep_2_tramp(sc_token **, int, int, void *);
static int sc_dep_3_tramp(sc_token **, int, int, void *);
static int sc_dep_4_tramp(sc_token **, int, int, void *);

static bool sc_dep_0_follow(sc_dep_in *_this) {
    /* line 23 */
    sc_token *s = _this->toks[0];
    /* line 23 */
    sc_token *t = _this->toks[1];
    /* line 24 */
    return false;
}

static bool sc_dep_1_follow(sc_dep_in *_this) {
    /* line 25 */
    sc_token *s = _this->toks[0];
    /* line 25 */
    sc_token *t = _this->toks[1];
    /* line 26 */
    return false;
}

static bool sc_dep_2_follow(sc_dep_in *_this) {
    /* line 27 */
    sc_token *s = _this->toks[0];
    /* line 27 */
    sc_token *t = _this->toks[1];
    /* line 28 */
    return false;
}

static bool sc_dep_3_follow(sc_dep_in *_this) {
    /* line 29 */
    sc_token *s = _this->toks[0];
    /* line 29 */
    sc_token *t = _this->toks[1];
    /* line 30 */
    return false;
}

static bool sc_dep_4_follow(sc_dep_in *_this) {
    /* line 31 */
    sc_token *s = _this->toks[0];
    /* line 31 */
    sc_token *t = _this->toks[1];
    /* line 32 */
    return false;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_tin = sc_token_bind("dom.in", NULL);
    sc_token_set_crit(sc_tin, 1, 0);
    sc_token_set_degree(sc_tin, 0, 2);
    sc_token_set_reach(sc_tin, 4);
    sc_token_set_batch(sc_tin, 1);
    sc_token_set_dom(sc_tin, 1, 4);
    sc_l = sc_token_bind("dom.l", NULL);
    sc_token_set_depth(sc_l, 1);
    sc_token_set_crit(sc_l, 1, 0);
    sc_token_set_degree(sc_l, 1, 1);
    sc_token_set_reach(sc_l, 2);
    sc_token_set_batch(sc_l, 2);
    sc_token_set_dom(sc_l, 0, 0);
    sc_r = sc_token_bind("dom.r", NULL);
    sc_token_set_depth(sc_r, 1);
    sc_token_set_crit(sc_r, 1, 0);
    sc_token_set_degree(sc_r, 1, 1);
    sc_token_set_reach(sc_r, 2);
    sc_token_set_batch(sc_r, 2);
    sc_token_set_dom(sc_r, 0, 0);
    sc_mid = sc_token_bind("dom.mid", NULL);
    sc_token_set_depth(sc_mid, 2);
    sc_token_set_crit(sc_mid, 1, 0);
    sc_token_set_degree(sc_mid, 2, 1);
    sc_token_set_reach(sc_mid, 1);
    sc_token_set_batch(sc_mid, 1);
    sc_token_set_dom(sc_mid, 1, 1);
    sc_tout = sc_token_bind("dom.out", NULL);
    sc_token_set_depth(sc_tout, 3);
    sc_token_set_crit(sc_tout, 1, 0);
    sc_token_set_degree(sc_tout, 1, 0);
    sc_token_set_reach(sc_tout, 0);
    sc_token_set_batch(sc_tout, 1);
    sc_token_set_dom(sc_tout, 0, 0);
    { sc_token *_deps0[] = { sc_tin, sc_l }; sc_token_depend_map(_deps0, 1, 1, 1, sc_dep_0_tramp, NULL); }
    { sc_token *_deps1[] = { sc_tin, sc_r }; sc_token_depend_map(_deps1, 1, 1, 1, sc_dep_1_tramp, NULL); }
    { sc_token *_deps2[] = { sc_l, sc_mid }; sc_token_depend_map(_deps2, 1, 1, 1, sc_dep_2_tramp, NULL); }
    { sc_token *_deps3[] = { sc_r, sc_mid }; sc_token_depend_map(_deps3, 1, 1, 1, sc_dep_3_tramp, NULL); }
    { sc_token *_deps4[] = { sc_mid, sc_tout }; sc_token_depend_map(_deps4, 1, 1, 1, sc_dep_4_tramp, NULL); }
    /* line 35 */
    printf("node     checkpoint dom_size\n");
    /* line 36 */
    int32_t _sq0 = sc_token_checkpoint(sc_tin);
    int32_t _sq1 = sc_token_dom_size(sc_tin);
    printf("in       %10d %8d\n", _sq0, _sq1);
    /* line 37 */
    int32_t _sq2 = sc_token_checkpoint(sc_l);
    int32_t _sq3 = sc_token_dom_size(sc_l);
    printf("l        %10d %8d\n", _sq2, _sq3);
    /* line 38 */
    int32_t _sq4 = sc_token_checkpoint(sc_r);
    int32_t _sq5 = sc_token_dom_size(sc_r);
    printf("r        %10d %8d\n", _sq4, _sq5);
    /* line 39 */
    int32_t _sq6 = sc_token_checkpoint(sc_mid);
    int32_t _sq7 = sc_token_dom_size(sc_mid);
    printf("mid      %10d %8d\n", _sq6, _sq7);
    /* line 40 */
    int32_t _sq8 = sc_token_checkpoint(sc_tout);
    int32_t _sq9 = sc_token_dom_size(sc_tout);
    printf("out      %10d %8d\n", _sq8, _sq9);
    /* line 41 */
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
static int sc_dep_4_tramp(sc_token **_ts, int _n, int _acting, void *_ctx) {
    sc_dep_in _self; _self.toks = _ts; _self.count = _n; _self.active = _acting; _self.ctx = _ctx;
    return (int)sc_dep_4_follow(&_self);
}
