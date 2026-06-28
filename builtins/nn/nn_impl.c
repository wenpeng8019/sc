/* nn_impl.c —— sc 内置 nn 模块的默认实现（自动微分引擎 + 模块 + 优化器）
 *
 * 编译器在单元图包含 builtins/nn/nn.sc 时自动编译并链接本文件。契约见同目录 nn.h、
 * 接口事实源见 nn.sc。本模块组合下层两件套：
 *   - ts（builtins/ts）：张量存储/算子 + 反向数值核（*_backward）；经 nn.sc `inc ts.sc`
 *     链接 ts_impl.c。nn 不在 ts 内自建 autograd（ts 保持纯数值无图）。
 *   - tok（builtins/tok）：token 依赖图，作 step/分布式宏调度；经 op→tok 隐式依赖恒链接。
 *
 * 架构（define-by-run 录带式 autograd）：
 *   - val（ag_val）：自动微分变量 = 数值张量 data + 惰性梯度 grad + requires_grad + 产生它
 *     的 tape 节点 node。叶子（参数/输入）node==NULL。
 *   - tape（g_tape）：进程内单线程录带。每个前向算子在计算数值的同时追加一个 ag_node
 *     （记录反向函数 + 输入 val + 必要的保存张量），并把输出 val 登记为 tape 拥有。
 *   - backward(loss)：以 loss 处 grad=1 播种，逆序遍历 tape 调用各节点反向函数，把梯度
 *     累加进各输入 val 的 grad。
 *   - nn_tape_clear()：每个训练 step 末释放 tape 拥有的「中间 val + 输入 val + 节点」；
 *     参数（叶子，由模块/用户持有）不在 tape 拥有集内，存续到显式 drop。
 *
 * 生命周期/所属权：
 *   - 参数 val（nn_param / 模块内建）由用户/模块持有，**不**登记进 tape，用 val_drop 释放。
 *   - 输入 val（nn_input）与算子输出 val 是「每步临时」，登记进 tape，由 nn_tape_clear 统一
 *     释放；用户**不得**对其调用 val_drop（会与 tape 重复释放）。
 *   - tape 是进程内全局单带，单线程使用。tok 仅在更上层做 step/分布式宏调度，不持有 tape 节点
 *     （高频瞬态节点不进 tok 全局只增不删的 intern 图，避免生命周期/性能问题）。 */
#include "nn.h"
#include "ts/ts.h"
#include "op.h"
#include <string.h>
#include <math.h>

/* ============================================================
 * 引擎数据结构
 * ============================================================ */

struct val {
    tensor          *data;          /* 前向数值（拥有） */
    tensor          *grad;          /* 累加梯度（惰性分配；形状同 data；拥有） */
    struct ag_node  *node;          /* 产生本 val 的 tape 节点（叶子为 NULL） */
    uint8_t          requires_grad;
    uint8_t          is_leaf;       /* 参数/输入 */
};

typedef void (*ag_bwd_fn)(struct ag_node *);

struct ag_node {
    ag_bwd_fn   bwd;
    struct val *out;                /* 输出 val（tape 拥有） */
    struct val *in[3];              /* 输入 val（最多 3） */
    int         n_in;
    tensor     *saved[2];           /* 保存张量（激活的输入/输出；拥有） */
    int         n_saved;
    int32_t     ia[8];              /* 算子整型参数（axis/stride/kernel/V...） */
    double      da[2];              /* 算子浮点参数（eps/slope/alpha/scale） */
};

typedef struct {
    struct ag_node **nodes; int n_nodes, cap_nodes;
    struct val     **owned; int n_owned, cap_owned;
} ag_tape;

static ag_tape g_tape;              /* 进程内单线程录带 */

/* ---- 小工具 ---- */

static void t_free(tensor **p) {
    if (p && *p) { tensor_drop(*p); sc_free(*p); *p = NULL; }
}

static void *grow_arr(void *arr, int *cap, int need, size_t esz) {
    if (*cap >= need) return arr;
    int nc = *cap ? *cap * 2 : 16;
    while (nc < need) nc *= 2;
    void *na = sc_alloc((size_t)nc * esz);
    if (!na) return arr;
    if (arr) { memcpy(na, arr, (size_t)(*cap) * esz); sc_free(arr); }
    *cap = nc;
    return na;
}

static void tape_push_node(struct ag_node *nd) {
    g_tape.nodes = (struct ag_node **)grow_arr(g_tape.nodes, &g_tape.cap_nodes,
                                               g_tape.n_nodes + 1, sizeof(struct ag_node *));
    g_tape.nodes[g_tape.n_nodes++] = nd;
}

static void tape_own_val(struct val *v) {
    g_tape.owned = (struct val **)grow_arr(g_tape.owned, &g_tape.cap_owned,
                                           g_tape.n_owned + 1, sizeof(struct val *));
    g_tape.owned[g_tape.n_owned++] = v;
}

/* 把梯度 g 累加进 v->grad（取得 g 所有权；不需要梯度则丢弃）。 */
static void ag_accum(struct val *v, tensor *g) {
    if (!g) return;
    if (!v || !v->requires_grad) { t_free(&g); return; }
    if (!v->grad) { v->grad = g; return; }
    tensor_add_(v->grad, g);
    t_free(&g);
}

static struct val *val_alloc(tensor *data_owned, int requires_grad, int is_leaf) {
    struct val *v = (struct val *)sc_alloc(sizeof(struct val));
    if (!v) { if (data_owned) { tensor_drop(data_owned); sc_free(data_owned); } return NULL; }
    v->data = data_owned; v->grad = NULL; v->node = NULL;
    v->requires_grad = (uint8_t)(requires_grad != 0);
    v->is_leaf = (uint8_t)(is_leaf != 0);
    return v;
}

static struct val *make_out(tensor *data_owned, int requires_grad) {
    struct val *v = val_alloc(data_owned, requires_grad, 0);
    if (v) tape_own_val(v);
    return v;
}

