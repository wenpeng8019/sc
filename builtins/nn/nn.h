/* nn.h —— sc 内置 nn 模块的 C ABI 契约（与 builtins/nn/nn.sc 协议、nn_impl.c 实现同步）
 *
 * 定位：nn 是「张量数值（ts）」与「依赖图/宏调度（tok）」之上的神经网络组合层。
 *   - 依赖 ts：张量存储/算子 + 反向数值核（*_backward）。
 *   - 依赖 tok：token 依赖图，用作 step/分布式宏调度（高频 autograd 节点不进 tok 全局图）。
 *   两个下层 builtin 互不依赖；nn 在其上组合，不把训练逻辑下沉进 ts（ts 保持纯数值无图）。
 *
 * 自动微分引擎（define-by-run 录带）：
 *   - val：自动微分变量 = data 张量 + 惰性 grad + requires_grad + tape 节点。
 *   - 前向算子在算数值的同时录带；val_backward(loss) 逆序遍历 tape 累加梯度。
 *   - nn_tape_clear() 每个 step 末释放 tape 拥有的中间/输入 val 与节点；参数（叶子）存续。
 *
 * 生命周期/所属权：
 *   - 参数 val（nn_param / 模块内建）由用户/模块持有 → val_drop 释放。
 *   - 输入 val（nn_input）与算子输出 val 由 tape 拥有 → nn_tape_clear 统一释放；
 *     用户**不得**对其 val_drop。
 *   - tape 为进程内全局单带，单线程使用。 */
#ifndef SC_NN_H
#define SC_NN_H

#include "ts/ts.h"   /* tensor 类型 */

