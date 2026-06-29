# ============================================================
# dnn-framework —— 深度神经网络框架脚手架（张量版）
# ============================================================
#
# 架构：把神经网络表达为一张 tok 依赖图，三套机制各司其职：
#   · 前向（forward）  = `dep…map` 显式 DAG 边：输入 → 输出 → loss，
#                        每条边的 follow 体即该层的前向算子（沿图级联触发）。
#   · 反向（backward） = `back` 语句：自 loss 反向遍历，按反拓扑序（靠近输出者先行）
#                        唤起各层 follow 的「反向分支」（this->active==TOK_BACK），
#                        逐层算梯度——链式法则的天然落地（loss.backward() 模型）。
#   · 训练（train）    = 外层控制循环：forward → backward → 优化器更新权重，重复。
#
# 数据面 = 真张量（builtin `ts`）：
#   token 值只携带 **epoch 触发标量**（驱动 map 级联 / back 反向遍历的「时序」），
#   真正的张量（输入/权重/梯度）存为模块级 `tensor&` 全局，follow 体直接读写它们。
#   即：tok 负责「何时、按什么序」算；ts 负责「算什么」（matmul/逐元素/规约算子核）。两层正交。
#
# ------------------------------------------------------------
# 本骨架内置一个最小可训练单元（单层线性回归，演示完整 前向/反向/训练 闭环）：
#
#     模型：  y = W·x + b           x:[2,1] 输入,  W:[2,2] 权重,  b:[2,1] 偏置,  y:[2,1] 输出
#     损失：  loss = mean((y - t)²) t:[2,1] 监督目标（MSE）
#     前向：  x ──► y ──► loss      （map 边级联；张量在全局间流动）
#     反向：  back loss             gGY = (2/N)(y-t),  gGW = gGY·xᵀ,  gGB = gGY
#     更新：  W -= lr·gGW ; b -= lr·gGB
#
#   固定输入 x=[1,2]、目标 t=[5,8]；每个 epoch loss 单调收敛到 0。
#   把单层换成多层（多权重张量 + 激活），即得通用 MLP（扩展见文末）。
# ============================================================
inc ts.sc

let LR:   f8 = 0.1            # 学习率
let NOUT: i8 = 2             # 输出维（MSE 求均值的分母）

# ---- 前向链：层 = 节点（token），值仅作 epoch 触发 ----
tok x:    "nn.x"            # 输入触发（源，depth 0）
tok y:    "nn.y"           # 输出层触发（depth 1）
tok loss: "nn.loss"        # 损失触发（depth 2；back 起点）

# ---- 数据面：模块级张量全局（follow 体直接读写）----
var gX:   tensor& = nil     # 输入 [2,1]（固定）
var gW:   tensor& = nil     # 权重 [2,2]（优化器更新）
var gB:   tensor& = nil     # 偏置 [2,1]（优化器更新）
var gY:   tensor& = nil     # 输出 [2,1]
var gTgt: tensor& = nil     # 监督目标 [2,1]
var gGY:  tensor& = nil     # ∂loss/∂y [2,1]
var gGW:  tensor& = nil     # ∂loss/∂W [2,2]（优化器据此更新 W）
var gGB:  tensor& = nil     # ∂loss/∂b [2,1]（优化器据此更新 b）
var gLoss: f8 = 0.0         # 标量损失（规约结果）

# ============================================================
# 层算子（kernel）：每条 map 边的 follow 体含「前向 + 反向」两分支。
#   this->active == TOK_BACK(-4) → 反向分支（读下游梯度张量、写上游/权重梯度张量）；
#   否则 → 前向分支（读上游张量、算、写下游张量；并把 epoch 触发标量传给下游 token）。
#   扩展点：加一层 = 加一个 tok + 一条 dep…map 边 + 在 follow 体写两个分支。
# ============================================================

# 输入层 x → 输出层 y：前向 y = W·x + b；反向 gGW = gGY·xᵀ, gGB = gGY。
dep all: a:"nn.x" map b:"nn.y"
    if this->active == 0 - 4              # 反向：链式法则到权重/偏置
        var xt: tensor& = gX->transpose()   # xᵀ [1,2]
        var gw: tensor& = gGY->matmul(xt)   # gGW = gGY[2,1] · xᵀ[1,2] = [2,2]
        gGW->copy_from(gw)
        gGB->copy_from(gGY)                 # gGB = gGY
        xt->drop()
        gw->drop()
        return false
    var wx: tensor& = gW->matmul(gX)        # 前向：y = W·x + b
    gY->copy_from(wx)
    gY->add_(gB)
    wx->drop()
    b->set((a->get(): *), 0)                # 传 epoch 触发标量给下游（驱动 y→loss 级联）
    return false

# 输出层 y → loss：前向 loss = mean((y-t)²)；反向 gGY = (2/N)(y-t)（MSE 导数）。
dep all: c:"nn.y" map o:"nn.loss"
    if this->active == 0 - 4              # 反向：损失对输出的梯度
        var dd: tensor& = gY->sub(gTgt)     # (y - t) [2,1]
        gGY->copy_from(dd)
        gGY->mul_scalar_(2.0 / (NOUT: f8))  # ×(2/N)
        dd->drop()
        return false
    var d:  tensor& = gY->sub(gTgt)         # 前向：loss = mean((y-t)²)
    var sq: tensor& = d->mul(d)
    gLoss = sq->mean_all()
    d->drop()
    sq->drop()
    o->set((c->get(): *), 0)                # 传 epoch 触发标量给 loss（back 起点就绪）
    return false

