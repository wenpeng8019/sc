/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static sc_token *sc_fetch = {0};
static sc_token *sc_decode = {0};
static sc_token *sc_render = {0};
static sc_token *sc_audio = {0};
static sc_token *sc_output = {0};
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
    /* line 20 */
    sc_token *s = _this->toks[0];
    /* line 20 */
    sc_token *t = _this->toks[1];
    /* line 21 */
    return false;
}

static bool sc_dep_1_follow(sc_dep_in *_this) {
    /* line 22 */
    sc_token *s = _this->toks[0];
    /* line 22 */
    sc_token *t = _this->toks[1];
    /* line 23 */
    return false;
}

static bool sc_dep_2_follow(sc_dep_in *_this) {
    /* line 24 */
    sc_token *s = _this->toks[0];
    /* line 24 */
    sc_token *t = _this->toks[1];
    /* line 25 */
    return false;
}

static bool sc_dep_3_follow(sc_dep_in *_this) {
    /* line 26 */
    sc_token *s = _this->toks[0];
    /* line 26 */
    sc_token *t = _this->toks[1];
    /* line 27 */
    return false;
}

static bool sc_dep_4_follow(sc_dep_in *_this) {
    /* line 28 */
    sc_token *s = _this->toks[0];
    /* line 28 */
    sc_token *t = _this->toks[1];
    /* line 29 */
    return false;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_fetch = sc_token_bind("pipe.fetch", NULL);
    sc_token_set_crit(sc_fetch, 1, 0);
    sc_token_set_degree(sc_fetch, 0, 2);
    sc_token_set_reach(sc_fetch, 4);
    sc_token_set_batch(sc_fetch, 1);
    sc_token_set_dom(sc_fetch, 1, 4);
    sc_decode = sc_token_bind("pipe.decode", NULL);
    sc_token_set_depth(sc_decode, 1);
    sc_token_set_crit(sc_decode, 1, 0);
    sc_token_set_degree(sc_decode, 1, 1);
    sc_token_set_reach(sc_decode, 2);
    sc_token_set_batch(sc_decode, 2);
    sc_token_set_dom(sc_decode, 1, 1);
    sc_render = sc_token_bind("pipe.render", NULL);
    sc_token_set_depth(sc_render, 2);
    sc_token_set_crit(sc_render, 1, 0);
    sc_token_set_degree(sc_render, 1, 1);
    sc_token_set_reach(sc_render, 1);
    sc_token_set_batch(sc_render, 1);
    sc_token_set_dom(sc_render, 0, 0);
    sc_audio = sc_token_bind("pipe.audio", NULL);
    sc_token_set_depth(sc_audio, 1);
    sc_token_set_crit(sc_audio, 0, 1);
    sc_token_set_degree(sc_audio, 1, 1);
    sc_token_set_reach(sc_audio, 1);
    sc_token_set_batch(sc_audio, 2);
    sc_token_set_dom(sc_audio, 0, 0);
    sc_output = sc_token_bind("pipe.output", NULL);
    sc_token_set_depth(sc_output, 3);
    sc_token_set_crit(sc_output, 1, 0);
    sc_token_set_degree(sc_output, 2, 0);
    sc_token_set_reach(sc_output, 0);
    sc_token_set_batch(sc_output, 1);
    sc_token_set_dom(sc_output, 0, 0);
    { sc_token *_deps0[] = { sc_fetch, sc_decode }; sc_token_depend_map(_deps0, 1, 1, 1, sc_dep_0_tramp, NULL); }
    { sc_token *_deps1[] = { sc_decode, sc_render }; sc_token_depend_map(_deps1, 1, 1, 1, sc_dep_1_tramp, NULL); }
    { sc_token *_deps2[] = { sc_render, sc_output }; sc_token_depend_map(_deps2, 1, 1, 1, sc_dep_2_tramp, NULL); }
    { sc_token *_deps3[] = { sc_fetch, sc_audio }; sc_token_depend_map(_deps3, 1, 1, 1, sc_dep_3_tramp, NULL); }
    { sc_token *_deps4[] = { sc_audio, sc_output }; sc_token_depend_map(_deps4, 1, 1, 1, sc_dep_4_tramp, NULL); }
    /* line 32 */
    printf("stage     depth crit slack\n");
    /* line 33 */
    int32_t _sq0 = sc_token_depth(sc_fetch);
    int32_t _sq1 = sc_token_critical(sc_fetch);
    int32_t _sq2 = sc_token_slack(sc_fetch);
    printf("fetch     %5d %4d %5d\n", _sq0, _sq1, _sq2);
    /* line 34 */
    int32_t _sq3 = sc_token_depth(sc_decode);
    int32_t _sq4 = sc_token_critical(sc_decode);
    int32_t _sq5 = sc_token_slack(sc_decode);
    printf("decode    %5d %4d %5d\n", _sq3, _sq4, _sq5);
    /* line 35 */
    int32_t _sq6 = sc_token_depth(sc_render);
    int32_t _sq7 = sc_token_critical(sc_render);
    int32_t _sq8 = sc_token_slack(sc_render);
    printf("render    %5d %4d %5d\n", _sq6, _sq7, _sq8);
    /* line 36 */
    int32_t _sq9 = sc_token_depth(sc_audio);
    int32_t _sq10 = sc_token_critical(sc_audio);
    int32_t _sq11 = sc_token_slack(sc_audio);
    printf("audio     %5d %4d %5d\n", _sq9, _sq10, _sq11);
    /* line 37 */
    int32_t _sq12 = sc_token_depth(sc_output);
    int32_t _sq13 = sc_token_critical(sc_output);
    int32_t _sq14 = sc_token_slack(sc_output);
    printf("output    %5d %4d %5d\n", _sq12, _sq13, _sq14);
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
