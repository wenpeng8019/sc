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
typedef struct sc_val       sc_val;
typedef struct sc_linear    sc_linear;
typedef struct sc_conv      sc_conv;
typedef struct sc_embed     sc_embed;
typedef struct sc_optim     sc_optim;

/* ---- 叶子构造 / tape 生命周期 ---- */
sc_val  *sc_nn_param(sc_tensor *t);          /* 参数叶子（克隆 t，requires_grad；用户/模块持有，val_drop 释放） */
sc_val  *sc_nn_input(sc_tensor *t);          /* 输入叶子（克隆 t，无梯度；每步临时，tape 拥有，勿 val_drop） */
void  sc_nn_tape_clear(void);          /* 释放 tape 拥有的中间/输入 val 与节点（每 step 末调用一次） */

/* ---- val 算子（记录 tape 节点；输出由 tape 拥有） ---- */
sc_val  *sc_val_matmul(sc_val *a, sc_val *b);            /* a·b（2D） */
sc_val  *sc_val_add(sc_val *a, sc_val *b);               /* a+b（广播） */
sc_val  *sc_val_sub(sc_val *a, sc_val *b);               /* a-b（广播） */
sc_val  *sc_val_mul(sc_val *a, sc_val *b);               /* a*b 逐元素（广播） */
sc_val  *sc_val_relu(sc_val *a);
sc_val  *sc_val_sigmoid(sc_val *a);
sc_val  *sc_val_tanh(sc_val *a);
sc_val  *sc_val_mse_loss(sc_val *x, sc_val *target);     /* 标量损失 mean((x-t)^2) */
sc_val  *sc_val_cross_entropy(sc_val *x, sc_val *target);/* 标量损失：logits[N,C] + 整型 target[N] */

/* ---- val 通用逐点 / 形变 ---- */
sc_val  *sc_val_div(sc_val *a, sc_val *b);               /* a/b（广播） */
sc_val  *sc_val_scale(sc_val *a, double s);           /* a*常量 */
sc_val  *sc_val_exp(sc_val *a);                        /* e^a */
sc_val  *sc_val_log(sc_val *a);                        /* ln a */
sc_val  *sc_val_reshape(sc_val *a, int32_t ndim, int32_t *shape); /* 重排形状（numel 不变） */
sc_val  *sc_val_permute(sc_val *a, int32_t ndim, int32_t *axes);  /* 维重排 */
sc_val  *sc_val_transpose(sc_val *a);                  /* 末两维转置 */

/* ---- val nn 算子 ---- */
sc_val  *sc_val_softmax(sc_val *a, int32_t axis);
sc_val  *sc_val_log_softmax(sc_val *a, int32_t axis);
sc_val  *sc_val_leaky_relu(sc_val *a, double slope);
sc_val  *sc_val_elu(sc_val *a, double alpha);
sc_val  *sc_val_silu(sc_val *a);
sc_val  *sc_val_gelu(sc_val *a);
sc_val  *sc_val_layer_norm(sc_val *a, int32_t axis, double eps);
sc_val  *sc_val_rms_norm(sc_val *a, int32_t axis, double eps);
sc_val  *sc_val_batch_norm(sc_val *a, double eps);     /* 训练，通道=dim1 */
sc_val  *sc_val_bmm(sc_val *a, sc_val *b);                 /* 批量矩阵乘 [B,M,K]·[B,K,N] */
sc_val  *sc_val_dropout(sc_val *a, double p, int32_t train);
sc_val  *sc_val_conv2d(sc_val *x, sc_val *w, sc_val *b, int32_t sh, int32_t sw, int32_t ph, int32_t pw);
sc_val  *sc_val_max_pool2d(sc_val *a, int32_t kh, int32_t kw, int32_t sh, int32_t sw, int32_t ph, int32_t pw);
sc_val  *sc_val_avg_pool2d(sc_val *a, int32_t kh, int32_t kw, int32_t sh, int32_t sw, int32_t ph, int32_t pw);
sc_val  *sc_val_embedding(sc_val *w, sc_val *idx);         /* 接收者为权重 [V,D]，idx 为整型类标 */