# ============================================================
# 训练引擎
# ============================================================

# 一次前向：灌 epoch 触发 → 沿 map 图级联到 loss。返回当前 loss。
#   用 pulse 而非 set：训练循环每 epoch 喂的触发即便同值也强制重算（拉取/迭代语义）；
#   触发标量取 epoch 序号，逐轮递增，天然绕过相等抑制。
fnc forward: f8, ep: i8
    x->pulse((ep: *), 0)
    return gLoss

# 一次反向 + 参数更新（一个梯度下降步）。
fnc backward_step:
    back loss                             # 自 loss 反向遍历：先算 gGY，再算 gGW/gGB（反拓扑序）
    var sw: tensor& = gGW->mul_scalar(LR) # W := W - lr·gGW
    gW->sub_(sw)
    sw->drop()
    var sb: tensor& = gGB->mul_scalar(LR) # b := b - lr·gGB
    gB->sub_(sb)
    sb->drop()
    return

# 训练若干 epoch。
fnc train: i4, epochs: i4
    for e in epochs
        var l: f8 = forward((e: i8))
        printf("epoch %2d  loss=%.6f  y=[%.4f, %.4f]\n", e, l, gY->at(0), gY->at(1))
        backward_step()
    return 0

# 张量分配：W/b 零初始化，输入与目标由字面缓冲拷入。
fnc setup:
    var s21[2]: i4                        # 形状 [2,1]（列向量）
    s21[0] = 2
    s21[1] = 1
    var s22[2]: i4                        # 形状 [2,2]（权重矩阵）
    s22[0] = 2
    s22[1] = 2
    var xb[2]: f4                         # 固定输入 x = [1, 2]ᵀ
    xb[0] = 1.0
    xb[1] = 2.0
    var tb[2]: f4                         # 监督目标 t = [5, 8]ᵀ
    tb[0] = 5.0
    tb[1] = 8.0
    gX   = from_data(2, s21, (xb: &), DT_F4)
    gTgt = from_data(2, s21, (tb: &), DT_F4)
    gW   = zeros(2, s22, DT_F4)
    gB   = zeros(2, s21, DT_F4)
    gY   = zeros(2, s21, DT_F4)
    gGY  = zeros(2, s21, DT_F4)
    gGW  = zeros(2, s22, DT_F4)
    gGB  = zeros(2, s21, DT_F4)
    return

fnc teardown:
    gX->drop()
    gTgt->drop()
    gW->drop()
    gB->drop()
    gY->drop()
    gGY->drop()
    gGW->drop()
    gGB->drop()
    return

fnc main: i4
    # 张量须先分配：form 激活前向链时可能级联触发 follow 体，此时张量全局须已就绪（非 nil）。
    setup()
    # 前向链须 form 激活：未 form 的 token 不就绪、其 set 不传播（见 syntax §21.2）。
    #   自输出 loss 向输入 x 依序灌初值（无 ctx 侧车，张量数据在全局）。
    form loss, (0: *)
    form y,    (0: *)
    form x,    (0: *)
    print "=== 训练（单层线性回归 y=W·x+b，x=[1,2], t=[5,8], lr=", LR, "）==="
    train(40)

    print "=== 推理 ==="
    var l: f8 = forward(40)
    printf("final  loss=%.6f  y=[%.4f, %.4f]  (target=[5, 8])\n", l, gY->at(0), gY->at(1))
    printf("权重 W=[[%.4f, %.4f],[%.4f, %.4f]]  b=[%.4f, %.4f]\n",
           gW->at(0), gW->at(1), gW->at(2), gW->at(3), gB->at(0), gB->at(1))
    teardown()
    return 0

# ============================================================
# 扩展指南
# ------------------------------------------------------------
# 1) 加一层（如隐层 h，带激活）：
#      tok h: "nn.h"
#      var gH: tensor& = nil ; var gW1: tensor& = nil ...     # 该层张量全局
#      dep all: a:"nn.x" map b:"nn.h"        # x → h
#          if this->active == 0 - 4 ...反向：gGW1 = gGH·xᵀ; 上游梯度...
#          else ...前向：h = relu(W1·x + b1)，用 gH->copy_from(...); gH->relu_()...
#      dep all: c:"nn.h" map d:"nn.y"        # h → y（同上）
#    depth()/critical() 自动重新烘焙 = 层的执行顺序与最深路径（瓶颈层）。
#
# 2) 批量（mini-batch）：把列向量 [in,1] 换成 [in,B]，y=[out,B]，
#    损失对 batch 求均值，梯度 gGW = gGY·Xᵀ 自动按 batch 累加（matmul 内积即求和）。
#
# 3) 真实优化器：backward_step 里把 `W - lr·gGW` 换成 Adam/动量
#    （再开几个 tensor& 全局存一阶/二阶矩，逐元素 ts 算子更新）。
#
# 4) 循环/反馈网络（RNN）：层间存在反馈环时，用 `dep loop` 替 `dep map`
#    （豁免环检测 + Tarjan 缩点），以 t.loop_run(steps) 按时间步展开迭代。
#
# 5) 并行：同 batch() 的层两两无依赖，可并行前向（见 syntax §15.2 run / rpc）。
#
# 6) BLAS 加速：matmul 在 -DSCC_WITH_BLAS 且 DT_F4 下走 cblas_sgemm（见 builtins/ts.md §8）。
# ============================================================
