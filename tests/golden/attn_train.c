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
    SC_CONSOLE_UTF8();
    sc_mod_ts_init();
    sc_mod_nn_init();
    /* line 11 */
    rand_seed(((int64_t)(5)));
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
    tensor *idxt = from_data(1, ((void*)(sidx)), ((void*)(ib)), DT_F4);
    /* line 23 */
    int32_t stg[1];
    /* line 24 */
    stg[0] = 1;
    /* line 25 */
    float tgb[1];
    /* line 26 */
    tgb[0] = 1.0;
    /* line 27 */
    tensor *tt = from_data(1, ((void*)(stg)), ((void*)(tgb)), DT_F4);
    /* line 29 */
    embed *emb = nn_embedding(6, 4);
    /* line 30 */
    linear *fc = nn_linear(12, 2);
    /* line 31 */
    optim *opt = nn_adam(0.02);
    /* line 32 */
    optim_track_embedding(opt, emb);
    /* line 33 */
    optim_track_linear(opt, fc);
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
            val *idx = nn_input(idxt);
            /* line 47 */
            val *tg = nn_input(tt);
            /* line 48 */
            val *e0 = embed_forward(emb, idx);
            /* line 49 */
            val *seq = val_reshape(e0, 3, ((void*)(s3)));
            /* line 50 */
            val *seqt = val_transpose(seq);
            /* line 51 */
            val *score = val_bmm(seq, seqt);
            /* line 52 */
            val *ss = val_scale(score, 0.5);
            /* line 53 */
            val *attn = val_softmax(ss, -(1));
            /* line 54 */
            val *ctx = val_bmm(attn, seq);
            /* line 55 */
            val *nrm = val_rms_norm(ctx, -(1), 0.00001);
            /* line 56 */
            val *dp = val_dropout(nrm, 0.0, 0);
            /* line 57 */
            val *fl = val_reshape(dp, 2, ((void*)(sf)));
            /* line 58 */
            val *logit = linear_forward(fc, fl);
            /* line 59 */
            val *loss = val_cross_entropy(logit, tg);
            /* line 60 */
            val_backward(loss);
            /* line 61 */
            optim_step(opt);
            /* line 62 */
            optim_zero_grad(opt);
            /* line 63 */
            last = val_item(loss);
            /* line 64 */
            if (e == 0) {
                /* line 65 */
                first = last;
            }
            /* line 66 */
            nn_tape_clear();
        }
    }
    /* line 67 */
    printf("attn_down=%d attn_low=%d\n", ((int32_t)(last < first)), ((int32_t)(last < 0.1)));
    /* line 69 */
    {
        int32_t _ret = 0;
        if (opt) { optim_drop(opt); sc_free(opt); }
        if (fc) { linear_drop(fc); sc_free(fc); }
        if (emb) { embed_drop(emb); sc_free(emb); }
        if (tt) { tensor_drop(tt); sc_free(tt); }
        if (idxt) { tensor_drop(idxt); sc_free(idxt); }
        sc_mod_nn_drop();
        sc_mod_ts_drop();
        return _ret;
    }
}
