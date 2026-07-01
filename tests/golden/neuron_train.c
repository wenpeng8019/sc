/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "scm__Users_wenpeng_dev_c_sc_builtins_neuron_neuron_sc.h"

static const float LR = 0.05f;
static const float B1 = 0.9f;
static const float B2 = 0.999f;
static const float EPS = 0.00000001f;
static const int32_t EPOCHS = 400;
static token *i0 = {0};
static token *i1 = {0};
static token *h0 = {0};
static token *h1 = {0};
static token *h2 = {0};
static token *o0 = {0};
static token *loss = {0};
static uint8_t __scdep_0_follow(__scdep_in *_this);
static uint8_t __scdep_1_follow(__scdep_in *_this);
static uint8_t __scdep_2_follow(__scdep_in *_this);
static uint8_t __scdep_3_follow(__scdep_in *_this);
static uint8_t __scdep_4_follow(__scdep_in *_this);
static neuron IN[2] = {0};
static neuron HID[3] = {0};
static neuron OUT[1] = {0};
static neuron LOSS_N = {0};
static float g_target = 5.0f;
static uint32_t g_seed = 22695477;
static float rndf(void);
static void init_net(void);
static void zero_grad(void);
static void zero_gw(void);
static float forward(void);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


void sc_mod_neuron_init(void); void sc_mod_neuron_drop(void);
static int __scdep_0_tramp(token **, int, int, void *);
static int __scdep_1_tramp(token **, int, int, void *);
static int __scdep_2_tramp(token **, int, int, void *);
static int __scdep_3_tramp(token **, int, int, void *);
static int __scdep_4_tramp(token **, int, int, void *);

static uint8_t __scdep_0_follow(__scdep_in *_this) {
    /* line 25 */
    token *a = _this->toks[0];
    /* line 25 */
    token *b = _this->toks[1];
    /* line 25 */
    token *c = _this->toks[2];
    /* line 26 */
    edge_step(_this, c);
    /* line 27 */
    return false;
}

static uint8_t __scdep_1_follow(__scdep_in *_this) {
    /* line 29 */
    token *a = _this->toks[0];
    /* line 29 */
    token *b = _this->toks[1];
    /* line 29 */
    token *c = _this->toks[2];
    /* line 30 */
    edge_step(_this, c);
    /* line 31 */
    return false;
}

static uint8_t __scdep_2_follow(__scdep_in *_this) {
    /* line 33 */
    token *a = _this->toks[0];
    /* line 33 */
    token *b = _this->toks[1];
    /* line 33 */
    token *c = _this->toks[2];
    /* line 34 */
    edge_step(_this, c);
    /* line 35 */
    return false;
}

static uint8_t __scdep_3_follow(__scdep_in *_this) {
    /* line 37 */
    token *a = _this->toks[0];
    /* line 37 */
    token *b = _this->toks[1];
    /* line 37 */
    token *c = _this->toks[2];
    /* line 37 */
    token *o = _this->toks[3];
    /* line 38 */
    edge_step(_this, o);
    /* line 39 */
    return false;
}

