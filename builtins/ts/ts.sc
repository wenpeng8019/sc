# ts —— sc 内置张量（tensor）模块：numpy 存储/视图/广播/规约 + PyTorch nn 算子核
#
# 本文件是 ts 的唯一接口事实源：
#   @def 定义纯数据结构布局（C ABI 契约的一部分，字段顺序须与 ts.h 一致）
#   fnc name:: 方法声明（无函数体）：extern 原型，实现在 C 侧
#   @fnc 工厂自由函数声明（构造新 tensor&，调用方负责 drop）
#
# 默认实现：同目录 ts_impl.c（编译器自动编译并链接）；设计与进度见同目录 ROADMAP.md。
#
# 架构（numpy 式存储-视图分离）：
#   - 多视图共享一块引用计数缓冲（ts_store，C 内部私有）；tensor 仅是 shape/strides/offset 描述符。
#   - 零拷贝视图：transpose/t/permute/squeeze/unsqueeze/reshape(连续)/slice/select/narrow/
#     broadcast_to/flip 等仅改描述符、共享存储；clone/contiguous 及各算子结果物化为新连续缓冲。
#
# 定位与边界：
#   - 提供 ndarray 存储、numpy 式广播/规约/形变/索引 + PyTorch 式 nn 算子（matmul/relu/softmax…）。
#   - 自动微分（autograd）不在本模块内自建——反向序由上层 tok 依赖图（back / TOK_BACK）编排，
#     张量在 tok 节点间以句柄（i8 = tensor& 地址）流转。可复用的 autograd 组件见 builtins/nn
#     （nn = ts 数值 + tok 调度 之上的组合层）；端到端示例见 templates/dnn-framework。
#   - 不扩展 sc 语法：切片/索引/掩码一律走成员函数（t.slice(...) 而非 t[1:3]）。
#
# 命名习惯（降低学习成本）：
#   - 数组操作沿用 numpy：zeros/ones/arange/reshape/transpose/sum/mean/argmax/concat/where…
#   - nn 算子沿用 PyTorch：matmul/relu/sigmoid/tanh/softmax…；带尾缀 `_` 的为原地变体。
#
# 所属权约定（同 adt string& / mt pool&）：
#   - tensor& 为堆专属：工厂（zeros/ones/...）返回新 tensor&，末尾须 t->drop() 释放。
#   - 算子返回「新」tensor&（调用方 drop）；视图也返回新 tensor&（须 drop，但与源共享存储）。
#   - `_` 原地变体写回自身、返回成功标志。伪构造 tensor() 调 init 得空张量。
#
# dtype 约定：各工厂末参 dtype 必填（DNN 主路径传 DT_F4，对齐 PyTorch float32 默认精度）。
# 数值约定：通用算子经 f8 中转，DT_I8 超 2^53 会损失精度；DNN 主路径用 DT_F4 不受影响。

# ---------------- dtype：元素类型枚举（各工厂末参 dtype 必填） ----------------
@def dtype: [
    DT_F4 = 0       # float    （float32，默认）
    DT_F8           # double   （float64）
    DT_I4           # int32
    DT_I8           # int64
    DT_BOOL         # uint8    （比较/逻辑/掩码输出；0/1）
] : i4

