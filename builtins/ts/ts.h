/* ts.h —— sc 内置张量（tensor）模块的 C ABI 契约（与 builtins/ts/ts.sc 同步维护）
 *
 * 定位：numpy ndarray 的存储/视图/广播/规约 + PyTorch 的 nn 算子核。自动微分（autograd）
 *       不在本模块内自建，由上层 tok 依赖图编排反向序（见 templates/dnn-framework）。
 *       设计与进度事实源见同目录 ROADMAP.md。
 *
 * 架构（numpy 式存储-视图分离）：
 *   - ts_store：引用计数的元素缓冲，多视图共享（模块内私有 refcount，不接入 op 的 sc_ref）。
 *   - tensor：视图描述符（shape/strides/offset + 共享 store）。元素物理地址
 *             = store->data + (offset + Σ coord[d]·strides[d]) · dt_size。
 *   - 零拷贝视图：transpose/T/permute/squeeze/unsqueeze/reshape(连续)/slice/select/
 *                 broadcast_to/flip 等仅改描述符、共享 store（refcnt++）。
 *   - 物化：clone/contiguous、所有逐元素/规约/matmul 结果新建连续 store。
 *
 * 自定义实现指南：
 *   按本头文件实现全部函数，编译为 .c/.o/.a 后随工程链接即可替换内置默认实现。
 *
 * 所属权约定（同 adt string& / mt pool&）：
 *   - tensor& 为堆专属对象，工厂函数（zeros/ones/...）返回新 tensor*，调用方负责 drop()。
 *   - 逐元素/规约/线代/激活等算子返回「新」tensor*（调用方 drop）；带尾缀 `_` 的为原地
 *     变体（写回 _this，返回 bool 成功标志），训练循环宜用原地变体避免反复分配。
 *   - 视图（transpose/slice/...）亦返回新 tensor*（须 drop），但与源共享 store（refcnt 保护）。
 *   - 失败（内存不足 / 形状不匹配）：返回 tensor* 的函数返回 NULL；返回 uint8_t 的返回 0。
 *
 * 数值约定：
 *   - 通用算子经 double 中转读写元素，故 DT_I8 超过 2^53 的整数会损失精度；深度学习
 *     主路径用 DT_F4（float32，对齐 PyTorch 默认）不受影响。
 */
#ifndef SC_TS_H
#define SC_TS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- dtype ---------------- */
/* 这里采用 ts.sc 的 @def dtype 裸名（DT_F4...）作为首选拼写：消费单元 inc ts.sc 后
 * 编译器直接 #include 本头，跨模块按裸名引用枚举常量须由本头提供同名定义。
 * 另给 TS_DT_* 前缀别名，供外部 C ABI 使用者按需选用（避免与宿主符号冲突）。 */
enum {
    DT_F4   = 0,   /* float    (float32，PyTorch 默认精度) */
    DT_F8   = 1,   /* double   (float64) */
    DT_I4   = 2,   /* int32_t */
    DT_I8   = 3,   /* int64_t */
    DT_BOOL = 4    /* uint8_t  (比较/逻辑/掩码输出；0/1) */
};
#define TS_DT_F4   DT_F4
#define TS_DT_F8   DT_F8
#define TS_DT_I4   DT_I4
#define TS_DT_I8   DT_I8
#define TS_DT_BOOL DT_BOOL

/* dtype 为各工厂的必填末参（sc 侧不省略；DNN 主路径传 DT_F4 对齐 PyTorch float32）。 */

/* ---------------- ts_store：引用计数共享缓冲 ---------------- */
typedef struct ts_store {
    void    *data;     /* 扁平字节缓冲（nbytes 字节；空为 NULL） */
    int64_t  nbytes;   /* 缓冲字节数 */
    int32_t  refcnt;   /* 视图引用数；减到 0 释放 data 与本结构 */
    int32_t  _pad;
} ts_store;

