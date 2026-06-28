inc ts.sc
inc nn.sc
inc ../utils/train_utils.sc

fnc main: i4
    rand_seed((42: i8))

    var sx[2]: i4
    sx[0] = 1
    sx[1] = 2
    var xb[2]: f4
    xb[0] = 1.0
    xb[1] = 2.0
    var tb[2]: f4
    tb[0] = 5.0
    tb[1] = 8.0

    var x: tensor& = from_data(2, sx, (xb: &), DT_F4)
    var t: tensor& = from_data(2, sx, (tb: &), DT_F4)

    var fc: linear& = nn_linear(2, 2)
    var opt: optim& = nn_sgd(0.1)
    opt->track_linear(fc)

    var loss: f8 = train_linear(fc, opt, x, t, 100)
    printf("final loss=%.6f\n", loss)

    # 推理
    var xi: val& = nn_input(x)
    var y: val& = fc->forward(xi)
    var yv: tensor& = y->value()
    printf("y=[%.4f, %.4f]\n", yv->at(0), yv->at(1))
    yv->drop()
    nn_tape_clear()

    fc->drop()
    opt->drop()
    x->drop()
    t->drop()
    return 0
