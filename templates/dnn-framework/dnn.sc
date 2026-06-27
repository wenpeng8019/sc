# ============================================================
# dnn-framework —— 深度神经网络框架脚手架
# ============================================================
#
# 架构：把神经网络表达为一张 tok 依赖图，三套机制各司其职：
#   · 前向（forward）  = `dep…map` 显式 DAG 边：输入 → 隐层 → 输出 → loss，
#                        每条边的 follow 体即该层的前向算子（设值即沿图级联）。
#   · 反向（backward） = `back` 语句：自 loss 反向遍历，按反拓扑序（靠近输出者先行）
#                        唤起各层 follow 的「反向分支」（this->active==TOK_BACK），
#                        逐层算梯度——链式法则的天然落地（loss.backward() 模型）。
#   · 训练（train）    = 外层控制循环：forward → backward → 优化器更新权重，重复。
#                        （图内反馈/循环网络 RNN 则可用 `dep loop`，见文末。）
#
# 权重 w 是「参数 token」：不在 map 图里（不被级联触发），各层 follow 体按句柄读它、
#   反向时算出其梯度 gw，优化器据 gw 做梯度下降更新。
#
# ------------------------------------------------------------
# 本骨架内置一个最小可训练单元（单权重线性回归，演示完整 前向/反向/训练 闭环）：
#
#     前向：  x ──► y ──► loss          y = w·x ,  loss = y - target
#             (map)  (map)
#     反向：  back loss                 gy = (y-target) ,  gw = gy·x
#     更新：  w := w - lr·gw            （MSE 梯度下降，lr=1/4 → 比例收敛到 target）
#
#   目标 target=50、输入 x=1、初值 w=10；每个 epoch loss=y-target 逐步收敛到 0。
#   把单权重换成多层多权重，即得通用 MLP（扩展见文末）。
# ============================================================

let TARGET: i8 = 50           # 监督目标
let LR_DIV: i8 = 4            # 学习率倒数（w -= gw / LR_DIV，等价 lr=0.25）

# ---- 前向链：层 = 节点（token）----
tok x:    "nn.x"             # 输入（源，depth 0）
tok y:    "nn.y"            # 输出层 y = w·x（depth 1）
tok loss: "nn.loss"        # 损失 loss = y - target（depth 2；back 起点）

# ---- 参数与梯度（enforce token：set 不级联，仅作存储槽）----
tok w:  "nn.w"             # 权重参数（各层 follow 读它；优化器更新）
tok gy: "nn.gy"            # ∂loss/∂y
tok gw: "nn.gw"            # ∂loss/∂w（优化器据此更新 w）

# ============================================================
# 层算子（kernel）：每条 map 边的 follow 体含「前向 + 反向」两分支。
#   this->active == TOK_BACK(-4) → 反向分支（读下游梯度、写上游/权重梯度）；
#   否则 → 前向分支（读上游、算、写下游）。
#   扩展点：加一层 = 加一个 tok + 一条 dep…map 边 + 在 follow 体写两个分支。
# ============================================================

# 输入层 x → 输出层 y：前向 y = w·x；反向 gw = gy · ∂y/∂w = gy · x。
dep all: a:"nn.x" map b:"nn.y"
    if this->active == 0 - 4              # 反向：链式法则到权重
        var gyv: i8 = (gy->get(): i8)
        var xv:  i8 = (a->get(): i8)
        gw->set(((gyv * xv): @), 0)       # TODO: 多权重时按各自 ∂y/∂w 分别求
        return false
    var xv: i8 = (a->get(): i8)           # 前向：y = w·x
    var wv: i8 = (w->get(): i8)
    b->set(((wv * xv): @), 0)
    return false

