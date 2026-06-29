# 由 scc --emit-sc 从 AST 再生成

inc ts.sc

inc nn.sc

fnc main: i4
    rand_seed((7: i8))
    var sx[2]: i4
    sx[0] = 1
    sx[1] = 2
    var xb[2]: f4
    xb[0] = 1.0
    xb[1] = 2.0
    var tb[2]: f4
    tb[0] = 5.0
    tb[1] = 8.0
    var xt: tensor@1 = from_data(2, sx, (xb: &), DT_F4)
    var tt: tensor@1 = from_data(2, sx, (tb: &), DT_F4)
    var fc1: linear@1 = nn_linear(2, 4)
    var fc2: linear@1 = nn_linear(4, 2)
    var opt: optim@1 = nn_sgd(0.05)
    opt->track_linear(fc1)
    opt->track_linear(fc2)
    var last: f8 = 0.0
    for e in 100
        var x: val& = nn_input(xt)
        var tgt: val& = nn_input(tt)
        var h: val& = fc1->forward(x)
        var hr: val& = h->relu()
        var y: val& = fc2->forward(hr)
        var loss: val& = y->mse_loss(tgt)
        loss->backward()
        opt->step()
        opt->zero_grad()
        last = loss->item()
        nn_tape_clear()
    printf("trained loss<1=%d\n", (last < 1.0: i4))
    var x2: val& = nn_input(xt)
    var h2: val& = fc1->forward(x2)
    var hr2: val& = h2->relu()
    var y2: val& = fc2->forward(hr2)
    var yv: tensor@1 = y2->value()
    printf("y0_close=%d y1_close=%d\n", (yv->at(0) > 4.0: i4), (yv->at(1) > 7.0: i4))
    nn_tape_clear()
    return 0
