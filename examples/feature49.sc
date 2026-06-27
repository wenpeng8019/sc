# feature49：token 依赖图反向遍历（back）—— 反向传播骨架（loss.backward 模型）。
#
# 三期机制：在 dep…map 显式有向图（DAG）之上，`back` 语句自某「输出」token 反向遍历——
#   back t            # 仅反向遍历（不灌种子，保留 t 当前值）
#   back t, seed      # 先把梯度种子 seed 灌入 t，再反向遍历
# 运行时 token_back(t, seed)：沿 producers[] 反向邻接收集全部上游 dep，按依赖图深度降序
#   （反拓扑序：靠近输出者先行）依次以 acting=TOK_BACK(-4) 唤起其 follow 体。
#   · follow 体内 this->active==(0-4) 即走「反向计算」分支（读目标的梯度、写源的梯度）；
#   · 反拓扑序保证：下游 dep 先算出中间梯度，上游 dep 再据此按链式法则累乘/累加（梯度累积）。
#   · 实际梯度数学由用户 follow 体负责（本骨架只保证「遍历顺序 + 方向标志」正确）。
#
# 本例搭一条线性前向链（DAG）：x → y → loss，前向 y=2x、loss=y+5；
#   反向以 seed=1 求各级梯度（链式法则）：dloss/dy=1 → gy=1；dy/dx=2 → gx=gy*2=2。

tok x:    "nn.x"        # 输入（源，depth 0）
tok y:    "nn.y"        # 隐层（depth 1）
tok loss: "nn.loss"     # 输出（depth 2；back 起点，其值即被灌入的梯度种子）

tok gx: "nn.gx"         # x 的梯度（enforce，无前向 dep → set 不触发级联）
tok gy: "nn.gy"         # y 的梯度

# 源 x → 目标 y：前向 y=2x；反向 gx = gy * dy/dx = gy * 2。
dep any: a:"nn.x" map b:"nn.y"
    if this->active == 0 - 4         # TOK_BACK：反向计算
        var g: i8 = (gy->get(): i8)
        gx->set(((g * 2): @), 0)
        return false
    var v: i8 = (a->get(): i8)       # 前向：y = 2x
    b->set(((v * 2): @), 0)
    return false

# 源 y → 目标 loss：前向 loss=y+5；反向 gy = gloss * dloss/dy = loss(种子) * 1。
dep any: c:"nn.y" map o:"nn.loss"
    if this->active == 0 - 4         # TOK_BACK：反向计算
        var gloss: i8 = (o->get(): i8)   # loss 此刻持梯度种子
        gy->set((gloss: @), 0)
        return false
    var v: i8 = (c->get(): i8)       # 前向：loss = y + 5
    o->set(((v + 5): @), 0)
    return false

fnc main: i4
    # 本模块为各量之主：前向链与梯度量均须 form 激活（自输出向输入 form，初值灌定）
    form gx,   (0: @)
    form gy,   (0: @)
    form loss, (0: @)
    form y,    (0: @)
    form x,    (0: @)

    # 前向：x=4 → y=8 → loss=13
    x->set((4: @), 0)
    printf("forward: x=%lld y=%lld loss=%lld\n", (x->get(): i8), (y->get(): i8), (loss->get(): i8))

    # 反向：以 seed=1 自 loss 反传，按反拓扑序求各级梯度（链式法则）
    back loss, (1: @)
    printf("backward(seed=1): gy=%lld gx=%lld\n", (gy->get(): i8), (gx->get(): i8))

    return 0
