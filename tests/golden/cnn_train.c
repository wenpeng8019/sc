/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/ts/ts.h"
#include "builtins/nn/nn.h"

typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


void sc_mod_ts_init(void); void sc_mod_ts_drop(void);
void sc_mod_nn_init(void); void sc_mod_nn_drop(void);

int32_t main(void) {
    sc_mod_ts_init();
    sc_mod_nn_init();
    /* line 10 */
    rand_seed(((int64_t)(3)));
    /* line 13 */
    int32_t sx[4];
    /* line 14 */
    sx[0] = 2;
    /* line 15 */
    sx[1] = 1;
    /* line 16 */
    sx[2] = 5;
    /* line 17 */
    sx[3] = 5;
    /* line 18 */
    tensor *xt = rand_normal(4, ((void*)(sx)), 0.0, 1.0, DT_F4);
    /* line 21 */
    int32_t stg[1];
    /* line 22 */
    stg[0] = 2;
    /* line 23 */
    float tb[2];
    /* line 24 */
    tb[0] = 0.0;
    /* line 25 */
    tb[1] = 1.0;
    /* line 26 */
    tensor *tt = from_data(1, ((void*)(stg)), ((void*)(tb)), DT_F4);
    /* line 28 */
    conv *conv = nn_conv2d(1, 3, 3, 3, 1, 1, 0, 0);
    /* line 29 */
    linear *fc = nn_linear(12, 2);
    /* line 30 */
    optim *opt = nn_adam(0.02);
    /* line 31 */
    optim_track_conv2d(opt, conv);
    /* line 32 */
    optim_track_linear(opt, fc);
    /* line 34 */
    int32_t fsh[2];
    /* line 35 */
    fsh[0] = 2;
    /* line 36 */
    fsh[1] = 12;
    /* line 38 */
    double first = 0.0;
    /* line 39 */
    double last = 0.0;
    /* line 40 */
    {
        int _flo0 = 0;
        int _fhi0 = (150);
        long _fc0 = 0; (void)_fc0;
        for (int _fi0 = _flo0 + 0; _fi0 < _fhi0; _fi0 += 1, _fc0++) {
            int e = _fi0;
            /* line 41 */
            val *x = nn_input(xt);
            /* line 42 */
            val *tg = nn_input(tt);
            /* line 43 */
            val *c = conv_forward(conv, x);
            /* line 44 */
            val *cr = val_relu(c);
            /* line 45 */
            val *p = val_max_pool2d(cr, 2, 2, 1, 1, 0, 0);
            /* line 46 */
            val *fl = val_reshape(p, 2, ((void*)(fsh)));
            /* line 47 */
            val *logit = linear_forward(fc, fl);
            /* line 48 */
            val *loss = val_cross_entropy(logit, tg);
            /* line 49 */
            val_backward(loss);
            /* line 50 */
            optim_step(opt);
            /* line 51 */
            optim_zero_grad(opt);
            /* line 52 */
            last = val_item(loss);
            /* line 53 */
            if (e == 0) {
                /* line 54 */
                first = last;
            }
            /* line 55 */
            nn_tape_clear();
        }
    }
    /* line 56 */
    printf("cnn_down=%d cnn_low=%d\n", ((int32_t)(last < first)), ((int32_t)(last < 0.1)));
    /* line 58 */
    {
        int32_t _ret = 0;
        if (opt) { optim_drop(opt); sc_free(opt); }
        if (fc) { linear_drop(fc); sc_free(fc); }
        if (conv) { conv_drop(conv); sc_free(conv); }
        if (tt) { tensor_drop(tt); sc_free(tt); }
        if (xt) { tensor_drop(xt); sc_free(xt); }
        sc_mod_nn_drop();
        sc_mod_ts_drop();
        return _ret;
    }
}
