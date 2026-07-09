/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "scm_builtins_neuron_neuron_sc.h"

static const float sc_LR = 0.05f;
static const float sc_B1 = 0.9f;
static const float sc_B2 = 0.999f;
static const float sc_EPS = 0.00000001f;
static const int32_t sc_EPOCHS = 400;
static sc_token *sc_i0 = {0};
static sc_token *sc_i1 = {0};
static sc_token *sc_h0 = {0};
static sc_token *sc_h1 = {0};
static sc_token *sc_h2 = {0};
static sc_token *sc_o0 = {0};
static sc_token *sc_loss = {0};
static bool sc_dep_0_follow(sc_dep_in *_this);
static bool sc_dep_1_follow(sc_dep_in *_this);
static bool sc_dep_2_follow(sc_dep_in *_this);
static bool sc_dep_3_follow(sc_dep_in *_this);
static bool sc_dep_4_follow(sc_dep_in *_this);
static sc_neuron sc_IN[2] = {0};
static sc_neuron sc_HID[3] = {0};
static sc_neuron sc_OUT[1] = {0};
static sc_neuron sc_LOSS_N = {0};
static float sc_g_target = 5.0f;
static uint32_t sc_g_seed = 22695477;
static float sc_rndf(void);
static void sc_init_net(void);
static void sc_zero_grad(void);
static void sc_zero_gw(void);
static float sc_forward(void);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_mod_neuron_init(void); void sc_mod_neuron_drop(void);
static int sc_dep_0_tramp(sc_token **, int, int, void *);
static int sc_dep_1_tramp(sc_token **, int, int, void *);
static int sc_dep_2_tramp(sc_token **, int, int, void *);
static int sc_dep_3_tramp(sc_token **, int, int, void *);
static int sc_dep_4_tramp(sc_token **, int, int, void *);

static bool sc_dep_0_follow(sc_dep_in *_this) {
    /* line 25 */
    sc_token *a = _this->toks[0];
    /* line 25 */
    sc_token *b = _this->toks[1];
    /* line 25 */
    sc_token *c = _this->toks[2];
    /* line 26 */
    sc_edge_step(_this, c);
    /* line 27 */
    return false;
}

static bool sc_dep_1_follow(sc_dep_in *_this) {
    /* line 29 */
    sc_token *a = _this->toks[0];
    /* line 29 */
    sc_token *b = _this->toks[1];
    /* line 29 */
    sc_token *c = _this->toks[2];
    /* line 30 */
    sc_edge_step(_this, c);
    /* line 31 */
    return false;
}

static bool sc_dep_2_follow(sc_dep_in *_this) {
    /* line 33 */
    sc_token *a = _this->toks[0];
    /* line 33 */
    sc_token *b = _this->toks[1];
    /* line 33 */
    sc_token *c = _this->toks[2];
    /* line 34 */
    sc_edge_step(_this, c);
    /* line 35 */
    return false;
}

static bool sc_dep_3_follow(sc_dep_in *_this) {
    /* line 37 */
    sc_token *a = _this->toks[0];
    /* line 37 */
    sc_token *b = _this->toks[1];
    /* line 37 */
    sc_token *c = _this->toks[2];
    /* line 37 */
    sc_token *o = _this->toks[3];
    /* line 38 */
    sc_edge_step(_this, o);
    /* line 39 */
    return false;
}

