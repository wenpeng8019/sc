inc ts.sc
inc nn.sc

# ============================================================
# utils/train_utils.sc —— 共享训练工具（纯 sc 实现）
# ============================================================
# 基于 builtins/nn 的自动微分引擎（val/tape/backward + optim）封装
# 统一的训练循环。每轮：构图前向 → backward → optim.step → 清梯度 →
# nn_tape_clear() 回收本轮录带。返回末轮 loss。
# ============================================================

# 训练单层 Linear（MSE + 给定 optim），跑 epochs 轮，返回末轮 loss。
# fc 须已被 opt 跟踪（opt->track_linear(fc)）。
@fnc train_linear: f8, fc: linear&, opt: optim&, x: tensor&, target: tensor&, epochs: i4
    var last: f8 = 0.0
    for e in epochs
        var xi: val& = nn_input(x)
        var ti: val& = nn_input(target)
        var y: val& = fc->forward(xi)
        var loss: val& = y->mse_loss(ti)
        loss->backward()
        opt->step()
        opt->zero_grad()
        last = loss->item()
        nn_tape_clear()
    return last