static struct ag_node *node_make(ag_bwd_fn bwd, struct val *out) {
    struct ag_node *n = (struct ag_node *)sc_alloc(sizeof(struct ag_node));
    if (!n) return NULL;
    memset(n, 0, sizeof(*n));
    n->bwd = bwd; n->out = out;
    if (out) out->node = n;
    tape_push_node(n);
    return n;
}

/* ============================================================
 * 反向函数（读 out->grad，累加进各输入 grad）
 * ============================================================ */

static tensor *sum_to_ref(tensor *g, tensor *ref) {
    return tensor_sum_to(g, ref->ndim, ref->shape);
}

static void bw_matmul(struct ag_node *s) {
    struct val *a = s->in[0], *b = s->in[1], *o = s->out;
    if (!o->grad) return;
    if (a->requires_grad) {                 /* gA = grad @ Bᵀ */
        tensor *bt = tensor_transpose(b->data);
        tensor *ga = bt ? tensor_matmul(o->grad, bt) : NULL;
        if (bt) { tensor_drop(bt); sc_free(bt); }
        ag_accum(a, ga);
    }
    if (b->requires_grad) {                 /* gB = Aᵀ @ grad */
        tensor *at = tensor_transpose(a->data);
        tensor *gb = at ? tensor_matmul(at, o->grad) : NULL;
        if (at) { tensor_drop(at); sc_free(at); }
        ag_accum(b, gb);
    }
}

static void bw_add(struct ag_node *s) {
    struct val *a = s->in[0], *b = s->in[1], *o = s->out;
    if (!o->grad) return;
    if (a->requires_grad) ag_accum(a, sum_to_ref(o->grad, a->data));
    if (b->requires_grad) ag_accum(b, sum_to_ref(o->grad, b->data));
}

static void bw_sub(struct ag_node *s) {
    struct val *a = s->in[0], *b = s->in[1], *o = s->out;
    if (!o->grad) return;
    if (a->requires_grad) ag_accum(a, sum_to_ref(o->grad, a->data));
    if (b->requires_grad) {
        tensor *neg = tensor_neg(o->grad);
        tensor *gb = neg ? sum_to_ref(neg, b->data) : NULL;
        t_free(&neg);
        ag_accum(b, gb);
    }
}

static void bw_mul(struct ag_node *s) {
    struct val *a = s->in[0], *b = s->in[1], *o = s->out;
    if (!o->grad) return;
    if (a->requires_grad) {
        tensor *ga = tensor_mul(o->grad, b->data);
        tensor *r = ga ? sum_to_ref(ga, a->data) : NULL;
        t_free(&ga);
        ag_accum(a, r);
    }
    if (b->requires_grad) {
        tensor *gb = tensor_mul(o->grad, a->data);
        tensor *r = gb ? sum_to_ref(gb, b->data) : NULL;
        t_free(&gb);
        ag_accum(b, r);
    }
}

static void bw_relu(struct ag_node *s) {
    struct val *a = s->in[0], *o = s->out;
    if (!o->grad) return;
    if (a->requires_grad) ag_accum(a, tensor_relu_backward(o->grad, s->saved[0]));
}

static void bw_sigmoid(struct ag_node *s) {
    struct val *a = s->in[0], *o = s->out;
    if (!o->grad) return;
    if (a->requires_grad) ag_accum(a, tensor_sigmoid_backward(o->grad, s->saved[0]));
}

static void bw_tanh(struct ag_node *s) {
    struct val *a = s->in[0], *o = s->out;
    if (!o->grad) return;
    if (a->requires_grad) ag_accum(a, tensor_tanh_backward(o->grad, s->saved[0]));
}

static void bw_mse(struct ag_node *s) {
    struct val *x = s->in[0], *tg = s->in[1], *o = s->out;
    if (!o->grad || !x->requires_grad) return;
    tensor *g = tensor_mse_backward(x->data, tg->data);
    double up = tensor_item(o->grad);
    if (g && up != 1.0) tensor_mul_scalar_(g, up);
    ag_accum(x, g);
}

static void bw_ce(struct ag_node *s) {
    struct val *x = s->in[0], *tg = s->in[1], *o = s->out;
    if (!o->grad || !x->requires_grad) return;
    tensor *g = tensor_cross_entropy_backward(x->data, tg->data);
    double up = tensor_item(o->grad);
    if (g && up != 1.0) tensor_mul_scalar_(g, up);
    ag_accum(x, g);
}

