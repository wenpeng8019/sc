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
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- dtype ---------------- */
/* 采用 ts.sc 的 @def dtype 枚举常量的 sc 命名域拼写（sc_DT_F4...）作为首选：消费单元 inc ts.sc 后
 * 编译器直接 #include 本头，跨模块按 scPfx 名引用枚举常量须由本头提供同名定义。
 * 另给 TS_DT_* 前缀别名，供外部 C ABI 使用者按需选用（避免与宿主符号冲突）。 */
enum {
    sc_DT_F4   = 0,   /* float    (float32，PyTorch 默认精度) */
    sc_DT_F8   = 1,   /* double   (float64) */
    sc_DT_I4   = 2,   /* int32_t */
    sc_DT_I8   = 3,   /* int64_t */
    sc_DT_BOOL = 4    /* uint8_t  (比较/逻辑/掩码输出；0/1) */
};
#define TS_DT_F4   sc_DT_F4
#define TS_DT_F8   sc_DT_F8
#define TS_DT_I4   sc_DT_I4
#define TS_DT_I8   sc_DT_I8
#define TS_DT_BOOL sc_DT_BOOL

/* dtype 为各工厂的必填末参（sc 侧不省略；DNN 主路径传 DT_F4 对齐 PyTorch float32）。 */

/* ---------------- ts_store：引用计数共享缓冲 ---------------- */
typedef struct sc_ts_store {
    void    *data;     /* 扁平字节缓冲（nbytes 字节；空为 NULL） */
    int64_t  nbytes;   /* 缓冲字节数 */
    int32_t  refcnt;   /* 视图引用数；减到 0 释放 data 与本结构 */
    int32_t  _pad;
} sc_ts_store;

/* ---------------- tensor：N 维数组视图描述符 ---------------- */
typedef struct sc_tensor {
    sc_ts_store *store;   /* 共享缓冲（NULL = 空张量） */
    int32_t  *shape;   /* [ndim] 各维长（每视图私有；ndim==0 为 NULL） */
    int32_t  *strides; /* [ndim] 各维步长（元素为单位，可负——支持 flip） */
    int64_t   offset;  /* 进入 store->data 的起始元素偏移 */
    int32_t   ndim;    /* 维数（0 = 标量/空） */
    int32_t   dtype;   /* TS_DT_* */
    int64_t   numel;   /* 元素总数 = ∏ shape[i]（0 = 空） */
} sc_tensor;

/* ---- 构造（声明即构造仅对值类型生效，tensor& 经伪构造 tensor() 调 init） ---- */
void     sc_tensor_init(sc_tensor *_this, int32_t ndim, int32_t *shape, int32_t dtype); /* 零张量；shape 为 NULL 或 ndim==0 → 空 */
void     sc_tensor_drop(sc_tensor *_this);          /* refcnt-- 释放共享缓冲（到 0 时）+ 释放 shape/strides */

/* ---- 工厂（自由函数，返回新 tensor*，调用方 drop()） ---- */
sc_tensor  *sc_zeros(int32_t ndim, int32_t *shape, int32_t dtype);
sc_tensor  *sc_ones(int32_t ndim, int32_t *shape, int32_t dtype);
sc_tensor  *sc_full(int32_t ndim, int32_t *shape, double value, int32_t dtype);
sc_tensor  *sc_empty(int32_t ndim, int32_t *shape, int32_t dtype);                  /* 未初始化 */
sc_tensor  *sc_arange(double start, double stop, double step, int32_t dtype);
sc_tensor  *sc_linspace(double start, double stop, int32_t num, int32_t dtype);     /* 含端点等差 num 个 */
sc_tensor  *sc_logspace(double start, double stop, int32_t num, double base, int32_t dtype); /* base^linspace */
sc_tensor  *sc_eye(int32_t n, int32_t dtype);
sc_tensor  *sc_from_data(int32_t ndim, int32_t *shape, void *data, int32_t dtype); /* 逐字节按 dtype 拷入 */
sc_tensor  *sc_zeros_like(sc_tensor *o);
sc_tensor  *sc_ones_like(sc_tensor *o);
sc_tensor  *sc_full_like(sc_tensor *o, double value);
sc_tensor  *sc_empty_like(sc_tensor *o);
sc_tensor  *sc_diag(sc_tensor *o, int32_t k);                                          /* 1D→对角阵 / 2D→第 k 对角线 */