# ---------------- tensor：N 维数组视图描述符（共享引用计数存储，堆专属） ----------------
@def tensor&: {
    store: &        # 共享缓冲 ts_store*（C 内部私有；nil = 空张量）
    shape: i4&      # [ndim] 各维长（每视图私有；ndim==0 为 nil）
    strides: i4&    # [ndim] 各维步长（元素为单位，可负——支持 flip）
    offset: i8      # 进入存储的起始元素偏移
    ndim: i4        # 维数（0 = 标量/空）
    dtype: i4       # DT_* 元素类型
    numel: i8       # 元素总数 = ∏ shape[i]

    # ---- 构造 / 析构 ----
    fnc init:: ndim: i4, shape: i4&, dtype: i4   # 零张量（shape 为 nil 或 ndim==0 → 空）
    fnc drop::                                    # refcnt-- 释放共享缓冲 + 释放 shape/strides

    # ---- 元信息 ----
    fnc ndim:: i4                                 # 维数
    fnc numel:: i8                                # 元素总数
    fnc dtype:: i4                                # 元素类型
    fnc dim:: i4, axis: i4                        # 第 axis 维长（支持负轴；越界返回 0）
    fnc is_same_shape:: bool, o: tensor&          # 形状是否一致
    fnc is_contiguous:: bool                       # 是否 C-连续

    # ---- 元素访问 ----
    fnc item:: f8                                 # 取唯一/首元素
    fnc at:: f8, idx: i8                          # 逻辑扁平读（行主序；越界返回 0）
    fnc set_at:: bool, idx: i8, v: f8             # 逻辑扁平写（越界返回 0）
    fnc at_nd:: f8, idx: i4&                      # 多维坐标读（idx 为 ndim 个）
    fnc set_nd:: bool, idx: i4&, v: f8            # 多维坐标写
    fnc data:: &                                  # 缓冲基址 + offset
    fnc fill:: v: f8                              # 全填充
    fnc clone:: tensor&                           # 深拷贝（新独立连续 tensor&）
    fnc contiguous:: tensor&                      # 已连续→视图；否则物化连续拷贝
    fnc astype:: tensor&, dtype: i4               # dtype 转换（新连续 tensor&）
    fnc copy_from:: bool, o: tensor&              # 形状一致，逐元素拷入（dtype 自动转换）

    # ---- 形变（视图优先；非连续时自动物化） ----
    fnc reshape:: tensor&, ndim: i4, shape: i4&   # numel 须一致；连续时零拷贝视图
    fnc transpose:: tensor&                       # 2D 转置（零拷贝视图；ndim!=2 → nil）
    fnc t:: tensor&                               # 末两维转置（>=2D；零拷贝视图）
    fnc permute:: tensor&, ndim: i4, axes: i4&    # 维重排（零拷贝视图）
    fnc squeeze:: tensor&                         # 去掉所有长度 1 的维（零拷贝视图）
    fnc unsqueeze:: tensor&, axis: i4             # 在 axis 处插入长度 1 的维（零拷贝视图）
    fnc ravel:: tensor&                           # 拉平为 1D（连续时视图，否则物化）
    fnc flatten:: tensor&                         # 拉平为 1D（总是物化连续拷贝）
    fnc broadcast_to:: tensor&, ndim: i4, shape: i4&  # 广播视图（stride=0）
    fnc flip:: tensor&, axis: i4                  # 沿 axis 翻转（负步长视图）
    fnc tile:: tensor&, reps: i4                  # 沿首维重复 reps 次（物化）
    fnc repeat:: tensor&, reps: i4, axis: i4      # 沿 axis 元素级重复（物化）
    fnc pad:: tensor&, pads: i4&, value: f8       # 常量填充（pads 为 [2*ndim]：每维 before,after；物化）
    fnc roll:: tensor&, shift: i8, axis: i4       # 循环位移（axis<0 则展平后整体滚动；物化）

    # ---- 索引（成员函数，不改 sc 语法） ----
    fnc slice:: tensor&, ax: i4, start: i8, stop: i8, step: i8  # 切片视图
    fnc select:: tensor&, ax: i4, idx: i8        # 取 ax==idx 降一维（视图）
    fnc narrow:: tensor&, ax: i4, start: i8, len: i8  # 截取 [start,start+len)（视图）
    fnc take:: tensor&, idx: tensor&              # 按整型张量 idx 扁平取值（物化 1D）
    fnc masked_select:: tensor&, mask: tensor&    # 布尔掩码取值（物化 1D）
    fnc nonzero:: tensor&                          # 非零元素坐标 [k, ndim]（DT_I8；物化）
    fnc gather:: tensor&, axis: i4, index: tensor&  # 沿 axis 按 index 采集（torch.gather；物化）
    fnc scatter_:: bool, axis: i4, index: tensor&, src: tensor&  # 沿 axis 按 index 原地写入 src

    # ---- 逐元素一元（返回新 tensor&） ----
    fnc neg:: tensor&
    fnc abs:: tensor&
    fnc sqrt:: tensor&
    fnc square:: tensor&
    fnc reciprocal:: tensor&
    fnc exp:: tensor&
    fnc log:: tensor&
    fnc floor:: tensor&
    fnc ceil:: tensor&
    fnc round:: tensor&
    fnc trunc:: tensor&
    fnc sign:: tensor&
    fnc pow_scalar:: tensor&, p: f8
    fnc clip:: tensor&, lo: f8, hi: f8
    # 三角 / 双曲（弧度制）
    fnc sin:: tensor&
    fnc cos:: tensor&
    fnc tan:: tensor&
    fnc asin:: tensor&
    fnc acos:: tensor&
    fnc atan:: tensor&
    fnc sinh:: tensor&
    fnc cosh:: tensor&

    # ---- 逐元素二元（numpy 广播：尾维对齐，维须相等或为 1） ----
    fnc add:: tensor&, o: tensor&
    fnc sub:: tensor&, o: tensor&
    fnc mul:: tensor&, o: tensor&
    fnc div:: tensor&, o: tensor&
    fnc pow:: tensor&, o: tensor&
    fnc mod:: tensor&, o: tensor&
    fnc maximum:: tensor&, o: tensor&
    fnc minimum:: tensor&, o: tensor&
    fnc atan2:: tensor&, o: tensor&             # 逐元素 atan2(self, o)（广播）
    fnc add_scalar:: tensor&, s: f8
    fnc sub_scalar:: tensor&, s: f8
    fnc mul_scalar:: tensor&, s: f8
    fnc div_scalar:: tensor&, s: f8

    # ---- 比较（→ DT_BOOL；广播） ----
    fnc gt:: tensor&, o: tensor&
    fnc ge:: tensor&, o: tensor&
    fnc lt:: tensor&, o: tensor&
    fnc le:: tensor&, o: tensor&
    fnc eq:: tensor&, o: tensor&
    fnc ne:: tensor&, o: tensor&

    # ---- 逻辑（→ DT_BOOL；按非零判真） ----
    fnc logical_and:: tensor&, o: tensor&
    fnc logical_or:: tensor&, o: tensor&
    fnc logical_not:: tensor&

    # ---- 逐元素原地（o 须可广播到自身形状；返回成功标志） ----
    fnc add_:: bool, o: tensor&
    fnc sub_:: bool, o: tensor&
    fnc mul_:: bool, o: tensor&
    fnc div_:: bool, o: tensor&
    fnc add_scalar_:: bool, s: f8
    fnc sub_scalar_:: bool, s: f8
    fnc mul_scalar_:: bool, s: f8
    fnc div_scalar_:: bool, s: f8

    # ---- 规约（axis<0 → 全规约得标量张量；否则沿 axis 降维；keepdim 保留长度 1 的维） ----
    fnc sum:: tensor&, axis: i4, keepdim: bool
    fnc mean:: tensor&, axis: i4, keepdim: bool
    fnc prod:: tensor&, axis: i4, keepdim: bool
    fnc max:: tensor&, axis: i4, keepdim: bool
    fnc min:: tensor&, axis: i4, keepdim: bool
    fnc argmax:: tensor&, axis: i4, keepdim: bool       # 结果 dtype=DT_I8（索引）
    fnc argmin:: tensor&, axis: i4, keepdim: bool       # 结果 dtype=DT_I8（索引）
    fnc std:: tensor&, axis: i4, keepdim: bool          # 总体标准差（除 N）
    fnc var:: tensor&, axis: i4, keepdim: bool          # 总体方差（除 N）
    fnc cumsum:: tensor&, axis: i4                       # 沿 axis 前缀和（形状不变）
    fnc cumprod:: tensor&, axis: i4                      # 沿 axis 前缀积（形状不变）
    fnc any:: tensor&, axis: i4, keepdim: bool          # → DT_BOOL
    fnc all:: tensor&, axis: i4, keepdim: bool          # → DT_BOOL
    fnc median:: tensor&, axis: i4, keepdim: bool       # 沿 axis 中位数
    fnc percentile:: tensor&, q: f8, axis: i4, keepdim: bool  # 第 q 百分位（0..100；线性插值）
    fnc sum_all:: f8
    fnc mean_all:: f8
    fnc prod_all:: f8
    fnc max_all:: f8
    fnc min_all:: f8
    fnc std_all:: f8
    fnc var_all:: f8
    fnc median_all:: f8
    fnc percentile_all:: f8, q: f8

    # ---- 线代 ----
    fnc matmul:: tensor&, o: tensor&             # 2D×2D；>2D 批量广播；可选 BLAS（2D F4）
    fnc dot:: f8, o: tensor&                     # 1D·1D 内积（长度须一致）
    fnc outer:: tensor&, o: tensor&              # 1D⊗1D → 2D 外积
    fnc trace:: f8                               # 2D 主对角线之和
    fnc diagonal:: tensor&, k: i4                # 2D 第 k 对角线（1D 视图）
    fnc bmm:: tensor&, o: tensor&                # 3D 批量矩乘（[B,M,K]×[B,K,N]→[B,M,N]，不做 batch 广播）
    fnc addmm:: tensor&, mat1: tensor&, mat2: tensor&, beta: f8, alpha: f8  # beta*self + alpha*(mat1@mat2)
    fnc triu:: tensor&, k: i4                    # 上三角（第 k 对角线以下置 0；物化）
    fnc tril:: tensor&, k: i4                    # 下三角（第 k 对角线以上置 0；物化）
    fnc norm:: f8, p: f8                         # 元素 p-范数（p=2 Frobenius；p<=0 视为 inf 范数）
    fnc det:: f8                                 # 方阵行列式（LU 分解）
    fnc inv:: tensor&                            # 方阵逆（Gauss-Jordan）
    fnc solve:: tensor&, b: tensor&              # 解 Ax=b（self=A 方阵，b 为 [n] 或 [n,k]）
    fnc cholesky:: tensor&                       # 对称正定的下三角 L（A=LLᵀ）
    fnc qr:: bool, out: &                        # QR 分解，out 为 tensor&[2]=（Q[m,n], R[n,n]）
    fnc eigh:: bool, out: &                      # 对称特征分解，out=（vals[n], vecs[n,n]）（Jacobi）
    fnc svd:: bool, out: &                       # 瘦 SVD，out=（U[m,r], S[r], V[n,r]），r=min(m,n)

    # ---- nn 激活/逐点函数（返回新 tensor&） ----
    fnc relu:: tensor&
    fnc sigmoid:: tensor&
    fnc tanh:: tensor&
    fnc softmax:: tensor&, axis: i4              # 沿 axis 做 softmax（数值稳定）
    fnc log_softmax:: tensor&, axis: i4          # 沿 axis 做 log-softmax（数值稳定）
    fnc leaky_relu:: tensor&, slope: f8          # x>0?x:slope*x
    fnc elu:: tensor&, alpha: f8                 # x>0?x:alpha*(e^x-1)
    fnc silu:: tensor&                           # x*sigmoid(x)
    fnc gelu:: tensor&                           # 近似 tanh GELU
    fnc relu_:: bool                              # 原地 relu
    fnc cross_entropy:: f8, target: tensor&      # logits[N,C] + 整型 target[N] → 平均交叉熵
    fnc mse_loss:: f8, target: tensor&           # 均方误差：mean((x-target)^2)
    fnc nll_loss:: f8, target: tensor&           # 负对数似然：input 视为 log-prob [N,C]
    fnc bce_with_logits:: f8, target: tensor&    # BCE(logits,target) 数值稳定实现
    fnc layer_norm:: tensor&, axis: i4, eps: f8  # 沿 axis 做 LayerNorm（每条切片独立标准化）
    fnc dropout:: tensor&, p: f8, train: bool    # 训练时随机置零并按 1/(1-p) 缩放；推理直通拷贝
    fnc sdpa:: tensor&, k: tensor&, v: tensor&, causal: bool  # scaled dot-product attention（Q,K,V 均 3D）
    fnc conv1d:: tensor&, w: tensor&, bias: tensor&, stride: i4, padding: i4   # x[N,Cin,L] * w[Cout,Cin,K]
    fnc conv2d:: tensor&, w: tensor&, bias: tensor&, stride_h: i4, stride_w: i4, pad_h: i4, pad_w: i4
    fnc max_pool1d:: tensor&, kernel: i4, stride: i4, padding: i4
    fnc avg_pool1d:: tensor&, kernel: i4, stride: i4, padding: i4
    fnc max_pool2d:: tensor&, kh: i4, kw: i4, sh: i4, sw: i4, ph: i4, pw: i4
    fnc avg_pool2d:: tensor&, kh: i4, kw: i4, sh: i4, sw: i4, ph: i4, pw: i4

    # ---- 比较/杂项 ----
    fnc equal:: bool, o: tensor&                 # 形状+逐元素全等
    fnc allclose:: bool, o: tensor&, rtol: f8, atol: f8  # 近似相等
    fnc shuffle_:: bool                           # 沿首维随机洗牌（原地）
    fnc save:: bool, path: const char&            # 存为 NumPy .npy（C-order）
    fnc print::                                   # 形状 + 元素（大张量省略）
}