/* ---- 通用逐点 / 形变 ---- */
static void bw_identity(struct ag_node *s) {
    struct val *a = s->in[0], *o = s->out;
    if (!o->grad || !a->requires_grad) return;
    ag_accum(a, tensor_clone(o->grad));
}
static void bw_scale(struct ag_node *s) {
    struct val *a = s->in[0], *o = s->out;
    if (!o->grad || !a->requires_grad) return;
    ag_accum(a, tensor_mul_scalar(o->grad, s->da[0]));
}
static void bw_div(struct ag_node *s) {
    struct val *a = s->in[0], *b = s->in[1], *o = s->out;
    if (!o->grad) return;
    if (a->requires_grad) {
        tensor *ga = tensor_div(o->grad, b->data);
        tensor *r = ga ? sum_to_ref(ga, a->data) : NULL;
        t_free(&ga); ag_accum(a, r);
    }
    if (b->requires_grad) {
        tensor *b2 = tensor_mul(b->data, b->data);
        tensor *ab = tensor_mul(a->data, o->grad);
        tensor *q = (ab && b2) ? tensor_div(ab, b2) : NULL;
        tensor *neg = q ? tensor_neg(q) : NULL;
        tensor *r = neg ? sum_to_ref(neg, b->data) : NULL;
        t_free(&b2); t_free(&ab); t_free(&q); t_free(&neg); ag_accum(b, r);
    }
}
static void bw_exp(struct ag_node *s) {
    struct val *a = s->in[0], *o = s->out;
    if (!o->grad || !a->requires_grad) return;
    ag_accum(a, tensor_mul(o->grad, o->data));
}
static void bw_log(struct ag_node *s) {
    struct val *a = s->in[0], *o = s->out;
    if (!o->grad || !a->requires_grad) return;
    ag_accum(a, tensor_div(o->grad, a->data));
}
static void bw_reshape(struct ag_node *s) {
    struct val *a = s->in[0], *o = s->out;
    if (!o->grad || !a->requires_grad) return;
    tensor *g = tensor_reshape(o->grad, a->data->ndim, a->data->shape);
    tensor *r = g ? tensor_clone(g) : NULL;
    t_free(&g); ag_accum(a, r);
}
static void bw_permute(struct ag_node *s) {
    struct val *a = s->in[0], *o = s->out;
    if (!o->grad || !a->requires_grad) return;
    int nd = s->ia[0];
    int32_t inv[16];
    for (int i = 0; i < nd; i++) inv[s->ia[1 + i]] = (int32_t)i;
    tensor *gv = tensor_permute(o->grad, nd, inv);
    tensor *r = gv ? tensor_clone(gv) : NULL;
    t_free(&gv); ag_accum(a, r);
}
static void bw_t(struct ag_node *s) {
    struct val *a = s->in[0], *o = s->out;
    if (!o->grad || !a->requires_grad) return;
    tensor *g = tensor_t(o->grad);
    tensor *r = g ? tensor_clone(g) : NULL;
    t_free(&g); ag_accum(a, r);
}

/* ---- nn 算子反向（读 in[i]->data / o->data） ---- */
static void bw_softmax(struct ag_node *s) {
    struct val *a = s->in[0], *o = s->out;
    if (!o->grad || !a->requires_grad) return;
    ag_accum(a, tensor_softmax_backward(o->grad, o->data, s->ia[0]));
}
static void bw_log_softmax(struct ag_node *s) {
    struct val *a = s->in[0], *o = s->out;
    if (!o->grad || !a->requires_grad) return;
    ag_accum(a, tensor_log_softmax_backward(o->grad, o->data, s->ia[0]));
}
static void bw_leaky_relu(struct ag_node *s) {
    struct val *a = s->in[0], *o = s->out;
    if (!o->grad || !a->requires_grad) return;
    ag_accum(a, tensor_leaky_relu_backward(o->grad, a->data, s->da[0]));
}
static void bw_elu(struct ag_node *s) {
    struct val *a = s->in[0], *o = s->out;
    if (!o->grad || !a->requires_grad) return;
    ag_accum(a, tensor_elu_backward(o->grad, a->data, s->da[0]));
}
static void bw_silu(struct ag_node *s) {
    struct val *a = s->in[0], *o = s->out;
    if (!o->grad || !a->requires_grad) return;
    ag_accum(a, tensor_silu_backward(o->grad, a->data));
}
static void bw_gelu(struct ag_node *s) {
    struct val *a = s->in[0], *o = s->out;
    if (!o->grad || !a->requires_grad) return;
    ag_accum(a, tensor_gelu_backward(o->grad, a->data));
}
static void bw_layer_norm(struct ag_node *s) {
    struct val *a = s->in[0], *o = s->out;
    if (!o->grad || !a->requires_grad) return;
    ag_accum(a, tensor_layer_norm_backward(o->grad, a->data, s->ia[0], s->da[0]));
}
static void bw_rms_norm(struct ag_node *s) {
    struct val *a = s->in[0], *o = s->out;
    if (!o->grad || !a->requires_grad) return;
    ag_accum(a, tensor_rms_norm_backward(o->grad, a->data, s->ia[0], s->da[0]));
}
static void bw_batch_norm(struct ag_node *s) {
    struct val *a = s->in[0], *o = s->out;
    if (!o->grad || !a->requires_grad) return;
    ag_accum(a, tensor_batch_norm_backward(o->grad, a->data, s->da[0]));
}
static void bw_bmm(struct ag_node *s) {
    struct val *a = s->in[0], *b = s->in[1], *o = s->out;
    if (!o->grad) return;
    if (a->requires_grad) {
        tensor *bt = tensor_t(b->data);
        tensor *ga = bt ? tensor_bmm(o->grad, bt) : NULL;
        t_free(&bt); ag_accum(a, ga);
    }
    if (b->requires_grad) {
        tensor *at = tensor_t(a->data);
        tensor *gb = at ? tensor_bmm(at, o->grad) : NULL;
        t_free(&at); ag_accum(b, gb);
    }
}
static void bw_dropout(struct ag_node *s) {
    struct val *a = s->in[0], *o = s->out;
    if (!o->grad || !a->requires_grad) return;
    ag_accum(a, tensor_mul(o->grad, s->saved[0]));   /* saved[0] = mask */
}
static void bw_conv2d(struct ag_node *s) {
    struct val *x = s->in[0], *w = s->in[1], *b = s->in[2], *o = s->out;
    if (!o->grad) return;
    int32_t sh = s->ia[0], sw = s->ia[1], ph = s->ia[2], pw = s->ia[3];
    if (x->requires_grad)
        ag_accum(x, tensor_conv2d_backward_input(o->grad, x->data, w->data, sh, sw, ph, pw));
    if (w->requires_grad)
        ag_accum(w, tensor_conv2d_backward_weight(o->grad, x->data, w->data, sh, sw, ph, pw));
    if (b && b->requires_grad)
        ag_accum(b, tensor_conv2d_backward_bias(o->grad));
}
static void bw_max_pool2d(struct ag_node *s) {
    struct val *a = s->in[0], *o = s->out;
    if (!o->grad || !a->requires_grad) return;
    ag_accum(a, tensor_max_pool2d_backward(o->grad, a->data,
             s->ia[0], s->ia[1], s->ia[2], s->ia[3], s->ia[4], s->ia[5]));
}
static void bw_avg_pool2d(struct ag_node *s) {
    struct val *a = s->in[0], *o = s->out;
    if (!o->grad || !a->requires_grad) return;
    ag_accum(a, tensor_avg_pool2d_backward(o->grad, a->data,
             s->ia[0], s->ia[1], s->ia[2], s->ia[3], s->ia[4], s->ia[5]));
}
static void bw_embedding(struct ag_node *s) {
    struct val *w = s->in[0], *idx = s->in[1], *o = s->out;
    if (!o->grad || !w->requires_grad) return;
    ag_accum(w, tensor_embedding_backward(o->grad, idx->data, s->ia[0]));
}

