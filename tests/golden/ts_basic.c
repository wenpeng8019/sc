/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/ts/ts.h"

typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


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
    tensor *a = zeros(2, shp, DT_F4);
    /* line 16 */
    tensor_fill(a, 2.0);
    /* line 17 */
    printf("a numel=%lld sum=%g\n", a->numel, tensor_sum_all(a));
    /* line 20 */
    tensor *b = arange(0.0, 6.0, 1.0, DT_F8);
    /* line 21 */
    tensor *b2 = tensor_reshape(b, 2, shp);
    /* line 22 */
    int32_t _sq0 = tensor_dim(b2, 0);
    int32_t _sq1 = tensor_dim(b2, 1);
    double _sq2 = tensor_at(b2, 5);
    int32_t _sq3 = ((int32_t)(tensor_is_contiguous(b2)));
    printf("b2 dim0=%d dim1=%d at5=%g dtype=%d contig=%d\n", _sq0, _sq1, _sq2, b2->dtype, _sq3);
    /* line 25 */
    tensor *c = tensor_add(a, b2);
    /* line 26 */
    printf("c sum=%g\n", tensor_sum_all(c));
    /* line 29 */
    int32_t rsh[2];
    /* line 30 */
    rsh[0] = 1;
    /* line 31 */
    rsh[1] = 3;
    /* line 32 */
    tensor *row = ones(2, rsh, DT_F8);
    /* line 33 */
    tensor *bc = tensor_add(b2, row);
    /* line 34 */
    double _sq4 = tensor_at(bc, 0);
    double _sq5 = tensor_at(bc, 5);
    printf("bc at0=%g at5=%g\n", _sq4, _sq5);
    /* line 37 */
    int32_t sh32[2];
    /* line 38 */
    sh32[0] = 3;
    /* line 39 */
    sh32[1] = 2;
    /* line 40 */
    tensor *d = ones(2, sh32, DT_F8);
    /* line 41 */
    tensor *mm = tensor_matmul(b2, d);
    /* line 42 */
    int32_t _sq6 = tensor_dim(mm, 0);
    int32_t _sq7 = tensor_dim(mm, 1);
    double _sq8 = tensor_at(mm, 0);
    double _sq9 = tensor_at(mm, 3);
    printf("mm shape=%dx%d at0=%g at3=%g\n", _sq6, _sq7, _sq8, _sq9);
    /* line 45 */
    tensor *sm = tensor_sum(b2, 1, false);
    /* line 46 */
    double _sq10 = tensor_at(sm, 0);
    double _sq11 = tensor_at(sm, 1);
    printf("rowsum0=%g rowsum1=%g ndim=%d\n", _sq10, _sq11, sm->ndim);
    /* line 49 */
    tensor *mn = tensor_mean(b2, 1, true);
    /* line 50 */
    int32_t _sq12 = tensor_dim(mn, 1);
    double _sq13 = tensor_at(mn, 0);
    double _sq14 = tensor_at(mn, 1);
    printf("mean ndim=%d dim1=%d at0=%g at1=%g\n", mn->ndim, _sq12, _sq13, _sq14);
    /* line 53 */
    tensor *am = tensor_argmax(b, -(1), false);
    /* line 54 */
    printf("argmax=%g amdtype=%d\n", tensor_at(am, 0), am->dtype);
    /* line 57 */
    tensor *ng = tensor_neg(b);
    /* line 58 */
    tensor *rl = tensor_relu(ng);
    /* line 59 */
    printf("relu sum=%g\n", tensor_sum_all(rl));
    /* line 62 */
    tensor *t = tensor_transpose(b2);
    /* line 63 */
    int32_t _sq15 = tensor_dim(t, 0);
    int32_t _sq16 = tensor_dim(t, 1);
    double _sq17 = tensor_at(t, 0);
    double _sq18 = tensor_at(t, 5);
    int32_t _sq19 = ((int32_t)(tensor_is_contiguous(t)));
    printf("t shape=%dx%d at0=%g at5=%g contig=%d\n", _sq15, _sq16, _sq17, _sq18, _sq19);
    /* line 64 */
    tensor *tc = tensor_contiguous(t);
    /* line 65 */
    int32_t _sq20 = ((int32_t)(tensor_is_contiguous(tc)));
    double _sq21 = tensor_at(tc, 1);
    printf("tc contig=%d at1=%g\n", _sq20, _sq21);
    /* line 68 */
    tensor *ey = eye(3, DT_F4);
    /* line 69 */
    double _sq22 = tensor_sum_all(ey);
    double _sq23 = tensor_at(ey, 4);
    printf("eye sum=%g diag4=%g\n", _sq22, _sq23);
    /* line 72 */
    tensor *ls = linspace(0.0 - 2.0, 2.0, 5, DT_F8);
    /* line 73 */
    tensor *ab = tensor_abs(ls);
    /* line 74 */
    tensor *cl = tensor_clip(ls, 0.0 - 1.0, 1.0);
    /* line 75 */
    double _sq24 = tensor_at(ls, 0);
    double _sq25 = tensor_at(ls, 4);
    double _sq26 = tensor_at(ab, 0);
    double _sq27 = tensor_at(cl, 0);
    double _sq28 = tensor_at(cl, 4);
    printf("ls at0=%g at4=%g abs at0=%g clip at0=%g at4=%g\n", _sq24, _sq25, _sq26, _sq27, _sq28);
    /* line 78 */
    tensor *gt = tensor_gt(b2, a);
    /* line 79 */
    printf("gt dtype=%d sum=%g\n", gt->dtype, tensor_sum_all(gt));
    /* line 80 */
    tensor *wh = where(gt, b2, a);
    /* line 81 */
    double _sq29 = tensor_at(wh, 0);
    double _sq30 = tensor_at(wh, 5);
    printf("where at0=%g at5=%g\n", _sq29, _sq30);
    /* line 84 */
    tensor *sl = tensor_slice(b, 0, 1, 5, 2);
    /* line 85 */
    double _sq31 = tensor_at(sl, 0);
    double _sq32 = tensor_at(sl, 1);
    printf("slice numel=%lld at0=%g at1=%g\n", sl->numel, _sq31, _sq32);
    /* line 88 */
    tensor *r0 = tensor_select(b2, 0, 0);
    /* line 89 */
    double _sq33 = tensor_at(r0, 0);
    double _sq34 = tensor_at(r0, 2);
    printf("select ndim=%d at0=%g at2=%g\n", r0->ndim, _sq33, _sq34);
    /* line 92 */
    tensor *fl = tensor_flip(b, 0);
    /* line 93 */
    double _sq35 = tensor_at(fl, 0);
    double _sq36 = tensor_at(fl, 5);
    printf("flip at0=%g at5=%g\n", _sq35, _sq36);
    /* line 96 */
    tensor *cs = tensor_cumsum(b, 0);
    /* line 97 */
    double _sq37 = tensor_at(cs, 0);
    double _sq38 = tensor_at(cs, 5);
    printf("cumsum at0=%g at5=%g\n", _sq37, _sq38);
    /* line 100 */
    double dp = tensor_dot(b, b);
    /* line 101 */
    tensor *ou = tensor_outer(sm, sm);
    /* line 102 */
    int32_t _sq39 = tensor_dim(ou, 0);
    int32_t _sq40 = tensor_dim(ou, 1);
    double _sq41 = tensor_at(ou, 0);
    printf("dot=%g outer dim0=%d dim1=%d at0=%g\n", dp, _sq39, _sq40, _sq41);
    /* line 105 */
    void *pair[2];
    /* line 106 */
    pair[0] = ((void*)(a));
    /* line 107 */
    pair[1] = ((void*)(b2));
    /* line 108 */
    tensor *cat = concat(((void*)(pair)), 2, 0);
    /* line 109 */
    int32_t _sq42 = tensor_dim(cat, 0);
    int32_t _sq43 = tensor_dim(cat, 1);
    printf("concat dim0=%d dim1=%d numel=%lld\n", _sq42, _sq43, cat->numel);
    /* line 112 */
    tensor *nz = tensor_nonzero(b2);
    /* line 113 */
    int32_t _sq44 = tensor_dim(nz, 0);
    int32_t _sq45 = tensor_dim(nz, 1);
    printf("nonzero dim0=%d dim1=%d dtype=%d\n", _sq44, _sq45, nz->dtype);
    /* line 116 */
    int32_t gidx[2];
    /* line 117 */
    gidx[0] = 2;
    /* line 118 */
    gidx[1] = 2;
    /* line 119 */
    tensor *gi = zeros(2, gidx, DT_I8);
    /* line 120 */
    tensor_set_at(gi, 0, 0.0);
    /* line 121 */
    tensor_set_at(gi, 1, 2.0);
    /* line 122 */
    tensor_set_at(gi, 2, 1.0);
    /* line 123 */
    tensor_set_at(gi, 3, 0.0);
    /* line 124 */
    tensor *go = tensor_gather(b2, 1, gi);
    /* line 125 */
    double _sq46 = tensor_at(go, 0);
    double _sq47 = tensor_at(go, 1);
    double _sq48 = tensor_at(go, 2);
    double _sq49 = tensor_at(go, 3);
    printf("gather at0=%g at1=%g at2=%g at3=%g\n", _sq46, _sq47, _sq48, _sq49);
    /* line 128 */
    tensor *sc0 = zeros(2, shp, DT_F8);
    /* line 129 */
    tensor *ssrc = ones(2, gidx, DT_F8);
    /* line 130 */
    uint8_t sc0r = tensor_scatter_(sc0, 1, gi, ssrc);
    /* line 131 */
    double _sq50 = tensor_at(sc0, 0);
    double _sq51 = tensor_at(sc0, 2);
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
    tensor *pd = tensor_pad(b2, pds, 0.0 - 1.0);
    /* line 140 */
    int32_t _sq52 = tensor_dim(pd, 0);
    int32_t _sq53 = tensor_dim(pd, 1);
    double _sq54 = tensor_at(pd, 0);
    printf("pad dim0=%d dim1=%d at0=%g\n", _sq52, _sq53, _sq54);
    /* line 143 */
    tensor *ro = tensor_roll(b, 1, 0);
    /* line 144 */
    double _sq55 = tensor_at(ro, 0);
    double _sq56 = tensor_at(ro, 5);
    printf("roll at0=%g at5=%g\n", _sq55, _sq56);
    /* line 147 */
    tensor *lsm = tensor_log_softmax(b2, 1);
    /* line 148 */
    printf("log_softmax ndim=%d at2=%g\n", lsm->ndim, tensor_at(lsm, 2));
    /* line 151 */
    tensor *lr = tensor_leaky_relu(ng, 0.1);
    /* line 152 */
    tensor *gl = tensor_gelu(b);
    /* line 153 */
    double _sq57 = tensor_at(lr, 0);
    double _sq58 = tensor_at(gl, 5);
    printf("leaky at0=%g gelu at5=%g\n", _sq57, _sq58);
    /* line 156 */
    tensor *tgt = zeros(1, shp, DT_I8);
    /* line 157 */
    tensor_set_at(tgt, 0, 2.0);
    /* line 158 */
    tensor_set_at(tgt, 1, 0.0);
    /* line 159 */
    double ce = tensor_cross_entropy(b2, tgt);
    /* line 160 */
    printf("cross_entropy=%.4f\n", ce);
    /* line 163 */
    tensor *mset = ones(2, shp, DT_F8);
    /* line 164 */
    double mse = tensor_mse_loss(b2, mset);
    /* line 165 */
    double nll = tensor_nll_loss(lsm, tgt);
    /* line 166 */
    tensor *bct = zeros(2, shp, DT_F8);
    /* line 167 */
    tensor_set_at(bct, 1, 1.0);
    /* line 168 */
    tensor_set_at(bct, 3, 1.0);
    /* line 169 */
    tensor_set_at(bct, 5, 1.0);
    /* line 170 */
    double bce = tensor_bce_with_logits(b2, bct);
    /* line 171 */
    printf("mse=%.4f nll=%.4f bce=%.4f\n", mse, nll, bce);
    /* line 174 */
    tensor *ln = tensor_layer_norm(b2, 1, 0.00001);
    /* line 175 */
    rand_seed(7);
    /* line 176 */
    tensor *dr = tensor_dropout(b2, 0.5, true);
    /* line 177 */
    tensor *dr_eval = tensor_dropout(b2, 0.5, false);
    /* line 178 */
    double _sq59 = tensor_at(ln, 2);
    double _sq60 = tensor_at(ln, 5);
    double _sq61 = tensor_sum_all(dr);
    int32_t _sq62 = ((int32_t)(tensor_allclose(dr_eval, b2, 0.000001, 0.000001)));
    printf("layer_norm at2=%.4f at5=%.4f dropout sum=%.4f eval_eq=%d\n", _sq59, _sq60, _sq61, _sq62);
    /* line 181 */
    tensor *sn = tensor_sin(b);
    /* line 182 */
    tensor *cn = tensor_cos(b);
    /* line 183 */
    double _sq63 = tensor_at(sn, 1);
    double _sq64 = tensor_at(cn, 0);
    printf("sin at1=%.4f cos at0=%.4f\n", _sq63, _sq64);
    /* line 186 */
    tensor *a2 = tensor_atan2(b2, a);
    /* line 187 */
    printf("atan2 at0=%.4f\n", tensor_at(a2, 0));
    /* line 190 */
    double _sq65 = tensor_median_all(b);
    double _sq66 = tensor_percentile_all(b, 50.0);
    printf("median_all=%g pct50=%g\n", _sq65, _sq66);
    /* line 193 */
    tensor *tu = tensor_triu(b2, 0);
    /* line 194 */
    tensor *tl = tensor_tril(b2, 0);
    /* line 195 */
    double _sq67 = tensor_at(tu, 1);
    double _sq68 = tensor_at(tu, 3);
    double _sq69 = tensor_at(tl, 1);
    double _sq70 = tensor_at(tl, 3);
    printf("triu at1=%g at3=%g tril at1=%g at3=%g\n", _sq67, _sq68, _sq69, _sq70);
    /* line 198 */
    int32_t s22[2];
    /* line 199 */
    s22[0] = 2;
    /* line 200 */
    s22[1] = 2;
    /* line 201 */
    tensor *M = zeros(2, s22, DT_F8);
    /* line 202 */
    tensor_set_at(M, 0, 4.0);
    /* line 203 */
    tensor_set_at(M, 1, 1.0);
    /* line 204 */
    tensor_set_at(M, 2, 1.0);
    /* line 205 */
    tensor_set_at(M, 3, 3.0);
    /* line 206 */
    double _sq71 = tensor_det(M);
    double _sq72 = tensor_norm(M, 2.0);
    printf("det=%.4f norm=%.4f\n", _sq71, _sq72);
    /* line 209 */
    tensor *Mi = tensor_inv(M);
    /* line 210 */
    double _sq73 = tensor_at(Mi, 0);
    double _sq74 = tensor_at(Mi, 3);
    printf("inv at0=%.4f at3=%.4f\n", _sq73, _sq74);
    /* line 213 */
    tensor *bb = zeros(1, s22, DT_F8);
    /* line 214 */
    tensor_set_at(bb, 0, 1.0);
    /* line 215 */
    tensor_set_at(bb, 1, 2.0);
    /* line 216 */
    tensor *xx = tensor_solve(M, bb);
    /* line 217 */
    double _sq75 = tensor_at(xx, 0);
    double _sq76 = tensor_at(xx, 1);
    printf("solve at0=%.4f at1=%.4f\n", _sq75, _sq76);
    /* line 220 */
    tensor *Lc = tensor_cholesky(M);
    /* line 221 */
    double _sq77 = tensor_at(Lc, 0);
    double _sq78 = tensor_at(Lc, 2);
    double _sq79 = tensor_at(Lc, 3);
    printf("chol at0=%.4f at2=%.4f at3=%.4f\n", _sq77, _sq78, _sq79);
    /* line 224 */
    void *eout[2];
    /* line 225 */
    uint8_t eok = tensor_eigh(M, ((void*)(eout)));
    /* line 226 */
    tensor *evals = ((tensor*)(eout[0]));
    /* line 227 */
    tensor *evecs = ((tensor*)(eout[1]));
    /* line 228 */
    double _sq80 = tensor_at(evals, 0);
    double _sq81 = tensor_at(evals, 1);
    printf("eigh ok=%d lo=%.4f hi=%.4f\n", ((int32_t)(eok)), _sq80, _sq81);
    /* line 231 */
    void *qout[2];
    /* line 232 */
    uint8_t qok = tensor_qr(M, ((void*)(qout)));
    /* line 233 */
    tensor *Qm = ((tensor*)(qout[0]));
    /* line 234 */
    tensor *Rm = ((tensor*)(qout[1]));
    /* line 235 */
    double _sq82 = tensor_at(Rm, 0);
    double _sq83 = tensor_at(Rm, 3);
    printf("qr ok=%d R00=%.4f R11=%.4f\n", ((int32_t)(qok)), _sq82, _sq83);
    /* line 238 */
    tensor *bu = tensor_unsqueeze(b2, 0);
    /* line 239 */
    tensor *du = tensor_unsqueeze(d, 0);
    /* line 240 */
    tensor *bm = tensor_bmm(bu, du);
    /* line 241 */
    int32_t _sq84 = tensor_dim(bm, 0);
    int32_t _sq85 = tensor_dim(bm, 1);
    int32_t _sq86 = tensor_dim(bm, 2);
    double _sq87 = tensor_at(bm, 3);
    printf("bmm dim0=%d dim1=%d dim2=%d at3=%g\n", _sq84, _sq85, _sq86, _sq87);
    /* line 243 */
    tensor *ones22 = ones(2, s22, DT_F8);
    /* line 244 */
    tensor *adm = tensor_addmm(ones22, M, Mi, 1.0, 1.0);
    /* line 245 */
    double _sq88 = tensor_at(adm, 0);
    double _sq89 = tensor_at(adm, 1);
    double _sq90 = tensor_at(adm, 3);
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
    tensor *q3 = zeros(3, s12, DT_F8);
    /* line 252 */
    tensor_set_at(q3, 0, 1.0);
    /* line 253 */
    tensor_set_at(q3, 3, 1.0);
    /* line 254 */
    tensor *k3 = zeros(3, s12, DT_F8);
    /* line 255 */
    tensor_set_at(k3, 0, 1.0);
    /* line 256 */
    tensor_set_at(k3, 3, 1.0);
    /* line 257 */
    tensor *v3 = zeros(3, s12, DT_F8);
    /* line 258 */
    tensor_set_at(v3, 0, 10.0);
    /* line 259 */
    tensor_set_at(v3, 3, 20.0);
    /* line 260 */
    tensor *sd = tensor_sdpa(q3, k3, v3, false);
    /* line 261 */
    tensor *sdc = tensor_sdpa(q3, k3, v3, true);
    /* line 262 */
    double _sq91 = tensor_at(sd, 0);
    double _sq92 = tensor_at(sd, 1);
    double _sq93 = tensor_at(sdc, 0);
    double _sq94 = tensor_at(sdc, 1);
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
    tensor *x1raw = arange(0.0, 6.0, 1.0, DT_F8);
    /* line 270 */
    tensor *x1d = tensor_reshape(x1raw, 3, x1sh);
    /* line 271 */
    int32_t w1sh[3];
    /* line 272 */
    w1sh[0] = 1;
    /* line 273 */
    w1sh[1] = 1;
    /* line 274 */
    w1sh[2] = 3;
    /* line 275 */
    tensor *w1d = ones(3, w1sh, DT_F8);
    /* line 276 */
    int32_t b1sh[1];
    /* line 277 */
    b1sh[0] = 1;
    /* line 278 */
    tensor *b1d = zeros(1, b1sh, DT_F8);
    /* line 279 */
    tensor *c1d = tensor_conv1d(x1d, w1d, b1d, 1, 0);
    /* line 280 */
    tensor *p1m = tensor_max_pool1d(x1d, 2, 2, 0);
    /* line 281 */
    tensor *p1a = tensor_avg_pool1d(x1d, 2, 2, 0);
    /* line 282 */
    double _sq95 = tensor_at(c1d, 0);
    double _sq96 = tensor_at(c1d, 3);
    double _sq97 = tensor_at(p1m, 2);
    double _sq98 = tensor_at(p1a, 2);
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
    tensor *x2raw = arange(0.0, 9.0, 1.0, DT_F8);
    /* line 290 */
    tensor *x2d = tensor_reshape(x2raw, 4, x2sh);
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
    tensor *w2d = ones(4, w2sh, DT_F8);
    /* line 297 */
    tensor *b2d = zeros(1, b1sh, DT_F8);
    /* line 298 */
    tensor *c2d = tensor_conv2d(x2d, w2d, b2d, 1, 1, 0, 0);
    /* line 299 */
    tensor *p2m = tensor_max_pool2d(x2d, 2, 2, 1, 1, 0, 0);
    /* line 300 */
    tensor *p2a = tensor_avg_pool2d(x2d, 2, 2, 1, 1, 0, 0);
    /* line 301 */
    double _sq99 = tensor_at(c2d, 0);
    double _sq100 = tensor_at(c2d, 3);
    double _sq101 = tensor_at(p2m, 0);
    double _sq102 = tensor_at(p2m, 3);
    double _sq103 = tensor_at(p2a, 0);
    double _sq104 = tensor_at(p2a, 3);
    printf("conv2d at0=%g at3=%g pool2d max0=%g max3=%g avg0=%.4f avg3=%.4f\n", _sq99, _sq100, _sq101, _sq102, _sq103, _sq104);
    /* line 304 */
    tensor *trm = tri(3, 3, 0, DT_F8);
    /* line 305 */
    double _sq105 = tensor_sum_all(trm);
    double _sq106 = tensor_at(trm, 0);
    double _sq107 = tensor_at(trm, 1);
    printf("tri sum=%g at0=%g at1=%g\n", _sq105, _sq106, _sq107);
    /* line 308 */
    tensor *xa = arange(0.0, 3.0, 1.0, DT_F8);
    /* line 309 */
    tensor *ya = arange(0.0, 2.0, 1.0, DT_F8);
    /* line 310 */
    void *marr[2];
    /* line 311 */
    marr[0] = ((void*)(xa));
    /* line 312 */
    marr[1] = ((void*)(ya));
    /* line 313 */
    void *mout[2];
    /* line 314 */
    uint8_t mgok = meshgrid(((void*)(marr)), 2, 0, ((void*)(mout)));
    /* line 315 */
    tensor *gx = ((tensor*)(mout[0]));
    /* line 316 */
    tensor *gy = ((tensor*)(mout[1]));
    /* line 317 */
    int32_t _sq108 = tensor_dim(gx, 0);
    int32_t _sq109 = tensor_dim(gx, 1);
    double _sq110 = tensor_at(gx, 0);
    double _sq111 = tensor_at(gx, 5);
    double _sq112 = tensor_at(gy, 0);
    double _sq113 = tensor_at(gy, 5);
    printf("meshgrid ok=%d gx d0=%d d1=%d at0=%g at5=%g gy at0=%g at5=%g\n", ((int32_t)(mgok)), _sq108, _sq109, _sq110, _sq111, _sq112, _sq113);
    /* line 320 */
    rand_seed(42);
    /* line 321 */
    tensor *ru = rand_uniform(2, shp, 0.0, 1.0, DT_F8);
    /* line 322 */
    tensor *rnd = rand_normal(2, shp, 0.0, 1.0, DT_F8);
    /* line 323 */
    tensor *ri = rand_randint(2, shp, 0, 10, DT_I8);
    /* line 324 */
    tensor *pm = permutation(5, DT_I8);
    /* line 325 */
    printf("rand numel=%lld randint numel=%lld perm sum=%g\n", ru->numel, ri->numel, tensor_sum_all(pm));
    /* line 326 */
    printf("normal numel=%lld\n", rnd->numel);
    /* line 329 */
    tensor *shf = arange(0.0, 6.0, 1.0, DT_F8);
    /* line 330 */
    uint8_t shfok = tensor_shuffle_(shf);
    /* line 331 */
    printf("shuffle ok=%d sum=%g\n", ((int32_t)(shfok)), tensor_sum_all(shf));
    /* line 334 */
    uint8_t svok = tensor_save(b2, "/tmp/ts_basic.npy");
    /* line 335 */
    tensor *ld = ts_load("/tmp/ts_basic.npy");
    /* line 336 */
    int32_t _sq114 = tensor_dim(ld, 0);
    int32_t _sq115 = tensor_dim(ld, 1);
    double _sq116 = tensor_at(ld, 5);
    printf("save ok=%d load dim0=%d dim1=%d at5=%g\n", ((int32_t)(svok)), _sq114, _sq115, _sq116);
    /* line 338 */
    {
        int32_t _ret = 0;
        if (ld) { tensor_drop(ld); sc_free(ld); }
        if (shf) { tensor_drop(shf); sc_free(shf); }
        if (pm) { tensor_drop(pm); sc_free(pm); }
        if (ri) { tensor_drop(ri); sc_free(ri); }
        if (rnd) { tensor_drop(rnd); sc_free(rnd); }
        if (ru) { tensor_drop(ru); sc_free(ru); }
        if (gy) { tensor_drop(gy); sc_free(gy); }
        if (gx) { tensor_drop(gx); sc_free(gx); }
        if (ya) { tensor_drop(ya); sc_free(ya); }
        if (xa) { tensor_drop(xa); sc_free(xa); }
        if (trm) { tensor_drop(trm); sc_free(trm); }
        if (p2a) { tensor_drop(p2a); sc_free(p2a); }
        if (p2m) { tensor_drop(p2m); sc_free(p2m); }
        if (c2d) { tensor_drop(c2d); sc_free(c2d); }
        if (b2d) { tensor_drop(b2d); sc_free(b2d); }
        if (w2d) { tensor_drop(w2d); sc_free(w2d); }
        if (x2d) { tensor_drop(x2d); sc_free(x2d); }
        if (x2raw) { tensor_drop(x2raw); sc_free(x2raw); }
        if (p1a) { tensor_drop(p1a); sc_free(p1a); }
        if (p1m) { tensor_drop(p1m); sc_free(p1m); }
        if (c1d) { tensor_drop(c1d); sc_free(c1d); }
        if (b1d) { tensor_drop(b1d); sc_free(b1d); }
        if (w1d) { tensor_drop(w1d); sc_free(w1d); }
        if (x1d) { tensor_drop(x1d); sc_free(x1d); }
        if (x1raw) { tensor_drop(x1raw); sc_free(x1raw); }
        if (sdc) { tensor_drop(sdc); sc_free(sdc); }
        if (sd) { tensor_drop(sd); sc_free(sd); }
        if (v3) { tensor_drop(v3); sc_free(v3); }
        if (k3) { tensor_drop(k3); sc_free(k3); }
        if (q3) { tensor_drop(q3); sc_free(q3); }
        if (adm) { tensor_drop(adm); sc_free(adm); }
        if (ones22) { tensor_drop(ones22); sc_free(ones22); }
        if (bm) { tensor_drop(bm); sc_free(bm); }
        if (du) { tensor_drop(du); sc_free(du); }
        if (bu) { tensor_drop(bu); sc_free(bu); }
        if (Rm) { tensor_drop(Rm); sc_free(Rm); }
        if (Qm) { tensor_drop(Qm); sc_free(Qm); }
        if (evecs) { tensor_drop(evecs); sc_free(evecs); }
        if (evals) { tensor_drop(evals); sc_free(evals); }
        if (Lc) { tensor_drop(Lc); sc_free(Lc); }
        if (xx) { tensor_drop(xx); sc_free(xx); }
        if (bb) { tensor_drop(bb); sc_free(bb); }
        if (Mi) { tensor_drop(Mi); sc_free(Mi); }
        if (M) { tensor_drop(M); sc_free(M); }
        if (tl) { tensor_drop(tl); sc_free(tl); }
        if (tu) { tensor_drop(tu); sc_free(tu); }
        if (a2) { tensor_drop(a2); sc_free(a2); }
        if (cn) { tensor_drop(cn); sc_free(cn); }
        if (sn) { tensor_drop(sn); sc_free(sn); }
        if (dr_eval) { tensor_drop(dr_eval); sc_free(dr_eval); }
        if (dr) { tensor_drop(dr); sc_free(dr); }
        if (ln) { tensor_drop(ln); sc_free(ln); }
        if (bct) { tensor_drop(bct); sc_free(bct); }
        if (mset) { tensor_drop(mset); sc_free(mset); }
        if (tgt) { tensor_drop(tgt); sc_free(tgt); }
        if (gl) { tensor_drop(gl); sc_free(gl); }
        if (lr) { tensor_drop(lr); sc_free(lr); }
        if (lsm) { tensor_drop(lsm); sc_free(lsm); }
        if (ro) { tensor_drop(ro); sc_free(ro); }
        if (pd) { tensor_drop(pd); sc_free(pd); }
        if (ssrc) { tensor_drop(ssrc); sc_free(ssrc); }
        if (sc0) { tensor_drop(sc0); sc_free(sc0); }
        if (go) { tensor_drop(go); sc_free(go); }
        if (gi) { tensor_drop(gi); sc_free(gi); }
        if (nz) { tensor_drop(nz); sc_free(nz); }
        if (cat) { tensor_drop(cat); sc_free(cat); }
        if (ou) { tensor_drop(ou); sc_free(ou); }
        if (cs) { tensor_drop(cs); sc_free(cs); }
        if (fl) { tensor_drop(fl); sc_free(fl); }
        if (r0) { tensor_drop(r0); sc_free(r0); }
        if (sl) { tensor_drop(sl); sc_free(sl); }
        if (wh) { tensor_drop(wh); sc_free(wh); }
        if (gt) { tensor_drop(gt); sc_free(gt); }
        if (cl) { tensor_drop(cl); sc_free(cl); }
        if (ab) { tensor_drop(ab); sc_free(ab); }
        if (ls) { tensor_drop(ls); sc_free(ls); }
        if (ey) { tensor_drop(ey); sc_free(ey); }
        if (tc) { tensor_drop(tc); sc_free(tc); }
        if (t) { tensor_drop(t); sc_free(t); }
        if (rl) { tensor_drop(rl); sc_free(rl); }
        if (ng) { tensor_drop(ng); sc_free(ng); }
        if (am) { tensor_drop(am); sc_free(am); }
        if (mn) { tensor_drop(mn); sc_free(mn); }
        if (sm) { tensor_drop(sm); sc_free(sm); }
        if (mm) { tensor_drop(mm); sc_free(mm); }
        if (d) { tensor_drop(d); sc_free(d); }
        if (bc) { tensor_drop(bc); sc_free(bc); }
        if (row) { tensor_drop(row); sc_free(row); }
        if (c) { tensor_drop(c); sc_free(c); }
        if (b2) { tensor_drop(b2); sc_free(b2); }
        if (b) { tensor_drop(b); sc_free(b); }
        if (a) { tensor_drop(a); sc_free(a); }
        sc_mod_ts_drop();
        return _ret;
    }
}