# ---------------- 工厂（自由函数；返回新 tensor&，调用方 drop） ----------------
# 末参 dtype 必填（DNN 主路径传 DT_F4，对齐 PyTorch float32 默认精度）。
@fnc zeros:: tensor&, ndim: i4, shape: i4&, dtype: i4                # 全 0
@fnc ones:: tensor&, ndim: i4, shape: i4&, dtype: i4                # 全 1
@fnc full:: tensor&, ndim: i4, shape: i4&, value: f8, dtype: i4     # 全填充 value
@fnc empty:: tensor&, ndim: i4, shape: i4&, dtype: i4              # 未初始化
@fnc arange:: tensor&, start: f8, stop: f8, step: f8, dtype: i4    # [start, stop) 步进 step
@fnc linspace:: tensor&, start: f8, stop: f8, num: i4, dtype: i4   # 含端点等差 num 个
@fnc logspace:: tensor&, start: f8, stop: f8, num: i4, base: f8, dtype: i4  # base^linspace
@fnc eye:: tensor&, n: i4, dtype: i4                               # n×n 单位阵
@fnc from_data:: tensor&, ndim: i4, shape: i4&, data: &, dtype: i4 # 由现有缓冲逐元素拷入
@fnc zeros_like:: tensor&, o: tensor&                              # 形状/类型随 o，全 0
@fnc ones_like:: tensor&, o: tensor&                              # 形状/类型随 o，全 1
@fnc full_like:: tensor&, o: tensor&, value: f8                   # 形状/类型随 o，全 value
@fnc empty_like:: tensor&, o: tensor&                            # 形状/类型随 o，未初始化
@fnc diag:: tensor&, o: tensor&, k: i4                            # 1D→对角阵 / 2D→第 k 对角线
@fnc where:: tensor&, cond: tensor&, a: tensor&, b: tensor&       # cond?a:b 逐元素（广播；物化）
@fnc concat:: tensor&, arr: &, n: i4, axis: i4                    # 沿 axis 拼接张量数组（arr 为 tensor&[]）
@fnc stack:: tensor&, arr: &, n: i4, axis: i4                     # 新增 axis 堆叠张量数组
@fnc tri:: tensor&, n: i4, m: i4, k: i4, dtype: i4                # [n,m] 第 k 对角线及以下为 1 的下三角
@fnc meshgrid:: bool, arr: &, n: i4, indexing: i4, out: &        # N 个 1D 坐标→N 个 N 维网格；indexing 0=ij 1=xy；arr/out 均为 tensor&[]

# ---------------- 随机（P4；模块级 RNG，xorshift128+） ----------------
@fnc rand_seed:: seed: i8                                         # 设定随机种子
@fnc rand_uniform:: tensor&, ndim: i4, shape: i4&, lo: f8, hi: f8, dtype: i4    # [lo,hi) 均匀
@fnc rand_normal:: tensor&, ndim: i4, shape: i4&, mean: f8, std: f8, dtype: i4 # 正态（Box-Muller）
@fnc rand_randint:: tensor&, ndim: i4, shape: i4&, lo: i8, hi: i8, dtype: i4   # [lo,hi) 整型
@fnc permutation:: tensor&, n: i4, dtype: i4                      # 0..n-1 的随机排列

# ---------------- IO（P4） ----------------
@fnc ts_load:: tensor&, path: const char&                        # 载入 .npy（兼容旧 .scts）失败返回 nil