/* ============================================================
 * 叶子构造 / tape 生命周期
 * ============================================================ */

struct val *nn_param(tensor *t) {
    if (!t) return NULL;
    return val_alloc(tensor_clone(t), 1, 1);   /* 参数：requires_grad，用户/模块持有 */
}

struct val *nn_input(tensor *t) {
    if (!t) return NULL;
    struct val *v = val_alloc(tensor_clone(t), 0, 1);  /* 输入：每步临时，tape 拥有 */
    if (v) tape_own_val(v);
    return v;
}

void nn_tape_clear(void) {
    for (int i = 0; i < g_tape.n_nodes; i++) {
        struct ag_node *nd = g_tape.nodes[i];
        if (!nd) continue;
        for (int k = 0; k < nd->n_saved; k++) t_free(&nd->saved[k]);
        sc_free(nd);
    }
    for (int i = 0; i < g_tape.n_owned; i++) {
        struct val *v = g_tape.owned[i];
        if (!v) continue;
        t_free(&v->data);
        t_free(&v->grad);
        sc_free(v);
    }
    g_tape.n_nodes = 0;
    g_tape.n_owned = 0;
}

/* ============================================================
 * val 算子（记录 tape 节点）
 * ============================================================ */

struct val *val_matmul(struct val *a, struct val *b) {
    if (!a || !b) return NULL;
    tensor *d = tensor_matmul(a->data, b->data);
    if (!d) return NULL;
    int rg = a->requires_grad || b->requires_grad;
    struct val *o = make_out(d, rg);
    if (o && rg) { struct ag_node *n = node_make(bw_matmul, o); if (n) { n->in[0] = a; n->in[1] = b; n->n_in = 2; } }
    return o;
}

struct val *val_add(struct val *a, struct val *b) {
    if (!a || !b) return NULL;
    tensor *d = tensor_add(a->data, b->data);
    if (!d) return NULL;
    int rg = a->requires_grad || b->requires_grad;
    struct val *o = make_out(d, rg);
    if (o && rg) { struct ag_node *n = node_make(bw_add, o); if (n) { n->in[0] = a; n->in[1] = b; n->n_in = 2; } }
    return o;
}

struct val *val_sub(struct val *a, struct val *b) {
    if (!a || !b) return NULL;
    tensor *d = tensor_sub(a->data, b->data);
    if (!d) return NULL;
    int rg = a->requires_grad || b->requires_grad;
    struct val *o = make_out(d, rg);
    if (o && rg) { struct ag_node *n = node_make(bw_sub, o); if (n) { n->in[0] = a; n->in[1] = b; n->n_in = 2; } }
    return o;
}

struct val *val_mul(struct val *a, struct val *b) {
    if (!a || !b) return NULL;
    tensor *d = tensor_mul(a->data, b->data);
    if (!d) return NULL;
    int rg = a->requires_grad || b->requires_grad;
    struct val *o = make_out(d, rg);
    if (o && rg) { struct ag_node *n = node_make(bw_mul, o); if (n) { n->in[0] = a; n->in[1] = b; n->n_in = 2; } }
    return o;
}

struct val *val_relu(struct val *a) {
    if (!a) return NULL;
    tensor *d = tensor_relu(a->data);
    if (!d) return NULL;
    struct val *o = make_out(d, a->requires_grad);
    if (o && a->requires_grad) {
        struct ag_node *n = node_make(bw_relu, o);
        if (n) { n->in[0] = a; n->n_in = 1; n->saved[0] = tensor_clone(a->data); n->n_saved = 1; }
    }
    return o;
}

struct val *val_sigmoid(struct val *a) {
    if (!a) return NULL;
    tensor *d = tensor_sigmoid(a->data);
    if (!d) return NULL;
    struct val *o = make_out(d, a->requires_grad);
    if (o && a->requires_grad) {
        struct ag_node *n = node_make(bw_sigmoid, o);
        if (n) { n->in[0] = a; n->n_in = 1; n->saved[0] = tensor_clone(d); n->n_saved = 1; }
    }
    return o;
}

struct val *val_tanh(struct val *a) {
    if (!a) return NULL;
    tensor *d = tensor_tanh(a->data);
    if (!d) return NULL;
    struct val *o = make_out(d, a->requires_grad);
    if (o && a->requires_grad) {
        struct ag_node *n = node_make(bw_tanh, o);
        if (n) { n->in[0] = a; n->n_in = 1; n->saved[0] = tensor_clone(d); n->n_saved = 1; }
    }
    return o;
}

struct val *val_mse_loss(struct val *x, struct val *target) {
    if (!x || !target) return NULL;
    double l = tensor_mse_loss(x->data, target->data);
    int32_t s1[1] = { 1 };
    tensor *d = full(1, s1, l, DT_F4);
    if (!d) return NULL;
    struct val *o = make_out(d, x->requires_grad);
    if (o && x->requires_grad) { struct ag_node *n = node_make(bw_mse, o); if (n) { n->in[0] = x; n->in[1] = target; n->n_in = 2; } }
    return o;
}