#ifdef __cplusplus
extern "C" {
#endif

/* 不透明句柄（sc 侧 @def 仅见 { p: & } 单指针；真实布局在 nn_impl.c）。 */
typedef struct val       val;
typedef struct linear    linear;
typedef struct conv      conv;
typedef struct embed     embed;
typedef struct optim     optim;

/* ---- 叶子构造 / tape 生命周期 ---- */
val  *nn_param(tensor *t);          /* 参数叶子（克隆 t，requires_grad；用户/模块持有，val_drop 释放） */
val  *nn_input(tensor *t);          /* 输入叶子（克隆 t，无梯度；每步临时，tape 拥有，勿 val_drop） */
void  nn_tape_clear(void);          /* 释放 tape 拥有的中间/输入 val 与节点（每 step 末调用一次） */

/* ---- val 算子（记录 tape 节点；输出由 tape 拥有） ---- */
val  *val_matmul(val *a, val *b);            /* a·b（2D） */
val  *val_add(val *a, val *b);               /* a+b（广播） */
val  *val_sub(val *a, val *b);               /* a-b（广播） */
val  *val_mul(val *a, val *b);               /* a*b 逐元素（广播） */
val  *val_relu(val *a);
val  *val_sigmoid(val *a);
val  *val_tanh(val *a);
val  *val_mse_loss(val *x, val *target);     /* 标量损失 mean((x-t)^2) */
val  *val_cross_entropy(val *x, val *target);/* 标量损失：logits[N,C] + 整型 target[N] */

/* ---- val 通用逐点 / 形变 ---- */
val  *val_div(val *a, val *b);               /* a/b（广播） */
val  *val_scale(val *a, double s);           /* a*常量 */
val  *val_exp(val *a);                        /* e^a */
val  *val_log(val *a);                        /* ln a */
val  *val_reshape(val *a, int32_t ndim, int32_t *shape); /* 重排形状（numel 不变） */
val  *val_permute(val *a, int32_t ndim, int32_t *axes);  /* 维重排 */
val  *val_transpose(val *a);                  /* 末两维转置 */

/* ---- val nn 算子 ---- */
val  *val_softmax(val *a, int32_t axis);
val  *val_log_softmax(val *a, int32_t axis);
val  *val_leaky_relu(val *a, double slope);
val  *val_elu(val *a, double alpha);
val  *val_silu(val *a);
val  *val_gelu(val *a);
val  *val_layer_norm(val *a, int32_t axis, double eps);
val  *val_rms_norm(val *a, int32_t axis, double eps);
val  *val_batch_norm(val *a, double eps);     /* 训练，通道=dim1 */
val  *val_bmm(val *a, val *b);                 /* 批量矩阵乘 [B,M,K]·[B,K,N] */
val  *val_dropout(val *a, double p, int32_t train);
val  *val_conv2d(val *x, val *w, val *b, int32_t sh, int32_t sw, int32_t ph, int32_t pw);
val  *val_max_pool2d(val *a, int32_t kh, int32_t kw, int32_t sh, int32_t sw, int32_t ph, int32_t pw);
val  *val_avg_pool2d(val *a, int32_t kh, int32_t kw, int32_t sh, int32_t sw, int32_t ph, int32_t pw);
val  *val_embedding(val *w, val *idx);         /* 接收者为权重 [V,D]，idx 为整型类标 */

/* ---- 反向 / 访问 / 释放 ---- */
void    val_backward(val *loss);    /* 以 loss 处 grad=1 播种，逆序遍历 tape 累加梯度 */
double  val_item(val *v);           /* data 唯一元素（标量损失值） */
tensor *val_value(val *v);          /* data 的拷贝（调用方 drop） */
tensor *val_grad(val *v);           /* grad 的拷贝（无梯度则零；调用方 drop） */
void    val_zero_grad(val *v);      /* 清空 grad */
void    val_drop(val *v);           /* 释放参数叶子 val（勿用于 tape 拥有的 val） */

/* ---- 模块：Linear（y = x·W + b；W[in,out], b[1,out]） ---- */
linear *nn_linear(int32_t in_dim, int32_t out_dim);   /* Kaiming(fan_in) 初始化 W，b=0 */
val    *linear_forward(linear *m, val *x);            /* x[N,in] → y[N,out] */
val    *linear_w(linear *m);                          /* 参数 W（供优化器跟踪） */
val    *linear_b(linear *m);                          /* 参数 b */
void    linear_drop(linear *m);                       /* 释放模块及其参数 */

/* ---- 模块：Conv2d（W[Cout,Cin,Kh,Kw], b[Cout]；构造定 stride/pad） ---- */
conv *nn_conv2d(int32_t cin, int32_t cout, int32_t kh, int32_t kw,
                int32_t sh, int32_t sw, int32_t ph, int32_t pw); /* Kaiming(fan_in) */
val  *conv_forward(conv *m, val *x);                 /* x[N,Cin,H,W] → y[N,Cout,Ho,Wo] */
val  *conv_w(conv *m);                               /* 参数 W */
val  *conv_b(conv *m);                               /* 参数 b */
void  conv_drop(conv *m);                            /* 释放模块及其参数 */

/* ---- 模块：Embedding（W[V,D]；idx 整型类标） ---- */
embed *nn_embedding(int32_t vocab, int32_t edim);    /* 小幅正态初始化 W */
val   *embed_forward(embed *m, val *idx);            /* idx → 嵌入向量 */
val   *embed_w(embed *m);                            /* 参数 W */
void   embed_drop(embed *m);                         /* 释放模块及其参数 */

/* ---- 优化器：SGD / Adam（跟踪显式参数数组，从 .grad 更新 .data） ---- */
optim *nn_sgd(double lr);                             /* 随机梯度下降 */
optim *nn_adam(double lr);                            /* Adam（beta1=.9,beta2=.999,eps=1e-8） */
void   optim_config(optim *o, double momentum, double weight_decay); /* SGD 动量/权重衰减 */
void   optim_track(optim *o, val *p);                 /* 注册一个参数 */
void   optim_track_linear(optim *o, linear *m);       /* 注册 Linear 的 W 与 b */
void   optim_track_conv2d(optim *o, conv *m);         /* 注册 Conv2d 的 W 与 b */
void   optim_track_embedding(optim *o, embed *m);     /* 注册 Embedding 的 W */
void   optim_zero_grad(optim *o);                     /* 清空所有跟踪参数的梯度 */
void   optim_step(optim *o);                          /* 用各参数 .grad 更新一步 */
void   optim_drop(optim *o);                          /* 释放优化器（不释放被跟踪参数） */

#ifdef __cplusplus
}
#endif

#endif /* SC_NN_H */