/* ---- 元信息 ---- */
int32_t  sc_tensor_ndim(sc_tensor *_this);
int64_t  sc_tensor_numel(sc_tensor *_this);
int32_t  sc_tensor_dtype(sc_tensor *_this);
int32_t  sc_tensor_dim(sc_tensor *_this, int32_t axis);          /* 第 axis 维长（支持负轴；越界返回 0） */
bool     sc_tensor_is_same_shape(sc_tensor *_this, sc_tensor *o);
bool     sc_tensor_is_contiguous(sc_tensor *_this);              /* 是否 C-连续 */

/* ---- 元素访问 ---- */
double   sc_tensor_item(sc_tensor *_this);                       /* 取唯一元素（numel==1；否则取首元素） */
double   sc_tensor_at(sc_tensor *_this, int64_t idx);            /* 逻辑扁平读（按 shape 行主序；越界 0） */
bool     sc_tensor_set_at(sc_tensor *_this, int64_t idx, double v); /* 逻辑扁平写（越界返回 0） */
double   sc_tensor_at_nd(sc_tensor *_this, int32_t *idx);        /* 多维坐标读（idx 为 ndim 个） */
bool     sc_tensor_set_nd(sc_tensor *_this, int32_t *idx, double v); /* 多维坐标写 */
void    *sc_tensor_data(sc_tensor *_this);                       /* 缓冲基址 + offset（连续时即元素首址） */
void     sc_tensor_fill(sc_tensor *_this, double v);             /* 全填充 v（写入视图覆盖区域） */
sc_tensor  *sc_tensor_clone(sc_tensor *_this);                      /* 深拷贝（新独立连续 tensor*） */
sc_tensor  *sc_tensor_contiguous(sc_tensor *_this);                 /* 已连续→共享视图；否则物化连续拷贝 */
sc_tensor  *sc_tensor_astype(sc_tensor *_this, int32_t dtype);      /* dtype 转换（新连续 tensor*） */
bool     sc_tensor_copy_from(sc_tensor *_this, sc_tensor *o);       /* 形状须一致，逐元素拷入（dtype 自动转换） */

/* ---- 形变（视图优先；非连续或跨步时自动物化） ---- */
sc_tensor  *sc_tensor_reshape(sc_tensor *_this, int32_t ndim, int32_t *shape); /* numel 须一致；连续时零拷贝视图 */
sc_tensor  *sc_tensor_transpose(sc_tensor *_this);                  /* 2D 转置（零拷贝视图；ndim!=2 返回 NULL） */
sc_tensor  *sc_tensor_t(sc_tensor *_this);                          /* 末两维转置（>=2D；零拷贝视图） */
sc_tensor  *sc_tensor_permute(sc_tensor *_this, int32_t ndim, int32_t *axes);  /* 维重排（零拷贝视图） */
sc_tensor  *sc_tensor_squeeze(sc_tensor *_this);                    /* 去掉所有长度 1 的维（零拷贝视图） */
sc_tensor  *sc_tensor_unsqueeze(sc_tensor *_this, int32_t axis);    /* 在 axis 处插入长度 1 的维（零拷贝视图） */
sc_tensor  *sc_tensor_ravel(sc_tensor *_this);                      /* 拉平为 1D（连续时视图，否则物化） */
sc_tensor  *sc_tensor_flatten(sc_tensor *_this);                    /* 拉平为 1D（总是物化连续拷贝） */
sc_tensor  *sc_tensor_broadcast_to(sc_tensor *_this, int32_t ndim, int32_t *shape); /* 广播视图（stride=0） */
sc_tensor  *sc_tensor_flip(sc_tensor *_this, int32_t axis);         /* 沿 axis 翻转（负步长视图） */
sc_tensor  *sc_concat(void *arr, int32_t n, int32_t axis);  /* 自由函数：沿 axis 拼接（物化），arr 为 tensor*[] */
sc_tensor  *sc_stack(void *arr, int32_t n, int32_t axis);   /* 自由函数：新增 axis 堆叠（物化），arr 为 tensor*[] */
int32_t  sc_tensor_split(sc_tensor *_this, int32_t parts, int32_t axis, sc_tensor **out); /* 均分为 parts 份视图，写 out[]，返回份数 */
sc_tensor  *sc_tensor_tile(sc_tensor *_this, int32_t reps);         /* 沿首维重复 reps 次（物化） */
sc_tensor  *sc_tensor_repeat(sc_tensor *_this, int32_t reps, int32_t axis); /* 沿 axis 元素级重复（物化） */
sc_tensor  *sc_tensor_pad(sc_tensor *_this, int32_t *pads, double value); /* 常量填充（pads[2*ndim]：每维 before,after；物化） */
sc_tensor  *sc_tensor_roll(sc_tensor *_this, int64_t shift, int32_t axis); /* 循环位移（axis<0 则展平后整体；物化） */

