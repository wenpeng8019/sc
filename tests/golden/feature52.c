/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static token *config = {0};
static token *a = {0};
static token *b = {0};
static token *c = {0};
static token *sink = {0};
static bool __scdep_0_follow(__scdep_in *_this);
static bool __scdep_1_follow(__scdep_in *_this);
static bool __scdep_2_follow(__scdep_in *_this);
static bool __scdep_3_follow(__scdep_in *_this);
static bool __scdep_4_follow(__scdep_in *_this);
static bool __scdep_5_follow(__scdep_in *_this);
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

static bool __scdep_0_follow(__scdep_in *_this) {
    /* line 18 */
    token *s = _this->toks[0];
    /* line 18 */
    token *t = _this->toks[1];
    /* line 19 */
    return false;
}

static bool __scdep_1_follow(__scdep_in *_this) {
    /* line 20 */
    token *s = _this->toks[0];
    /* line 20 */
    token *t = _this->toks[1];
    /* line 21 */
    return false;
}

static bool __scdep_2_follow(__scdep_in *_this) {
    /* line 22 */
    token *s = _this->toks[0];
    /* line 22 */
    token *t = _this->toks[1];
    /* line 23 */
    return false;
}

static bool __scdep_3_follow(__scdep_in *_this) {
    /* line 24 */
    token *s = _this->toks[0];
    /* line 24 */
    token *t = _this->toks[1];
    /* line 25 */
    return false;
}

static bool __scdep_4_follow(__scdep_in *_this) {
    /* line 26 */
    token *s = _this->toks[0];
    /* line 26 */
    token *t = _this->toks[1];
    /* line 27 */
    return false;
}

static bool __scdep_5_follow(__scdep_in *_this) {
    /* line 28 */
    token *s = _this->toks[0];
    /* line 28 */
    token *t = _this->toks[1];
    /* line 29 */
    return false;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    config = token_bind("hub.config", NULL);
    token_set_crit(config, 1, 0);
    token_set_degree(config, 0, 3);
    token_set_reach(config, 4);
    token_set_batch(config, 1);
    token_set_dom(config, 1, 4);
    a = token_bind("hub.a", NULL);
    token_set_depth(a, 1);
    token_set_crit(a, 1, 0);
    token_set_degree(a, 1, 1);
    token_set_reach(a, 1);
    token_set_batch(a, 3);
    token_set_dom(a, 0, 0);
    b = token_bind("hub.b", NULL);
    token_set_depth(b, 1);
    token_set_crit(b, 1, 0);
    token_set_degree(b, 1, 1);
    token_set_reach(b, 1);
    token_set_batch(b, 3);
    token_set_dom(b, 0, 0);
    c = token_bind("hub.c", NULL);
    token_set_depth(c, 1);
    token_set_crit(c, 1, 0);
    token_set_degree(c, 1, 1);
    token_set_reach(c, 1);
    token_set_batch(c, 3);
    token_set_dom(c, 0, 0);
    sink = token_bind("hub.sink", NULL);
    token_set_depth(sink, 2);
    token_set_crit(sink, 1, 0);
    token_set_degree(sink, 3, 0);
    token_set_reach(sink, 0);
    token_set_batch(sink, 1);
    token_set_dom(sink, 0, 0);
    { token *_deps0[] = { config, a }; token_depend_map(_deps0, 1, 1, 1, __scdep_0_tramp, NULL); }
    { token *_deps1[] = { config, b }; token_depend_map(_deps1, 1, 1, 1, __scdep_1_tramp, NULL); }
    { token *_deps2[] = { config, c }; token_depend_map(_deps2, 1, 1, 1, __scdep_2_tramp, NULL); }
    { token *_deps3[] = { a, sink }; token_depend_map(_deps3, 1, 1, 1, __scdep_3_tramp, NULL); }
    { token *_deps4[] = { b, sink }; token_depend_map(_deps4, 1, 1, 1, __scdep_4_tramp, NULL); }
    { token *_deps5[] = { c, sink }; token_depend_map(_deps5, 1, 1, 1, __scdep_5_tramp, NULL); }
    /* line 32 */
    printf("node     fanin fanout\n");
    /* line 33 */
    int32_t _sq0 = token_fanin(config);
    int32_t _sq1 = token_fanout(config);
    printf("config   %5d %6d\n", _sq0, _sq1);
    /* line 34 */
    int32_t _sq2 = token_fanin(a);
    int32_t _sq3 = token_fanout(a);
    printf("a        %5d %6d\n", _sq2, _sq3);
    /* line 35 */
    int32_t _sq4 = token_fanin(b);
    int32_t _sq5 = token_fanout(b);
    printf("b        %5d %6d\n", _sq4, _sq5);
    /* line 36 */
    int32_t _sq6 = token_fanin(c);
    int32_t _sq7 = token_fanout(c);
    printf("c        %5d %6d\n", _sq6, _sq7);
    /* line 37 */
    int32_t _sq8 = token_fanin(sink);
    int32_t _sq9 = token_fanout(sink);
    printf("sink     %5d %6d\n", _sq8, _sq9);
    /* line 38 */
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
