# nn —— sc 内置神经网络模块（ts 数值 + tok 宏调度 之上的自动微分引擎 / 模块 / 优化器）
#
# 本文件是 nn 的唯一接口事实源：
#   @def 定义不透明句柄（{ p: & } 单指针；真实布局在 C 侧 nn_impl.c）
#   fnc name:: 方法声明（无函数体）：extern 原型，实现在 C 侧（生成 <type>_<name>）
#   @fnc 工厂自由函数声明
#
# 默认实现：同目录 nn_impl.c（编译器自动编译并链接）。
#
# 依赖：
#   - ts（张量数值 + 反向数值核）：本模块 `inc ts.sc`，把 ts 单元纳入图（链接 ts_impl.c），
#     并令 tensor 类型/算子在本模块与消费单元可见。
#   - tok（依赖图/宏调度）：tok 运行时经 op→tok 隐式依赖恒随工程链接（无需 inc）。高频
#     autograd 节点走 nn 进程内录带（tape），不进 tok 全局只增不删的 intern 图。
#
# 自动微分引擎（define-by-run 录带）：
#   var w: val& = nn_param(wt)                 # 参数叶子（克隆 wt，requires_grad）
#   var x: val& = nn_input(xt)                 # 输入叶子（每步临时，tape 拥有）
#   var y: val& = x->matmul(w)->add(b)         # 前向同时录带
#   var loss: val& = y->mse_loss(target)
#   loss->backward()                            # 逆序遍历 tape 累加梯度
#   opt->step();  opt->zero_grad();  nn_tape_clear()   # 更新 + 清梯度 + 释放本步 tape
#
# 所属权（关键）：
#   - 参数 val（nn_param / 模块内建）由用户/模块持有 → val 的 drop() 释放。
#   - 输入 val（nn_input）与算子输出 val 由 tape 拥有 → nn_tape_clear() 统一释放；
#     用户**不得**对其调用 drop()（否则与 tape 重复释放）。
#   - tape 为进程内全局单带，单线程使用。

inc ts.sc

# ---------------- val：自动微分变量（不透明句柄） ----------------
@def val&: {
    p: &
    fnc matmul:: val&, o: val&            # a·b（2D）
    fnc add:: val&, o: val&               # a+b（广播）
    fnc sub:: val&, o: val&               # a-b（广播）
    fnc mul:: val&, o: val&               # a*b 逐元素（广播）
    fnc relu:: val&
    fnc sigmoid:: val&
    fnc tanh:: val&
    fnc mse_loss:: val&, target: val&     # 标量损失 mean((x-t)^2)
    fnc cross_entropy:: val&, target: val& # 标量损失：logits[N,C] + 整型 target[N]
    # ---- 通用逐点 / 形变 ----
    fnc div:: val&, o: val&               # a/b（广播）
    fnc scale:: val&, s: f8               # a*常量
    fnc exp:: val&                        # e^a
    fnc log:: val&                        # ln a
    fnc reshape:: val&, ndim: i4, shape: i4&   # 重排形状（numel 不变）
    fnc permute:: val&, ndim: i4, axes: i4&    # 维重排
    fnc transpose:: val&                  # 末两维转置
    # ---- nn 算子 ----
    fnc softmax:: val&, axis: i4
    fnc log_softmax:: val&, axis: i4
    fnc leaky_relu:: val&, slope: f8
    fnc elu:: val&, alpha: f8
    fnc silu:: val&
    fnc gelu:: val&
    fnc layer_norm:: val&, axis: i4, eps: f8
    fnc rms_norm:: val&, axis: i4, eps: f8
    fnc batch_norm:: val&, eps: f8        # 训练，通道=dim1
    fnc bmm:: val&, o: val&               # 批量矩阵乘 [B,M,K]·[B,K,N]
    fnc dropout:: val&, p: f8, train: i4
    fnc conv2d:: val&, w: val&, b: val&, sh: i4, sw: i4, ph: i4, pw: i4
    fnc max_pool2d:: val&, kh: i4, kw: i4, sh: i4, sw: i4, ph: i4, pw: i4
    fnc avg_pool2d:: val&, kh: i4, kw: i4, sh: i4, sw: i4, ph: i4, pw: i4
    fnc embedding:: val&, idx: val&       # 接收者为权重 [V,D]，idx 为整型类标
    fnc backward::                         # 以 loss 处 grad=1 播种，逆序累加梯度
    fnc item:: f8                          # data 唯一元素（标量损失值）
    fnc value:: tensor&                    # data 的拷贝（调用方 drop）
    fnc grad:: tensor&                     # grad 的拷贝（无梯度则零；调用方 drop）
    fnc zero_grad::                        # 清空 grad
    fnc drop::                             # 释放参数叶子 val（勿用于 tape 拥有的 val）
}

# ---------------- linear：全连接层（y = x·W + b） ----------------
@def linear&: {
    p: &
    fnc forward:: val&, x: val&            # x[N,in] → y[N,out]
    fnc w:: val&                           # 参数 W（供优化器跟踪）
    fnc b:: val&                           # 参数 b
    fnc drop::                             # 释放模块及其参数
}

# ---------------- conv：二维卷积层（W[Cout,Cin,Kh,Kw], b[Cout]） ----------------
@def conv&: {
    p: &
    fnc forward:: val&, x: val&            # x[N,Cin,H,W] → y[N,Cout,Ho,Wo]
    fnc w:: val&                           # 参数 W
    fnc b:: val&                           # 参数 b
    fnc drop::                             # 释放模块及其参数
}

# ---------------- embed：查表嵌入（W[V,D]，idx 整型类标） ----------------
@def embed&: {
    p: &
    fnc forward:: val&, idx: val&          # idx → 嵌入向量（形状 idx.shape+[D]）
    fnc w:: val&                           # 参数 W
    fnc drop::                             # 释放模块及其参数
}

# ---------------- optim：优化器（SGD / Adam） ----------------
@def optim&: {
    p: &
    fnc config:: momentum: f8, weight_decay: f8  # SGD 动量/权重衰减
    fnc track:: p: val&                          # 注册一个参数
    fnc track_linear:: m: linear&                # 注册 Linear 的 W 与 b
    fnc track_conv2d:: m: conv&                  # 注册 Conv2d 的 W 与 b
    fnc track_embedding:: m: embed&              # 注册 Embedding 的 W
    fnc zero_grad::                              # 清空所有跟踪参数的梯度
    fnc step::                                   # 用各参数 .grad 更新一步
    fnc drop::                                   # 释放优化器（不释放被跟踪参数）
}

# ---------------- 叶子构造 / tape 生命周期 / 工厂 ----------------
@fnc nn_param:: val&, t: tensor&                 # 参数叶子（克隆 t，requires_grad）
@fnc nn_input:: val&, t: tensor&                 # 输入叶子（克隆 t，无梯度；tape 拥有）
@fnc nn_tape_clear::                             # 释放 tape 拥有的中间/输入 val 与节点（每 step 末）
@fnc nn_linear:: linear&, in_dim: i4, out_dim: i4 # Kaiming(fan_in) 初始化 W，b=0
@fnc nn_conv2d:: conv&, cin: i4, cout: i4, kh: i4, kw: i4, sh: i4, sw: i4, ph: i4, pw: i4  # Kaiming(fan_in)
@fnc nn_embedding:: embed&, vocab: i4, edim: i4  # 小幅正态初始化 W[V,D]
@fnc nn_sgd:: optim&, lr: f8                      # 随机梯度下降
@fnc nn_adam:: optim&, lr: f8                     # Adam（beta1=.9,beta2=.999,eps=1e-8）
