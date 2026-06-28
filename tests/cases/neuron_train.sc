# neuron 模块回归用例（tok 设计路线 / 独立神经元）：
#   - tok=神经元 / dep=突触束 / back=反向传播；edge_step 折叠 dep 样板（C）；
#   - 隐层 sigmoid + 输出线性，MSE + Adam（覆盖算子库相对 SGD/relu 的拉齐项）；
#   - 单样本回归 [1,2] → [5]，确定性 LCG 初始化，训练后断言 loss 收敛。
# 注：不用 @@ 根标记——tests/cases 是多个独立根共处的目录，@@ 会被同目录其它
#   EXE 用例的 findRootModule 误当作全局根而拉入链接（main 重复）。单文件用例无需。

inc neuron.sc

let LR:    f4 = 0.05f
let B1:    f4 = 0.9f
let B2:    f4 = 0.999f
let EPS:   f4 = 0.00000001f
let EPOCHS: i4 = 400

# 网络：2 → 3(sigmoid) → 1(线性)
tok i0: "g.i0"
tok i1: "g.i1"
tok h0: "g.h0"
tok h1: "g.h1"
tok h2: "g.h2"
tok o0: "g.o0"
tok loss: "g.loss"

dep all: a:"g.i0", b:"g.i1" map c:"g.h0"
    edge_step(this, c)
    return false

dep all: a:"g.i0", b:"g.i1" map c:"g.h1"
    edge_step(this, c)
    return false

dep all: a:"g.i0", b:"g.i1" map c:"g.h2"
    edge_step(this, c)
    return false

dep all: a:"g.h0", b:"g.h1", c:"g.h2" map o:"g.o0"
    edge_step(this, o)
    return false

dep all: p:"g.o0" map l:"g.loss"
    var on: neuron& = (p->ctx(): neuron&)
    if this->active == 0 - 4
        on->seed_mse(g_target)
        return false
    var ln: neuron& = (l->ctx(): neuron&)
    ln->act = mse_loss(on->act, g_target)
    l->pulse((0: @), 0)
    return false

var IN[2]:  neuron
var HID[3]: neuron
var OUT[1]: neuron
var LOSS_N: neuron

var g_target: f4 = 5.0f

var g_seed: u4 = 22695477
fnc rndf: f4
    g_seed = g_seed * 1103515245 + 12345
    var r: u4 = (g_seed >> 16) & 32767
    return (r: f4) / 32768.0f - 0.5f

fnc init_net:
    for k in 3
        HID[k].nin = 2
        HID[k].akind = AK_SIGMOID
        HID[k].bias = 0.0f
        HID[k].w[0] = rndf()
        HID[k].w[1] = rndf()
    OUT[0].nin = 3
    OUT[0].akind = AK_IDENT
    OUT[0].bias = 0.0f
    for i in 3
        OUT[0].w[i] = rndf()
    return

fnc zero_grad:
    for k in 2
        IN[k].grad = 0.0f
    for k in 3
        HID[k].grad = 0.0f
    OUT[0].grad = 0.0f
    return

fnc zero_gw:
    for k in 3
        HID[k].gbias = 0.0f
        for i in 2
            HID[k].gw[i] = 0.0f
    OUT[0].gbias = 0.0f
    for i in 3
        OUT[0].gw[i] = 0.0f
    return

fnc forward: f4
    IN[0].act = 1.0f
    IN[1].act = 2.0f
    i0->pulse((0: @), 0)
    i1->pulse((0: @), 0)
    return OUT[0].act

fnc main: i4
    init_net()
    form loss, (0: @), &LOSS_N
    form o0, (0: @), &OUT[0]
    form h0, (0: @), &HID[0]
    form h1, (0: @), &HID[1]
    form h2, (0: @), &HID[2]
    form i0, (0: @), &IN[0]
    form i1, (0: @), &IN[1]

    var last: f4 = 0.0f
    for e in EPOCHS
        zero_grad()
        zero_gw()
        var pred: f4 = forward()
        var d: f4 = pred - g_target
        last = d * d
        back loss
        var t: i4 = e + 1
        for k in 3
            HID[k].adam(LR, B1, B2, EPS, t)
        OUT[0].adam(LR, B1, B2, EPS, t)

    var pred: f4 = forward()
    printf("loss_small=%d pred_close=%d\n", (last < 0.01f: i4), (pred > 4.8f: i4))
    return 0
