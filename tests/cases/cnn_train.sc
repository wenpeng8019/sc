# nn 卷积网络端到端训练回归用例（define-by-run + conv 模块）：
#   - conv 模块（W[Cout,Cin,Kh,Kw], b[Cout]，Kaiming 初始化）+ relu + max_pool2d；
#   - reshape 展平 → linear → cross_entropy；Adam 优化；
#   - 2 样本二分类，验证多轮训练后损失明显下降；
#   - 末尾 nn_tape_clear() + 各模块 drop()。
inc ts.sc
inc nn.sc

fnc main: i4
    rand_seed((3: i8))

    # 输入 x[2,1,5,5]，两个样本
    var sx[4]: i4
    sx[0] = 2
    sx[1] = 1
    sx[2] = 5
    sx[3] = 5
    var xt: tensor@1 = rand_normal(4, (sx: &), 0.0, 1.0, DT_F4)

    # 目标类标 [2] = {0, 1}
    var stg[1]: i4
    stg[0] = 2
    var tb[2]: f4
    tb[0] = 0.0
    tb[1] = 1.0
    var tt: tensor@1 = from_data(1, (stg: &), (tb: &), DT_F4)

    var conv: conv@1 = nn_conv2d(1, 3, 3, 3, 1, 1, 0, 0)   # [2,3,3,3]
    var fc: linear@1 = nn_linear(12, 2)                    # 3*2*2=12 → 2
    var opt: optim@1 = nn_adam(0.02)
    opt->track_conv2d(conv)
    opt->track_linear(fc)

    var fsh[2]: i4
    fsh[0] = 2
    fsh[1] = 12

    var first: f8 = 0.0
    var last: f8 = 0.0
    for e in 150
        var x: val& = nn_input(xt)
        var tg: val& = nn_input(tt)
        var c: val& = conv->forward(x)
        var cr: val& = c->relu()
        var p: val& = cr->max_pool2d(2, 2, 1, 1, 0, 0)    # [2,3,2,2]
        var fl: val& = p->reshape(2, (fsh: &))            # [2,12]
        var logit: val& = fc->forward(fl)
        var loss: val& = logit->cross_entropy(tg)
        loss->backward()
        opt->step()
        opt->zero_grad()
        last = loss->item()
        if e == 0
            first = last
        nn_tape_clear()
    printf("cnn_down=%d cnn_low=%d\n", (last < first: i4), (last < 0.1: i4))

    return 0
