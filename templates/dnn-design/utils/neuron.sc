# ============================================================
# utils/neuron.sc —— 神经元侧车结构 + 激活函数库（可复用模块对象）
# ============================================================
# 本单元只导出「与网络拓扑无关」的通用件，被 hello-dnn 等项目 inc：
#   · neuron     —— 一个神经元的真实数据（激活/梯度/权重/动量），与一个 token 一一对应。
#   · act_fwd    —— 激活函数前向。
#   · act_bwd    —— 激活函数导数（反向用）。
# 激活种类用整型编码（0=恒等 / 1=ReLU / 2=Leaky-ReLU）；命名常量因 let 不跨模块，
# 由消费端（hello.sc）自行定义 A_IDENT/A_RELU/A_LEAKY，与此处编码约定一致。
# ============================================================

# 神经元侧车：token 管拓扑 + 触发 + 反向序；neuron 管真实数据。
# 经 form t,v,&n 绑到 token 的 ctx，dep 体经 t->ctx() 取下游、this->toks[i]->ctx() 取上游。
@def neuron: {
    act:   f4               # 前向激活输出 a = act(z)
    pre:   f4               # 前激活 z（反向算 act' 用）
    grad:  f4               # 反传累加的 ∂L/∂a（每步反向前清零，back 自动累加）
    bias:  f4               # 偏置 b
    gbias: f4               # ∂L/∂b
    vbias: f4               # 偏置动量速度
    nin:   i4               # 入突触数（= 上游神经元数）
    akind: i4               # 激活种类（0=恒等 / 1=ReLU / 2=Leaky-ReLU）
    w[8]:  f4               # 入突触权重 wᵢ（[i] = 上游 i → 本神经元）
    gw[8]: f4               # ∂L/∂wᵢ
    vw[8]: f4               # 权重动量速度
}

# 激活前向 —— 纯算术，零外部依赖。
#   扩展 sigmoid/tanh：声明 `fnc expf:: f4, x: f4`（libm，macOS 自动链接 / Linux 加 -lm），
#   在此追加分支即可（如 sigmoid=1/(1+exp(-z))，导数 a·(1-a)）。
@fnc act_fwd: f4, z: f4, k: i4
    if k == 1                       # ReLU
        if z > 0.0f
            return z
        return 0.0f
    if k == 2                       # Leaky-ReLU（斜率 0.01，避免死神经元）
        if z > 0.0f
            return z
        return 0.01f * z
    return z                        # 恒等

# 激活导数（反向链式法则用）。
@fnc act_bwd: f4, pre: f4, k: i4
    if k == 1
        if pre > 0.0f
            return 1.0f
        return 0.0f
    if k == 2
        if pre > 0.0f
            return 1.0f
        return 0.01f
    return 1.0f