/* ---- 索引（全走成员函数，不改 sc 语法） ---- */
sc_tensor  *sc_tensor_slice(sc_tensor *_this, int32_t dim, int64_t start, int64_t stop, int64_t step); /* 切片视图 */
sc_tensor  *sc_tensor_select(sc_tensor *_this, int32_t dim, int64_t idx);  /* 取 dim==idx 降一维（视图） */
sc_tensor  *sc_tensor_narrow(sc_tensor *_this, int32_t dim, int64_t start, int64_t len); /* 截取 [start,start+len)（视图） */
sc_tensor  *sc_tensor_take(sc_tensor *_this, sc_tensor *idx);          /* 按 idx（整型张量）扁平取值（物化 1D） */
sc_tensor  *sc_tensor_masked_select(sc_tensor *_this, sc_tensor *mask); /* 布尔掩码取值（物化 1D） */
sc_tensor  *sc_tensor_nonzero(sc_tensor *_this);                    /* 非零元素坐标 [k, ndim]（DT_I8；物化） */
sc_tensor  *sc_tensor_gather(sc_tensor *_this, int32_t axis, sc_tensor *index); /* 沿 axis 按 index 采集（物化） */
bool     sc_tensor_scatter_(sc_tensor *_this, int32_t axis, sc_tensor *index, sc_tensor *src); /* 沿 axis 按 index 原地写入 src */
sc_tensor  *sc_where(sc_tensor *cond, sc_tensor *a, sc_tensor *b); /* 自由函数：cond?a:b 逐元素（广播；物化） */

/* ---- 逐元素一元（返回新 sc_tensor*） ---- */
sc_tensor  *sc_tensor_neg(sc_tensor *_this);
sc_tensor  *sc_tensor_abs(sc_tensor *_this);
sc_tensor  *sc_tensor_sqrt(sc_tensor *_this);
sc_tensor  *sc_tensor_square(sc_tensor *_this);
sc_tensor  *sc_tensor_reciprocal(sc_tensor *_this);
sc_tensor  *sc_tensor_exp(sc_tensor *_this);
sc_tensor  *sc_tensor_log(sc_tensor *_this);
sc_tensor  *sc_tensor_floor(sc_tensor *_this);
sc_tensor  *sc_tensor_ceil(sc_tensor *_this);
sc_tensor  *sc_tensor_round(sc_tensor *_this);
sc_tensor  *sc_tensor_trunc(sc_tensor *_this);
sc_tensor  *sc_tensor_sign(sc_tensor *_this);
sc_tensor  *sc_tensor_pow_scalar(sc_tensor *_this, double p);
sc_tensor  *sc_tensor_clip(sc_tensor *_this, double lo, double hi);
sc_tensor  *sc_tensor_sin(sc_tensor *_this);
sc_tensor  *sc_tensor_cos(sc_tensor *_this);
sc_tensor  *sc_tensor_tan(sc_tensor *_this);
sc_tensor  *sc_tensor_asin(sc_tensor *_this);
sc_tensor  *sc_tensor_acos(sc_tensor *_this);
sc_tensor  *sc_tensor_atan(sc_tensor *_this);
sc_tensor  *sc_tensor_sinh(sc_tensor *_this);
sc_tensor  *sc_tensor_cosh(sc_tensor *_this);