/* ---------------- tensor：N 维数组视图描述符 ---------------- */
typedef struct tensor {
    ts_store *store;   /* 共享缓冲（NULL = 空张量） */
    int32_t  *shape;   /* [ndim] 各维长（每视图私有；ndim==0 为 NULL） */
    int32_t  *strides; /* [ndim] 各维步长（元素为单位，可负——支持 flip） */
    int64_t   offset;  /* 进入 store->data 的起始元素偏移 */
    int32_t   ndim;    /* 维数（0 = 标量/空） */
    int32_t   dtype;   /* TS_DT_* */
    int64_t   numel;   /* 元素总数 = ∏ shape[i]（0 = 空） */
} tensor;

/* ---- 构造（声明即构造仅对值类型生效，tensor& 经伪构造 tensor() 调 init） ---- */
void     tensor_init(tensor *_this, int32_t ndim, int32_t *shape, int32_t dtype); /* 零张量；shape 为 NULL 或 ndim==0 → 空 */
void     tensor_drop(tensor *_this);          /* refcnt-- 释放共享缓冲（到 0 时）+ 释放 shape/strides */

/* ---- 工厂（自由函数，返回新 tensor*，调用方 drop()） ---- */
tensor  *zeros(int32_t ndim, int32_t *shape, int32_t dtype);
tensor  *ones(int32_t ndim, int32_t *shape, int32_t dtype);
tensor  *full(int32_t ndim, int32_t *shape, double value, int32_t dtype);
tensor  *empty(int32_t ndim, int32_t *shape, int32_t dtype);                  /* 未初始化 */
tensor  *arange(double start, double stop, double step, int32_t dtype);
tensor  *linspace(double start, double stop, int32_t num, int32_t dtype);     /* 含端点等差 num 个 */
tensor  *logspace(double start, double stop, int32_t num, double base, int32_t dtype); /* base^linspace */
tensor  *eye(int32_t n, int32_t dtype);
tensor  *from_data(int32_t ndim, int32_t *shape, void *data, int32_t dtype); /* 逐字节按 dtype 拷入 */
tensor  *zeros_like(tensor *o);
tensor  *ones_like(tensor *o);
tensor  *full_like(tensor *o, double value);
tensor  *empty_like(tensor *o);
tensor  *diag(tensor *o, int32_t k);                                          /* 1D→对角阵 / 2D→第 k 对角线 */

/* ---- 元信息 ---- */
int32_t  tensor_ndim(tensor *_this);
int64_t  tensor_numel(tensor *_this);
int32_t  tensor_dtype(tensor *_this);
int32_t  tensor_dim(tensor *_this, int32_t axis);          /* 第 axis 维长（支持负轴；越界返回 0） */
uint8_t  tensor_is_same_shape(tensor *_this, tensor *o);
uint8_t  tensor_is_contiguous(tensor *_this);              /* 是否 C-连续 */

/* ---- 元素访问 ---- */
double   tensor_item(tensor *_this);                       /* 取唯一元素（numel==1；否则取首元素） */
double   tensor_at(tensor *_this, int64_t idx);            /* 逻辑扁平读（按 shape 行主序；越界 0） */
uint8_t  tensor_set_at(tensor *_this, int64_t idx, double v); /* 逻辑扁平写（越界返回 0） */
double   tensor_at_nd(tensor *_this, int32_t *idx);        /* 多维坐标读（idx 为 ndim 个） */
uint8_t  tensor_set_nd(tensor *_this, int32_t *idx, double v); /* 多维坐标写 */
void    *tensor_data(tensor *_this);                       /* 缓冲基址 + offset（连续时即元素首址） */
void     tensor_fill(tensor *_this, double v);             /* 全填充 v（写入视图覆盖区域） */
tensor  *tensor_clone(tensor *_this);                      /* 深拷贝（新独立连续 tensor*） */
tensor  *tensor_contiguous(tensor *_this);                 /* 已连续→共享视图；否则物化连续拷贝 */
tensor  *tensor_astype(tensor *_this, int32_t dtype);      /* dtype 转换（新连续 tensor*） */
uint8_t  tensor_copy_from(tensor *_this, tensor *o);       /* 形状须一致，逐元素拷入（dtype 自动转换） */