struct val *val_cross_entropy(struct val *x, struct val *target) {
    if (!x || !target) return NULL;
    double l = tensor_cross_entropy(x->data, target->data);
    int32_t s1[1] = { 1 };
    tensor *d = full(1, s1, l, DT_F4);
    if (!d) return NULL;
    struct val *o = make_out(d, x->requires_grad);
    if (o && x->requires_grad) { struct ag_node *n = node_make(bw_ce, o); if (n) { n->in[0] = x; n->in[1] = target; n->n_in = 2; } }
    return o;
}

/* ---- 通用逐点 / 形变 ---- */
struct val *val_scale(struct val *a, double s) {
    if (!a) return NULL;
    tensor *d = tensor_mul_scalar(a->data, s);
    if (!d) return NULL;
    struct val *o = make_out(d, a->requires_grad);
    if (o && a->requires_grad) { struct ag_node *n = node_make(bw_scale, o); if (n) { n->in[0] = a; n->n_in = 1; n->da[0] = s; } }
    return o;
}

struct val *val_div(struct val *a, struct val *b) {
    if (!a || !b) return NULL;
    tensor *d = tensor_div(a->data, b->data);
    if (!d) return NULL;
    int rg = a->requires_grad || b->requires_grad;
    struct val *o = make_out(d, rg);
    if (o && rg) { struct ag_node *n = node_make(bw_div, o); if (n) { n->in[0] = a; n->in[1] = b; n->n_in = 2; } }
    return o;
}

struct val *val_exp(struct val *a) {
    if (!a) return NULL;
    tensor *d = tensor_exp(a->data);
    if (!d) return NULL;
    struct val *o = make_out(d, a->requires_grad);
    if (o && a->requires_grad) { struct ag_node *n = node_make(bw_exp, o); if (n) { n->in[0] = a; n->n_in = 1; } }
    return o;
}

struct val *val_log(struct val *a) {
    if (!a) return NULL;
    tensor *d = tensor_log(a->data);
    if (!d) return NULL;
    struct val *o = make_out(d, a->requires_grad);
    if (o && a->requires_grad) { struct ag_node *n = node_make(bw_log, o); if (n) { n->in[0] = a; n->n_in = 1; } }
    return o;
}

struct val *val_reshape(struct val *a, int32_t ndim, int32_t *shape) {
    if (!a) return NULL;
    tensor *d = tensor_reshape(a->data, ndim, shape);
    if (!d) return NULL;
    tensor *dc = tensor_clone(d); t_free(&d);
    if (!dc) return NULL;
    struct val *o = make_out(dc, a->requires_grad);
    if (o && a->requires_grad) { struct ag_node *n = node_make(bw_reshape, o); if (n) { n->in[0] = a; n->n_in = 1; } }
    return o;
}

struct val *val_permute(struct val *a, int32_t ndim, int32_t *axes) {
    if (!a || ndim > 7) return NULL;
    tensor *d = tensor_permute(a->data, ndim, axes);
    if (!d) return NULL;
    tensor *dc = tensor_clone(d); t_free(&d);
    if (!dc) return NULL;
    struct val *o = make_out(dc, a->requires_grad);
    if (o && a->requires_grad) {
        struct ag_node *n = node_make(bw_permute, o);
        if (n) { n->in[0] = a; n->n_in = 1; n->ia[0] = ndim; for (int i = 0; i < ndim; i++) n->ia[1 + i] = axes[i]; }
    }
    return o;
}

struct val *val_transpose(struct val *a) {
    if (!a) return NULL;
    tensor *d = tensor_t(a->data);
    if (!d) return NULL;
    tensor *dc = tensor_clone(d); t_free(&d);
    if (!dc) return NULL;
    struct val *o = make_out(dc, a->requires_grad);
    if (o && a->requires_grad) { struct ag_node *n = node_make(bw_t, o); if (n) { n->in[0] = a; n->n_in = 1; } }
    return o;
}

/* ---- nn 算子 ---- */
struct val *val_softmax(struct val *a, int32_t axis) {
    if (!a) return NULL;
    tensor *d = tensor_softmax(a->data, axis);
    if (!d) return NULL;
    struct val *o = make_out(d, a->requires_grad);
    if (o && a->requires_grad) { struct ag_node *n = node_make(bw_softmax, o); if (n) { n->in[0] = a; n->n_in = 1; n->ia[0] = axis; } }
    return o;
}

struct val *val_log_softmax(struct val *a, int32_t axis) {
    if (!a) return NULL;
    tensor *d = tensor_log_softmax(a->data, axis);
    if (!d) return NULL;
    struct val *o = make_out(d, a->requires_grad);
    if (o && a->requires_grad) { struct ag_node *n = node_make(bw_log_softmax, o); if (n) { n->in[0] = a; n->n_in = 1; n->ia[0] = axis; } }
    return o;
}

struct val *val_leaky_relu(struct val *a, double slope) {
    if (!a) return NULL;
    tensor *d = tensor_leaky_relu(a->data, slope);
    if (!d) return NULL;
    struct val *o = make_out(d, a->requires_grad);
    if (o && a->requires_grad) { struct ag_node *n = node_make(bw_leaky_relu, o); if (n) { n->in[0] = a; n->n_in = 1; n->da[0] = slope; } }
    return o;
}

struct val *val_elu(struct val *a, double alpha) {
    if (!a) return NULL;
    tensor *d = tensor_elu(a->data, alpha);
    if (!d) return NULL;
    struct val *o = make_out(d, a->requires_grad);
    if (o && a->requires_grad) { struct ag_node *n = node_make(bw_elu, o); if (n) { n->in[0] = a; n->n_in = 1; n->da[0] = alpha; } }
    return o;
}

struct val *val_silu(struct val *a) {
    if (!a) return NULL;
    tensor *d = tensor_silu(a->data);
    if (!d) return NULL;
    struct val *o = make_out(d, a->requires_grad);
    if (o && a->requires_grad) { struct ag_node *n = node_make(bw_silu, o); if (n) { n->in[0] = a; n->n_in = 1; } }
    return o;
}