/* ---- 逐元素二元（numpy 广播：尾维对齐，维须相等或为 1） ---- */
sc_tensor  *sc_tensor_add(sc_tensor *_this, sc_tensor *o);
sc_tensor  *sc_tensor_sub(sc_tensor *_this, sc_tensor *o);
sc_tensor  *sc_tensor_mul(sc_tensor *_this, sc_tensor *o);
sc_tensor  *sc_tensor_div(sc_tensor *_this, sc_tensor *o);
sc_tensor  *sc_tensor_pow(sc_tensor *_this, sc_tensor *o);
sc_tensor  *sc_tensor_mod(sc_tensor *_this, sc_tensor *o);
sc_tensor  *sc_tensor_maximum(sc_tensor *_this, sc_tensor *o);
sc_tensor  *sc_tensor_minimum(sc_tensor *_this, sc_tensor *o);
sc_tensor  *sc_tensor_atan2(sc_tensor *_this, sc_tensor *o);
sc_tensor  *sc_tensor_add_scalar(sc_tensor *_this, double s);
sc_tensor  *sc_tensor_sub_scalar(sc_tensor *_this, double s);
sc_tensor  *sc_tensor_mul_scalar(sc_tensor *_this, double s);
sc_tensor  *sc_tensor_div_scalar(sc_tensor *_this, double s);

/* ---- 比较（→ DT_BOOL；广播） ---- */
sc_tensor  *sc_tensor_gt(sc_tensor *_this, sc_tensor *o);
sc_tensor  *sc_tensor_ge(sc_tensor *_this, sc_tensor *o);
sc_tensor  *sc_tensor_lt(sc_tensor *_this, sc_tensor *o);
sc_tensor  *sc_tensor_le(sc_tensor *_this, sc_tensor *o);
sc_tensor  *sc_tensor_eq(sc_tensor *_this, sc_tensor *o);
sc_tensor  *sc_tensor_ne(sc_tensor *_this, sc_tensor *o);

/* ---- 逻辑（→ DT_BOOL；按非零判真） ---- */
sc_tensor  *sc_tensor_logical_and(sc_tensor *_this, sc_tensor *o);
sc_tensor  *sc_tensor_logical_or(sc_tensor *_this, sc_tensor *o);
sc_tensor  *sc_tensor_logical_not(sc_tensor *_this);

/* ---- 逐元素原地（o 须可广播到 _this 形状；返回成功标志） ---- */
bool     sc_tensor_add_(sc_tensor *_this, sc_tensor *o);
bool     sc_tensor_sub_(sc_tensor *_this, sc_tensor *o);
bool     sc_tensor_mul_(sc_tensor *_this, sc_tensor *o);
bool     sc_tensor_div_(sc_tensor *_this, sc_tensor *o);
bool     sc_tensor_add_scalar_(sc_tensor *_this, double s);
bool     sc_tensor_sub_scalar_(sc_tensor *_this, double s);
bool     sc_tensor_mul_scalar_(sc_tensor *_this, double s);
bool     sc_tensor_div_scalar_(sc_tensor *_this, double s);

/* ---- 规约（axis<0 → 全规约得标量张量；否则沿 axis 降维；keepdim!=0 保留长度 1 的维） ---- */
sc_tensor  *sc_tensor_sum(sc_tensor *_this, int32_t axis, bool keepdim);
sc_tensor  *sc_tensor_mean(sc_tensor *_this, int32_t axis, bool keepdim);
sc_tensor  *sc_tensor_prod(sc_tensor *_this, int32_t axis, bool keepdim);
sc_tensor  *sc_tensor_max(sc_tensor *_this, int32_t axis, bool keepdim);
sc_tensor  *sc_tensor_min(sc_tensor *_this, int32_t axis, bool keepdim);
sc_tensor  *sc_tensor_argmax(sc_tensor *_this, int32_t axis, bool keepdim);       /* 结果 dtype=I8 */
sc_tensor  *sc_tensor_argmin(sc_tensor *_this, int32_t axis, bool keepdim);       /* 结果 dtype=I8 */
sc_tensor  *sc_tensor_std(sc_tensor *_this, int32_t axis, bool keepdim);          /* 总体标准差（除 N） */
sc_tensor  *sc_tensor_var(sc_tensor *_this, int32_t axis, bool keepdim);          /* 总体方差（除 N） */
sc_tensor  *sc_tensor_cumsum(sc_tensor *_this, int32_t axis);                        /* 沿 axis 前缀和（形状不变） */
sc_tensor  *sc_tensor_cumprod(sc_tensor *_this, int32_t axis);                       /* 沿 axis 前缀积（形状不变） */
sc_tensor  *sc_tensor_any(sc_tensor *_this, int32_t axis, bool keepdim);          /* → DT_BOOL */
sc_tensor  *sc_tensor_all(sc_tensor *_this, int32_t axis, bool keepdim);          /* → DT_BOOL */
sc_tensor  *sc_tensor_median(sc_tensor *_this, int32_t axis, bool keepdim);       /* 沿 axis 中位数 */
sc_tensor  *sc_tensor_percentile(sc_tensor *_this, double q, int32_t axis, bool keepdim); /* 第 q 百分位 */
double   sc_tensor_sum_all(sc_tensor *_this);
double   sc_tensor_mean_all(sc_tensor *_this);
double   sc_tensor_prod_all(sc_tensor *_this);
double   sc_tensor_max_all(sc_tensor *_this);
double   sc_tensor_min_all(sc_tensor *_this);
double   sc_tensor_std_all(sc_tensor *_this);
double   sc_tensor_var_all(sc_tensor *_this);
double   sc_tensor_median_all(sc_tensor *_this);
double   sc_tensor_percentile_all(sc_tensor *_this, double q);

