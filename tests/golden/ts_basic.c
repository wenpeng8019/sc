/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/ts/ts.h"

typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_mod_ts_init(void); void sc_mod_ts_drop(void);

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_ts_init();
    /* line 12 */
    int32_t shp[2];
    /* line 13 */
    shp[0] = 2;
    /* line 14 */
    shp[1] = 3;
    /* line 15 */
    sc_tensor *a = sc_zeros(2, shp, sc_DT_F4);
    /* line 16 */
    sc_tensor_fill(a, 2.0);
    /* line 17 */
    printf("a numel=%lld sum=%g\n", a->numel, sc_tensor_sum_all(a));
    /* line 20 */
    sc_tensor *b = sc_arange(0.0, 6.0, 1.0, sc_DT_F8);
    /* line 21 */
    sc_tensor *b2 = sc_tensor_reshape(b, 2, shp);
    /* line 22 */
    int32_t _sq0 = sc_tensor_dim(b2, 0);
    int32_t _sq1 = sc_tensor_dim(b2, 1);
    double _sq2 = sc_tensor_at(b2, 5);
    int32_t _sq3 = ((int32_t)(sc_tensor_is_contiguous(b2)));
    printf("b2 dim0=%d dim1=%d at5=%g dtype=%d contig=%d\n", _sq0, _sq1, _sq2, b2->dtype, _sq3);
    /* line 25 */
    sc_tensor *c = sc_tensor_add(a, b2);
    /* line 26 */
    printf("c sum=%g\n", sc_tensor_sum_all(c));
    /* line 29 */
    int32_t rsh[2];
    /* line 30 */
    rsh[0] = 1;
    /* line 31 */
    rsh[1] = 3;
    /* line 32 */
    sc_tensor *row = sc_ones(2, rsh, sc_DT_F8);
    /* line 33 */
    sc_tensor *bc = sc_tensor_add(b2, row);
    /* line 34 */
    double _sq4 = sc_tensor_at(bc, 0);
    double _sq5 = sc_tensor_at(bc, 5);
    printf("bc at0=%g at5=%g\n", _sq4, _sq5);
    /* line 37 */
    int32_t sh32[2];
    /* line 38 */
    sh32[0] = 3;
    /* line 39 */
    sh32[1] = 2;
    /* line 40 */
    sc_tensor *d = sc_ones(2, sh32, sc_DT_F8);
    /* line 41 */
    sc_tensor *mm = sc_tensor_matmul(b2, d);
    /* line 42 */
    int32_t _sq6 = sc_tensor_dim(mm, 0);
    int32_t _sq7 = sc_tensor_dim(mm, 1);
    double _sq8 = sc_tensor_at(mm, 0);
    double _sq9 = sc_tensor_at(mm, 3);
    printf("mm shape=%dx%d at0=%g at3=%g\n", _sq6, _sq7, _sq8, _sq9);
    /* line 45 */
    sc_tensor *sm = sc_tensor_sum(b2, 1, false);
    /* line 46 */
    double _sq10 = sc_tensor_at(sm, 0);
    double _sq11 = sc_tensor_at(sm, 1);
    printf("rowsum0=%g rowsum1=%g ndim=%d\n", _sq10, _sq11, sm->ndim);
    /* line 49 */
    sc_tensor *mn = sc_tensor_mean(b2, 1, true);
    /* line 50 */
    int32_t _sq12 = sc_tensor_dim(mn, 1);
    double _sq13 = sc_tensor_at(mn, 0);
    double _sq14 = sc_tensor_at(mn, 1);
    printf("mean ndim=%d dim1=%d at0=%g at1=%g\n", mn->ndim, _sq12, _sq13, _sq14);
    /* line 53 */
    sc_tensor *am = sc_tensor_argmax(b, -(1), false);
    /* line 54 */
    printf("argmax=%g amdtype=%d\n", sc_tensor_at(am, 0), am->dtype);
    /* line 57 */
    sc_tensor *ng = sc_tensor_neg(b);
    /* line 58 */
    sc_tensor *rl = sc_tensor_relu(ng);
    /* line 59 */
    printf("relu sum=%g\n", sc_tensor_sum_all(rl));
    /* line 62 */
    sc_tensor *t = sc_tensor_transpose(b2);
    /* line 63 */
    int32_t _sq15 = sc_tensor_dim(t, 0);
    int32_t _sq16 = sc_tensor_dim(t, 1);
    double _sq17 = sc_tensor_at(t, 0);
    double _sq18 = sc_tensor_at(t, 5);
    int32_t _sq19 = ((int32_t)(sc_tensor_is_contiguous(t)));
    printf("t shape=%dx%d at0=%g at5=%g contig=%d\n", _sq15, _sq16, _sq17, _sq18, _sq19);
    /* line 64 */
    sc_tensor *tc = sc_tensor_contiguous(t);
    /* line 65 */
    int32_t _sq20 = ((int32_t)(sc_tensor_is_contiguous(tc)));
    double _sq21 = sc_tensor_at(tc, 1);
    printf("tc contig=%d at1=%g\n", _sq20, _sq21);
    /* line 68 */
    sc_tensor *ey = sc_eye(3, sc_DT_F4);
    /* line 69 */
    double _sq22 = sc_tensor_sum_all(ey);
    double _sq23 = sc_tensor_at(ey, 4);
    printf("eye sum=%g diag4=%g\n", _sq22, _sq23);
    /* line 72 */
    sc_tensor *ls = sc_linspace(0.0 - 2.0, 2.0, 5, sc_DT_F8);
    /* line 73 */
    sc_tensor *ab = sc_tensor_abs(ls);
    /* line 74 */
    sc_tensor *cl = sc_tensor_clip(ls, 0.0 - 1.0, 1.0);
    /* line 75 */
    double _sq24 = sc_tensor_at(ls, 0);
    double _sq25 = sc_tensor_at(ls, 4);
    double _sq26 = sc_tensor_at(ab, 0);
    double _sq27 = sc_tensor_at(cl, 0);
    double _sq28 = sc_tensor_at(cl, 4);
    printf("ls at0=%g at4=%g abs at0=%g clip at0=%g at4=%g\n", _sq24, _sq25, _sq26, _sq27, _sq28);
    /* line 78 */
    sc_tensor *gt = sc_tensor_gt(b2, a);
    /* line 79 */
    printf("gt dtype=%d sum=%g\n", gt->dtype, sc_tensor_sum_all(gt));
    /* line 80 */
    sc_tensor *wh = sc_where(gt, b2, a);
    /* line 81 */
    double _sq29 = sc_tensor_at(wh, 0);
    double _sq30 = sc_tensor_at(wh, 5);
    printf("where at0=%g at5=%g\n", _sq29, _sq30);
    /* line 84 */
    sc_tensor *sl = sc_tensor_slice(b, 0, 1, 5, 2);
    /* line 85 */
    double _sq31 = sc_tensor_at(sl, 0);
    double _sq32 = sc_tensor_at(sl, 1);
    printf("slice numel=%lld at0=%g at1=%g\n", sl->numel, _sq31, _sq32);
    /* line 88 */
    sc_tensor *r0 = sc_tensor_select(b2, 0, 0);
    /* line 89 */
    double _sq33 = sc_tensor_at(r0, 0);
    double _sq34 = sc_tensor_at(r0, 2);
    printf("select ndim=%d at0=%g at2=%g\n", r0->ndim, _sq33, _sq34);
    /* line 92 */
    sc_tensor *fl = sc_tensor_flip(b, 0);
    /* line 93 */
    double _sq35 = sc_tensor_at(fl, 0);
    double _sq36 = sc_tensor_at(fl, 5);
    printf("flip at0=%g at5=%g\n", _sq35, _sq36);
    /* line 96 */
    sc_tensor *cs = sc_tensor_cumsum(b, 0);
    /* line 97 */
    double _sq37 = sc_tensor_at(cs, 0);
    double _sq38 = sc_tensor_at(cs, 5);
    printf("cumsum at0=%g at5=%g\n", _sq37, _sq38);
    /* line 100 */
    double dp = sc_tensor_dot(b, b);
    /* line 101 */
    sc_tensor *ou = sc_tensor_outer(sm, sm);
    /* line 102 */
    int32_t _sq39 = sc_tensor_dim(ou, 0);
    int32_t _sq40 = sc_tensor_dim(ou, 1);
    double _sq41 = sc_tensor_at(ou, 0);
    printf("dot=%g outer dim0=%d dim1=%d at0=%g\n", dp, _sq39, _sq40, _sq41);
    /* line 105 */
    void *pair[2];
    /* line 106 */
    pair[0] = ((void*)(a));
    /* line 107 */
    pair[1] = ((void*)(b2));
    /* line 108 */
    sc_tensor *cat = sc_concat(((void*)(pair)), 2, 0);
    /* line 109 */
    int32_t _sq42 = sc_tensor_dim(cat, 0);
    int32_t _sq43 = sc_tensor_dim(cat, 1);
    printf("concat dim0=%d dim1=%d numel=%lld\n", _sq42, _sq43, cat->numel);
    /* line 112 */
    sc_tensor *nz = sc_tensor_nonzero(b2);
    /* line 113 */
    int32_t _sq44 = sc_tensor_dim(nz, 0);
    int32_t _sq45 = sc_tensor_dim(nz, 1);
    printf("nonzero dim0=%d dim1=%d dtype=%d\n", _sq44, _sq45, nz->dtype);
    /* line 116 */
    int32_t gidx[2];
    /* line 117 */
    gidx[0] = 2;
    /* line 118 */
    gidx[1] = 2;
    /* line 119 */
    sc_tensor *gi = sc_zeros(2, gidx, sc_DT_I8);
    /* line 120 */
    sc_tensor_set_at(gi, 0, 0.0);
    /* line 121 */
    sc_tensor_set_at(gi, 1, 2.0);
    /* line 122 */
    sc_tensor_set_at(gi, 2, 1.0);
    /* line 123 */
    sc_tensor_set_at(gi, 3, 0.0);
    /* line 124 */
    sc_tensor *go = sc_tensor_gather(b2, 1, gi);
    /* line 125 */
    double _sq46 = sc_tensor_at(go, 0);
    double _sq47 = sc_tensor_at(go, 1);
    double _sq48 = sc_tensor_at(go, 2);
    double _sq49 = sc_tensor_at(go, 3);
    printf("gather at0=%g at1=%g at2=%g at3=%g\n", _sq46, _sq47, _sq48, _sq49);
    /* line 128 */
    sc_tensor *sc0 = sc_zeros(2, shp, sc_DT_F8);
    /* line 129 */
    sc_tensor *ssrc = sc_ones(2, gidx, sc_DT_F8);
    /* line 130 */
    bool sc0r = sc_tensor_scatter_(sc0, 1, gi, ssrc);
    /* line 131 */
    double _sq50 = sc_tensor_at(sc0, 0);
    double _sq51 = sc_tensor_at(sc0, 2);
    printf("scatter_ ok=%d at0=%g at2=%g\n", ((int32_t)(sc0r)), _sq50, _sq51);
    /* line 134 */
    int32_t pds[4];
    /* line 135 */
    pds[0] = 1;
    /* line 136 */
    pds[1] = 0;
    /* line 137 */
    pds[2] = 0;
    /* line 138 */
    pds[3] = 1;
    /* line 139 */
    sc_tensor *pd = sc_tensor_pad(b2, pds, 0.0 - 1.0);
    /* line 140 */
    int32_t _sq52 = sc_tensor_dim(pd, 0);
    int32_t _sq53 = sc_tensor_dim(pd, 1);
    double _sq54 = sc_tensor_at(pd, 0);
    printf("pad dim0=%d dim1=%d at0=%g\n", _sq52, _sq53, _sq54);
    /* line 143 */
    sc_tensor *ro = sc_tensor_roll(b, 1, 0);
    /* line 144 */
    double _sq55 = sc_tensor_at(ro, 0);
    double _sq56 = sc_tensor_at(ro, 5);
    printf("roll at0=%g at5=%g\n", _sq55, _sq56);
    /* line 147 */
    sc_tensor *lsm = sc_tensor_log_softmax(b2, 1);
    /* line 148 */
    printf("log_softmax ndim=%d at2=%g\n", lsm->ndim, sc_tensor_at(lsm, 2));
    /* line 151 */
    sc_tensor *lr = sc_tensor_leaky_relu(ng, 0.1);
    /* line 152 */
    sc_tensor *gl = sc_tensor_gelu(b);
    /* line 153 */
    double _sq57 = sc_tensor_at(lr, 0);
    double _sq58 = sc_tensor_at(gl, 5);
    printf("leaky at0=%g gelu at5=%g\n", _sq57, _sq58);
    /* line 156 */
    sc_tensor *tgt = sc_zeros(1, shp, sc_DT_I8);
    /* line 157 */
    sc_tensor_set_at(tgt, 0, 2.0);
    /* line 158 */
    sc_tensor_set_at(tgt, 1, 0.0);
    /* line 159 */
    double ce = sc_tensor_cross_entropy(b2, tgt);
    /* line 160 */
    printf("cross_entropy=%.4f\n", ce);
    /* line 163 */
    sc_tensor *mset = sc_ones(2, shp, sc_DT_F8);
    /* line 164 */
    double mse = sc_tensor_mse_loss(b2, mset);
    /* line 165 */
    double nll = sc_tensor_nll_loss(lsm, tgt);
    /* line 166 */
    sc_tensor *bct = sc_zeros(2, shp, sc_DT_F8);
    /* line 167 */
    sc_tensor_set_at(bct, 1, 1.0);
    /* line 168 */
    sc_tensor_set_at(bct, 3, 1.0);
    /* line 169 */
    sc_tensor_set_at(bct, 5, 1.0);
    /* line 170 */
    double bce = sc_tensor_bce_with_logits(b2, bct);
    /* line 171 */
    printf("mse=%.4f nll=%.4f bce=%.4f\n", mse, nll, bce);
    /* line 174 */
    sc_tensor *ln = sc_tensor_layer_norm(b2, 1, 0.00001);
    /* line 175 */
    sc_rand_seed(7);
    /* line 176 */
    sc_tensor *dr = sc_tensor_dropout(b2, 0.5, true);
    /* line 177 */
    sc_tensor *dr_eval = sc_tensor_dropout(b2, 0.5, false);
    /* line 178 */
    double _sq59 = sc_tensor_at(ln, 2);
    double _sq60 = sc_tensor_at(ln, 5);
    double _sq61 = sc_tensor_sum_all(dr);
    int32_t _sq62 = ((int32_t)(sc_tensor_allclose(dr_eval, b2, 0.000001, 0.000001)));
    printf("layer_norm at2=%.4f at5=%.4f dropout sum=%.4f eval_eq=%d\n", _sq59, _sq60, _sq61, _sq62);
    /* line 181 */
    sc_tensor *sn = sc_tensor_sin(b);
    /* line 182 */
    sc_tensor *cn = sc_tensor_cos(b);
    /* line 183 */
    double _sq63 = sc_tensor_at(sn, 1);
    double _sq64 = sc_tensor_at(cn, 0);
    printf("sin at1=%.4f cos at0=%.4f\n", _sq63, _sq64);
    /* line 186 */
    sc_tensor *a2 = sc_tensor_atan2(b2, a);
    /* line 187 */
    printf("atan2 at0=%.4f\n", sc_tensor_at(a2, 0));
    /* line 190 */
    double _sq65 = sc_tensor_median_all(b);
    double _sq66 = sc_tensor_percentile_all(b, 50.0);
    printf("median_all=%g pct50=%g\n", _sq65, _sq66);
    /* line 193 */
    sc_tensor *tu = sc_tensor_triu(b2, 0);
    /* line 194 */
    sc_tensor *tl = sc_tensor_tril(b2, 0);
    /* line 195 */
    double _sq67 = sc_tensor_at(tu, 1);
    double _sq68 = sc_tensor_at(tu, 3);
    double _sq69 = sc_tensor_at(tl, 1);
    double _sq70 = sc_tensor_at(tl, 3);
    printf("triu at1=%g at3=%g tril at1=%g at3=%g\n", _sq67, _sq68, _sq69, _sq70);
    /* line 198 */
    int32_t s22[2];
    /* line 199 */
    s22[0] = 2;
    /* line 200 */
    s22[1] = 2;
    /* line 201 */
    sc_tensor *M = sc_zeros(2, s22, sc_DT_F8);
    /* line 202 */
    sc_tensor_set_at(M, 0, 4.0);
    /* line 203 */
    sc_tensor_set_at(M, 1, 1.0);
    /* line 204 */
    sc_tensor_set_at(M, 2, 1.0);
    /* line 205 */
    sc_tensor_set_at(M, 3, 3.0);
    /* line 206 */
    double _sq71 = sc_tensor_det(M);
    double _sq72 = sc_tensor_norm(M, 2.0);
    printf("det=%.4f norm=%.4f\n", _sq71, _sq72);
    /* line 209 */
    sc_tensor *Mi = sc_tensor_inv(M);
    /* line 210 */
    double _sq73 = sc_tensor_at(Mi, 0);
    double _sq74 = sc_tensor_at(Mi, 3);
    printf("inv at0=%.4f at3=%.4f\n", _sq73, _sq74);
    /* line 213 */
    sc_tensor *bb = sc_zeros(1, s22, sc_DT_F8);
    /* line 214 */
    sc_tensor_set_at(bb, 0, 1.0);
    /* line 215 */
    sc_tensor_set_at(bb, 1, 2.0);
    /* line 216 */
    sc_tensor *xx = sc_tensor_solve(M, bb);
    /* line 217 */
    double _sq75 = sc_tensor_at(xx, 0);
    double _sq76 = sc_tensor_at(xx, 1);
    printf("solve at0=%.4f at1=%.4f\n", _sq75, _sq76);
    /* line 220 */
    sc_tensor *Lc = sc_tensor_cholesky(M);
    /* line 221 */
    double _sq77 = sc_tensor_at(Lc, 0);
    double _sq78 = sc_tensor_at(Lc, 2);
    double _sq79 = sc_tensor_at(Lc, 3);
    printf("chol at0=%.4f at2=%.4f at3=%.4f\n", _sq77, _sq78, _sq79);
    /* line 224 */
    void *eout[2];
    /* line 225 */
    bool eok = sc_tensor_eigh(M, ((void*)(eout)));
    /* line 226 */
    sc_tensor *evals = ((sc_tensor*)(eout[0]));
    /* line 227 */
    sc_tensor *evecs = ((sc_tensor*)(eout[1]));
    /* line 228 */
    double _sq80 = sc_tensor_at(evals, 0);
    double _sq81 = sc_tensor_at(evals, 1);
    printf("eigh ok=%d lo=%.4f hi=%.4f\n", ((int32_t)(eok)), _sq80, _sq81);
    /* line 231 */
    void *qout[2];
    /* line 232 */
    bool qok = sc_tensor_qr(M, ((void*)(qout)));
    /* line 233 */
    sc_tensor *Qm = ((sc_tensor*)(qout[0]));
    /* line 234 */
    sc_tensor *Rm = ((sc_tensor*)(qout[1]));
    /* line 235 */
    double _sq82 = sc_tensor_at(Rm, 0);
    double _sq83 = sc_tensor_at(Rm, 3);
    printf("qr ok=%d R00=%.4f R11=%.4f\n", ((int32_t)(qok)), _sq82, _sq83);
    /* line 238 */
    sc_tensor *bu = sc_tensor_unsqueeze(b2, 0);
    /* line 239 */
    sc_tensor *du = sc_tensor_unsqueeze(d, 0);
    /* line 240 */
    sc_tensor *bm = sc_tensor_bmm(bu, du);
    /* line 241 */
    int32_t _sq84 = sc_tensor_dim(bm, 0);
    int32_t _sq85 = sc_tensor_dim(bm, 1);
    int32_t _sq86 = sc_tensor_dim(bm, 2);
    double _sq87 = sc_tensor_at(bm, 3);
    printf("bmm dim0=%d dim1=%d dim2=%d at3=%g\n", _sq84, _sq85, _sq86, _sq87);
    /* line 243 */
    sc_tensor *ones22 = sc_ones(2, s22, sc_DT_F8);
    /* line 244 */
    sc_tensor *adm = sc_tensor_addmm(ones22, M, Mi, 1.0, 1.0);
    /* line 245 */
    double _sq88 = sc_tensor_at(adm, 0);
    double _sq89 = sc_tensor_at(adm, 1);
    double _sq90 = sc_tensor_at(adm, 3);
    printf("addmm at0=%.4f at1=%.4f at3=%.4f\n", _sq88, _sq89, _sq90);
    /* line 247 */
    int32_t s12[3];
    /* line 248 */
    s12[0] = 1;
    /* line 249 */
    s12[1] = 2;
    /* line 250 */
    s12[2] = 2;
    /* line 251 */
    sc_tensor *q3 = sc_zeros(3, s12, sc_DT_F8);
    /* line 252 */
    sc_tensor_set_at(q3, 0, 1.0);
    /* line 253 */
    sc_tensor_set_at(q3, 3, 1.0);
    /* line 254 */
    sc_tensor *k3 = sc_zeros(3, s12, sc_DT_F8);
    /* line 255 */
    sc_tensor_set_at(k3, 0, 1.0);
    /* line 256 */
    sc_tensor_set_at(k3, 3, 1.0);
    /* line 257 */
    sc_tensor *v3 = sc_zeros(3, s12, sc_DT_F8);
    /* line 258 */
    sc_tensor_set_at(v3, 0, 10.0);
    /* line 259 */
    sc_tensor_set_at(v3, 3, 20.0);
    /* line 260 */
    sc_tensor *sd = sc_tensor_sdpa(q3, k3, v3, false);
    /* line 261 */
    sc_tensor *sdc = sc_tensor_sdpa(q3, k3, v3, true);
    /* line 262 */
    double _sq91 = sc_tensor_at(sd, 0);
    double _sq92 = sc_tensor_at(sd, 1);
    double _sq93 = sc_tensor_at(sdc, 0);
    double _sq94 = sc_tensor_at(sdc, 1);
    printf("sdpa at0=%.4f at1=%.4f causal at0=%.4f at1=%.4f\n", _sq91, _sq92, _sq93, _sq94);
    /* line 265 */
    int32_t x1sh[3];
    /* line 266 */
    x1sh[0] = 1;
    /* line 267 */
    x1sh[1] = 1;
    /* line 268 */
    x1sh[2] = 6;
    /* line 269 */
    sc_tensor *x1raw = sc_arange(0.0, 6.0, 1.0, sc_DT_F8);
    /* line 270 */
    sc_tensor *x1d = sc_tensor_reshape(x1raw, 3, x1sh);
    /* line 271 */
    int32_t w1sh[3];
    /* line 272 */
    w1sh[0] = 1;
    /* line 273 */
    w1sh[1] = 1;
    /* line 274 */
    w1sh[2] = 3;
    /* line 275 */
    sc_tensor *w1d = sc_ones(3, w1sh, sc_DT_F8);
    /* line 276 */
    int32_t b1sh[1];
    /* line 277 */
    b1sh[0] = 1;
    /* line 278 */
    sc_tensor *b1d = sc_zeros(1, b1sh, sc_DT_F8);
    /* line 279 */
    sc_tensor *c1d = sc_tensor_conv1d(x1d, w1d, b1d, 1, 0);
    /* line 280 */
    sc_tensor *p1m = sc_tensor_max_pool1d(x1d, 2, 2, 0);
    /* line 281 */
    sc_tensor *p1a = sc_tensor_avg_pool1d(x1d, 2, 2, 0);
    /* line 282 */
    double _sq95 = sc_tensor_at(c1d, 0);
    double _sq96 = sc_tensor_at(c1d, 3);
    double _sq97 = sc_tensor_at(p1m, 2);
    double _sq98 = sc_tensor_at(p1a, 2);
    printf("conv1d at0=%g at3=%g pool1d max2=%g avg2=%.4f\n", _sq95, _sq96, _sq97, _sq98);
    /* line 284 */
    int32_t x2sh[4];
    /* line 285 */
    x2sh[0] = 1;
    /* line 286 */
    x2sh[1] = 1;
    /* line 287 */
    x2sh[2] = 3;
    /* line 288 */
    x2sh[3] = 3;
    /* line 289 */
    sc_tensor *x2raw = sc_arange(0.0, 9.0, 1.0, sc_DT_F8);
    /* line 290 */
    sc_tensor *x2d = sc_tensor_reshape(x2raw, 4, x2sh);
    /* line 291 */
    int32_t w2sh[4];
    /* line 292 */
    w2sh[0] = 1;
    /* line 293 */
    w2sh[1] = 1;
    /* line 294 */
    w2sh[2] = 2;
    /* line 295 */
    w2sh[3] = 2;
    /* line 296 */
    sc_tensor *w2d = sc_ones(4, w2sh, sc_DT_F8);
    /* line 297 */
    sc_tensor *b2d = sc_zeros(1, b1sh, sc_DT_F8);
    /* line 298 */
    sc_tensor *c2d = sc_tensor_conv2d(x2d, w2d, b2d, 1, 1, 0, 0);
    /* line 299 */
    sc_tensor *p2m = sc_tensor_max_pool2d(x2d, 2, 2, 1, 1, 0, 0);
    /* line 300 */
    sc_tensor *p2a = sc_tensor_avg_pool2d(x2d, 2, 2, 1, 1, 0, 0);
    /* line 301 */
    double _sq99 = sc_tensor_at(c2d, 0);
    double _sq100 = sc_tensor_at(c2d, 3);
    double _sq101 = sc_tensor_at(p2m, 0);
    double _sq102 = sc_tensor_at(p2m, 3);
    double _sq103 = sc_tensor_at(p2a, 0);
    double _sq104 = sc_tensor_at(p2a, 3);
    printf("conv2d at0=%g at3=%g pool2d max0=%g max3=%g avg0=%.4f avg3=%.4f\n", _sq99, _sq100, _sq101, _sq102, _sq103, _sq104);
    /* line 304 */
    sc_tensor *trm = sc_tri(3, 3, 0, sc_DT_F8);
    /* line 305 */
    double _sq105 = sc_tensor_sum_all(trm);
    double _sq106 = sc_tensor_at(trm, 0);
    double _sq107 = sc_tensor_at(trm, 1);
    printf("tri sum=%g at0=%g at1=%g\n", _sq105, _sq106, _sq107);
    /* line 308 */
    sc_tensor *xa = sc_arange(0.0, 3.0, 1.0, sc_DT_F8);
    /* line 309 */
    sc_tensor *ya = sc_arange(0.0, 2.0, 1.0, sc_DT_F8);
    /* line 310 */
    void *marr[2];
    /* line 311 */
    marr[0] = ((void*)(xa));
    /* line 312 */
    marr[1] = ((void*)(ya));
    /* line 313 */
    void *mout[2];
    /* line 314 */
    bool mgok = sc_meshgrid(((void*)(marr)), 2, 0, ((void*)(mout)));
    /* line 315 */
    sc_tensor *gx = ((sc_tensor*)(mout[0]));
    /* line 316 */
    sc_tensor *gy = ((sc_tensor*)(mout[1]));
    /* line 317 */
    int32_t _sq108 = sc_tensor_dim(gx, 0);
    int32_t _sq109 = sc_tensor_dim(gx, 1);
    double _sq110 = sc_tensor_at(gx, 0);
    double _sq111 = sc_tensor_at(gx, 5);
    double _sq112 = sc_tensor_at(gy, 0);
    double _sq113 = sc_tensor_at(gy, 5);
    printf("meshgrid ok=%d gx d0=%d d1=%d at0=%g at5=%g gy at0=%g at5=%g\n", ((int32_t)(mgok)), _sq108, _sq109, _sq110, _sq111, _sq112, _sq113);
    /* line 320 */
    sc_rand_seed(42);
    /* line 321 */
    sc_tensor *ru = sc_rand_uniform(2, shp, 0.0, 1.0, sc_DT_F8);
    /* line 322 */
    sc_tensor *rnd = sc_rand_normal(2, shp, 0.0, 1.0, sc_DT_F8);
    /* line 323 */
    sc_tensor *ri = sc_rand_randint(2, shp, 0, 10, sc_DT_I8);
    /* line 324 */
    sc_tensor *pm = sc_permutation(5, sc_DT_I8);
    /* line 325 */
    printf("rand numel=%lld randint numel=%lld perm sum=%g\n", ru->numel, ri->numel, sc_tensor_sum_all(pm));
    /* line 326 */
    printf("normal numel=%lld\n", rnd->numel);
    /* line 329 */
    sc_tensor *shf = sc_arange(0.0, 6.0, 1.0, sc_DT_F8);
    /* line 330 */
    bool shfok = sc_tensor_shuffle_(shf);
    /* line 331 */
    printf("shuffle ok=%d sum=%g\n", ((int32_t)(shfok)), sc_tensor_sum_all(shf));
    /* line 334 */
    bool svok = sc_tensor_save(b2, "/tmp/ts_basic.npy");
    /* line 335 */
    sc_tensor *ld = sc_ts_load("/tmp/ts_basic.npy");
    /* line 336 */
    int32_t _sq114 = sc_tensor_dim(ld, 0);
    int32_t _sq115 = sc_tensor_dim(ld, 1);
    double _sq116 = sc_tensor_at(ld, 5);
    printf("save ok=%d load dim0=%d dim1=%d at5=%g\n", ((int32_t)(svok)), _sq114, _sq115, _sq116);
    /* line 338 */
    {
        int32_t _ret = 0;
        sc_ptr_drop_slot((void *)&(ld), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(shf), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(pm), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(ri), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(rnd), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(ru), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(gy), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(gx), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(ya), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(xa), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(trm), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(p2a), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(p2m), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(c2d), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(b2d), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(w2d), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(x2d), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(x2raw), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(p1a), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(p1m), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(c1d), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(b1d), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(w1d), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(x1d), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(x1raw), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(sdc), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(sd), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(v3), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(k3), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(q3), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(adm), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(ones22), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(bm), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(du), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(bu), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(Rm), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(Qm), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(evecs), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(evals), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(Lc), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(xx), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(bb), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(Mi), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(M), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(tl), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(tu), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(a2), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(cn), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(sn), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(dr_eval), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(dr), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(ln), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(bct), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(mset), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(tgt), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(gl), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(lr), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(lsm), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(ro), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(pd), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(ssrc), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(sc0), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(go), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(gi), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(nz), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(cat), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(ou), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(cs), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(fl), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(r0), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(sl), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(wh), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(gt), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(cl), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(ab), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(ls), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(ey), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(tc), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(t), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(rl), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(ng), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(am), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(mn), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(sm), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(mm), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(d), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(bc), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(row), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(c), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(b2), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(b), (void (*)(void *))sc_tensor_drop);
        sc_ptr_drop_slot((void *)&(a), (void (*)(void *))sc_tensor_drop);
        sc_mod_ts_drop();
        return _ret;
    }
}