struct val *val_gelu(struct val *a) {
    if (!a) return NULL;
    tensor *d = tensor_gelu(a->data);
    if (!d) return NULL;
    struct val *o = make_out(d, a->requires_grad);
    if (o && a->requires_grad) { struct ag_node *n = node_make(bw_gelu, o); if (n) { n->in[0] = a; n->n_in = 1; } }
    return o;
}

struct val *val_layer_norm(struct val *a, int32_t axis, double eps) {
    if (!a) return NULL;
    tensor *d = tensor_layer_norm(a->data, axis, eps);
    if (!d) return NULL;
    struct val *o = make_out(d, a->requires_grad);
    if (o && a->requires_grad) { struct ag_node *n = node_make(bw_layer_norm, o); if (n) { n->in[0] = a; n->n_in = 1; n->ia[0] = axis; n->da[0] = eps; } }
    return o;
}

struct val *val_rms_norm(struct val *a, int32_t axis, double eps) {
    if (!a) return NULL;
    tensor *d = tensor_rms_norm(a->data, axis, eps);
    if (!d) return NULL;
    struct val *o = make_out(d, a->requires_grad);
    if (o && a->requires_grad) { struct ag_node *n = node_make(bw_rms_norm, o); if (n) { n->in[0] = a; n->n_in = 1; n->ia[0] = axis; n->da[0] = eps; } }
    return o;
}

struct val *val_batch_norm(struct val *a, double eps) {
    if (!a) return NULL;
    tensor *d = tensor_batch_norm(a->data, eps);
    if (!d) return NULL;
    struct val *o = make_out(d, a->requires_grad);
    if (o && a->requires_grad) { struct ag_node *n = node_make(bw_batch_norm, o); if (n) { n->in[0] = a; n->n_in = 1; n->da[0] = eps; } }
    return o;
}

struct val *val_bmm(struct val *a, struct val *b) {
    if (!a || !b) return NULL;
    tensor *d = tensor_bmm(a->data, b->data);
    if (!d) return NULL;
    int rg = a->requires_grad || b->requires_grad;
    struct val *o = make_out(d, rg);
    if (o && rg) { struct ag_node *n = node_make(bw_bmm, o); if (n) { n->in[0] = a; n->in[1] = b; n->n_in = 2; } }
    return o;
}

struct val *val_dropout(struct val *a, double p, int32_t train) {
    if (!a) return NULL;
    if (!train || p <= 0.0) {
        tensor *d = tensor_clone(a->data);
        if (!d) return NULL;
        struct val *o = make_out(d, a->requires_grad);
        if (o && a->requires_grad) { struct ag_node *n = node_make(bw_identity, o); if (n) { n->in[0] = a; n->n_in = 1; } }
        return o;
    }
    tensor *mask = tensor_dropout_mask(a->data, p);
    if (!mask) return NULL;
    tensor *d = tensor_mul(a->data, mask);
    if (!d) { t_free(&mask); return NULL; }
    struct val *o = make_out(d, a->requires_grad);
    if (o && a->requires_grad) {
        struct ag_node *n = node_make(bw_dropout, o);
        if (n) { n->in[0] = a; n->n_in = 1; n->saved[0] = mask; n->n_saved = 1; }
        else t_free(&mask);
    } else t_free(&mask);
    return o;
}

struct val *val_conv2d(struct val *x, struct val *w, struct val *b,
        int32_t sh, int32_t sw, int32_t ph, int32_t pw) {
    if (!x || !w || !b) return NULL;
    tensor *d = tensor_conv2d(x->data, w->data, b->data, sh, sw, ph, pw);
    if (!d) return NULL;
    int rg = x->requires_grad || w->requires_grad || b->requires_grad;
    struct val *o = make_out(d, rg);
    if (o && rg) {
        struct ag_node *n = node_make(bw_conv2d, o);
        if (n) { n->in[0] = x; n->in[1] = w; n->in[2] = b; n->n_in = 3;
                 n->ia[0] = sh; n->ia[1] = sw; n->ia[2] = ph; n->ia[3] = pw; }
    }
    return o;
}

struct val *val_max_pool2d(struct val *a, int32_t kh, int32_t kw,
        int32_t sh, int32_t sw, int32_t ph, int32_t pw) {
    if (!a) return NULL;
    tensor *d = tensor_max_pool2d(a->data, kh, kw, sh, sw, ph, pw);
    if (!d) return NULL;
    struct val *o = make_out(d, a->requires_grad);
    if (o && a->requires_grad) {
        struct ag_node *n = node_make(bw_max_pool2d, o);
        if (n) { n->in[0] = a; n->n_in = 1;
                 n->ia[0] = kh; n->ia[1] = kw; n->ia[2] = sh; n->ia[3] = sw; n->ia[4] = ph; n->ia[5] = pw; }
    }
    return o;
}

struct val *val_avg_pool2d(struct val *a, int32_t kh, int32_t kw,
        int32_t sh, int32_t sw, int32_t ph, int32_t pw) {
    if (!a) return NULL;
    tensor *d = tensor_avg_pool2d(a->data, kh, kw, sh, sw, ph, pw);
    if (!d) return NULL;
    struct val *o = make_out(d, a->requires_grad);
    if (o && a->requires_grad) {
        struct ag_node *n = node_make(bw_avg_pool2d, o);
        if (n) { n->in[0] = a; n->n_in = 1;
                 n->ia[0] = kh; n->ia[1] = kw; n->ia[2] = sh; n->ia[3] = sw; n->ia[4] = ph; n->ia[5] = pw; }
    }
    return o;
}

