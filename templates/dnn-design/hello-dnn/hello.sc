# ============================================================
# hello-dnn —— dnn-design 的最小可运行示例工程
# ============================================================
# 用 tok 原语把「自动微分引擎」本身显式展开：tok = 一个【神经元】，dep = 一个下游神经元
# 的整束【全连接突触】（多对一），back = 一句话反向传播。每个神经元、每条突触、每步梯度
# 都看得见、可调试——面向研究与学习，而非工业级吞吐（粒度取舍见 ../README.md）。
#
# 本工程只负责【网络拓扑 + 训练流程】；通用件全部 inc 自独立神经元模块 neuron：
#   neuron.sc  神经元对象 neuron（方法 forward/backward/sgd/adam/seed_mse）+ 激活库
#          act_fwd/act_bwd + 统一调度 edge_step + 损失 mse_loss/xent_seed。
#          （neuron = 独立神经元基元，是工业级张量库 nn 的「研究设计级」对照件。）
#
# tok 机制 → 神经网络概念：
#   tok t:"n.h0"          一个神经元（真实数据放侧车 neuron，token 纯做触发/拓扑）
#   dep all: i0,i1 map h0 h0 的整束入边（与门 all = 全连接：所有上游就绪才前向）
#   follow else 分支       前向算子：调 n->forward，激活后 pulse 触发级联
#   follow TOK_BACK 分支   反向算子：调 n->backward（链式法则 + 梯度汇聚）
#   back loss             按【反拓扑序】唤起每条 dep 的反向分支——顺序由 tok 内核免费保证
#
# 内置网络：2-4-1 MLP（输入2 → 隐层4 leaky-relu → 输出1 线性），MSE + 带动量 SGD，
#   学习仿射目标 y = x0 + 2·x1（4 样本）。训练日志显示 loss 单调下降、推理逼近真值。
# ============================================================

@@                            # 根模块：本工程为可执行入口（同目录唯一的 main/@@）

inc neuron.sc                 # 独立神经元基元（neuron 对象/激活/edge_step/优化器/损失）

# ---- 超参数（let 不跨模块，故与优化器解耦、在此定义并传入）----
let LR:       f4 = 0.01f      # 学习率
let MOMENTUM: f4 = 0.9f       # 动量系数
let EPOCHS:   i4 = 1000       # 训练轮数
let NSAMP:    i4 = 4          # 样本数

# ============================================================
# 网络拓扑：tok = 神经元（2-4-1）。
# ============================================================
tok i0: "n.i0"
tok i1: "n.i1"
tok h0: "n.h0"
tok h1: "n.h1"
tok h2: "n.h2"
tok h3: "n.h3"
tok o0: "n.o0"
tok loss: "n.loss"

# ---- 突触束：dep = 一个下游神经元的整束全连接入边（与门 = 全连接）----
# 输入层 → 隐层（4 条；每条 2 个上游）。dep 体经 edge_step 统一调度：
#   前向触发级联 / active=-4 反传，样板收成一行。
dep all: a:"n.i0", b:"n.i1" map c:"n.h0"
    edge_step(this, c)
    return false

dep all: a:"n.i0", b:"n.i1" map c:"n.h1"
    edge_step(this, c)
    return false

dep all: a:"n.i0", b:"n.i1" map c:"n.h2"
    edge_step(this, c)
    return false

dep all: a:"n.i0", b:"n.i1" map c:"n.h3"
    edge_step(this, c)
    return false

# 隐层 → 输出（1 条；4 个上游）—— 全连接汇聚
dep all: a:"n.h0", b:"n.h1", c:"n.h2", d:"n.h3" map o:"n.o0"
    edge_step(this, o)
    return false

# 输出 → 损失：前向算 MSE；反向播种输出层 grad = ∂MSE/∂pred = 2(pred-target)。
#   back loss 时本边 rank 最大、最先唤起 → 先播种 o0.grad，再逐层反传，顺序天然正确。
dep all: p:"n.o0" map l:"n.loss"
    var on: neuron& = (p->ctx(): neuron&)
    if this->active == 0 - 4
        on->seed_mse(g_target)
        return false
    var ln: neuron& = (l->ctx(): neuron&)
    ln->act = mse_loss(on->act, g_target)
    l->pulse((0: @), 0)
    return false

# ============================================================
# 全局状态：神经元侧车数组 + 数据集 + 当前样本目标。
# ============================================================
var IN[2]:  neuron
var HID[4]: neuron
var OUT[1]: neuron
var LOSS_N: neuron

var g_target: f4 = 0.0f       # 当前样本监督目标（loss 边读它）