/* ---- 形变（视图优先；非连续或跨步时自动物化） ---- */
tensor  *tensor_reshape(tensor *_this, int32_t ndim, int32_t *shape); /* numel 须一致；连续时零拷贝视图 */
tensor  *tensor_transpose(tensor *_this);                  /* 2D 转置（零拷贝视图；ndim!=2 返回 NULL） */
tensor  *tensor_t(tensor *_this);                          /* 末两维转置（>=2D；零拷贝视图） */
tensor  *tensor_permute(tensor *_this, int32_t ndim, int32_t *axes);  /* 维重排（零拷贝视图） */
tensor  *tensor_squeeze(tensor *_this);                    /* 去掉所有长度 1 的维（零拷贝视图） */
tensor  *tensor_unsqueeze(tensor *_this, int32_t axis);    /* 在 axis 处插入长度 1 的维（零拷贝视图） */
tensor  *tensor_ravel(tensor *_this);                      /* 拉平为 1D（连续时视图，否则物化） */
tensor  *tensor_flatten(tensor *_this);                    /* 拉平为 1D（总是物化连续拷贝） */
tensor  *tensor_broadcast_to(tensor *_this, int32_t ndim, int32_t *shape); /* 广播视图（stride=0） */
tensor  *tensor_flip(tensor *_this, int32_t axis);         /* 沿 axis 翻转（负步长视图） */
tensor  *concat(void *arr, int32_t n, int32_t axis);  /* 自由函数：沿 axis 拼接（物化），arr 为 tensor*[] */
tensor  *stack(void *arr, int32_t n, int32_t axis);   /* 自由函数：新增 axis 堆叠（物化），arr 为 tensor*[] */
int32_t  tensor_split(tensor *_this, int32_t parts, int32_t axis, tensor **out); /* 均分为 parts 份视图，写 out[]，返回份数 */
tensor  *tensor_tile(tensor *_this, int32_t reps);         /* 沿首维重复 reps 次（物化） */
tensor  *tensor_repeat(tensor *_this, int32_t reps, int32_t axis); /* 沿 axis 元素级重复（物化） */
tensor  *tensor_pad(tensor *_this, int32_t *pads, double value); /* 常量填充（pads[2*ndim]：每维 before,after；物化） */
tensor  *tensor_roll(tensor *_this, int64_t shift, int32_t axis); /* 循环位移（axis<0 则展平后整体；物化） */

/* ---- 索引（全走成员函数，不改 sc 语法） ---- */
tensor  *tensor_slice(tensor *_this, int32_t dim, int64_t start, int64_t stop, int64_t step); /* 切片视图 */
tensor  *tensor_select(tensor *_this, int32_t dim, int64_t idx);  /* 取 dim==idx 降一维（视图） */
tensor  *tensor_narrow(tensor *_this, int32_t dim, int64_t start, int64_t len); /* 截取 [start,start+len)（视图） */
tensor  *tensor_take(tensor *_this, tensor *idx);          /* 按 idx（整型张量）扁平取值（物化 1D） */
tensor  *tensor_masked_select(tensor *_this, tensor *mask); /* 布尔掩码取值（物化 1D） */
tensor  *tensor_nonzero(tensor *_this);                    /* 非零元素坐标 [k, ndim]（DT_I8；物化） */
tensor  *tensor_gather(tensor *_this, int32_t axis, tensor *index); /* 沿 axis 按 index 采集（物化） */
uint8_t  tensor_scatter_(tensor *_this, int32_t axis, tensor *index, tensor *src); /* 沿 axis 按 index 原地写入 src */
tensor  *where(tensor *cond, tensor *a, tensor *b); /* 自由函数：cond?a:b 逐元素（广播；物化） */

