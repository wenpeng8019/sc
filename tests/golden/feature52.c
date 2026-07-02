/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static sc_token *sc_config = {0};
static sc_token *sc_a = {0};
static sc_token *sc_b = {0};
static sc_token *sc_c = {0};
static sc_token *sc_sink = {0};
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

static bool sc_dep_4_follow(sc_dep_in *_this) {
    /* line 26 */
    sc_token *s = _this->toks[0];
    /* line 26 */
    sc_token *t = _this->toks[1];
    /* line 27 */
    return false;
}

static bool sc_dep_5_follow(sc_dep_in *_this) {
    /* line 28 */
    sc_token *s = _this->toks[0];
    /* line 28 */
    sc_token *t = _this->toks[1];
    /* line 29 */
    return false;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_config = sc_token_bind("hub.config", NULL);
    sc_token_set_crit(sc_config, 1, 0);
    sc_token_set_degree(sc_config, 0, 3);
    sc_token_set_reach(sc_config, 4);
    sc_token_set_batch(sc_config, 1);
    sc_token_set_dom(sc_config, 1, 4);
    sc_a = sc_token_bind("hub.a", NULL);
    sc_token_set_depth(sc_a, 1);
    sc_token_set_crit(sc_a, 1, 0);
    sc_token_set_degree(sc_a, 1, 1);
    sc_token_set_reach(sc_a, 1);
    sc_token_set_batch(sc_a, 3);
    sc_token_set_dom(sc_a, 0, 0);
    sc_b = sc_token_bind("hub.b", NULL);
    sc_token_set_depth(sc_b, 1);
    sc_token_set_crit(sc_b, 1, 0);
    sc_token_set_degree(sc_b, 1, 1);
    sc_token_set_reach(sc_b, 1);
    sc_token_set_batch(sc_b, 3);
    sc_token_set_dom(sc_b, 0, 0);
    sc_c = sc_token_bind("hub.c", NULL);
    sc_token_set_depth(sc_c, 1);
    sc_token_set_crit(sc_c, 1, 0);
    sc_token_set_degree(sc_c, 1, 1);
    sc_token_set_reach(sc_c, 1);
    sc_token_set_batch(sc_c, 3);
    sc_token_set_dom(sc_c, 0, 0);
    sc_sink = sc_token_bind("hub.sink", NULL);
    sc_token_set_depth(sc_sink, 2);
    sc_token_set_crit(sc_sink, 1, 0);
    sc_token_set_degree(sc_sink, 3, 0);
    sc_token_set_reach(sc_sink, 0);
    sc_token_set_batch(sc_sink, 1);
    sc_token_set_dom(sc_sink, 0, 0);
    { sc_token *_deps0[] = { sc_config, sc_a }; sc_token_depend_map(_deps0, 1, 1, 1, sc_dep_0_tramp, NULL); }
    { sc_token *_deps1[] = { sc_config, sc_b }; sc_token_depend_map(_deps1, 1, 1, 1, sc_dep_1_tramp, NULL); }
    { sc_token *_deps2[] = { sc_config, sc_c }; sc_token_depend_map(_deps2, 1, 1, 1, sc_dep_2_tramp, NULL); }
    { sc_token *_deps3[] = { sc_a, sc_sink }; sc_token_depend_map(_deps3, 1, 1, 1, sc_dep_3_tramp, NULL); }
    { sc_token *_deps4[] = { sc_b, sc_sink }; sc_token_depend_map(_deps4, 1, 1, 1, sc_dep_4_tramp, NULL); }
    { sc_token *_deps5[] = { sc_c, sc_sink }; sc_token_depend_map(_deps5, 1, 1, 1, sc_dep_5_tramp, NULL); }
    /* line 32 */
    printf("node     fanin fanout\n");
    /* line 33 */
    int32_t _sq0 = sc_token_fanin(sc_config);
    int32_t _sq1 = sc_token_fanout(sc_config);
    printf("config   %5d %6d\n", _sq0, _sq1);
    /* line 34 */
    int32_t _sq2 = sc_token_fanin(sc_a);
    int32_t _sq3 = sc_token_fanout(sc_a);
    printf("a        %5d %6d\n", _sq2, _sq3);
    /* line 35 */
    int32_t _sq4 = sc_token_fanin(sc_b);
    int32_t _sq5 = sc_token_fanout(sc_b);
    printf("b        %5d %6d\n", _sq4, _sq5);
    /* line 36 */
    int32_t _sq6 = sc_token_fanin(sc_c);
    int32_t _sq7 = sc_token_fanout(sc_c);
    printf("c        %5d %6d\n", _sq6, _sq7);
    /* line 37 */
    int32_t _sq8 = sc_token_fanin(sc_sink);
    int32_t _sq9 = sc_token_fanout(sc_sink);
    printf("sink     %5d %6d\n", _sq8, _sq9);
    /* line 38 */
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
