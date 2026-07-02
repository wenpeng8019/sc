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
    /* line 11 */
    sc_rand_seed(((int64_t)(5)));
    /* line 14 */
    int32_t sidx[1];
    /* line 15 */
    sidx[0] = 3;
    /* line 16 */
    float ib[3];
    /* line 17 */
    ib[0] = 1.0;
    /* line 18 */
    ib[1] = 3.0;
    /* line 19 */
    ib[2] = 5.0;
    /* line 20 */
    sc_tensor *idxt = sc_from_data(1, ((void*)(sidx)), ((void*)(ib)), sc_DT_F4);
    /* line 23 */
    int32_t stg[1];
    /* line 24 */
    stg[0] = 1;
    /* line 25 */
    float tgb[1];
    /* line 26 */
    tgb[0] = 1.0;
    /* line 27 */
    sc_tensor *tt = sc_from_data(1, ((void*)(stg)), ((void*)(tgb)), sc_DT_F4);
    /* line 29 */
    sc_embed *emb = sc_nn_embedding(6, 4);
    /* line 30 */
    sc_linear *fc = sc_nn_linear(12, 2);
    /* line 31 */
    sc_optim *opt = sc_nn_adam(0.02);
    /* line 32 */
    sc_optim_track_embedding(opt, emb);
    /* line 33 */
    sc_optim_track_linear(opt, fc);
    /* line 35 */
    int32_t s3[3];
    /* line 36 */
    s3[0] = 1;
    /* line 37 */
    s3[1] = 3;
    /* line 38 */
    s3[2] = 4;
    /* line 39 */
    int32_t sf[2];
    /* line 40 */
    sf[0] = 1;
    /* line 41 */
    sf[1] = 12;
    /* line 43 */
    double first = 0.0;
    /* line 44 */
    double last = 0.0;
    /* line 45 */
    {
        int _flo0 = 0;
        int _fhi0 = (150);
        long _fc0 = 0; (void)_fc0;
        for (int _fi0 = _flo0 + 0; _fi0 < _fhi0; _fi0 += 1, _fc0++) {
            int e = _fi0;
            /* line 46 */
            sc_val *idx = sc_nn_input(idxt);
            /* line 47 */
            sc_val *tg = sc_nn_input(tt);
            /* line 48 */
            sc_val *e0 = sc_embed_forward(emb, idx);
            /* line 49 */
            sc_val *seq = sc_val_reshape(e0, 3, ((void*)(s3)));
            /* line 50 */
            sc_val *seqt = sc_val_transpose(seq);
            /* line 51 */
            sc_val *score = sc_val_bmm(seq, seqt);
            /* line 52 */
            sc_val *ss = sc_val_scale(score, 0.5);
            /* line 53 */
            sc_val *attn = sc_val_softmax(ss, -(1));
            /* line 54 */
            sc_val *ctx = sc_val_bmm(attn, seq);
            /* line 55 */
            sc_val *nrm = sc_val_rms_norm(ctx, -(1), 0.00001);
            /* line 56 */
            sc_val *dp = sc_val_dropout(nrm, 0.0, 0);
            /* line 57 */
            sc_val *fl = sc_val_reshape(dp, 2, ((void*)(sf)));
            /* line 58 */
            sc_val *logit = sc_linear_forward(fc, fl);
            /* line 59 */
            sc_val *loss = sc_val_cross_entropy(logit, tg);
            /* line 60 */
            sc_val_backward(loss);
            /* line 61 */
            sc_optim_step(opt);
            /* line 62 */
            sc_optim_zero_grad(opt);
            /* line 63 */
            last = sc_val_item(loss);
            /* line 64 */
            if (e == 0) {
                /* line 65 */
                first = last;
            }
            /* line 66 */
            sc_nn_tape_clear();
        }
    }
    /* line 67 */
    printf("attn_down=%d attn_low=%d\n", ((int32_t)(last < first)), ((int32_t)(last < 0.1)));
    /* line 69 */
    {
        int32_t _ret = 0;
        if (opt) { sc_optim_drop(opt); sc_free(opt); }
        if (fc) { sc_linear_drop(fc); sc_free(fc); }
        if (emb) { sc_embed_drop(emb); sc_free(emb); }
        if (tt) { sc_tensor_drop(tt); sc_free(tt); }
        if (idxt) { sc_tensor_drop(idxt); sc_free(idxt); }
        sc_mod_nn_drop();
        sc_mod_ts_drop();
        return _ret;
    }
}