/* ---- 线代 ---- */
sc_tensor  *sc_tensor_matmul(sc_tensor *_this, sc_tensor *o);          /* 2D×2D；>2D 批量广播；可选 BLAS（2D F4） */
double   sc_tensor_dot(sc_tensor *_this, sc_tensor *o);             /* 1D·1D 内积（长度须一致） */
sc_tensor  *sc_tensor_outer(sc_tensor *_this, sc_tensor *o);           /* 1D⊗1D → 2D 外积 */
double   sc_tensor_trace(sc_tensor *_this);                      /* 2D 主对角线之和 */
sc_tensor  *sc_tensor_diagonal(sc_tensor *_this, int32_t k);        /* 2D 第 k 对角线（1D 视图） */
sc_tensor  *sc_tensor_bmm(sc_tensor *_this, sc_tensor *o);             /* 3D 批量矩乘（不做 batch 广播） */
sc_tensor  *sc_tensor_addmm(sc_tensor *_this, sc_tensor *mat1, sc_tensor *mat2, double beta, double alpha); /* beta*self + alpha*(mat1@mat2) */
sc_tensor  *sc_tensor_triu(sc_tensor *_this, int32_t k);            /* 上三角（物化） */
sc_tensor  *sc_tensor_tril(sc_tensor *_this, int32_t k);            /* 下三角（物化） */
double   sc_tensor_norm(sc_tensor *_this, double p);             /* 元素 p-范数（p=2 Frobenius；p<=0 → inf 范数） */
double   sc_tensor_det(sc_tensor *_this);                        /* 方阵行列式（LU） */
sc_tensor  *sc_tensor_inv(sc_tensor *_this);                        /* 方阵逆（Gauss-Jordan） */
sc_tensor  *sc_tensor_solve(sc_tensor *_this, sc_tensor *b);           /* 解 Ax=b */
sc_tensor  *sc_tensor_cholesky(sc_tensor *_this);                   /* 下三角 L（A=LLᵀ） */
bool     sc_tensor_qr(sc_tensor *_this, void *out);              /* QR；out=tensor*[2]=(Q,R) */
bool     sc_tensor_eigh(sc_tensor *_this, void *out);            /* 对称特征（Jacobi）；out=(vals,vecs) */
bool     sc_tensor_svd(sc_tensor *_this, void *out);             /* SVD；out=tensor*[3]=(U,S,V) */