static uint8_t __scdep_4_follow(__scdep_in *_this) {
    /* line 41 */
    token *p = _this->toks[0];
    /* line 41 */
    token *l = _this->toks[1];
    /* line 42 */
    neuron *on = ((neuron*)(token_ctx(p)));
    /* line 43 */
    if (_this->active == (0 - 4)) {
        /* line 44 */
        neuron_seed_mse(on, g_target);
        /* line 45 */
        return false;
    }
    /* line 46 */
    neuron *ln = ((neuron*)(token_ctx(l)));
    /* line 47 */
    ln->act = mse_loss(on->act, g_target);
    /* line 48 */
    token_pulse(l, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 49 */
    return false;
}

static float rndf(void) {
    /* line 60 */
    g_seed = ((g_seed * 1103515245) + 12345);
    /* line 61 */
    uint32_t r = (g_seed >> 16) & 32767;
    /* line 62 */
    return (((float)(r)) / 32768.0f) - 0.5f;
}

static void init_net(void) {
    /* line 65 */
    {
        int _flo0 = 0;
        int _fhi0 = (3);
        long _fc0 = 0; (void)_fc0;
        for (int _fi0 = _flo0 + 0; _fi0 < _fhi0; _fi0 += 1, _fc0++) {
            int k = _fi0;
            /* line 66 */
            HID[k].nin = 2;
            /* line 67 */
            HID[k].akind = AK_SIGMOID;
            /* line 68 */
            HID[k].bias = 0.0f;
            /* line 69 */
            HID[k].w[0] = rndf();
            /* line 70 */
            HID[k].w[1] = rndf();
        }
    }
    /* line 71 */
    OUT[0].nin = 3;
    /* line 72 */
    OUT[0].akind = AK_IDENT;
    /* line 73 */
    OUT[0].bias = 0.0f;
    /* line 74 */
    {
        int _flo1 = 0;
        int _fhi1 = (3);
        long _fc1 = 0; (void)_fc1;
        for (int _fi1 = _flo1 + 0; _fi1 < _fhi1; _fi1 += 1, _fc1++) {
            int i = _fi1;
            /* line 75 */
            OUT[0].w[i] = rndf();
        }
    }
    /* line 76 */
    return;
}

static void zero_grad(void) {
    /* line 79 */
    {
        int _flo2 = 0;
        int _fhi2 = (2);
        long _fc2 = 0; (void)_fc2;
        for (int _fi2 = _flo2 + 0; _fi2 < _fhi2; _fi2 += 1, _fc2++) {
            int k = _fi2;
            /* line 80 */
            IN[k].grad = 0.0f;
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
            HID[k].grad = 0.0f;
        }
    }
    /* line 83 */
    OUT[0].grad = 0.0f;
    /* line 84 */
    return;
}

static void zero_gw(void) {
    /* line 87 */
    {
        int _flo4 = 0;
        int _fhi4 = (3);
        long _fc4 = 0; (void)_fc4;
        for (int _fi4 = _flo4 + 0; _fi4 < _fhi4; _fi4 += 1, _fc4++) {
            int k = _fi4;
            /* line 88 */
            HID[k].gbias = 0.0f;
            /* line 89 */
            {
                int _flo5 = 0;
                int _fhi5 = (2);
                long _fc5 = 0; (void)_fc5;
                for (int _fi5 = _flo5 + 0; _fi5 < _fhi5; _fi5 += 1, _fc5++) {
                    int i = _fi5;
                    /* line 90 */
                    HID[k].gw[i] = 0.0f;
                }
            }
        }
    }
    /* line 91 */
    OUT[0].gbias = 0.0f;
    /* line 92 */
    {
        int _flo6 = 0;
        int _fhi6 = (3);
        long _fc6 = 0; (void)_fc6;
        for (int _fi6 = _flo6 + 0; _fi6 < _fhi6; _fi6 += 1, _fc6++) {
            int i = _fi6;
            /* line 93 */
            OUT[0].gw[i] = 0.0f;
        }
    }
    /* line 94 */
    return;
}

static float forward(void) {
    /* line 97 */
    IN[0].act = 1.0f;
    /* line 98 */
    IN[1].act = 2.0f;
    /* line 99 */
    token_pulse(i0, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 100 */
    token_pulse(i1, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0);
    /* line 101 */
    return OUT[0].act;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_neuron_init();
    i0 = token_bind("g.i0", NULL);
    token_set_crit(i0, 1, 0);
    token_set_degree(i0, 0, 3);
    token_set_reach(i0, 5);
    token_set_batch(i0, 2);
    token_set_dom(i0, 0, 0);
    i1 = token_bind("g.i1", NULL);
    token_set_crit(i1, 1, 0);
    token_set_degree(i1, 0, 3);
    token_set_reach(i1, 5);
    token_set_batch(i1, 2);
    token_set_dom(i1, 0, 0);
    h0 = token_bind("g.h0", NULL);
    token_set_depth(h0, 1);
    token_set_crit(h0, 1, 0);
    token_set_degree(h0, 2, 1);
    token_set_reach(h0, 2);
    token_set_batch(h0, 3);
    token_set_dom(h0, 0, 0);
    h1 = token_bind("g.h1", NULL);
    token_set_depth(h1, 1);
    token_set_crit(h1, 1, 0);
    token_set_degree(h1, 2, 1);
    token_set_reach(h1, 2);
    token_set_batch(h1, 3);
    token_set_dom(h1, 0, 0);
    h2 = token_bind("g.h2", NULL);
    token_set_depth(h2, 1);
    token_set_crit(h2, 1, 0);
    token_set_degree(h2, 2, 1);
    token_set_reach(h2, 2);
    token_set_batch(h2, 3);
    token_set_dom(h2, 0, 0);
    o0 = token_bind("g.o0", NULL);
    token_set_depth(o0, 2);
    token_set_crit(o0, 1, 0);
    token_set_degree(o0, 3, 1);
    token_set_reach(o0, 1);
    token_set_batch(o0, 1);
    token_set_dom(o0, 1, 1);
    loss = token_bind("g.loss", NULL);
    token_set_depth(loss, 3);
    token_set_crit(loss, 1, 0);
    token_set_degree(loss, 1, 0);
    token_set_reach(loss, 0);
    token_set_batch(loss, 1);
    token_set_dom(loss, 0, 0);
    { token *_deps0[] = { i0, i1, h0 }; token_depend_map(_deps0, 2, 1, 1, __scdep_0_tramp, NULL); }
    { token *_deps1[] = { i0, i1, h1 }; token_depend_map(_deps1, 2, 1, 1, __scdep_1_tramp, NULL); }
    { token *_deps2[] = { i0, i1, h2 }; token_depend_map(_deps2, 2, 1, 1, __scdep_2_tramp, NULL); }
    { token *_deps3[] = { h0, h1, h2, o0 }; token_depend_map(_deps3, 3, 1, 1, __scdep_3_tramp, NULL); }
    { token *_deps4[] = { o0, loss }; token_depend_map(_deps4, 1, 1, 1, __scdep_4_tramp, NULL); }
    /* line 104 */
    init_net();
    /* line 105 */
    token_form(loss, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)&(LOSS_N), (token_exec)0);
    /* line 106 */
    token_form(o0, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)&(OUT[0]), (token_exec)0);
    /* line 107 */
    token_form(h0, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)&(HID[0]), (token_exec)0);
    /* line 108 */
    token_form(h1, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)&(HID[1]), (token_exec)0);
    /* line 109 */
    token_form(h2, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)&(HID[2]), (token_exec)0);
    /* line 110 */
    token_form(i0, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)&(IN[0]), (token_exec)0);
    /* line 111 */
    token_form(i1, ((sc_thin){(void *)(intptr_t)(0), (int32_t *)0, (void (*)(void *))0}), 0, (void*)&(IN[1]), (token_exec)0);
    /* line 113 */
    float last = 0.0f;
    /* line 114 */
    {
        int _flo7 = 0;
        int _fhi7 = (EPOCHS);
        long _fc7 = 0; (void)_fc7;
        for (int _fi7 = _flo7 + 0; _fi7 < _fhi7; _fi7 += 1, _fc7++) {
            int e = _fi7;
            /* line 115 */
            zero_grad();
            /* line 116 */
            zero_gw();
            /* line 117 */
            float pred = forward();
            /* line 118 */
            float d = pred - g_target;
            /* line 119 */
            last = (d * d);
            /* line 120 */
            token_back(loss, (sc_thin){(void *)0, (int32_t *)0, (void (*)(void *))0}, 0);
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
                    neuron_adam(&HID[k], LR, B1, B2, EPS, t);
                }
            }
            /* line 124 */
            neuron_adam(&OUT[0], LR, B1, B2, EPS, t);
        }
    }
    /* line 126 */
    float pred = forward();
    /* line 127 */
    printf("loss_small=%d pred_close=%d\n", ((int32_t)(last < 0.01f)), ((int32_t)(pred > 4.8f)));
    /* line 128 */
    {
        int32_t _ret = 0;
        sc_mod_neuron_drop();
        return _ret;
    }
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
