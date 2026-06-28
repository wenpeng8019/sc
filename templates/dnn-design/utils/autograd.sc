# ============================================================
# utils/autograd.sc —— 突触束算子（自动微分核心，可复用模块对象）
# ============================================================
# 一条 dep（一个下游神经元的整束全连接入边）的前向 / 反向算子，对任意入突触数 n
# 动态成立。所有 dep 的 follow 体都复用这一对函数，网络拓扑只决定「连谁」，数值与
# 梯度逻辑全在此处——这正是「张量之上的 autograd 引擎」那一层。
# ============================================================
inc neuron.sc                       # 复用 neuron 侧车类型与激活库

# 前向：z = bias + Σ wᵢ·xᵢ → 激活 → 写下游神经元（act/pre）。
#   toks = 本 dep 的句柄数组（前 n 个为上游源），dn = 下游神经元侧车。
@fnc edge_fwd: toks: token&&, n: i4, dn: neuron&
    var z: f4 = dn->bias
    for i in n
        var up: neuron& = (toks[i]->ctx(): neuron&)
        z = z + dn->w[i] * up->act
    dn->pre = z
    dn->act = act_fwd(z, dn->akind)
    return

# 反向（链式法则）：gz = grad·act'(pre)；∂L/∂wᵢ += gz·xᵢ，∂L/∂b += gz，
#   并把 wᵢ·gz 累加回上游 grad（多下游时按 back 反拓扑序自动汇聚，时序天然正确）。
@fnc edge_bwd: toks: token&&, n: i4, dn: neuron&
    var gz: f4 = dn->grad * act_bwd(dn->pre, dn->akind)
    for i in n
        var up: neuron& = (toks[i]->ctx(): neuron&)
        dn->gw[i] = dn->gw[i] + gz * up->act
        up->grad = up->grad + dn->w[i] * gz
    dn->gbias = dn->gbias + gz
    return
