# ============================================================
# neuron —— 独立神经元基元（tok 设计路线 / dnn-design）
# ============================================================
# 与工业级 `nn`（张量/层粒度，define-by-run tape）成对的「研究设计级」自动微分件：
#   · 一个 tok        = 一个【神经元】（真实数据放 neuron 侧车，token 纯做触发/拓扑）
#   · 一条 dep        = 一束【全连接突触】（多对一）
#   · 一句 back       = 反向传播（反拓扑序由 tok 内核免费保证 → 链式法则次序天然正确）
#
# 本模块只提供「与网络拓扑无关」的可复用件——【神经元对象 neuron】及其算子。【拓扑】
# （tok 声明 + dep 连边 + back）仍由使用方在自己的 .sc 里显式写出，与 tok/dep/back 框架
# 直接配合——这正是设计路线的价值：autograd 的【机制】（谁连谁、梯度何时流）全程可见、
# 可断点。数值算术虽收进本库，但都是纯标量、纯 sc，可直接阅读本文件。
#
# 抽象：以【神经元对象 `neuron`】为中心，把「主语是单个神经元」的操作做成方法
#   （n->forward / backward / sgd / adam / seed_mse，均返回 this 可链式）；纯标量数学
#   （act_fwd/act_bwd/mse_loss）与跨多神经元的操作（xent_seed）保持自由函数。
#   dep follow 体的样板由 edge_step 收成一行。
#
# 自动微分【引擎】本身 = tok 的 `back`/`TOK_BACK`（已是 tok 内核）；本库是其【算子层】。
#
# 用法：`inc neuron.sc`，dep 体内仅需 `edge_step(this, 下游token)` + `return false`。
# 激活种类用枚举裸名常量 AK_*（跨模块可引用）。
# 依赖 libm：expf/tanhf/logf/powf/sqrtf（macOS 随 libSystem 自动链接；Linux 须 -lm）。
# ============================================================

# ---- libm（标量超越函数）----
@fnc expf:: f4, x: f4
@fnc logf:: f4, x: f4
@fnc tanhf:: f4, x: f4
@fnc powf:: f4, base: f4, ex: f4
@fnc sqrtf:: f4, x: f4

# ---- 激活种类（跨模块裸名枚举常量；与 neuron.akind 字段约定一致）----
@def act_kind: [
    AK_IDENT = 0    # 恒等 a=z（回归输出层 / 线性）
    AK_RELU         # ReLU      a=max(0,z)
    AK_LEAKY        # Leaky-ReLU a=z>0?z:0.01z（避免死神经元）
    AK_SIGMOID      # Logistic  a=1/(1+e^-z)
    AK_TANH         # 双曲正切   a=tanh(z)
] : i4

# ============================================================
# 激活函数库（纯标量算术，供 neuron 方法调用）
# ============================================================
@fnc act_fwd: f4, z: f4, k: i4
    if k == AK_RELU
        if z > 0.0f
            return z
        return 0.0f
    if k == AK_LEAKY
        if z > 0.0f
            return z
        return 0.01f * z
    if k == AK_SIGMOID
        return 1.0f / (1.0f + expf(0.0f - z))
    if k == AK_TANH
        return tanhf(z)
    return z                        # AK_IDENT

@fnc act_bwd: f4, pre: f4, k: i4
    if k == AK_RELU
        if pre > 0.0f
            return 1.0f
        return 0.0f
    if k == AK_LEAKY
        if pre > 0.0f
            return 1.0f
        return 0.01f
    if k == AK_SIGMOID
        var s: f4 = 1.0f / (1.0f + expf(0.0f - pre))
        return s * (1.0f - s)
    if k == AK_TANH
        var t: f4 = tanhf(pre)
        return 1.0f - t * t
    return 1.0f                     # AK_IDENT