/* ---- 逐元素一元（返回新 tensor*） ---- */
tensor  *tensor_neg(tensor *_this);
tensor  *tensor_abs(tensor *_this);
tensor  *tensor_sqrt(tensor *_this);
tensor  *tensor_square(tensor *_this);
tensor  *tensor_reciprocal(tensor *_this);
tensor  *tensor_exp(tensor *_this);
tensor  *tensor_log(tensor *_this);
tensor  *tensor_floor(tensor *_this);
tensor  *tensor_ceil(tensor *_this);
tensor  *tensor_round(tensor *_this);
tensor  *tensor_trunc(tensor *_this);
tensor  *tensor_sign(tensor *_this);
tensor  *tensor_pow_scalar(tensor *_this, double p);
tensor  *tensor_clip(tensor *_this, double lo, double hi);
tensor  *tensor_sin(tensor *_this);
tensor  *tensor_cos(tensor *_this);
tensor  *tensor_tan(tensor *_this);
tensor  *tensor_asin(tensor *_this);
tensor  *tensor_acos(tensor *_this);
tensor  *tensor_atan(tensor *_this);
tensor  *tensor_sinh(tensor *_this);
tensor  *tensor_cosh(tensor *_this);

/* ---- 逐元素二元（numpy 广播：尾维对齐，维须相等或为 1） ---- */
tensor  *tensor_add(tensor *_this, tensor *o);
tensor  *tensor_sub(tensor *_this, tensor *o);
tensor  *tensor_mul(tensor *_this, tensor *o);
tensor  *tensor_div(tensor *_this, tensor *o);
tensor  *tensor_pow(tensor *_this, tensor *o);
tensor  *tensor_mod(tensor *_this, tensor *o);
tensor  *tensor_maximum(tensor *_this, tensor *o);
tensor  *tensor_minimum(tensor *_this, tensor *o);
tensor  *tensor_atan2(tensor *_this, tensor *o);
tensor  *tensor_add_scalar(tensor *_this, double s);
tensor  *tensor_sub_scalar(tensor *_this, double s);
tensor  *tensor_mul_scalar(tensor *_this, double s);
tensor  *tensor_div_scalar(tensor *_this, double s);

/* ---- 比较（→ DT_BOOL；广播） ---- */
tensor  *tensor_gt(tensor *_this, tensor *o);
tensor  *tensor_ge(tensor *_this, tensor *o);
tensor  *tensor_lt(tensor *_this, tensor *o);
tensor  *tensor_le(tensor *_this, tensor *o);
tensor  *tensor_eq(tensor *_this, tensor *o);
tensor  *tensor_ne(tensor *_this, tensor *o);

/* ---- 逻辑（→ DT_BOOL；按非零判真） ---- */
tensor  *tensor_logical_and(tensor *_this, tensor *o);
tensor  *tensor_logical_or(tensor *_this, tensor *o);
tensor  *tensor_logical_not(tensor *_this);

/* ---- 逐元素原地（o 须可广播到 _this 形状；返回成功标志） ---- */
uint8_t  tensor_add_(tensor *_this, tensor *o);
uint8_t  tensor_sub_(tensor *_this, tensor *o);
uint8_t  tensor_mul_(tensor *_this, tensor *o);
uint8_t  tensor_div_(tensor *_this, tensor *o);
uint8_t  tensor_add_scalar_(tensor *_this, double s);
uint8_t  tensor_sub_scalar_(tensor *_this, double s);
uint8_t  tensor_mul_scalar_(tensor *_this, double s);
uint8_t  tensor_div_scalar_(tensor *_this, double s);