/* ---- nn 激活/逐点函数（返回新 tensor*） ---- */
sc_tensor  *sc_tensor_relu(sc_tensor *_this);
sc_tensor  *sc_tensor_sigmoid(sc_tensor *_this);
sc_tensor  *sc_tensor_tanh(sc_tensor *_this);
sc_tensor  *sc_tensor_softmax(sc_tensor *_this, int32_t axis);      /* 沿 axis 做 softmax（数值稳定） */
sc_tensor  *sc_tensor_log_softmax(sc_tensor *_this, int32_t axis);  /* 沿 axis 做 log-softmax（数值稳定） */
sc_tensor  *sc_tensor_leaky_relu(sc_tensor *_this, double slope);   /* x>0?x:slope*x */
sc_tensor  *sc_tensor_elu(sc_tensor *_this, double alpha);          /* x>0?x:alpha*(e^x-1) */
sc_tensor  *sc_tensor_silu(sc_tensor *_this);                       /* x*sigmoid(x) */
sc_tensor  *sc_tensor_gelu(sc_tensor *_this);                       /* 近似 tanh GELU */
bool     sc_tensor_relu_(sc_tensor *_this);                      /* 原地 relu */
double   sc_tensor_cross_entropy(sc_tensor *_this, sc_tensor *target); /* logits[N,C] + 整型 target[N] → 平均交叉熵 */
double   sc_tensor_mse_loss(sc_tensor *_this, sc_tensor *target);   /* mean((x-target)^2) */
double   sc_tensor_nll_loss(sc_tensor *_this, sc_tensor *target);   /* input 为 log-prob [N,C] */
double   sc_tensor_bce_with_logits(sc_tensor *_this, sc_tensor *target); /* BCE(logits,target) */
sc_tensor  *sc_tensor_layer_norm(sc_tensor *_this, int32_t axis, double eps); /* 沿 axis 标准化 */
sc_tensor  *sc_tensor_dropout(sc_tensor *_this, double p, bool train); /* 训练随机置零，推理直通 */
sc_tensor  *sc_tensor_sdpa(sc_tensor *_this, sc_tensor *k, sc_tensor *v, bool causal); /* scaled dot-product attention */
sc_tensor  *sc_tensor_conv1d(sc_tensor *_this, sc_tensor *w, sc_tensor *bias, int32_t stride, int32_t padding);
sc_tensor  *sc_tensor_conv2d(sc_tensor *_this, sc_tensor *w, sc_tensor *bias, int32_t stride_h, int32_t stride_w, int32_t pad_h, int32_t pad_w);
sc_tensor  *sc_tensor_max_pool1d(sc_tensor *_this, int32_t kernel, int32_t stride, int32_t padding);
sc_tensor  *sc_tensor_avg_pool1d(sc_tensor *_this, int32_t kernel, int32_t stride, int32_t padding);
sc_tensor  *sc_tensor_max_pool2d(sc_tensor *_this, int32_t kh, int32_t kw, int32_t sh, int32_t sw, int32_t ph, int32_t pw);
sc_tensor  *sc_tensor_avg_pool2d(sc_tensor *_this, int32_t kh, int32_t kw, int32_t sh, int32_t sw, int32_t ph, int32_t pw);
sc_tensor  *sc_tensor_embedding(sc_tensor *weight, sc_tensor *idx);    /* weight[V,D]+idx[...] → out[...,D] */
sc_tensor  *sc_tensor_batch_norm(sc_tensor *x, double eps);         /* 训练，通道=dim1，无仿射 */
sc_tensor  *sc_tensor_rms_norm(sc_tensor *x, int32_t axis, double eps); /* 沿 axis 的 RMSNorm，无仿射 */
sc_tensor  *sc_tensor_dropout_mask(sc_tensor *_this, double p);     /* 训练掩码：保留→1/(1-p)，丢弃→0 */