# ============================================================
# neuron —— 神经元对象（数据侧车 + 方法）
# ============================================================
# token 管拓扑/触发/反向序；neuron 管真实数据。经 `form t, v, &n` 绑到 token 的 ctx；
# dep 体经 `down->ctx()` 取下游、`toks[i]->ctx()` 取上游。
# 动量字段一组复用：SGD 用 vw/vbias 作速度，Adam 用 vw/vbias 作一阶矩、sw/sbias 作二阶矩
# （同一时刻只用一种优化器）。方法均返回 this（neuron&），可链式调用。
@def neuron: {
    act:   f4               # 前向激活输出 a = act(z)
    pre:   f4               # 前激活 z（反向算 act' 用 = save_for_backward）
    grad:  f4               # 反传累加的 ∂L/∂a（每步反向前清零，back 自动累加）
    bias:  f4               # 偏置 b
    gbias: f4               # ∂L/∂b
    vbias: f4               # 偏置：SGD 速度 / Adam 一阶矩
    sbias: f4               # 偏置：Adam 二阶矩
    nin:   i4               # 入突触数（= 上游神经元数）
    akind: i4               # 激活种类（AK_*）
    w[8]:  f4               # 入突触权重 wᵢ
    gw[8]: f4               # ∂L/∂wᵢ
    vw[8]: f4               # 权重：SGD 速度 / Adam 一阶矩
    sw[8]: f4               # 权重：Adam 二阶矩

    # 前向：z = bias + Σ wᵢ·xᵢ → 激活，写自身 pre/act。
    #   ins —— 上游 token 数组（dep 的 toks）；n —— 入突触数。
    forward: fnc: neuron&, ins: token&&, n: i4
        var z: f4 = this->bias
        for i in n
            var up: neuron& = (ins[i]->ctx(): neuron&)
            z = z + this->w[i] * up->act
        this->pre = z
        this->act = act_fwd(z, this->akind)
        return this

    # 反向（链式法则）：gz = grad·act'(pre)；∂L/∂wᵢ += gz·xᵢ，∂L/∂b += gz，
    #   并把 wᵢ·gz 累加回上游 grad（多下游时按 back 反拓扑序自动汇聚，时序天然正确）。
    backward: fnc: neuron&, ins: token&&, n: i4
        var gz: f4 = this->grad * act_bwd(this->pre, this->akind)
        for i in n
            var up: neuron& = (ins[i]->ctx(): neuron&)
            this->gw[i] = this->gw[i] + gz * up->act
            up->grad = up->grad + this->w[i] * gz
        this->gbias = this->gbias + gz
        return this

    # 带动量 SGD 一步：v ← momentum·v − lr·g；θ ← θ + v。
    sgd: fnc: neuron&, lr: f4, momentum: f4
        for i in this->nin
            this->vw[i] = momentum * this->vw[i] - lr * this->gw[i]
            this->w[i] = this->w[i] + this->vw[i]
        this->vbias = momentum * this->vbias - lr * this->gbias
        this->bias = this->bias + this->vbias
        return this

    # Adam 一步：一阶矩 vw、二阶矩 sw 指数滑动 + 偏差校正（t = 当前步数，从 1 起）。
    #   常用超参 beta1=0.9 beta2=0.999 eps=1e-8。
    adam: fnc: neuron&, lr: f4, beta1: f4, beta2: f4, eps: f4, t: i4
        var bc1: f4 = 1.0f - powf(beta1, (t: f4))
        var bc2: f4 = 1.0f - powf(beta2, (t: f4))
        for i in this->nin
            this->vw[i] = beta1 * this->vw[i] + (1.0f - beta1) * this->gw[i]
            this->sw[i] = beta2 * this->sw[i] + (1.0f - beta2) * this->gw[i] * this->gw[i]
            var mh: f4 = this->vw[i] / bc1
            var vh: f4 = this->sw[i] / bc2
            this->w[i] = this->w[i] - lr * mh / (sqrtf(vh) + eps)
        this->vbias = beta1 * this->vbias + (1.0f - beta1) * this->gbias
        this->sbias = beta2 * this->sbias + (1.0f - beta2) * this->gbias * this->gbias
        var mhb: f4 = this->vbias / bc1
        var vhb: f4 = this->sbias / bc2
        this->bias = this->bias - lr * mhb / (sqrtf(vhb) + eps)
        return this

    # MSE 反向播种：grad = ∂MSE/∂pred = 2(act-target)（输出层任意激活皆适用）。
    seed_mse: fnc: neuron&, target: f4
        this->grad = 2.0f * (this->act - target)
        return this
}

# ============================================================
# 突触束调度（dep follow 体的统一一行入口）
# ============================================================
# 把每条 dep 的样板（前向触发级联 / active=-4 反传）收成一行。
# 用法：dep 体内仅需 `edge_step(this, c)` + `return false`。
#   d    —— follow 上下文（this，__scdep_in&）：toks/count/active；
#   down —— 本束的下游目标神经元 token（map c:"…" 的 c）。
@fnc edge_step: d: __scdep_in&, down: token&
    var dn: neuron& = (down->ctx(): neuron&)
    if d->active == 0 - 4
        dn->backward(d->toks, d->count - 1)
        return
    dn->forward(d->toks, d->count - 1)
    down->pulse((0: *), 0)
    return

# ============================================================
# 损失（纯标量 / 跨多神经元）
# ============================================================
# MSE（单输出回归）：返回 (pred-target)²。配 neuron.seed_mse 播种输出梯度。
@fnc mse_loss: f4, pred: f4, target: f4
    var d: f4 = pred - target
    return d * d

# Softmax 交叉熵（多分类）：outs 为输出神经元【指针数组】（neuron&&），n 类别数，
#   target 真类下标。播种每个输出 grad = softmaxᵢ − [i==target]（输出层须 AK_IDENT，
#   故 logit 即 act，反传 gz=grad）。返回该样本交叉熵 −log(softmax[target])。
@fnc xent_seed: f4, outs: neuron&&, n: i4, target: i4
    var mx: f4 = outs[0]->act
    for i in n
        if outs[i]->act > mx
            mx = outs[i]->act
    var s: f4 = 0.0f
    for i in n
        s = s + expf(outs[i]->act - mx)
    for i in n
        var sm: f4 = expf(outs[i]->act - mx) / s
        outs[i]->grad = sm
    var dn: neuron& = outs[target]
    dn->grad = dn->grad - 1.0f
    var smt: f4 = expf(outs[target]->act - mx) / s
    return 0.0f - logf(smt)
