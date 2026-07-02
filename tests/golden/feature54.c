/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static sc_token *sc_s = {0};
static sc_token *sc_a = {0};
static sc_token *sc_b = {0};
static sc_token *sc_c = {0};
static sc_token *sc_t = {0};
static bool sc_dep_0_follow(sc_dep_in *_this);
static bool sc_dep_1_follow(sc_dep_in *_this);
static bool sc_dep_2_follow(sc_dep_in *_this);
static bool sc_dep_3_follow(sc_dep_in *_this);
static bool sc_dep_4_follow(sc_dep_in *_this);
static bool sc_dep_5_follow(sc_dep_in *_this);
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
static int sc_dep_5_tramp(sc_token **, int, int, void *);

static bool sc_dep_0_follow(sc_dep_in *_this) {
    /* line 19 */
    sc_token *s = _this->toks[0];
    /* line 19 */
    sc_token *t = _this->toks[1];
    /* line 20 */
    return false;
}

static bool sc_dep_1_follow(sc_dep_in *_this) {
    /* line 21 */
    sc_token *s = _this->toks[0];
    /* line 21 */
    sc_token *t = _this->toks[1];
    /* line 22 */
    return false;
}

static bool sc_dep_2_follow(sc_dep_in *_this) {
    /* line 23 */
    sc_token *s = _this->toks[0];
    /* line 23 */
    sc_token *t = _this->toks[1];
    /* line 24 */
    return false;
}

static bool sc_dep_3_follow(sc_dep_in *_this) {
    /* line 25 */
    sc_token *s = _this->toks[0];
    /* line 25 */
    sc_token *t = _this->toks[1];
    /* line 26 */
    return false;
}

static bool sc_dep_4_follow(sc_dep_in *_this) {
    /* line 27 */
    sc_token *s = _this->toks[0];
    /* line 27 */
    sc_token *t = _this->toks[1];
    /* line 28 */
    return false;
}

static bool sc_dep_5_follow(sc_dep_in *_this) {
    /* line 29 */
    sc_token *s = _this->toks[0];
    /* line 29 */
    sc_token *t = _this->toks[1];
    /* line 30 */
    return false;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_s = sc_token_bind("bat.s", NULL);
    sc_token_set_crit(sc_s, 1, 0);
    sc_token_set_degree(sc_s, 0, 3);
    sc_token_set_reach(sc_s, 4);
    sc_token_set_batch(sc_s, 1);
    sc_token_set_dom(sc_s, 1, 4);
    sc_a = sc_token_bind("bat.a", NULL);
    sc_token_set_depth(sc_a, 1);
    sc_token_set_crit(sc_a, 1, 0);
    sc_token_set_degree(sc_a, 1, 1);
    sc_token_set_reach(sc_a, 1);
    sc_token_set_batch(sc_a, 3);
    sc_token_set_dom(sc_a, 0, 0);
    sc_b = sc_token_bind("bat.b", NULL);
    sc_token_set_depth(sc_b, 1);
    sc_token_set_crit(sc_b, 1, 0);
    sc_token_set_degree(sc_b, 1, 1);
    sc_token_set_reach(sc_b, 1);
    sc_token_set_batch(sc_b, 3);
    sc_token_set_dom(sc_b, 0, 0);
    sc_c = sc_token_bind("bat.c", NULL);
    sc_token_set_depth(sc_c, 1);
    sc_token_set_crit(sc_c, 1, 0);
    sc_token_set_degree(sc_c, 1, 1);
    sc_token_set_reach(sc_c, 1);
    sc_token_set_batch(sc_c, 3);
    sc_token_set_dom(sc_c, 0, 0);
    sc_t = sc_token_bind("bat.t", NULL);
    sc_token_set_depth(sc_t, 2);
    sc_token_set_crit(sc_t, 1, 0);
    sc_token_set_degree(sc_t, 3, 0);
    sc_token_set_reach(sc_t, 0);
    sc_token_set_batch(sc_t, 1);
    sc_token_set_dom(sc_t, 0, 0);
    { sc_token *_deps0[] = { sc_s, sc_a }; sc_token_depend_map(_deps0, 1, 1, 1, sc_dep_0_tramp, NULL); }
    { sc_token *_deps1[] = { sc_s, sc_b }; sc_token_depend_map(_deps1, 1, 1, 1, sc_dep_1_tramp, NULL); }
    { sc_token *_deps2[] = { sc_s, sc_c }; sc_token_depend_map(_deps2, 1, 1, 1, sc_dep_2_tramp, NULL); }
    { sc_token *_deps3[] = { sc_a, sc_t }; sc_token_depend_map(_deps3, 1, 1, 1, sc_dep_3_tramp, NULL); }
    { sc_token *_deps4[] = { sc_b, sc_t }; sc_token_depend_map(_deps4, 1, 1, 1, sc_dep_4_tramp, NULL); }
    { sc_token *_deps5[] = { sc_c, sc_t }; sc_token_depend_map(_deps5, 1, 1, 1, sc_dep_5_tramp, NULL); }
    /* line 33 */
    printf("node     batch width\n");
    /* line 34 */
    int32_t _sq0 = sc_token_batch(sc_s);
    int32_t _sq1 = sc_token_batch_width(sc_s);
    printf("s        %5d %5d\n", _sq0, _sq1);
    /* line 35 */
    int32_t _sq2 = sc_token_batch(sc_a);
    int32_t _sq3 = sc_token_batch_width(sc_a);
    printf("a        %5d %5d\n", _sq2, _sq3);
    /* line 36 */
    int32_t _sq4 = sc_token_batch(sc_b);
    int32_t _sq5 = sc_token_batch_width(sc_b);
    printf("b        %5d %5d\n", _sq4, _sq5);
    /* line 37 */
    int32_t _sq6 = sc_token_batch(sc_c);
    int32_t _sq7 = sc_token_batch_width(sc_c);
    printf("c        %5d %5d\n", _sq6, _sq7);
    /* line 38 */
    int32_t _sq8 = sc_token_batch(sc_t);
    int32_t _sq9 = sc_token_batch_width(sc_t);
    printf("t        %5d %5d\n", _sq8, _sq9);
    /* line 39 */
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
static int sc_dep_5_tramp(sc_token **_ts, int _n, int _acting, void *_ctx) {
    sc_dep_in _self; _self.toks = _ts; _self.count = _n; _self.active = _acting; _self.ctx = _ctx;
    return (int)sc_dep_5_follow(&_self);
}
