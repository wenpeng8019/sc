/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static token *fetch = {0};
static token *decode = {0};
static token *render = {0};
static token *audio = {0};
static token *output = {0};
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
    /* line 20 */
    token *s = _this->toks[0];
    /* line 20 */
    token *t = _this->toks[1];
    /* line 21 */
    return false;
}

static uint8_t __scdep_1_follow(__scdep_in *_this) {
    /* line 22 */
    token *s = _this->toks[0];
    /* line 22 */
    token *t = _this->toks[1];
    /* line 23 */
    return false;
}

static uint8_t __scdep_2_follow(__scdep_in *_this) {
    /* line 24 */
    token *s = _this->toks[0];
    /* line 24 */
    token *t = _this->toks[1];
    /* line 25 */
    return false;
}

static uint8_t __scdep_3_follow(__scdep_in *_this) {
    /* line 26 */
    token *s = _this->toks[0];
    /* line 26 */
    token *t = _this->toks[1];
    /* line 27 */
    return false;
}

static uint8_t __scdep_4_follow(__scdep_in *_this) {
    /* line 28 */
    token *s = _this->toks[0];
    /* line 28 */
    token *t = _this->toks[1];
    /* line 29 */
    return false;
}

int32_t main(void) {
    fetch = token_bind("pipe.fetch", NULL);
    token_set_crit(fetch, 1, 0);
    token_set_degree(fetch, 0, 2);
    token_set_reach(fetch, 4);
    token_set_batch(fetch, 1);
    token_set_dom(fetch, 1, 4);
    decode = token_bind("pipe.decode", NULL);
    token_set_depth(decode, 1);
    token_set_crit(decode, 1, 0);
    token_set_degree(decode, 1, 1);
    token_set_reach(decode, 2);
    token_set_batch(decode, 2);
    token_set_dom(decode, 1, 1);
    render = token_bind("pipe.render", NULL);
    token_set_depth(render, 2);
    token_set_crit(render, 1, 0);
    token_set_degree(render, 1, 1);
    token_set_reach(render, 1);
    token_set_batch(render, 1);
    token_set_dom(render, 0, 0);
    audio = token_bind("pipe.audio", NULL);
    token_set_depth(audio, 1);
    token_set_crit(audio, 0, 1);
    token_set_degree(audio, 1, 1);
    token_set_reach(audio, 1);
    token_set_batch(audio, 2);
    token_set_dom(audio, 0, 0);
    output = token_bind("pipe.output", NULL);
    token_set_depth(output, 3);
    token_set_crit(output, 1, 0);
    token_set_degree(output, 2, 0);
    token_set_reach(output, 0);
    token_set_batch(output, 1);
    token_set_dom(output, 0, 0);
    { token *_deps0[] = { fetch, decode }; token_depend_map(_deps0, 1, 1, 1, __scdep_0_tramp, NULL); }
    { token *_deps1[] = { decode, render }; token_depend_map(_deps1, 1, 1, 1, __scdep_1_tramp, NULL); }
    { token *_deps2[] = { render, output }; token_depend_map(_deps2, 1, 1, 1, __scdep_2_tramp, NULL); }
    { token *_deps3[] = { fetch, audio }; token_depend_map(_deps3, 1, 1, 1, __scdep_3_tramp, NULL); }
    { token *_deps4[] = { audio, output }; token_depend_map(_deps4, 1, 1, 1, __scdep_4_tramp, NULL); }
    /* line 32 */
    printf("stage     depth crit slack\n");
    /* line 33 */
    printf("fetch     %5d %4d %5d\n", token_depth(fetch), token_critical(fetch), token_slack(fetch));
    /* line 34 */
    printf("decode    %5d %4d %5d\n", token_depth(decode), token_critical(decode), token_slack(decode));
    /* line 35 */
    printf("render    %5d %4d %5d\n", token_depth(render), token_critical(render), token_slack(render));
    /* line 36 */
    printf("audio     %5d %4d %5d\n", token_depth(audio), token_critical(audio), token_slack(audio));
    /* line 37 */
    printf("output    %5d %4d %5d\n", token_depth(output), token_critical(output), token_slack(output));
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