# 输出层 y → loss：前向 loss = y - target；反向 gy = ∂loss/∂y = (y - target)（MSE 导数）。
dep all: c:"nn.y" map o:"nn.loss"
    if this->active == 0 - 4              # 反向：损失对输出的梯度
        var yv: i8 = (c->get(): i8)
        gy->set(((yv - TARGET): @), 0)    # TODO: 换损失函数即改此处导数
        return false
    var yv: i8 = (c->get(): i8)           # 前向：loss = y - target
    o->set(((yv - TARGET): @), 0)
    return false

# ============================================================
# 训练引擎
# ============================================================

# 一次前向：灌输入 → 沿 map 图级联到 loss。返回当前 loss。
#   用 pulse 而非 set：训练循环每 epoch 喂同一输入 x（值不变），set 会被相等检测抑制 → 前向
#   不重算 → w 更新后 y 不刷新。pulse 绕过抑制，强制每轮重跑前向（拉取/迭代语义：每次喂入皆事件）。
fnc forward: i8, input: i8
    x->pulse((input: @), 0)
    return (loss->get(): i8)

# 一次反向 + 参数更新（一个梯度下降步）。
fnc backward_step:
    back loss                             # 自 loss 反向遍历（loss 当前值即 ∂loss/∂y 种子）
    var wv: i8 = (w->get(): i8)
    var g:  i8 = (gw->get(): i8)
    w->set(((wv - g / LR_DIV): @), 0)     # w := w - lr·gw（优化器；TODO: 换 Adam/动量）
    return

# 训练若干 epoch。
fnc train: i4, epochs: i4, input: i8
    for e in epochs
        var l: i8 = forward(input)
        print "epoch ", (e: "%2d"), "  w=", ((w->get(): i8): "%2lld"), "  y=", ((y->get(): i8): "%2lld"), "  loss=", l
        backward_step()
    return 0

fnc main: i4
    # 本模块为各量之主：前向链（自输出 loss 向输入 x 灌）与参数/梯度槽均须 form 激活，
    # 未 form 的 token 不就绪、其 set 不传播（见 syntax §21.2）。
    form loss, (0: @)                     # 前向链：输出 → 输入 依序灌初值
    form y,    (0: @)
    form x,    (0: @)
    form gy,   (0: @)                     # 梯度/参数槽（enforce，set 不级联）
    form gw,   (0: @)
    form w,    (0: @)
    w->set((10: @), 0)                    # 初始化权重
    print "=== 训练（target=", TARGET, ", x=1, lr=1/", LR_DIV, "）==="
    train(12, 1)

    print "=== 推理 ==="
    var l: i8 = forward(1)
    print "final w=", (w->get(): i8), "  ->  y=", (y->get(): i8), "  (target=", TARGET, ", loss=", l, ")"
    return 0

# ============================================================
# 扩展指南
# ------------------------------------------------------------
# 1) 加一层（如隐层 h）：
#      tok h: "nn.h"
#      dep all: a:"nn.x" map b:"nn.h"        # x → h
#          if this->active == 0 - 4 ...反向... else ...前向 h = relu(w1·x)...
#      dep all: c:"nn.h" map d:"nn.y"        # h → y
#          ...
#    depth()/critical() 自动重新烘焙 = 层的执行顺序与最深路径（瓶颈层）。
#
# 2) 张量权重 / 批量数据：把标量换成堆专属缓冲，经 @ 装箱传递：
#      def tensor&: { n: i4, data: [1024]f4 }     # 见 syntax §10.2.1
#    层 follow 体里取 (s->get(): tensor@) 做矩阵运算，t->set((out: @), 0) 写下游。
#
# 3) 真实优化器：backward_step 里把 `w - g/LR_DIV` 换成 Adam/动量（再开几个
#    状态 token 存一阶/二阶矩）。多权重则每个权重一个 gw token。
#
# 4) 循环/反馈网络（RNN）：层间存在反馈环时，用 `dep loop` 替 `dep map`
#    （豁免环检测 + Tarjan 缩点），以 t.loop_run(steps) 按时间步展开迭代。
#
# 5) 并行：同 batch() 的层两两无依赖，可并行前向（见 syntax §15.2 run / rpc）。
# ============================================================