/* ---- 反向数值核（与前向算子成对；供 nn 自动微分引擎组合调用，不在 sc 表面暴露） ---- */
sc_tensor  *sc_tensor_sum_to(sc_tensor *grad, int32_t ndim, int32_t *shape); /* 广播反向：sum-reduce 回 shape */
sc_tensor  *sc_tensor_relu_backward(sc_tensor *grad, sc_tensor *x);            /* grad * (x>0) */
sc_tensor  *sc_tensor_sigmoid_backward(sc_tensor *grad, sc_tensor *y);         /* grad * y*(1-y)，y 为前向输出 */
sc_tensor  *sc_tensor_tanh_backward(sc_tensor *grad, sc_tensor *y);            /* grad * (1-y^2)，y 为前向输出 */
sc_tensor  *sc_tensor_mse_backward(sc_tensor *x, sc_tensor *target);          /* 2/N*(x-target) */
sc_tensor  *sc_tensor_cross_entropy_backward(sc_tensor *logits, sc_tensor *target); /* (softmax-onehot)/N */
sc_tensor  *sc_tensor_softmax_backward(sc_tensor *grad, sc_tensor *y, int32_t axis);       /* y 为前向输出 */
sc_tensor  *sc_tensor_log_softmax_backward(sc_tensor *grad, sc_tensor *out, int32_t axis); /* out 为前向输出 */
sc_tensor  *sc_tensor_leaky_relu_backward(sc_tensor *grad, sc_tensor *x, double slope);    /* x 为前向输入 */
sc_tensor  *sc_tensor_elu_backward(sc_tensor *grad, sc_tensor *x, double alpha);           /* x 为前向输入 */
sc_tensor  *sc_tensor_silu_backward(sc_tensor *grad, sc_tensor *x);                        /* x 为前向输入 */
sc_tensor  *sc_tensor_gelu_backward(sc_tensor *grad, sc_tensor *x);                        /* x 为前向输入 */
sc_tensor  *sc_tensor_layer_norm_backward(sc_tensor *grad, sc_tensor *x, int32_t axis, double eps); /* 无仿射 */
sc_tensor  *sc_tensor_conv2d_backward_input(sc_tensor *grad, sc_tensor *x, sc_tensor *w, int32_t sh, int32_t sw, int32_t ph, int32_t pw);
sc_tensor  *sc_tensor_conv2d_backward_weight(sc_tensor *grad, sc_tensor *x, sc_tensor *w, int32_t sh, int32_t sw, int32_t ph, int32_t pw);
sc_tensor  *sc_tensor_conv2d_backward_bias(sc_tensor *grad);
sc_tensor  *sc_tensor_max_pool2d_backward(sc_tensor *grad, sc_tensor *x, int32_t kh, int32_t kw, int32_t sh, int32_t sw, int32_t ph, int32_t pw);
sc_tensor  *sc_tensor_avg_pool2d_backward(sc_tensor *grad, sc_tensor *x, int32_t kh, int32_t kw, int32_t sh, int32_t sw, int32_t ph, int32_t pw);
sc_tensor  *sc_tensor_embedding_backward(sc_tensor *grad, sc_tensor *idx, int32_t V);      /* scatter-add → dW[V,D] */
sc_tensor  *sc_tensor_batch_norm_backward(sc_tensor *grad, sc_tensor *x, double eps);      /* 无仿射 */
sc_tensor  *sc_tensor_rms_norm_backward(sc_tensor *grad, sc_tensor *x, int32_t axis, double eps); /* 无仿射 */

/* ---- 比较/杂项 ---- */
bool     sc_tensor_equal(sc_tensor *_this, sc_tensor *o);                       /* 形状+逐元素全等 */
bool     sc_tensor_allclose(sc_tensor *_this, sc_tensor *o, double rtol, double atol); /* 近似相等 */
bool     sc_tensor_shuffle_(sc_tensor *_this);                   /* 沿首维随机洗牌（原地） */
bool     sc_tensor_save(sc_tensor *_this, const char *path);     /* 存为 NumPy .npy（C-order） */
void     sc_tensor_print(sc_tensor *_this);                      /* 形状 + 元素（大张量省略） */

/* ---- 构造补充 / 随机 / IO（自由函数） ---- */
sc_tensor  *sc_tri(int32_t n, int32_t m, int32_t k, int32_t dtype); /* [n,m] 下三角 1 */
bool     sc_meshgrid(void *arr, int32_t n, int32_t indexing, void *out); /* N 个 1D→N 个 N 维网格 */
void     sc_rand_seed(int64_t seed);
sc_tensor  *sc_rand_uniform(int32_t ndim, int32_t *shape, double lo, double hi, int32_t dtype);
sc_tensor  *sc_rand_normal(int32_t ndim, int32_t *shape, double mean, double std, int32_t dtype);
sc_tensor  *sc_rand_randint(int32_t ndim, int32_t *shape, int64_t lo, int64_t hi, int32_t dtype);
sc_tensor  *sc_permutation(int32_t n, int32_t dtype);
sc_tensor  *sc_ts_load(const char *path);                         /* 读取 .npy（兼容旧 .scts） */

#ifdef __cplusplus
}
#endif

#endif /* SC_TS_H */
