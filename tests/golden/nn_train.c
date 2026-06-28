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
    rand_seed(((int64_t)(7)));
    /* line 12 */
    int32_t sx[2];
    /* line 13 */
    sx[0] = 1;
    /* line 14 */
    sx[1] = 2;
    /* line 15 */
    float xb[2];
    /* line 16 */
    xb[0] = 1.0;
    /* line 17 */
    xb[1] = 2.0;
    /* line 18 */
    float tb[2];
    /* line 19 */
    tb[0] = 5.0;
    /* line 20 */
    tb[1] = 8.0;
    /* line 21 */
    tensor *xt = from_data(2, sx, ((void*)(xb)), DT_F4);
    /* line 22 */
    tensor *tt = from_data(2, sx, ((void*)(tb)), DT_F4);
    /* line 24 */
    linear *fc1 = nn_linear(2, 4);
    /* line 25 */
    linear *fc2 = nn_linear(4, 2);
    /* line 26 */
    optim *opt = nn_sgd(0.05);
    /* line 27 */
    optim_track_linear(opt, fc1);
    /* line 28 */
    optim_track_linear(opt, fc2);
    /* line 30 */
    double last = 0.0;
    /* line 31 */
    {
        int _flo0 = 0;
        int _fhi0 = (100);
        long _fc0 = 0; (void)_fc0;
        for (int _fi0 = _flo0 + 0; _fi0 < _fhi0; _fi0 += 1, _fc0++) {
            int e = _fi0;
            /* line 32 */
            val *x = nn_input(xt);
            /* line 33 */
            val *tgt = nn_input(tt);
            /* line 34 */
            val *h = linear_forward(fc1, x);
            /* line 35 */
            val *hr = val_relu(h);
            /* line 36 */
            val *y = linear_forward(fc2, hr);
            /* line 37 */
            val *loss = val_mse_loss(y, tgt);
            /* line 38 */
            val_backward(loss);
            /* line 39 */
            optim_step(opt);
            /* line 40 */
            optim_zero_grad(opt);
            /* line 41 */
            last = val_item(loss);
            /* line 42 */
            nn_tape_clear();
        }
    }
    /* line 43 */
    printf("trained loss<1=%d\n", ((int32_t)(last < 1.0)));
    /* line 45 */
    val *x2 = nn_input(xt);
    /* line 46 */
    val *h2 = linear_forward(fc1, x2);
    /* line 47 */
    val *hr2 = val_relu(h2);
    /* line 48 */
    val *y2 = linear_forward(fc2, hr2);
    /* line 49 */
    tensor *yv = val_value(y2);
    /* line 50 */
    printf("y0_close=%d y1_close=%d\n", ((int32_t)(tensor_at(yv, 0) > 4.0)), ((int32_t)(tensor_at(yv, 1) > 7.0)));
    /* line 51 */
    nn_tape_clear();
    /* line 53 */
    {
        int32_t _ret = 0;
        if (yv) { tensor_drop(yv); sc_free(yv); }
        if (opt) { optim_drop(opt); sc_free(opt); }
        if (fc2) { linear_drop(fc2); sc_free(fc2); }
        if (fc1) { linear_drop(fc1); sc_free(fc1); }
        if (tt) { tensor_drop(tt); sc_free(tt); }
        if (xt) { tensor_drop(xt); sc_free(xt); }
        sc_mod_nn_drop();
        sc_mod_ts_drop();
        return _ret;
    }
}