/* ---- 规约（axis<0 → 全规约得标量张量；否则沿 axis 降维；keepdim!=0 保留长度 1 的维） ---- */
tensor  *tensor_sum(tensor *_this, int32_t axis, uint8_t keepdim);
tensor  *tensor_mean(tensor *_this, int32_t axis, uint8_t keepdim);
tensor  *tensor_prod(tensor *_this, int32_t axis, uint8_t keepdim);
tensor  *tensor_max(tensor *_this, int32_t axis, uint8_t keepdim);
tensor  *tensor_min(tensor *_this, int32_t axis, uint8_t keepdim);
tensor  *tensor_argmax(tensor *_this, int32_t axis, uint8_t keepdim);       /* 结果 dtype=I8 */
tensor  *tensor_argmin(tensor *_this, int32_t axis, uint8_t keepdim);       /* 结果 dtype=I8 */
tensor  *tensor_std(tensor *_this, int32_t axis, uint8_t keepdim);          /* 总体标准差（除 N） */
tensor  *tensor_var(tensor *_this, int32_t axis, uint8_t keepdim);          /* 总体方差（除 N） */
tensor  *tensor_cumsum(tensor *_this, int32_t axis);                        /* 沿 axis 前缀和（形状不变） */
tensor  *tensor_cumprod(tensor *_this, int32_t axis);                       /* 沿 axis 前缀积（形状不变） */
tensor  *tensor_any(tensor *_this, int32_t axis, uint8_t keepdim);          /* → DT_BOOL */
tensor  *tensor_all(tensor *_this, int32_t axis, uint8_t keepdim);          /* → DT_BOOL */
tensor  *tensor_median(tensor *_this, int32_t axis, uint8_t keepdim);       /* 沿 axis 中位数 */
tensor  *tensor_percentile(tensor *_this, double q, int32_t axis, uint8_t keepdim); /* 第 q 百分位 */
double   tensor_sum_all(tensor *_this);
double   tensor_mean_all(tensor *_this);
double   tensor_prod_all(tensor *_this);
double   tensor_max_all(tensor *_this);
double   tensor_min_all(tensor *_this);
double   tensor_std_all(tensor *_this);
double   tensor_var_all(tensor *_this);
double   tensor_median_all(tensor *_this);
double   tensor_percentile_all(tensor *_this, double q);

/* ---- 线代 ---- */
tensor  *tensor_matmul(tensor *_this, tensor *o);          /* 2D×2D；>2D 批量广播；可选 BLAS（2D F4） */
double   tensor_dot(tensor *_this, tensor *o);             /* 1D·1D 内积（长度须一致） */
tensor  *tensor_outer(tensor *_this, tensor *o);           /* 1D⊗1D → 2D 外积 */
double   tensor_trace(tensor *_this);                      /* 2D 主对角线之和 */
tensor  *tensor_diagonal(tensor *_this, int32_t k);        /* 2D 第 k 对角线（1D 视图） */
tensor  *tensor_bmm(tensor *_this, tensor *o);             /* 3D 批量矩乘（不做 batch 广播） */
tensor  *tensor_addmm(tensor *_this, tensor *mat1, tensor *mat2, double beta, double alpha); /* beta*self + alpha*(mat1@mat2) */
tensor  *tensor_triu(tensor *_this, int32_t k);            /* 上三角（物化） */
tensor  *tensor_tril(tensor *_this, int32_t k);            /* 下三角（物化） */
double   tensor_norm(tensor *_this, double p);             /* 元素 p-范数（p=2 Frobenius；p<=0 → inf 范数） */
double   tensor_det(tensor *_this);                        /* 方阵行列式（LU） */
tensor  *tensor_inv(tensor *_this);                        /* 方阵逆（Gauss-Jordan） */
tensor  *tensor_solve(tensor *_this, tensor *b);           /* 解 Ax=b */
tensor  *tensor_cholesky(tensor *_this);                   /* 下三角 L（A=LLᵀ） */
uint8_t  tensor_qr(tensor *_this, void *out);              /* QR；out=tensor*[2]=(Q,R) */
uint8_t  tensor_eigh(tensor *_this, void *out);            /* 对称特征（Jacobi）；out=(vals,vecs) */
uint8_t  tensor_svd(tensor *_this, void *out);             /* SVD；out=tensor*[3]=(U,S,V) */

