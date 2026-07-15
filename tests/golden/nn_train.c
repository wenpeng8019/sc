/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/ts/ts.h"
#include "builtins/nn/nn.h"

typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_mod_ts_init(void); void sc_mod_ts_drop(void);
void sc_mod_nn_init(void); void sc_mod_nn_drop(void);

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_ts_init();
    sc_mod_nn_init();
    /* line 10 */
    sc_rand_seed(((int64_t)(7)));
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
    sc_tensor *xt = sc_from_data(2, sx, ((void*)(xb)), sc_DT_F4);
    /* line 22 */
    sc_tensor *tt = sc_from_data(2, sx, ((void*)(tb)), sc_DT_F4);
    /* line 24 */
    sc_linear *fc1 = sc_nn_linear(2, 4);
    /* line 25 */
    sc_linear *fc2 = sc_nn_linear(4, 2);
    /* line 26 */
    sc_optim *opt = sc_nn_sgd(0.05);
    /* line 27 */
    sc_optim_track_linear(opt, fc1);
    /* line 28 */
    sc_optim_track_linear(opt, fc2);
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
            sc_val *x = sc_nn_input(xt);
            /* line 33 */
            sc_val *tgt = sc_nn_input(tt);
            /* line 34 */
            sc_val *h = sc_linear_forward(fc1, x);
            /* line 35 */
            sc_val *hr = sc_val_relu(h);
            /* line 36 */
            sc_val *y = sc_linear_forward(fc2, hr);
            /* line 37 */
            sc_val *loss = sc_val_mse_loss(y, tgt);
            /* line 38 */
            sc_val_backward(loss);
            /* line 39 */
            sc_optim_step(opt);
            /* line 40 */
            sc_optim_zero_grad(opt);
            /* line 41 */
            last = sc_val_item(loss);
            /* line 42 */
            sc_nn_tape_clear();
        }
    }
    /* line 43 */
    printf("trained loss<1=%d\n", ((int32_t)(last < 1.0)));
    /* line 45 */
    sc_val *x2 = sc_nn_input(xt);
    /* line 46 */
    sc_val *h2 = sc_linear_forward(fc1, x2);
    /* line 47 */
    sc_val *hr2 = sc_val_relu(h2);
    /* line 48 */
    sc_val *y2 = sc_linear_forward(fc2, hr2);
    /* line 49 */
    sc_tensor *yv = sc_val_value(y2);
    /* line 50 */
    int32_t _sq0 = ((int32_t)(sc_tensor_at(yv, 0) > 4.0));
    int32_t _sq1 = ((int32_t)(sc_tensor_at(yv, 1) > 7.0));
    printf("y0_close=%d y1_close=%d\n", _sq0, _sq1);
    /* line 51 */
    sc_nn_tape_clear();
    /* line 53 */
    {
        int32_t _ret = 0;
        sc_ptr_drop_slot((void *)&(yv), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(opt), (void (*)(void *))sc_optim_drop);
        sc_ptr_drop_slot((void *)&(fc2), (void (*)(void *))sc_linear_drop);
        sc_ptr_drop_slot((void *)&(fc1), (void (*)(void *))sc_linear_drop);
        sc_ptr_drop_slot((void *)&(tt), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(xt), (void (*)(void *))sc_tensor_drop);
        sc_mod_nn_drop();
        sc_mod_ts_drop();
        return _ret;
    }
}
