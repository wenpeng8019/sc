# ============================================================
# utils/optim.sc —— 优化器（带动量 SGD，可复用模块对象）
# ============================================================
# 按已累加的参数梯度（gw/gbias）更新一个神经元的权重与偏置。学习率 lr、动量 momentum
# 作为参数传入（常量 let 不跨模块），故优化器本身与超参解耦、可被任意项目复用。
#   扩展 Adam：在 neuron 侧车加一/二阶矩字段 m[]/v[]，照此写一个 adam_one 即可。
# ============================================================
inc neuron.sc                       # 复用 neuron 侧车类型

# 带动量的 SGD 更新一个神经元：v ← momentum·v − lr·g；θ ← θ + v。
@fnc sgd_one: n: neuron&, lr: f4, momentum: f4
    for i in n->nin
        n->vw[i] = momentum * n->vw[i] - lr * n->gw[i]
        n->w[i] = n->w[i] + n->vw[i]
    n->vbias = momentum * n->vbias - lr * n->gbias
    n->bias = n->bias + n->vbias
    return