/* ---- 反向 / 访问 / 释放 ---- */
void    sc_val_backward(sc_val *loss);    /* 以 loss 处 grad=1 播种，逆序遍历 tape 累加梯度 */
double  sc_val_item(sc_val *v);           /* data 唯一元素（标量损失值） */
sc_tensor *sc_val_value(sc_val *v);          /* data 的拷贝（调用方 drop） */
sc_tensor *sc_val_grad(sc_val *v);           /* grad 的拷贝（无梯度则零；调用方 drop） */
void    sc_val_zero_grad(sc_val *v);      /* 清空 grad */
void    sc_val_drop(sc_val *v);           /* 释放参数叶子 val（勿用于 tape 拥有的 val） */

/* ---- 模块：Linear（y = x·W + b；W[in,out], b[1,out]） ---- */
sc_linear *sc_nn_linear(int32_t in_dim, int32_t out_dim);   /* Kaiming(fan_in) 初始化 W，b=0 */
sc_val    *sc_linear_forward(sc_linear *m, sc_val *x);            /* x[N,in] → y[N,out] */
sc_val    *sc_linear_w(sc_linear *m);                          /* 参数 W（供优化器跟踪） */
sc_val    *sc_linear_b(sc_linear *m);                          /* 参数 b */
void    sc_linear_drop(sc_linear *m);                       /* 释放模块及其参数 */

/* ---- 模块：Conv2d（W[Cout,Cin,Kh,Kw], b[Cout]；构造定 stride/pad） ---- */
sc_conv *sc_nn_conv2d(int32_t cin, int32_t cout, int32_t kh, int32_t kw,
                int32_t sh, int32_t sw, int32_t ph, int32_t pw); /* Kaiming(fan_in) */
sc_val  *sc_conv_forward(sc_conv *m, sc_val *x);                 /* x[N,Cin,H,W] → y[N,Cout,Ho,Wo] */
sc_val  *sc_conv_w(sc_conv *m);                               /* 参数 W */
sc_val  *sc_conv_b(sc_conv *m);                               /* 参数 b */
void  sc_conv_drop(sc_conv *m);                            /* 释放模块及其参数 */

/* ---- 模块：Embedding（W[V,D]；idx 整型类标） ---- */
sc_embed *sc_nn_embedding(int32_t vocab, int32_t edim);    /* 小幅正态初始化 W */
sc_val   *sc_embed_forward(sc_embed *m, sc_val *idx);            /* idx → 嵌入向量 */
sc_val   *sc_embed_w(sc_embed *m);                            /* 参数 W */
void   sc_embed_drop(sc_embed *m);                         /* 释放模块及其参数 */

/* ---- 优化器：SGD / Adam（跟踪显式参数数组，从 .grad 更新 .data） ---- */
sc_optim *sc_nn_sgd(double lr);                             /* 随机梯度下降 */
sc_optim *sc_nn_adam(double lr);                            /* Adam（beta1=.9,beta2=.999,eps=1e-8） */
void   sc_optim_config(sc_optim *o, double momentum, double weight_decay); /* SGD 动量/权重衰减 */
void   sc_optim_track(sc_optim *o, sc_val *p);                 /* 注册一个参数 */
void   sc_optim_track_linear(sc_optim *o, sc_linear *m);       /* 注册 Linear 的 W 与 b */
void   sc_optim_track_conv2d(sc_optim *o, sc_conv *m);         /* 注册 Conv2d 的 W 与 b */
void   sc_optim_track_embedding(sc_optim *o, sc_embed *m);     /* 注册 Embedding 的 W */
void   sc_optim_zero_grad(sc_optim *o);                     /* 清空所有跟踪参数的梯度 */
void   sc_optim_step(sc_optim *o);                          /* 用各参数 .grad 更新一步 */
void   sc_optim_drop(sc_optim *o);                          /* 释放优化器（不释放被跟踪参数） */

#ifdef __cplusplus
}
#endif

#endif /* SC_NN_H */
