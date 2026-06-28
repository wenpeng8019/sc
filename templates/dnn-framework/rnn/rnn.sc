inc ts.sc
inc ../utils/tensor_utils.sc

# RNN 模板：单步 recurrence 骨架（h_t = tanh(Wx x_t + Wh h_{t-1} + b)）
fnc main: i4
    var s21[2]: i4
    s21[0] = 2
    s21[1] = 1
    var xb[2]: f4
    xb[0] = 1.0
    xb[1] = 2.0
    var hb[2]: f4
    hb[0] = 0.0
    hb[1] = 0.0

    var x: tensor& = from_data(2, s21, (xb: &), DT_F4)
    var hprev: tensor& = from_data(2, s21, (hb: &), DT_F4)

    var s22[2]: i4
    s22[0] = 2
    s22[1] = 2
    var Wx: tensor& = ones(2, s22, DT_F4)
    var Wh: tensor& = ones(2, s22, DT_F4)
    var b: tensor& = zeros(2, s21, DT_F4)

    var a: tensor& = Wx->matmul(x)
    var hlin: tensor& = Wh->matmul(hprev)
    a->add_(hlin)
    a->add_(b)
    var h: tensor& = a->tanh()
    print_vec(h, "rnn.h")

    x->drop()
    hprev->drop()
    Wx->drop()
    Wh->drop()
    b->drop()
    a->drop()
    hlin->drop()
    h->drop()
    return 0
