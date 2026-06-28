# ts 张量模块回归用例（真·numpy 级重构后）：
#   - 工厂：zeros/ones/arange/eye/linspace/full + *_like；
#   - 视图（零拷贝）：reshape(连续)/transpose/slice/select/flip；contiguous/is_contiguous；
#   - 广播二元 + 比较(→bool) + where；一元 abs/clip；
#   - 规约（axis+keepdim）sum/mean/argmax/cumsum；线代 matmul/dot/outer；
#   - nn relu/neg；拼接 concat；跨模块枚举常量 DT_F8/DT_BOOL 裸名引用；末尾逐一 drop()。
inc ts.sc

fnc main: i4

    # ---------- 工厂 + fill + 全规约（dtype 必填） ----------
    var shp[2]: i4
    shp[0] = 2
    shp[1] = 3
    var a: tensor@& = zeros(2, shp, DT_F4)
    a->fill(2.0)
    printf("a numel=%lld sum=%g\n", a->numel, a->sum_all())

    # ---------- arange（跨模块枚举常量 DT_F8）+ reshape（连续→零拷贝视图） ----------
    var b: tensor@& = arange(0.0, 6.0, 1.0, DT_F8)
    var b2: tensor@& = b->reshape(2, shp)
    printf("b2 dim0=%d dim1=%d at5=%g dtype=%d contig=%d\n", b2->dim(0), b2->dim(1), b2->at(5), b2->dtype, (b2->is_contiguous(): i4))

    # ---------- 逐元素加（混合 dtype，结果取 _this dtype） ----------
    var c: tensor@& = a->add(b2)
    printf("c sum=%g\n", c->sum_all())

    # ---------- numpy 广播：[2,3] + [1,3] ----------
    var rsh[2]: i4
    rsh[0] = 1
    rsh[1] = 3
    var row: tensor@& = ones(2, rsh, DT_F8)
    var bc: tensor@& = b2->add(row)
    printf("bc at0=%g at5=%g\n", bc->at(0), bc->at(5))

    # ---------- matmul：[2,3] x [3,2] → [2,2] ----------
    var sh32[2]: i4
    sh32[0] = 3
    sh32[1] = 2
    var d: tensor@& = ones(2, sh32, DT_F8)
    var mm: tensor@& = b2->matmul(d)
    printf("mm shape=%dx%d at0=%g at3=%g\n", mm->dim(0), mm->dim(1), mm->at(0), mm->at(3))

    # ---------- 沿轴规约：sum(axis=1, keepdim=false) → [2] ----------
    var sm: tensor@& = b2->sum(1, false)
    printf("rowsum0=%g rowsum1=%g ndim=%d\n", sm->at(0), sm->at(1), sm->ndim)

    # ---------- keepdim：mean(axis=1, keepdim=true) → [2,1] ----------
    var mn: tensor@& = b2->mean(1, true)
    printf("mean ndim=%d dim1=%d at0=%g at1=%g\n", mn->ndim, mn->dim(1), mn->at(0), mn->at(1))

    # ---------- argmax（全规约，结果 DT_I8） ----------
    var am: tensor@& = b->argmax(-1, false)
    printf("argmax=%g amdtype=%d\n", am->at(0), am->dtype)

    # ---------- 一元：neg + relu ----------
    var ng: tensor@& = b->neg()
    var rl: tensor@& = ng->relu()
    printf("relu sum=%g\n", rl->sum_all())

    # ---------- transpose（零拷贝视图，非连续）+ contiguous 物化 ----------
    var t: tensor@& = b2->transpose()
    printf("t shape=%dx%d at0=%g at5=%g contig=%d\n", t->dim(0), t->dim(1), t->at(0), t->at(5), (t->is_contiguous(): i4))
    var tc: tensor@& = t->contiguous()
    printf("tc contig=%d at1=%g\n", (tc->is_contiguous(): i4), tc->at(1))

    # ---------- 单位阵 ----------
    var ey: tensor@& = eye(3, DT_F4)
    printf("eye sum=%g diag4=%g\n", ey->sum_all(), ey->at(4))

    # ---------- linspace + 一元 abs/clip ----------
    var ls: tensor@& = linspace(0.0 - 2.0, 2.0, 5, DT_F8)
    var ab: tensor@& = ls->abs()
    var cl: tensor@& = ls->clip(0.0 - 1.0, 1.0)
    printf("ls at0=%g at4=%g abs at0=%g clip at0=%g at4=%g\n", ls->at(0), ls->at(4), ab->at(0), cl->at(0), cl->at(4))

    # ---------- 比较（→ DT_BOOL）+ where ----------
    var gt: tensor@& = b2->gt(a)
    printf("gt dtype=%d sum=%g\n", gt->dtype, gt->sum_all())
    var wh: tensor@& = where(gt, b2, a)
    printf("where at0=%g at5=%g\n", wh->at(0), wh->at(5))

    # ---------- slice（视图）：b[1:5:2] ----------
    var sl: tensor@& = b->slice(0, 1, 5, 2)
    printf("slice numel=%lld at0=%g at1=%g\n", sl->numel, sl->at(0), sl->at(1))

    # ---------- select（降维视图）：b2 第 0 行 ----------
    var r0: tensor@& = b2->select(0, 0)
    printf("select ndim=%d at0=%g at2=%g\n", r0->ndim, r0->at(0), r0->at(2))

    # ---------- flip（负步长视图） ----------
    var fl: tensor@& = b->flip(0)
    printf("flip at0=%g at5=%g\n", fl->at(0), fl->at(5))

    # ---------- cumsum ----------
    var cs: tensor@& = b->cumsum(0)
    printf("cumsum at0=%g at5=%g\n", cs->at(0), cs->at(5))

    # ---------- dot + outer（1D） ----------
    var dp: f8 = b->dot(b)
    var ou: tensor@& = sm->outer(sm)
    printf("dot=%g outer dim0=%d dim1=%d at0=%g\n", dp, ou->dim(0), ou->dim(1), ou->at(0))

    # ---------- concat（沿 axis=0） ----------
    var pair[2]: &
    pair[0] = (a: &)
    pair[1] = (b2: &)
    var cat: tensor@& = concat((pair: &), 2, 0)
    printf("concat dim0=%d dim1=%d numel=%lld\n", cat->dim(0), cat->dim(1), cat->numel)

    # ---------- P2 nonzero（[k, ndim]，DT_I8） ----------
    var nz: tensor@& = b2->nonzero()
    printf("nonzero dim0=%d dim1=%d dtype=%d\n", nz->dim(0), nz->dim(1), nz->dtype)

    # ---------- P2 gather（沿 axis=1 取每行 idx 列） ----------
    var gidx[2]: i4
    gidx[0] = 2
    gidx[1] = 2
    var gi: tensor@& = zeros(2, gidx, DT_I8)
    gi->set_at(0, 0.0)
    gi->set_at(1, 2.0)
    gi->set_at(2, 1.0)
    gi->set_at(3, 0.0)
    var go: tensor@& = b2->gather(1, gi)
    printf("gather at0=%g at1=%g at2=%g at3=%g\n", go->at(0), go->at(1), go->at(2), go->at(3))

    # ---------- P2 scatter_（原地写入） ----------
    var sc0: tensor@& = zeros(2, shp, DT_F8)
    var ssrc: tensor@& = ones(2, gidx, DT_F8)
    var sc0r: bool = sc0->scatter_(1, gi, ssrc)
    printf("scatter_ ok=%d at0=%g at2=%g\n", (sc0r: i4), sc0->at(0), sc0->at(2))

    # ---------- P2 pad（每维 before/after 常量填充） ----------
    var pds[4]: i4
    pds[0] = 1
    pds[1] = 0
    pds[2] = 0
    pds[3] = 1
    var pd: tensor@& = b2->pad(pds, 0.0 - 1.0)
    printf("pad dim0=%d dim1=%d at0=%g\n", pd->dim(0), pd->dim(1), pd->at(0))

    # ---------- P2 roll（沿 axis=0 滚动 1） ----------
    var ro: tensor@& = b->roll(1, 0)
    printf("roll at0=%g at5=%g\n", ro->at(0), ro->at(5))

    # ---------- P2 log_softmax（沿 axis=1） ----------
    var lsm: tensor@& = b2->log_softmax(1)
    printf("log_softmax ndim=%d at2=%g\n", lsm->ndim, lsm->at(2))

    # ---------- P2 leaky_relu + gelu ----------
    var lr: tensor@& = ng->leaky_relu(0.1)
    var gl: tensor@& = b->gelu()
    printf("leaky at0=%g gelu at5=%g\n", lr->at(0), gl->at(5))

    # ---------- P2 cross_entropy（logits[2,3] + target[2]） ----------
    var tgt: tensor@& = zeros(1, shp, DT_I8)
    tgt->set_at(0, 2.0)
    tgt->set_at(1, 0.0)
    var ce: f8 = b2->cross_entropy(tgt)
    printf("cross_entropy=%.4f\n", ce)

    # ---------- P5 loss：mse / nll / bce_with_logits ----------
    var mset: tensor@& = ones(2, shp, DT_F8)
    var mse: f8 = b2->mse_loss(mset)
    var nll: f8 = lsm->nll_loss(tgt)
    var bct: tensor@& = zeros(2, shp, DT_F8)
    bct->set_at(1, 1.0)
    bct->set_at(3, 1.0)
    bct->set_at(5, 1.0)
    var bce: f8 = b2->bce_with_logits(bct)
    printf("mse=%.4f nll=%.4f bce=%.4f\n", mse, nll, bce)

    # ---------- P5 layer_norm / dropout ----------
    var ln: tensor@& = b2->layer_norm(1, 0.00001)
    rand_seed(7)
    var dr: tensor@& = b2->dropout(0.5, true)
    var dr_eval: tensor@& = b2->dropout(0.5, false)
    printf("layer_norm at2=%.4f at5=%.4f dropout sum=%.4f eval_eq=%d\n", ln->at(2), ln->at(5), dr->sum_all(), (dr_eval->allclose(b2, 0.000001, 0.000001): i4))

    # ---------- P3 三角函数（sin/cos） ----------
    var sn: tensor@& = b->sin()
    var cn: tensor@& = b->cos()
    printf("sin at1=%.4f cos at0=%.4f\n", sn->at(1), cn->at(0))

    # ---------- P3 atan2 ----------
    var a2: tensor@& = b2->atan2(a)
    printf("atan2 at0=%.4f\n", a2->at(0))

    # ---------- P3 median / percentile（全规约） ----------
    printf("median_all=%g pct50=%g\n", b->median_all(), b->percentile_all(50.0))

    # ---------- P3 triu / tril ----------
    var tu: tensor@& = b2->triu(0)
    var tl: tensor@& = b2->tril(0)
    printf("triu at1=%g at3=%g tril at1=%g at3=%g\n", tu->at(1), tu->at(3), tl->at(1), tl->at(3))

    # ---------- P3 线代：构造 2x2 SPD 矩阵 M=[[4,1],[1,3]] ----------
    var s22[2]: i4
    s22[0] = 2
    s22[1] = 2
    var M: tensor@& = zeros(2, s22, DT_F8)
    M->set_at(0, 4.0)
    M->set_at(1, 1.0)
    M->set_at(2, 1.0)
    M->set_at(3, 3.0)
    printf("det=%.4f norm=%.4f\n", M->det(), M->norm(2.0))

    # ---------- P3 inv ----------
    var Mi: tensor@& = M->inv()
    printf("inv at0=%.4f at3=%.4f\n", Mi->at(0), Mi->at(3))

    # ---------- P3 solve M x = [1,2] ----------
    var bb: tensor@& = zeros(1, s22, DT_F8)
    bb->set_at(0, 1.0)
    bb->set_at(1, 2.0)
    var xx: tensor@& = M->solve(bb)
    printf("solve at0=%.4f at1=%.4f\n", xx->at(0), xx->at(1))

    # ---------- P3 cholesky ----------
    var Lc: tensor@& = M->cholesky()
    printf("chol at0=%.4f at2=%.4f at3=%.4f\n", Lc->at(0), Lc->at(2), Lc->at(3))

    # ---------- P3 eigh（升序特征值） ----------
    var eout[2]: &
    var eok: bool = M->eigh((eout: &))
    var evals: tensor@& = (eout[0]: tensor&)
    var evecs: tensor@& = (eout[1]: tensor&)
    printf("eigh ok=%d lo=%.4f hi=%.4f\n", (eok: i4), evals->at(0), evals->at(1))

    # ---------- P3 qr（[2,2]） ----------
    var qout[2]: &
    var qok: bool = M->qr((qout: &))
    var Qm: tensor@& = (qout[0]: tensor&)
    var Rm: tensor@& = (qout[1]: tensor&)
    printf("qr ok=%d R00=%.4f R11=%.4f\n", (qok: i4), Rm->at(0), Rm->at(3))

    # ---------- P5 bmm / addmm / sdpa ----------
    var bu: tensor@& = b2->unsqueeze(0)
    var du: tensor@& = d->unsqueeze(0)
    var bm: tensor@& = bu->bmm(du)
    printf("bmm dim0=%d dim1=%d dim2=%d at3=%g\n", bm->dim(0), bm->dim(1), bm->dim(2), bm->at(3))

    var ones22: tensor@& = ones(2, s22, DT_F8)
    var adm: tensor@& = ones22->addmm(M, Mi, 1.0, 1.0)
    printf("addmm at0=%.4f at1=%.4f at3=%.4f\n", adm->at(0), adm->at(1), adm->at(3))

    var s12[3]: i4
    s12[0] = 1
    s12[1] = 2
    s12[2] = 2
    var q3: tensor@& = zeros(3, s12, DT_F8)
    q3->set_at(0, 1.0)
    q3->set_at(3, 1.0)
    var k3: tensor@& = zeros(3, s12, DT_F8)
    k3->set_at(0, 1.0)
    k3->set_at(3, 1.0)
    var v3: tensor@& = zeros(3, s12, DT_F8)
    v3->set_at(0, 10.0)
    v3->set_at(3, 20.0)
    var sd: tensor@& = q3->sdpa(k3, v3, false)
    var sdc: tensor@& = q3->sdpa(k3, v3, true)
    printf("sdpa at0=%.4f at1=%.4f causal at0=%.4f at1=%.4f\n", sd->at(0), sd->at(1), sdc->at(0), sdc->at(1))

    # ---------- P5 conv / pool ----------
    var x1sh[3]: i4
    x1sh[0] = 1
    x1sh[1] = 1
    x1sh[2] = 6
    var x1raw: tensor@& = arange(0.0, 6.0, 1.0, DT_F8)
    var x1d: tensor@& = x1raw->reshape(3, x1sh)
    var w1sh[3]: i4
    w1sh[0] = 1
    w1sh[1] = 1
    w1sh[2] = 3
    var w1d: tensor@& = ones(3, w1sh, DT_F8)
    var b1sh[1]: i4
    b1sh[0] = 1
    var b1d: tensor@& = zeros(1, b1sh, DT_F8)
    var c1d: tensor@& = x1d->conv1d(w1d, b1d, 1, 0)
    var p1m: tensor@& = x1d->max_pool1d(2, 2, 0)
    var p1a: tensor@& = x1d->avg_pool1d(2, 2, 0)
    printf("conv1d at0=%g at3=%g pool1d max2=%g avg2=%.4f\n", c1d->at(0), c1d->at(3), p1m->at(2), p1a->at(2))

    var x2sh[4]: i4
    x2sh[0] = 1
    x2sh[1] = 1
    x2sh[2] = 3
    x2sh[3] = 3
    var x2raw: tensor@& = arange(0.0, 9.0, 1.0, DT_F8)
    var x2d: tensor@& = x2raw->reshape(4, x2sh)
    var w2sh[4]: i4
    w2sh[0] = 1
    w2sh[1] = 1
    w2sh[2] = 2
    w2sh[3] = 2
    var w2d: tensor@& = ones(4, w2sh, DT_F8)
    var b2d: tensor@& = zeros(1, b1sh, DT_F8)
    var c2d: tensor@& = x2d->conv2d(w2d, b2d, 1, 1, 0, 0)
    var p2m: tensor@& = x2d->max_pool2d(2, 2, 1, 1, 0, 0)
    var p2a: tensor@& = x2d->avg_pool2d(2, 2, 1, 1, 0, 0)
    printf("conv2d at0=%g at3=%g pool2d max0=%g max3=%g avg0=%.4f avg3=%.4f\n", c2d->at(0), c2d->at(3), p2m->at(0), p2m->at(3), p2a->at(0), p2a->at(3))

    # ---------- P4 tri ----------
    var trm: tensor@& = tri(3, 3, 0, DT_F8)
    printf("tri sum=%g at0=%g at1=%g\n", trm->sum_all(), trm->at(0), trm->at(1))

    # ---------- meshgrid（ij：xa[3] × ya[2] → 两个 [3,2] 网格） ----------
    var xa: tensor@& = arange(0.0, 3.0, 1.0, DT_F8)
    var ya: tensor@& = arange(0.0, 2.0, 1.0, DT_F8)
    var marr[2]: &
    marr[0] = (xa: &)
    marr[1] = (ya: &)
    var mout[2]: &
    var mgok: bool = meshgrid((marr: &), 2, 0, (mout: &))
    var gx: tensor@& = (mout[0]: tensor&)
    var gy: tensor@& = (mout[1]: tensor&)
    printf("meshgrid ok=%d gx d0=%d d1=%d at0=%g at5=%g gy at0=%g at5=%g\n", (mgok: i4), gx->dim(0), gx->dim(1), gx->at(0), gx->at(5), gy->at(0), gy->at(5))

    # ---------- P4 随机：种子 + uniform/normal/randint/permutation ----------
    rand_seed(42)
    var ru: tensor@& = rand_uniform(2, shp, 0.0, 1.0, DT_F8)
    var rnd: tensor@& = rand_normal(2, shp, 0.0, 1.0, DT_F8)
    var ri: tensor@& = rand_randint(2, shp, 0, 10, DT_I8)
    var pm: tensor@& = permutation(5, DT_I8)
    printf("rand numel=%lld randint numel=%lld perm sum=%g\n", ru->numel, ri->numel, pm->sum_all())
    printf("normal numel=%lld\n", rnd->numel)

    # ---------- P4 shuffle_（原地） ----------
    var shf: tensor@& = arange(0.0, 6.0, 1.0, DT_F8)
    var shfok: bool = shf->shuffle_()
    printf("shuffle ok=%d sum=%g\n", (shfok: i4), shf->sum_all())

    # ---------- P4 save / load 往返（NumPy .npy） ----------
    var svok: bool = b2->save("/tmp/ts_basic.npy")
    var ld: tensor@& = ts_load("/tmp/ts_basic.npy")
    printf("save ok=%d load dim0=%d dim1=%d at5=%g\n", (svok: i4), ld->dim(0), ld->dim(1), ld->at(5))

    return 0