static bool sc_dep_4_follow(sc_dep_in *_this) {
    /* line 41 */
    sc_token *p = _this->toks[0];
    /* line 41 */
    sc_token *l = _this->toks[1];
    /* line 42 */
    sc_neuron *on = ((sc_neuron*)(sc_token_ctx(p)));
    /* line 43 */
    if (_this->active == (0 - 4)) {
        /* line 44 */
        sc_neuron_seed_mse(on, sc_g_target);
        /* line 45 */
        return false;
    }
    /* line 46 */
    sc_neuron *ln = ((sc_neuron*)(sc_token_ctx(l)));
    /* line 47 */
    ln->act = sc_mse_loss(on->act, sc_g_target);
    /* line 48 */
    sc_token_pulse(l, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 49 */
    return false;
}

static float sc_rndf(void) {
    /* line 60 */
    sc_g_seed = ((sc_g_seed * 1103515245) + 12345);
    /* line 61 */
    uint32_t r = (sc_g_seed >> 16) & 32767;
    /* line 62 */
    return (((float)(r)) / 32768.0f) - 0.5f;
}

static void sc_init_net(void) {
    /* line 65 */
    {
        int _flo0 = 0;
        int _fhi0 = (3);
        long _fc0 = 0; (void)_fc0;
        for (int _fi0 = _flo0 + 0; _fi0 < _fhi0; _fi0 += 1, _fc0++) {
            int k = _fi0;
            /* line 66 */
            sc_HID[k].nin = 2;
            /* line 67 */
            sc_HID[k].akind = sc_AK_SIGMOID;
            /* line 68 */
            sc_HID[k].bias = 0.0f;
            /* line 69 */
            sc_HID[k].w[0] = sc_rndf();
            /* line 70 */
            sc_HID[k].w[1] = sc_rndf();
        }
    }
    /* line 71 */
    sc_OUT[0].nin = 3;
    /* line 72 */
    sc_OUT[0].akind = sc_AK_IDENT;
    /* line 73 */
    sc_OUT[0].bias = 0.0f;
    /* line 74 */
    {
        int _flo1 = 0;
        int _fhi1 = (3);
        long _fc1 = 0; (void)_fc1;
        for (int _fi1 = _flo1 + 0; _fi1 < _fhi1; _fi1 += 1, _fc1++) {
            int i = _fi1;
            /* line 75 */
            sc_OUT[0].w[i] = sc_rndf();
        }
    }
    /* line 76 */
    return;
}

static void sc_zero_grad(void) {
    /* line 79 */
    {
        int _flo2 = 0;
        int _fhi2 = (2);
        long _fc2 = 0; (void)_fc2;
        for (int _fi2 = _flo2 + 0; _fi2 < _fhi2; _fi2 += 1, _fc2++) {
            int k = _fi2;
            /* line 80 */
            sc_IN[k].grad = 0.0f;
        }
    }
    /* line 81 */
    {
        int _flo3 = 0;
        int _fhi3 = (3);
        long _fc3 = 0; (void)_fc3;
        for (int _fi3 = _flo3 + 0; _fi3 < _fhi3; _fi3 += 1, _fc3++) {
            int k = _fi3;
            /* line 82 */
            sc_HID[k].grad = 0.0f;
        }
    }
    /* line 83 */
    sc_OUT[0].grad = 0.0f;
    /* line 84 */
    return;
}

static void sc_zero_gw(void) {
    /* line 87 */
    {
        int _flo4 = 0;
        int _fhi4 = (3);
        long _fc4 = 0; (void)_fc4;
        for (int _fi4 = _flo4 + 0; _fi4 < _fhi4; _fi4 += 1, _fc4++) {
            int k = _fi4;
            /* line 88 */
            sc_HID[k].gbias = 0.0f;
            /* line 89 */
            {
                int _flo5 = 0;
                int _fhi5 = (2);
                long _fc5 = 0; (void)_fc5;
                for (int _fi5 = _flo5 + 0; _fi5 < _fhi5; _fi5 += 1, _fc5++) {
                    int i = _fi5;
                    /* line 90 */
                    sc_HID[k].gw[i] = 0.0f;
                }
            }
        }
    }
    /* line 91 */
    sc_OUT[0].gbias = 0.0f;
    /* line 92 */
    {
        int _flo6 = 0;
        int _fhi6 = (3);
        long _fc6 = 0; (void)_fc6;
        for (int _fi6 = _flo6 + 0; _fi6 < _fhi6; _fi6 += 1, _fc6++) {
            int i = _fi6;
            /* line 93 */
            sc_OUT[0].gw[i] = 0.0f;
        }
    }
    /* line 94 */
    return;
}

static float sc_forward(void) {
    /* line 97 */
    sc_IN[0].act = 1.0f;
    /* line 98 */
    sc_IN[1].act = 2.0f;
    /* line 99 */
    sc_token_pulse(sc_i0, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 100 */
    sc_token_pulse(sc_i1, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 101 */
    return sc_OUT[0].act;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_neuron_init();
    sc_i0 = sc_token_bind("g.i0", NULL);
    sc_token_set_crit(sc_i0, 1, 0);
    sc_token_set_degree(sc_i0, 0, 3);
    sc_token_set_reach(sc_i0, 5);
    sc_token_set_batch(sc_i0, 2);
    sc_token_set_dom(sc_i0, 0, 0);
    sc_i1 = sc_token_bind("g.i1", NULL);
    sc_token_set_crit(sc_i1, 1, 0);
    sc_token_set_degree(sc_i1, 0, 3);
    sc_token_set_reach(sc_i1, 5);
    sc_token_set_batch(sc_i1, 2);
    sc_token_set_dom(sc_i1, 0, 0);
    sc_h0 = sc_token_bind("g.h0", NULL);
    sc_token_set_depth(sc_h0, 1);
    sc_token_set_crit(sc_h0, 1, 0);
    sc_token_set_degree(sc_h0, 2, 1);
    sc_token_set_reach(sc_h0, 2);
    sc_token_set_batch(sc_h0, 3);
    sc_token_set_dom(sc_h0, 0, 0);
    sc_h1 = sc_token_bind("g.h1", NULL);
    sc_token_set_depth(sc_h1, 1);
    sc_token_set_crit(sc_h1, 1, 0);
    sc_token_set_degree(sc_h1, 2, 1);
    sc_token_set_reach(sc_h1, 2);
    sc_token_set_batch(sc_h1, 3);
    sc_token_set_dom(sc_h1, 0, 0);
    sc_h2 = sc_token_bind("g.h2", NULL);
    sc_token_set_depth(sc_h2, 1);
    sc_token_set_crit(sc_h2, 1, 0);
    sc_token_set_degree(sc_h2, 2, 1);
    sc_token_set_reach(sc_h2, 2);
    sc_token_set_batch(sc_h2, 3);
    sc_token_set_dom(sc_h2, 0, 0);
    sc_o0 = sc_token_bind("g.o0", NULL);
    sc_token_set_depth(sc_o0, 2);
    sc_token_set_crit(sc_o0, 1, 0);
    sc_token_set_degree(sc_o0, 3, 1);
    sc_token_set_reach(sc_o0, 1);
    sc_token_set_batch(sc_o0, 1);
    sc_token_set_dom(sc_o0, 1, 1);
    sc_loss = sc_token_bind("g.loss", NULL);
    sc_token_set_depth(sc_loss, 3);
    sc_token_set_crit(sc_loss, 1, 0);
    sc_token_set_degree(sc_loss, 1, 0);
    sc_token_set_reach(sc_loss, 0);
    sc_token_set_batch(sc_loss, 1);
    sc_token_set_dom(sc_loss, 0, 0);
    { sc_token *_deps0[] = { sc_i0, sc_i1, sc_h0 }; sc_token_depend_map(_deps0, 2, 1, 1, sc_dep_0_tramp, NULL); }
    { sc_token *_deps1[] = { sc_i0, sc_i1, sc_h1 }; sc_token_depend_map(_deps1, 2, 1, 1, sc_dep_1_tramp, NULL); }
    { sc_token *_deps2[] = { sc_i0, sc_i1, sc_h2 }; sc_token_depend_map(_deps2, 2, 1, 1, sc_dep_2_tramp, NULL); }
    { sc_token *_deps3[] = { sc_h0, sc_h1, sc_h2, sc_o0 }; sc_token_depend_map(_deps3, 3, 1, 1, sc_dep_3_tramp, NULL); }
    { sc_token *_deps4[] = { sc_o0, sc_loss }; sc_token_depend_map(_deps4, 1, 1, 1, sc_dep_4_tramp, NULL); }
    /* line 104 */
    sc_init_net();
    /* line 105 */
    sc_token_form(sc_loss, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)&(sc_LOSS_N), (sc_token_exec)0);
    /* line 106 */
    sc_token_form(sc_o0, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)&(sc_OUT[0]), (sc_token_exec)0);
    /* line 107 */
    sc_token_form(sc_h0, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)&(sc_HID[0]), (sc_token_exec)0);
    /* line 108 */
    sc_token_form(sc_h1, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)&(sc_HID[1]), (sc_token_exec)0);
    /* line 109 */
    sc_token_form(sc_h2, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)&(sc_HID[2]), (sc_token_exec)0);
    /* line 110 */
    sc_token_form(sc_i0, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)&(sc_IN[0]), (sc_token_exec)0);
    /* line 111 */
    sc_token_form(sc_i1, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)&(sc_IN[1]), (sc_token_exec)0);
    /* line 113 */
    float last = 0.0f;
    /* line 114 */
    {
        int _flo7 = 0;
        int _fhi7 = (sc_EPOCHS);
        long _fc7 = 0; (void)_fc7;
        for (int _fi7 = _flo7 + 0; _fi7 < _fhi7; _fi7 += 1, _fc7++) {
            int e = _fi7;
            /* line 115 */
            sc_zero_grad();
            /* line 116 */
            sc_zero_gw();
            /* line 117 */
            float pred = sc_forward();
            /* line 118 */
            float d = pred - sc_g_target;
            /* line 119 */
            last = (d * d);
            /* line 120 */
            sc_token_back(sc_loss, (sc_thin){(void *)0, (int32_t *)0, (void (*)(void *))0}, 0);
            /* line 121 */
            int32_t t = e + 1;
            /* line 122 */
            {
                int _flo8 = 0;
                int _fhi8 = (3);
                long _fc8 = 0; (void)_fc8;
                for (int _fi8 = _flo8 + 0; _fi8 < _fhi8; _fi8 += 1, _fc8++) {
                    int k = _fi8;
                    /* line 123 */
                    sc_neuron_adam(&sc_HID[k], sc_LR, sc_B1, sc_B2, sc_EPS, t);
                }
            }
            /* line 124 */
            sc_neuron_adam(&sc_OUT[0], sc_LR, sc_B1, sc_B2, sc_EPS, t);
        }
    }
    /* line 126 */
    float pred = sc_forward();
    /* line 127 */
    printf("loss_small=%d pred_close=%d\n", ((int32_t)(last < 0.01f)), ((int32_t)(pred > 4.8f)));
    /* line 128 */
    {
        int32_t _ret = 0;
        sc_mod_neuron_drop();
        return _ret;
    }
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
