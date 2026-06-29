# ts 端到端示例：构造 -> 统计 -> 线代 -> meshgrid -> save/load
inc ts.sc

fnc main: i4
    # 1) 构造输入
    var shp[2]: i4
    shp[0] = 2
    shp[1] = 3
    var x: tensor@1 = arange(0.0, 6.0, 1.0, DT_F8)
    var x2: tensor@1 = x->reshape(2, shp)

    # 2) 统计与标准化
    var mu: f8 = x2->mean_all()
    var sd: f8 = x2->std_all()
    var xn: tensor@1 = x2->sub_scalar(mu)->div_scalar(sd)
    printf("stats mean=%g std=%g xn_sum=%g\n", mu, sd, xn->sum_all())

    # 3) 线代：SPD 矩阵 det/inv/solve
    var s22[2]: i4
    s22[0] = 2
    s22[1] = 2
    var M: tensor@1 = zeros(2, s22, DT_F8)
    M->set_at(0, 4.0)
    M->set_at(1, 1.0)
    M->set_at(2, 1.0)
    M->set_at(3, 3.0)

    var b: tensor@1 = zeros(1, s22, DT_F8)
    b->set_at(0, 1.0)
    b->set_at(1, 2.0)

    var Minv: tensor@1 = M->inv()
    var sol: tensor@1 = M->solve(b)
    printf("linalg det=%g inv00=%.4f x0=%.4f x1=%.4f\n", M->det(), Minv->at(0), sol->at(0), sol->at(1))

    # 4) meshgrid（ij）
    var xv: tensor@1 = arange(0.0, 3.0, 1.0, DT_F8)
    var yv: tensor@1 = arange(0.0, 2.0, 1.0, DT_F8)
    var inp[2]: &
    inp[0] = (xv: &)
    inp[1] = (yv: &)
    var out[2]: &
    var ok: bool = meshgrid((inp: &), 2, 0, (out: &))
    var gx: tensor@1 = (out[0]: tensor&)
    var gy: tensor@1 = (out[1]: tensor&)
    printf("meshgrid ok=%d gx5=%g gy5=%g\n", (ok: i4), gx->at(5), gy->at(5))

    # 5) save/load（NumPy .npy）
    var svok: bool = x2->save("/tmp/ts_end2end.npy")
    var xl: tensor@1 = ts_load("/tmp/ts_end2end.npy")
    printf("io save_ok=%d same=%d\n", (svok: i4), (x2->allclose(xl, 0.000000000001, 0.000000000001): i4))

    return 0