struct val *val_embedding(struct val *w, struct val *idx) {
    if (!w || !idx) return NULL;
    tensor *d = tensor_embedding(w->data, idx->data);
    if (!d) return NULL;
    struct val *o = make_out(d, w->requires_grad);
    if (o && w->requires_grad) {
        struct ag_node *n = node_make(bw_embedding, o);
        if (n) { n->in[0] = w; n->in[1] = idx; n->n_in = 2; n->ia[0] = w->data->shape[0]; }
    }
    return o;
}

/* ============================================================
 * 反向 / 访问 / 释放
 * ============================================================ */

void val_backward(struct val *loss) {
    if (!loss || !loss->data) return;
    t_free(&loss->grad);
    loss->grad = ones_like(loss->data);     /* 标量损失种子 grad=1 */
    for (int i = g_tape.n_nodes - 1; i >= 0; i--) {
        struct ag_node *nd = g_tape.nodes[i];
        if (nd && nd->bwd) nd->bwd(nd);
    }
}

double val_item(struct val *v) { return (v && v->data) ? tensor_item(v->data) : 0.0; }

tensor *val_value(struct val *v) { return (v && v->data) ? tensor_clone(v->data) : NULL; }

tensor *val_grad(struct val *v) {
    if (!v) return NULL;
    if (v->grad) return tensor_clone(v->grad);
    if (v->data) return zeros_like(v->data);
    return NULL;
}

void val_zero_grad(struct val *v) { if (v) t_free(&v->grad); }

/* 完全释放（清内部资源 + 释放盒子）：供 nn 内部直接持有的 val（如模块参数）使用。 */
static void val_release(struct val *v) {
    if (!v) return;
    t_free(&v->data);
    t_free(&v->grad);
    sc_free(v);
}

/* sc 析构器：仅清内部资源；val 盒子由编译器在 ->drop() 返回后释放。
   注意：仅对经 sc `->drop()` 析构的 val（如 nn_param 参数）调用；tape 拥有的 val 由
   nn_tape_clear 直接回收，勿对其调用本函数。 */
void val_drop(struct val *v) {
    if (!v) return;
    t_free(&v->data);
    t_free(&v->grad);
}

/* ============================================================
 * 模块：Linear（参数 W[in,out], b[1,out]；前向 y = x·W + b）
 *   注：W 以 [in,out] 存储（与 PyTorch 的 [out,in] 转置），免去前向转置；属内部约定。
 * ============================================================ */

struct linear {
    struct val *w;          /* [in_dim, out_dim] */
    struct val *b;          /* [1, out_dim] */
    int32_t     in_dim, out_dim;
};

struct linear *nn_linear(int32_t in_dim, int32_t out_dim) {
    struct linear *m = (struct linear *)sc_alloc(sizeof(struct linear));
    if (!m) return NULL;
    m->in_dim = in_dim; m->out_dim = out_dim;
    double std = sqrt(2.0 / (double)(in_dim > 0 ? in_dim : 1));   /* Kaiming(fan_in) */
    int32_t ws[2] = { in_dim, out_dim };
    int32_t bs[2] = { 1, out_dim };
    tensor *wt = rand_normal(2, ws, 0.0, std, DT_F4);
    tensor *bt = zeros(2, bs, DT_F4);
    m->w = val_alloc(wt, 1, 1);
    m->b = val_alloc(bt, 1, 1);
    return m;
}

struct val *linear_forward(struct linear *m, struct val *x) {
    if (!m || !x) return NULL;
    struct val *xw = val_matmul(x, m->w);
    if (!xw) return NULL;
    return val_add(xw, m->b);
}

struct val *linear_w(struct linear *m) { return m ? m->w : NULL; }
struct val *linear_b(struct linear *m) { return m ? m->b : NULL; }

void linear_drop(struct linear *m) {
    if (!m) return;
    if (m->w) val_release(m->w);
    if (m->b) val_release(m->b);
    /* m 盒子由编译器在 ->drop() 返回后释放 */
}

/* ============================================================
 * 模块：Conv2d（参数 W[Cout,Cin,Kh,Kw], b[Cout]；前向 val_conv2d）
 *   构造时固定 stride/pad；前向只取输入 x[N,Cin,H,W] → y[N,Cout,Ho,Wo]。
 * ============================================================ */

struct conv {
    struct val *w;          /* [Cout,Cin,Kh,Kw] */
    struct val *b;          /* [Cout] */
    int32_t cin, cout, kh, kw, sh, sw, ph, pw;
};

struct conv *nn_conv2d(int32_t cin, int32_t cout, int32_t kh, int32_t kw,
                         int32_t sh, int32_t sw, int32_t ph, int32_t pw) {
    struct conv *m = (struct conv *)sc_alloc(sizeof(struct conv));
    if (!m) return NULL;
    m->cin = cin; m->cout = cout; m->kh = kh; m->kw = kw;
    m->sh = sh; m->sw = sw; m->ph = ph; m->pw = pw;
    int32_t fan_in = cin * kh * kw;
    double std = sqrt(2.0 / (double)(fan_in > 0 ? fan_in : 1));   /* Kaiming(fan_in) */
    int32_t ws[4] = { cout, cin, kh, kw };
    int32_t bs[1] = { cout };
    tensor *wt = rand_normal(4, ws, 0.0, std, DT_F4);
    tensor *bt = zeros(1, bs, DT_F4);
    m->w = val_alloc(wt, 1, 1);
    m->b = val_alloc(bt, 1, 1);
    return m;
}

struct val *conv_forward(struct conv *m, struct val *x) {
    if (!m || !x) return NULL;
    return val_conv2d(x, m->w, m->b, m->sh, m->sw, m->ph, m->pw);
}

struct val *conv_w(struct conv *m) { return m ? m->w : NULL; }
struct val *conv_b(struct conv *m) { return m ? m->b : NULL; }

void conv_drop(struct conv *m) {
    if (!m) return;
    if (m->w) val_release(m->w);
    if (m->b) val_release(m->b);
}