/* ---- nn 激活/逐点函数（返回新 tensor*） ---- */
tensor  *tensor_relu(tensor *_this);
tensor  *tensor_sigmoid(tensor *_this);
tensor  *tensor_tanh(tensor *_this);
tensor  *tensor_softmax(tensor *_this, int32_t axis);      /* 沿 axis 做 softmax（数值稳定） */
tensor  *tensor_log_softmax(tensor *_this, int32_t axis);  /* 沿 axis 做 log-softmax（数值稳定） */
tensor  *tensor_leaky_relu(tensor *_this, double slope);   /* x>0?x:slope*x */
tensor  *tensor_elu(tensor *_this, double alpha);          /* x>0?x:alpha*(e^x-1) */
tensor  *tensor_silu(tensor *_this);                       /* x*sigmoid(x) */
tensor  *tensor_gelu(tensor *_this);                       /* 近似 tanh GELU */
uint8_t  tensor_relu_(tensor *_this);                      /* 原地 relu */
double   tensor_cross_entropy(tensor *_this, tensor *target); /* logits[N,C] + 整型 target[N] → 平均交叉熵 */
double   tensor_mse_loss(tensor *_this, tensor *target);   /* mean((x-target)^2) */
double   tensor_nll_loss(tensor *_this, tensor *target);   /* input 为 log-prob [N,C] */
double   tensor_bce_with_logits(tensor *_this, tensor *target); /* BCE(logits,target) */
tensor  *tensor_layer_norm(tensor *_this, int32_t axis, double eps); /* 沿 axis 标准化 */
tensor  *tensor_dropout(tensor *_this, double p, uint8_t train); /* 训练随机置零，推理直通 */
tensor  *tensor_sdpa(tensor *_this, tensor *k, tensor *v, uint8_t causal); /* scaled dot-product attention */
tensor  *tensor_conv1d(tensor *_this, tensor *w, tensor *bias, int32_t stride, int32_t padding);
tensor  *tensor_conv2d(tensor *_this, tensor *w, tensor *bias, int32_t stride_h, int32_t stride_w, int32_t pad_h, int32_t pad_w);
tensor  *tensor_max_pool1d(tensor *_this, int32_t kernel, int32_t stride, int32_t padding);
tensor  *tensor_avg_pool1d(tensor *_this, int32_t kernel, int32_t stride, int32_t padding);
tensor  *tensor_max_pool2d(tensor *_this, int32_t kh, int32_t kw, int32_t sh, int32_t sw, int32_t ph, int32_t pw);
tensor  *tensor_avg_pool2d(tensor *_this, int32_t kh, int32_t kw, int32_t sh, int32_t sw, int32_t ph, int32_t pw);
tensor  *tensor_embedding(tensor *weight, tensor *idx);    /* weight[V,D]+idx[...] → out[...,D] */
tensor  *tensor_batch_norm(tensor *x, double eps);         /* 训练，通道=dim1，无仿射 */
tensor  *tensor_rms_norm(tensor *x, int32_t axis, double eps); /* 沿 axis 的 RMSNorm，无仿射 */
tensor  *tensor_dropout_mask(tensor *_this, double p);     /* 训练掩码：保留→1/(1-p)，丢弃→0 */