# 数据集（仿射 y = x0 + 2·x1）：输入取 O(1) 正值，梯度信号充分、leaky-relu 隐层不致全死。
var DS_X0[4]: f4 = [1.0f, 3.0f, 2.0f, 4.0f]
var DS_X1[4]: f4 = [2.0f, 1.0f, 3.0f, 2.0f]
var DS_Y[4]:  f4 = [5.0f, 5.0f, 8.0f, 8.0f]

# ============================================================
# 训练引擎
# ============================================================

# 确定性伪随机（LCG）：可复现的权重初始化。
var g_seed: u4 = 22695477
fnc rndf: f4
    g_seed = g_seed * 1103515245 + 12345
    var r: u4 = (g_seed >> 16) & 32767
    return (r: f4) / 32768.0f - 0.5f

fnc init_net:
    for k in 4                    # 隐层：leaky-relu，随机权重（破对称 + 足够强的初始信号）
        HID[k].nin = 2
        HID[k].akind = AK_LEAKY
        HID[k].bias = 0.0f
        HID[k].w[0] = rndf()
        HID[k].w[1] = rndf()
    OUT[0].nin = 4                # 输出：线性
    OUT[0].akind = AK_IDENT
    OUT[0].bias = 0.0f
    for i in 4
        OUT[0].w[i] = rndf()
    return

# 每步反向前清零神经元 grad（back 累加；输出 grad 由 loss 边覆盖写，无须清）。
fnc zero_grad:
    for k in 2
        IN[k].grad = 0.0f
    for k in 4
        HID[k].grad = 0.0f
    OUT[0].grad = 0.0f
    return

# 清零参数梯度累加器（每个 mini-batch 步前）。
fnc zero_gw:
    for k in 4
        HID[k].gbias = 0.0f
        for i in 2
            HID[k].gw[i] = 0.0f
    OUT[0].gbias = 0.0f
    for i in 4
        OUT[0].gw[i] = 0.0f
    return

# 一次前向：灌输入神经元 → pulse 输入 token → 沿全连接突触级联到 loss。
fnc forward: f4, x0: f4, x1: f4
    IN[0].act = x0
    IN[1].act = x1
    i0->pulse((0: @), 0)          # pulse 而非 set：训练多轮、值不变也强制重算（迭代语义）
    i1->pulse((0: @), 0)          # 两个输入集齐 → 隐层与门触发 → 逐层级联到 o0/loss
    return OUT[0].act

# 优化器一步：对各神经元调 neuron 的 sgd 方法（传入超参）。
fnc sgd_step:
    for k in 4
        HID[k].sgd(LR, MOMENTUM)
    OUT[0].sgd(LR, MOMENTUM)
    return

# 训练 EPOCHS 轮（逐样本 SGD：清梯度 → 前向 → back 反传 → 更新）。
fnc train: i4
    for e in EPOCHS
        var tot: f4 = 0.0f
        for s in NSAMP
            g_target = DS_Y[s]
            zero_grad()
            zero_gw()
            var pred: f4 = forward(DS_X0[s], DS_X1[s])
            var d: f4 = pred - g_target
            tot = tot + d * d
            back loss             # 自动微分：一句话反向传播（反拓扑序，链式法则）
            sgd_step()            # 优化器一步
        if e % 100 == 0
            print "epoch ", (e: "%4d"), "  loss=", ((tot / 4.0f): "%.5f")
    return 0

fnc main: i4
    init_net()
    # form（输出→输入）：token 就绪 + 绑定神经元侧车。未 form 的 token 不就绪、pulse 不传播。
    form loss, (0: @), &LOSS_N
    form o0,   (0: @), &OUT[0]
    form h0,   (0: @), &HID[0]
    form h1,   (0: @), &HID[1]
    form h2,   (0: @), &HID[2]
    form h3,   (0: @), &HID[3]
    form i0,   (0: @), &IN[0]
    form i1,   (0: @), &IN[1]

    print "=== 编译期烘焙的网络结构（lightmap，O(1) 读取）==="
    print "i0    depth=", i0->depth(),   "  fanout=", i0->fanout(), "  reach=", i0->reach()
    print "h0    depth=", h0->depth(),   "  fanin=",  h0->fanin(),  "  fanout=", h0->fanout(), "  critical=", h0->critical()
    print "o0    depth=", o0->depth(),   "  fanin=",  o0->fanin(),  "  batch_width=", o0->batch_width()
    print "loss  depth=", loss->depth(), "  critical=", loss->critical()

    print "=== 训练（2-4-1 MLP，目标 y = x0 + 2·x1）==="
    train()

    print "=== 推理 ==="
    for s in NSAMP
        var p: f4 = forward(DS_X0[s], DS_X1[s])
        print "x=(", (DS_X0[s]: "%.1f"), ",", (DS_X1[s]: "%.1f"), ")  pred=", (p: "%.4f"), "  target=", (DS_Y[s]: "%.1f")
    return 0