/* ============================================================
 * 模块：Embedding（参数 W[V,D]；前向 val_embedding，idx 整型类标）
 * ============================================================ */

struct embed {
    struct val *w;          /* [V, D] */
    int32_t vocab, dim;
};

struct embed *nn_embedding(int32_t vocab, int32_t edim) {
    struct embed *m = (struct embed *)sc_alloc(sizeof(struct embed));
    if (!m) return NULL;
    m->vocab = vocab; m->dim = edim;
    int32_t ws[2] = { vocab, edim };
    tensor *wt = rand_normal(2, ws, 0.0, 0.02, DT_F4);   /* 小幅正态（GPT 风格） */
    m->w = val_alloc(wt, 1, 1);
    return m;
}

struct val *embed_forward(struct embed *m, struct val *idx) {
    if (!m || !idx) return NULL;
    return val_embedding(m->w, idx);
}

struct val *embed_w(struct embed *m) { return m ? m->w : NULL; }

void embed_drop(struct embed *m) {
    if (!m) return;
    if (m->w) val_release(m->w);
}

/* ============================================================
 * 优化器：SGD（含 momentum/weight_decay）/ Adam
 * ============================================================ */

typedef struct { struct val *p; tensor *m; tensor *v; } opt_slot;

struct optim {
    int       kind;         /* 0=SGD, 1=Adam */
    double    lr, beta1, beta2, eps, momentum, wd;
    opt_slot *slots; int n, cap;
    int64_t   t;
};

struct optim *nn_sgd(double lr) {
    struct optim *o = (struct optim *)sc_alloc(sizeof(struct optim));
    if (!o) return NULL;
    memset(o, 0, sizeof(*o));
    o->kind = 0; o->lr = lr;
    return o;
}

struct optim *nn_adam(double lr) {
    struct optim *o = (struct optim *)sc_alloc(sizeof(struct optim));
    if (!o) return NULL;
    memset(o, 0, sizeof(*o));
    o->kind = 1; o->lr = lr; o->beta1 = 0.9; o->beta2 = 0.999; o->eps = 1e-8;
    return o;
}

void optim_config(struct optim *o, double momentum, double weight_decay) {
    if (o) { o->momentum = momentum; o->wd = weight_decay; }
}

void optim_track(struct optim *o, struct val *p) {
    if (!o || !p) return;
    o->slots = (opt_slot *)grow_arr(o->slots, &o->cap, o->n + 1, sizeof(opt_slot));
    o->slots[o->n].p = p; o->slots[o->n].m = NULL; o->slots[o->n].v = NULL;
    o->n++;
}

void optim_track_linear(struct optim *o, struct linear *m) {
    if (!o || !m) return;
    optim_track(o, m->w);
    optim_track(o, m->b);
}

void optim_track_conv2d(struct optim *o, struct conv *m) {
    if (!o || !m) return;
    optim_track(o, m->w);
    optim_track(o, m->b);
}

void optim_track_embedding(struct optim *o, struct embed *m) {
    if (!o || !m) return;
    optim_track(o, m->w);
}

void optim_zero_grad(struct optim *o) {
    if (!o) return;
    for (int i = 0; i < o->n; i++) val_zero_grad(o->slots[i].p);
}

void optim_step(struct optim *o) {
    if (!o) return;
    if (o->kind == 1) o->t++;
    for (int i = 0; i < o->n; i++) {
        struct val *p = o->slots[i].p;
        if (!p || !p->grad) continue;
        tensor *g = tensor_clone(p->grad);
        if (o->wd != 0.0) { tensor *wd = tensor_mul_scalar(p->data, o->wd); tensor_add_(g, wd); t_free(&wd); }
        if (o->kind == 0) {
            if (o->momentum != 0.0) {
                if (!o->slots[i].m) o->slots[i].m = zeros_like(p->data);
                tensor_mul_scalar_(o->slots[i].m, o->momentum);
                tensor_add_(o->slots[i].m, g);
                t_free(&g);
                g = tensor_clone(o->slots[i].m);
            }
            tensor_mul_scalar_(g, o->lr);
            tensor_sub_(p->data, g);
            t_free(&g);
        } else {
            if (!o->slots[i].m) o->slots[i].m = zeros_like(p->data);
            if (!o->slots[i].v) o->slots[i].v = zeros_like(p->data);
            tensor *mbuf = o->slots[i].m, *vbuf = o->slots[i].v;
            tensor_mul_scalar_(mbuf, o->beta1);
            tensor *g1 = tensor_mul_scalar(g, 1.0 - o->beta1);
            tensor_add_(mbuf, g1); t_free(&g1);
            tensor_mul_scalar_(vbuf, o->beta2);
            tensor *gg = tensor_mul(g, g);
            tensor_mul_scalar_(gg, 1.0 - o->beta2);
            tensor_add_(vbuf, gg); t_free(&gg);
            double bc1 = 1.0 - pow(o->beta1, (double)o->t);
            double bc2 = 1.0 - pow(o->beta2, (double)o->t);
            tensor *mhat = tensor_mul_scalar(mbuf, 1.0 / bc1);
            tensor *vhat = tensor_mul_scalar(vbuf, 1.0 / bc2);
            tensor *sq = tensor_sqrt(vhat);
            tensor_add_scalar_(sq, o->eps);
            tensor *upd = tensor_div(mhat, sq);
            tensor_mul_scalar_(upd, o->lr);
            tensor_sub_(p->data, upd);
            t_free(&mhat); t_free(&vhat); t_free(&sq); t_free(&upd); t_free(&g);
        }
    }
}

void optim_drop(struct optim *o) {
    if (!o) return;
    for (int i = 0; i < o->n; i++) { t_free(&o->slots[i].m); t_free(&o->slots[i].v); }
    if (o->slots) sc_free(o->slots);
    /* o 盒子由编译器在 ->drop() 返回后释放 */
}