/* ---- 反向数值核（与前向算子成对；供 nn 自动微分引擎组合调用，不在 sc 表面暴露） ---- */
tensor  *tensor_sum_to(tensor *grad, int32_t ndim, int32_t *shape); /* 广播反向：sum-reduce 回 shape */
tensor  *tensor_relu_backward(tensor *grad, tensor *x);            /* grad * (x>0) */
tensor  *tensor_sigmoid_backward(tensor *grad, tensor *y);         /* grad * y*(1-y)，y 为前向输出 */
tensor  *tensor_tanh_backward(tensor *grad, tensor *y);            /* grad * (1-y^2)，y 为前向输出 */
tensor  *tensor_mse_backward(tensor *x, tensor *target);          /* 2/N*(x-target) */
tensor  *tensor_cross_entropy_backward(tensor *logits, tensor *target); /* (softmax-onehot)/N */
tensor  *tensor_softmax_backward(tensor *grad, tensor *y, int32_t axis);       /* y 为前向输出 */
tensor  *tensor_log_softmax_backward(tensor *grad, tensor *out, int32_t axis); /* out 为前向输出 */
tensor  *tensor_leaky_relu_backward(tensor *grad, tensor *x, double slope);    /* x 为前向输入 */
tensor  *tensor_elu_backward(tensor *grad, tensor *x, double alpha);           /* x 为前向输入 */
tensor  *tensor_silu_backward(tensor *grad, tensor *x);                        /* x 为前向输入 */
tensor  *tensor_gelu_backward(tensor *grad, tensor *x);                        /* x 为前向输入 */
tensor  *tensor_layer_norm_backward(tensor *grad, tensor *x, int32_t axis, double eps); /* 无仿射 */
tensor  *tensor_conv2d_backward_input(tensor *grad, tensor *x, tensor *w, int32_t sh, int32_t sw, int32_t ph, int32_t pw);
tensor  *tensor_conv2d_backward_weight(tensor *grad, tensor *x, tensor *w, int32_t sh, int32_t sw, int32_t ph, int32_t pw);
tensor  *tensor_conv2d_backward_bias(tensor *grad);
tensor  *tensor_max_pool2d_backward(tensor *grad, tensor *x, int32_t kh, int32_t kw, int32_t sh, int32_t sw, int32_t ph, int32_t pw);
tensor  *tensor_avg_pool2d_backward(tensor *grad, tensor *x, int32_t kh, int32_t kw, int32_t sh, int32_t sw, int32_t ph, int32_t pw);
tensor  *tensor_embedding_backward(tensor *grad, tensor *idx, int32_t V);      /* scatter-add → dW[V,D] */
tensor  *tensor_batch_norm_backward(tensor *grad, tensor *x, double eps);      /* 无仿射 */
tensor  *tensor_rms_norm_backward(tensor *grad, tensor *x, int32_t axis, double eps); /* 无仿射 */

/* ---- 比较/杂项 ---- */
uint8_t  tensor_equal(tensor *_this, tensor *o);                       /* 形状+逐元素全等 */
uint8_t  tensor_allclose(tensor *_this, tensor *o, double rtol, double atol); /* 近似相等 */
uint8_t  tensor_shuffle_(tensor *_this);                   /* 沿首维随机洗牌（原地） */
uint8_t  tensor_save(tensor *_this, const char *path);     /* 存为 NumPy .npy（C-order） */
void     tensor_print(tensor *_this);                      /* 形状 + 元素（大张量省略） */

/* ---- 构造补充 / 随机 / IO（自由函数） ---- */
tensor  *tri(int32_t n, int32_t m, int32_t k, int32_t dtype); /* [n,m] 下三角 1 */
uint8_t  meshgrid(void *arr, int32_t n, int32_t indexing, void *out); /* N 个 1D→N 个 N 维网格 */
void     rand_seed(int64_t seed);
tensor  *rand_uniform(int32_t ndim, int32_t *shape, double lo, double hi, int32_t dtype);
tensor  *rand_normal(int32_t ndim, int32_t *shape, double mean, double std, int32_t dtype);
tensor  *rand_randint(int32_t ndim, int32_t *shape, int64_t lo, int64_t hi, int32_t dtype);
tensor  *permutation(int32_t n, int32_t dtype);
tensor  *ts_load(const char *path);                         /* 读取 .npy（兼容旧 .scts） */

#ifdef __cplusplus
}
#endif

#endif /* SC_TS_H */
