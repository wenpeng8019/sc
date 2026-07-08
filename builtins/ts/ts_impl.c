/* ts_impl.c —— sc 内置张量（tensor）模块的默认实现（numpy 式存储-视图分离）
 *
 * 编译器在单元图包含 builtins/ts/ts.sc 时自动编译并链接本文件（拼接进同 TU）。
 * 契约见同目录 ts.h；设计与进度见 ROADMAP.md；定位见 ts.sc 头注。
 *
 * 架构：
 *   ts_store —— 引用计数共享缓冲（模块内私有 refcount，不接入 op 的 sc_ref）。
 *   tensor   —— 视图描述符（store + shape/strides/offset）。元素物理偏移（元素为单位）
 *               = offset + Σ coord[d]·strides[d]；物理地址 = store->data + 该偏移·dt_size。
 *   零拷贝视图：transpose/permute/squeeze/unsqueeze/reshape(连续)/slice/select/narrow/
 *               broadcast_to/flip/diagonal/ravel(连续) —— 共享 store、refcnt++。
 *   物化：clone/contiguous(非连续)/各逐元素·规约·matmul 结果 —— 新建连续 store。
 *
 * 内存：store->data 与 shape/strides 缓冲走 op 层 sc_alloc/sc_free（可经 -DSC_POOL 切池化）。
 *       tensor 结构体盒子由工厂 sc_alloc 分配——堆专属 tensor& 的 t->drop() 由编译器发射为
 *       「sc_tensor_drop(t) 后 sc_free(t)」，故本文件 tensor_drop 只解引用 store/释放 shape/strides。
 *
 * 数值：通用算子经 double 中转（tel_get/tel_set），DT_I8 超 2^53 损失精度（文档已注）。
 * 可选 BLAS：定义 SCC_WITH_BLAS 且 dtype==DT_F4 时 2D matmul 走 cblas_sgemm。
 * 可选 LAPACK：定义 SCC_WITH_LAPACK 时 det/inv/solve/cholesky/qr/eigh/svd 走 LAPACKE（数值健壮）；
 *              否则用自研轻量数值核（默认，无外部依赖）。
 */
#include "ts.h"
#include "op.h"        /* sc_alloc / sc_realloc / sc_free（op 层内存接口） */
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifdef SCC_WITH_BLAS
#include <cblas.h>
#endif

#ifdef SCC_WITH_LAPACK
#include <lapacke.h>   /* 可选：定义 SCC_WITH_LAPACK 时 det/inv/solve/cholesky/qr/eigh/svd 走 LAPACK（数值健壮） */
#endif

#define TS_MAXD 32     /* 最大维数（numpy 同量级） */

/* 随机数实现位于后文“随机/序列化”章节；这里前置声明供 dropout 使用。 */
static uint64_t rng_next(void);
static double rng_double(void);

/* ============================================================
 * 0. dtype 尺寸与元素读写（double 中转）
 * ============================================================ */

static size_t dt_size(int32_t dt) {
    switch (dt) {
        case TS_DT_F4:   return sizeof(float);
        case TS_DT_F8:   return sizeof(double);
        case TS_DT_I4:   return sizeof(int32_t);
        case TS_DT_I8:   return sizeof(int64_t);
        case TS_DT_BOOL: return sizeof(uint8_t);
        default:         return sizeof(float);
    }
}

/* 按 dtype 从缓冲基址 base 的物理偏移 off（元素为单位）读一个元素为 double。 */
static double el_get(const void *base, int32_t dt, int64_t off) {
    const char *b = (const char *)base;
    switch (dt) {
        case TS_DT_F4:   return (double)((const float   *)b)[off];
        case TS_DT_F8:   return         ((const double  *)b)[off];
        case TS_DT_I4:   return (double)((const int32_t *)b)[off];
        case TS_DT_I8:   return (double)((const int64_t *)b)[off];
        case TS_DT_BOOL: return (double)((const uint8_t *)b)[off];
        default:         return 0.0;
    }
}

static void el_set(void *base, int32_t dt, int64_t off, double v) {
    char *b = (char *)base;
    switch (dt) {
        case TS_DT_F4:   ((float   *)b)[off] = (float)v;             break;
        case TS_DT_F8:   ((double  *)b)[off] = v;                    break;
        case TS_DT_I4:   ((int32_t *)b)[off] = (int32_t)v;           break;
        case TS_DT_I8:   ((int64_t *)b)[off] = (int64_t)v;           break;
        case TS_DT_BOOL: ((uint8_t *)b)[off] = (uint8_t)(v != 0.0);  break;
        default: break;
    }
}

/* ============================================================
 * 1. 存储层 ts_store（引用计数）
 * ============================================================ */

static sc_ts_store *store_new(int64_t nelem, int32_t dt) {
    sc_ts_store *s = (sc_ts_store *)sc_alloc(sizeof(sc_ts_store));
    if (!s) return NULL;
    s->nbytes = nelem * (int64_t)dt_size(dt);
    s->refcnt = 1;
    s->_pad   = 0;
    s->data   = NULL;
    if (nelem > 0) {
        s->data = sc_alloc((size_t)s->nbytes);
        if (!s->data) { sc_free(s); return NULL; }
    }
    return s;
}

static void store_incref(sc_ts_store *s) { if (s) s->refcnt++; }

static void store_decref(sc_ts_store *s) {
    if (!s) return;
    if (--s->refcnt <= 0) {
        sc_free(s->data);
        sc_free(s);
    }
}

/* ============================================================
 * 2. 张量描述符内部构建
 * ============================================================ */

/* 连续行主序步长：strides[d] = ∏_{k>d} shape[k]。 */
static void contig_strides(int ndim, const int32_t *shape, int32_t *strides) {
    int64_t s = 1;
    for (int i = ndim - 1; i >= 0; i--) { strides[i] = (int32_t)s; s *= shape[i]; }
}

static int64_t prod_shape(int ndim, const int32_t *shape) {
    int64_t n = 1;
    for (int i = 0; i < ndim; i++) n *= (shape[i] < 0 ? 0 : shape[i]);
    return n;
}

/* 为 tensor 分配 shape/strides 数组（ndim>0）。失败返回 0。 */
static int alloc_meta(sc_tensor *t, int ndim) {
    t->shape   = (int32_t *)sc_alloc((size_t)ndim * sizeof(int32_t));
    t->strides = (int32_t *)sc_alloc((size_t)ndim * sizeof(int32_t));
    if (!t->shape || !t->strides) {
        sc_free(t->shape); sc_free(t->strides);
        t->shape = NULL; t->strides = NULL; return 0;
    }
    return 1;
}

/* 把已分配的盒子构建为「全 0 描述符」（无存储）。 */
static void desc_empty(sc_tensor *t, int32_t dtype) {
    t->store = NULL; t->shape = NULL; t->strides = NULL;
    t->offset = 0; t->ndim = 0; t->dtype = dtype; t->numel = 0;
}

/* 在盒子 t 上构建拥有「新连续存储」的张量（零初始化与否由 zero 决定）。失败 → 空描述符，返回 0。 */
static int build_contig(sc_tensor *t, int ndim, const int32_t *shape, int32_t dtype, int zero) {
    desc_empty(t, dtype);
    if (ndim <= 0 || !shape) return 1;     /* 空/标量张量：合法 */
    if (ndim > TS_MAXD) ndim = TS_MAXD;
    if (!alloc_meta(t, ndim)) return 0;
    int64_t n = 1;
    for (int i = 0; i < ndim; i++) {
        int32_t d = shape[i] < 0 ? 0 : shape[i];
        t->shape[i] = d; n *= d;
    }
    contig_strides(ndim, t->shape, t->strides);
    t->ndim = ndim; t->numel = n;
    t->store = store_new(n, dtype);
    if (!t->store) {
        sc_free(t->shape); sc_free(t->strides);
        desc_empty(t, dtype); return 0;
    }
    if (zero && n > 0) memset(t->store->data, 0, (size_t)n * dt_size(dtype));
    return 1;
}

/* 分配新盒子 + 连续存储（工厂内部用；调用方 drop()）。zero 控制是否清零。 */
static sc_tensor *new_contig(int ndim, const int32_t *shape, int32_t dtype, int zero) {
    sc_tensor *t = (sc_tensor *)sc_alloc(sizeof(sc_tensor));
    if (!t) return NULL;
    if (!build_contig(t, ndim, shape, dtype, zero)) { sc_free(t); return NULL; }
    return t;
}

/* 分配新盒子 + 「视图」：共享 src 的 store（refcnt++），自带 shape/strides/offset。失败 NULL。 */
static sc_tensor *new_view(sc_tensor *src, int ndim, const int32_t *shape,
                        const int32_t *strides, int64_t offset) {
    sc_tensor *t = (sc_tensor *)sc_alloc(sizeof(sc_tensor));
    if (!t) return NULL;
    desc_empty(t, src->dtype);
    if (ndim > 0) {
        if (!alloc_meta(t, ndim)) { sc_free(t); return NULL; }
        for (int i = 0; i < ndim; i++) { t->shape[i] = shape[i]; t->strides[i] = strides[i]; }
    }
    t->ndim = ndim;
    t->numel = prod_shape(ndim, shape);
    t->offset = offset;
    t->store = src->store;
    store_incref(t->store);
    return t;
}

/* ============================================================
 * 3. stride-aware 迭代：逻辑扁平序 → 物理偏移
 * ============================================================ */

/* 第 logical（0..numel-1，按 shape 行主序）个元素的物理偏移（元素为单位）。 */
static int64_t phys_off(const sc_tensor *t, int64_t logical) {
    int64_t off = t->offset;
    for (int d = t->ndim - 1; d >= 0; d--) {
        int32_t dim = t->shape[d];
        if (dim <= 0) return t->offset;
        int64_t c = logical % dim;
        logical /= dim;
        off += c * t->strides[d];
    }
    return off;
}

/* 逻辑扁平读/写（经物理偏移）。 */
static double tel_get(const sc_tensor *t, int64_t logical) {
    if (!t->store) return 0.0;
    return el_get(t->store->data, t->dtype, phys_off(t, logical));
}
static void tel_set(sc_tensor *t, int64_t logical, double v) {
    if (!t->store) return;
    el_set(t->store->data, t->dtype, phys_off(t, logical), v);
}

bool sc_tensor_is_contiguous(sc_tensor *_this) {
    if (_this->offset != 0) return 0;
    int64_t s = 1;
    for (int d = _this->ndim - 1; d >= 0; d--) {
        if (_this->strides[d] != (int32_t)s) return 0;
        s *= _this->shape[d];
    }
    return 1;
}

/* 物化为新连续张量（深拷贝当前视图的逻辑元素）。 */
static sc_tensor *materialize(sc_tensor *t) {
    sc_tensor *r = new_contig(t->ndim, t->shape, t->dtype, 0);
    if (!r) return NULL;
    if (sc_tensor_is_contiguous(t) && t->store) {
        memcpy(r->store->data, (char *)t->store->data + t->offset * dt_size(t->dtype),
               (size_t)t->numel * dt_size(t->dtype));
    } else {
        for (int64_t i = 0; i < t->numel; i++) tel_set(r, i, tel_get(t, i));
    }
    return r;
}

/* ============================================================
 * 4. 构造 / 析构
 * ============================================================ */

void sc_tensor_init(sc_tensor *_this, int32_t ndim, int32_t *shape, int32_t dtype) {
    if (!build_contig(_this, ndim, shape, dtype, 1)) desc_empty(_this, dtype);
}

void sc_tensor_drop(sc_tensor *_this) {
    if (!_this) return;
    store_decref(_this->store);
    sc_free(_this->shape);
    sc_free(_this->strides);
    _this->store = NULL; _this->shape = NULL; _this->strides = NULL;
    _this->ndim = 0; _this->numel = 0; _this->offset = 0;
}

/* ============================================================
 * 5. 工厂
 * ============================================================ */

sc_tensor *sc_zeros(int32_t ndim, int32_t *shape, int32_t dtype) { return new_contig(ndim, shape, dtype, 1); }
sc_tensor *sc_empty(int32_t ndim, int32_t *shape, int32_t dtype) { return new_contig(ndim, shape, dtype, 0); }

sc_tensor *sc_ones(int32_t ndim, int32_t *shape, int32_t dtype) {
    sc_tensor *t = new_contig(ndim, shape, dtype, 0);
    if (t) for (int64_t i = 0; i < t->numel; i++) tel_set(t, i, 1.0);
    return t;
}

sc_tensor *sc_full(int32_t ndim, int32_t *shape, double value, int32_t dtype) {
    sc_tensor *t = new_contig(ndim, shape, dtype, 0);
    if (t) for (int64_t i = 0; i < t->numel; i++) tel_set(t, i, value);
    return t;
}

sc_tensor *sc_arange(double start, double stop, double step, int32_t dtype) {
    if (step == 0.0) step = 1.0;
    int64_t n = 0;
    if (step > 0.0 && stop > start) n = (int64_t)ceil((stop - start) / step);
    else if (step < 0.0 && stop < start) n = (int64_t)ceil((stop - start) / step);
    if (n < 0) n = 0;
    int32_t shp = (int32_t)n;
    sc_tensor *t = new_contig(1, &shp, dtype, 0);
    if (t) for (int64_t i = 0; i < n; i++) tel_set(t, i, start + (double)i * step);
    return t;
}

sc_tensor *sc_linspace(double start, double stop, int32_t num, int32_t dtype) {
    if (num < 0) num = 0;
    int32_t shp = num;
    sc_tensor *t = new_contig(1, &shp, dtype, 0);
    if (!t) return NULL;
    if (num == 1) { tel_set(t, 0, start); return t; }
    for (int32_t i = 0; i < num; i++)
        tel_set(t, i, start + (stop - start) * (double)i / (double)(num - 1));
    return t;
}

sc_tensor *sc_logspace(double start, double stop, int32_t num, double base, int32_t dtype) {
    if (num < 0) num = 0;
    int32_t shp = num;
    sc_tensor *t = new_contig(1, &shp, dtype, 0);
    if (!t) return NULL;
    for (int32_t i = 0; i < num; i++) {
        double e = (num == 1) ? start
                              : start + (stop - start) * (double)i / (double)(num - 1);
        tel_set(t, i, pow(base, e));
    }
    return t;
}

sc_tensor *sc_eye(int32_t n, int32_t dtype) {
    if (n < 0) n = 0;
    int32_t shp[2] = { n, n };
    sc_tensor *t = new_contig(2, shp, dtype, 1);
    if (t) for (int32_t i = 0; i < n; i++) tel_set(t, (int64_t)i * n + i, 1.0);
    return t;
}

sc_tensor *sc_from_data(int32_t ndim, int32_t *shape, void *data, int32_t dtype) {
    sc_tensor *t = new_contig(ndim, shape, dtype, 0);
    if (t && data && t->numel > 0)
        memcpy(t->store->data, data, (size_t)t->numel * dt_size(dtype));
    return t;
}

sc_tensor *sc_zeros_like(sc_tensor *o) { return new_contig(o->ndim, o->shape, o->dtype, 1); }
sc_tensor *sc_empty_like(sc_tensor *o) { return new_contig(o->ndim, o->shape, o->dtype, 0); }

sc_tensor *sc_ones_like(sc_tensor *o) {
    sc_tensor *t = new_contig(o->ndim, o->shape, o->dtype, 0);
    if (t) for (int64_t i = 0; i < t->numel; i++) tel_set(t, i, 1.0);
    return t;
}

sc_tensor *sc_full_like(sc_tensor *o, double value) {
    sc_tensor *t = new_contig(o->ndim, o->shape, o->dtype, 0);
    if (t) for (int64_t i = 0; i < t->numel; i++) tel_set(t, i, value);
    return t;
}

/* 1D → 对角阵（n×n，主对角线为输入，k 偏移）；2D → 取第 k 对角线（1D）。 */
sc_tensor *sc_diag(sc_tensor *o, int32_t k) {
    if (o->ndim == 1) {
        int32_t n = o->shape[0] + (k < 0 ? -k : k);
        int32_t shp[2] = { n, n };
        sc_tensor *t = new_contig(2, shp, o->dtype, 1);
        if (!t) return NULL;
        for (int32_t i = 0; i < o->shape[0]; i++) {
            int32_t r = k >= 0 ? i : i - k;
            int32_t c = k >= 0 ? i + k : i;
            tel_set(t, (int64_t)r * n + c, tel_get(o, i));
        }
        return t;
    }
    if (o->ndim == 2) {
        int32_t rows = o->shape[0], cols = o->shape[1];
        int32_t len;
        if (k >= 0) len = (cols - k < rows) ? (cols - k) : rows;
        else        len = (rows + k < cols) ? (rows + k) : cols;
        if (len < 0) len = 0;
        int32_t shp = len;
        sc_tensor *t = new_contig(1, &shp, o->dtype, 0);
        if (!t) return NULL;
        for (int32_t i = 0; i < len; i++) {
            int32_t r = k >= 0 ? i : i - k;
            int32_t c = k >= 0 ? i + k : i;
            tel_set(t, i, tel_get(o, (int64_t)r * cols + c));
        }
        return t;
    }
    return NULL;
}

/* ============================================================
 * 6. 元信息
 * ============================================================ */

int32_t sc_tensor_ndim(sc_tensor *_this)  { return _this->ndim; }
int64_t sc_tensor_numel(sc_tensor *_this) { return _this->numel; }
int32_t sc_tensor_dtype(sc_tensor *_this) { return _this->dtype; }

int32_t sc_tensor_dim(sc_tensor *_this, int32_t axis) {
    if (axis < 0) axis += _this->ndim;
    if (axis < 0 || axis >= _this->ndim) return 0;
    return _this->shape[axis];
}

bool sc_tensor_is_same_shape(sc_tensor *_this, sc_tensor *o) {
    if (!o || _this->ndim != o->ndim) return 0;
    for (int i = 0; i < _this->ndim; i++)
        if (_this->shape[i] != o->shape[i]) return 0;
    return 1;
}

/* ============================================================
 * 7. 元素访问
 * ============================================================ */

double sc_tensor_item(sc_tensor *_this) {
    return _this->numel > 0 ? tel_get(_this, 0) : 0.0;
}

double sc_tensor_at(sc_tensor *_this, int64_t idx) {
    return (idx >= 0 && idx < _this->numel) ? tel_get(_this, idx) : 0.0;
}

bool sc_tensor_set_at(sc_tensor *_this, int64_t idx, double v) {
    if (idx < 0 || idx >= _this->numel) return 0;
    tel_set(_this, idx, v);
    return 1;
}

/* 多维坐标 → 物理偏移。 */
static int64_t nd_off(const sc_tensor *t, const int32_t *idx) {
    int64_t off = t->offset;
    for (int d = 0; d < t->ndim; d++) {
        int32_t c = idx[d];
        if (c < 0) c += t->shape[d];
        off += (int64_t)c * t->strides[d];
    }
    return off;
}

double sc_tensor_at_nd(sc_tensor *_this, int32_t *idx) {
    if (!_this->store) return 0.0;
    return el_get(_this->store->data, _this->dtype, nd_off(_this, idx));
}

bool sc_tensor_set_nd(sc_tensor *_this, int32_t *idx, double v) {
    if (!_this->store) return 0;
    el_set(_this->store->data, _this->dtype, nd_off(_this, idx), v);
    return 1;
}

void *sc_tensor_data(sc_tensor *_this) {
    if (!_this->store) return NULL;
    return (char *)_this->store->data + _this->offset * dt_size(_this->dtype);
}

void sc_tensor_fill(sc_tensor *_this, double v) {
    for (int64_t i = 0; i < _this->numel; i++) tel_set(_this, i, v);
}

sc_tensor *sc_tensor_clone(sc_tensor *_this) { return materialize(_this); }

sc_tensor *sc_tensor_contiguous(sc_tensor *_this) {
    if (sc_tensor_is_contiguous(_this))
        return new_view(_this, _this->ndim, _this->shape, _this->strides, _this->offset);
    return materialize(_this);
}

sc_tensor *sc_tensor_astype(sc_tensor *_this, int32_t dtype) {
    sc_tensor *r = new_contig(_this->ndim, _this->shape, dtype, 0);
    if (r) for (int64_t i = 0; i < _this->numel; i++) tel_set(r, i, tel_get(_this, i));
    return r;
}

bool sc_tensor_copy_from(sc_tensor *_this, sc_tensor *o) {
    if (!o || _this->numel != o->numel) return 0;
    for (int64_t i = 0; i < _this->numel; i++) tel_set(_this, i, tel_get(o, i));
    return 1;
}

/* ============================================================
 * 8. 形变（视图优先；非连续时自动物化）
 * ============================================================ */

sc_tensor *sc_tensor_reshape(sc_tensor *_this, int32_t ndim, int32_t *shape) {
    if (ndim <= 0 || !shape) return NULL;
    int32_t rshape[TS_MAXD];
    int64_t n = 1; int infer = -1;
    if (ndim > TS_MAXD) return NULL;
    for (int i = 0; i < ndim; i++) {
        if (shape[i] < 0) { if (infer >= 0) return NULL; infer = i; rshape[i] = 1; }
        else { rshape[i] = shape[i]; n *= shape[i]; }
    }
    if (infer >= 0) {                       /* -1 维：自动推断 */
        if (n == 0) return NULL;
        rshape[infer] = (int32_t)(_this->numel / n);
        n *= rshape[infer];
    }
    if (n != _this->numel) return NULL;
    if (sc_tensor_is_contiguous(_this)) {      /* 连续：零拷贝视图 */
        int32_t rstr[TS_MAXD];
        contig_strides(ndim, rshape, rstr);
        return new_view(_this, ndim, rshape, rstr, _this->offset);
    }
    sc_tensor *c = materialize(_this);         /* 非连续：物化后再 reshape（连续视图） */
    if (!c) return NULL;
    int32_t rstr[TS_MAXD];
    contig_strides(ndim, rshape, rstr);
    sc_tensor *r = new_view(c, ndim, rshape, rstr, 0);
    sc_tensor_drop(c); sc_free(c);
    return r;
}

/* 按 axes 排列重排维度（零拷贝视图）。axes[i] = 源维号。 */
static sc_tensor *permute_view(sc_tensor *src, int ndim, const int32_t *axes) {
    int32_t rshape[TS_MAXD], rstr[TS_MAXD];
    char seen[TS_MAXD]; for (int i = 0; i < ndim; i++) seen[i] = 0;
    for (int i = 0; i < ndim; i++) {
        int a = axes[i];
        if (a < 0 || a >= ndim || seen[a]) return NULL;
        seen[a] = 1;
        rshape[i] = src->shape[a];
        rstr[i]   = src->strides[a];
    }
    return new_view(src, ndim, rshape, rstr, src->offset);
}

sc_tensor *sc_tensor_transpose(sc_tensor *_this) {
    if (_this->ndim != 2) return NULL;
    int32_t axes[2] = { 1, 0 };
    return permute_view(_this, 2, axes);
}

sc_tensor *sc_tensor_t(sc_tensor *_this) {
    int n = _this->ndim;
    if (n < 2) return NULL;
    int32_t axes[TS_MAXD];
    for (int i = 0; i < n; i++) axes[i] = i;
    axes[n - 1] = n - 2; axes[n - 2] = n - 1;
    return permute_view(_this, n, axes);
}

sc_tensor *sc_tensor_permute(sc_tensor *_this, int32_t ndim, int32_t *axes) {
    if (ndim != _this->ndim || !axes) return NULL;
    return permute_view(_this, ndim, axes);
}

sc_tensor *sc_tensor_squeeze(sc_tensor *_this) {
    int32_t rshape[TS_MAXD], rstr[TS_MAXD]; int rn = 0;
    for (int i = 0; i < _this->ndim; i++)
        if (_this->shape[i] != 1) { rshape[rn] = _this->shape[i]; rstr[rn] = _this->strides[i]; rn++; }
    return new_view(_this, rn, rshape, rstr, _this->offset);
}

sc_tensor *sc_tensor_unsqueeze(sc_tensor *_this, int32_t axis) {
    int n = _this->ndim;
    if (axis < 0) axis += n + 1;
    if (axis < 0 || axis > n) return NULL;
    int32_t rshape[TS_MAXD], rstr[TS_MAXD]; int rn = 0;
    for (int i = 0; i < axis; i++) { rshape[rn] = _this->shape[i]; rstr[rn] = _this->strides[i]; rn++; }
    rshape[rn] = 1; rstr[rn] = 0; rn++;        /* 新维步长 0（长度 1 无所谓） */
    for (int i = axis; i < n; i++) { rshape[rn] = _this->shape[i]; rstr[rn] = _this->strides[i]; rn++; }
    return new_view(_this, rn, rshape, rstr, _this->offset);
}

sc_tensor *sc_tensor_ravel(sc_tensor *_this) {
    if (sc_tensor_is_contiguous(_this)) {
        int32_t shp = (int32_t)_this->numel, str = 1;
        return new_view(_this, 1, &shp, &str, _this->offset);
    }
    return sc_tensor_flatten(_this);
}

sc_tensor *sc_tensor_flatten(sc_tensor *_this) {
    sc_tensor *c = materialize(_this);
    if (!c) return NULL;
    int32_t shp = (int32_t)c->numel, str = 1;
    sc_tensor *r = new_view(c, 1, &shp, &str, 0);
    sc_tensor_drop(c); sc_free(c);
    return r;
}

sc_tensor *sc_tensor_broadcast_to(sc_tensor *_this, int32_t ndim, int32_t *shape) {
    if (ndim < _this->ndim || ndim > TS_MAXD || !shape) return NULL;
    int32_t rstr[TS_MAXD];
    int off = ndim - _this->ndim;
    for (int i = 0; i < ndim; i++) {
        int si = i - off;
        int32_t sd = si >= 0 ? _this->shape[si] : 1;
        if (sd == shape[i])      rstr[i] = si >= 0 ? _this->strides[si] : 0;
        else if (sd == 1)        rstr[i] = 0;       /* 广播维：步长 0 */
        else return NULL;
    }
    return new_view(_this, ndim, shape, rstr, _this->offset);
}

sc_tensor *sc_tensor_flip(sc_tensor *_this, int32_t axis) {
    if (axis < 0) axis += _this->ndim;
    if (axis < 0 || axis >= _this->ndim) return NULL;
    int32_t rstr[TS_MAXD];
    for (int i = 0; i < _this->ndim; i++) rstr[i] = _this->strides[i];
    rstr[axis] = -rstr[axis];                       /* 负步长 */
    int64_t off = _this->offset + (int64_t)(_this->shape[axis] - 1) * _this->strides[axis];
    return new_view(_this, _this->ndim, _this->shape, rstr, off);
}

/* ============================================================
 * 9. 索引（slice/select/narrow 视图；take/masked_select 物化）
 * ============================================================ */

sc_tensor *sc_tensor_slice(sc_tensor *_this, int32_t dim, int64_t start, int64_t stop, int64_t step) {
    if (dim < 0) dim += _this->ndim;
    if (dim < 0 || dim >= _this->ndim) return NULL;
    if (step == 0) step = 1;
    int64_t len = _this->shape[dim];
    if (start < 0) start += len; if (stop < 0) stop += len;
    if (start < 0) start = 0; if (start > len) start = len;
    if (stop  < 0) stop  = 0; if (stop  > len) stop  = len;
    int64_t n;
    if (step > 0) n = (stop > start) ? (stop - start + step - 1) / step : 0;
    else          n = (start > stop) ? (start - stop + (-step) - 1) / (-step) : 0;
    int32_t rshape[TS_MAXD], rstr[TS_MAXD];
    for (int i = 0; i < _this->ndim; i++) { rshape[i] = _this->shape[i]; rstr[i] = _this->strides[i]; }
    rshape[dim] = (int32_t)n;
    rstr[dim]   = (int32_t)(_this->strides[dim] * step);
    int64_t off = _this->offset + start * _this->strides[dim];
    return new_view(_this, _this->ndim, rshape, rstr, off);
}

sc_tensor *sc_tensor_select(sc_tensor *_this, int32_t dim, int64_t idx) {
    if (dim < 0) dim += _this->ndim;
    if (dim < 0 || dim >= _this->ndim) return NULL;
    if (idx < 0) idx += _this->shape[dim];
    if (idx < 0 || idx >= _this->shape[dim]) return NULL;
    int32_t rshape[TS_MAXD], rstr[TS_MAXD]; int rn = 0;
    for (int i = 0; i < _this->ndim; i++)
        if (i != dim) { rshape[rn] = _this->shape[i]; rstr[rn] = _this->strides[i]; rn++; }
    int64_t off = _this->offset + idx * _this->strides[dim];
    return new_view(_this, rn, rshape, rstr, off);
}

sc_tensor *sc_tensor_narrow(sc_tensor *_this, int32_t dim, int64_t start, int64_t len) {
    return sc_tensor_slice(_this, dim, start, start + len, 1);
}

sc_tensor *sc_tensor_take(sc_tensor *_this, sc_tensor *idx) {
    if (!idx) return NULL;
    int32_t shp = (int32_t)idx->numel;
    sc_tensor *r = new_contig(1, &shp, _this->dtype, 0);
    if (!r) return NULL;
    for (int64_t i = 0; i < idx->numel; i++) {
        int64_t k = (int64_t)tel_get(idx, i);
        if (k < 0) k += _this->numel;
        tel_set(r, i, (k >= 0 && k < _this->numel) ? tel_get(_this, k) : 0.0);
    }
    return r;
}

sc_tensor *sc_tensor_masked_select(sc_tensor *_this, sc_tensor *mask) {
    if (!mask || mask->numel != _this->numel) return NULL;
    int64_t cnt = 0;
    for (int64_t i = 0; i < mask->numel; i++) if (tel_get(mask, i) != 0.0) cnt++;
    int32_t shp = (int32_t)cnt;
    sc_tensor *r = new_contig(1, &shp, _this->dtype, 0);
    if (!r) return NULL;
    int64_t j = 0;
    for (int64_t i = 0; i < _this->numel; i++)
        if (tel_get(mask, i) != 0.0) tel_set(r, j++, tel_get(_this, i));
    return r;
}

/* 非零元素坐标 [k, ndim]（DT_I8；物化）。 */
sc_tensor *sc_tensor_nonzero(sc_tensor *_this) {
    int64_t cnt = 0;
    for (int64_t i = 0; i < _this->numel; i++) if (tel_get(_this, i) != 0.0) cnt++;
    int32_t shp[2]; shp[0] = (int32_t)cnt; shp[1] = _this->ndim;
    sc_tensor *r = new_contig(2, shp, TS_DT_I8, 0);
    if (!r) return NULL;
    int64_t row = 0;
    for (int64_t i = 0; i < _this->numel; i++) {
        if (tel_get(_this, i) == 0.0) continue;
        int64_t rem = i;
        for (int d = _this->ndim - 1; d >= 0; d--) {
            int32_t dim = _this->shape[d];
            int64_t c = dim > 0 ? rem % dim : 0;
            if (dim > 0) rem /= dim;
            tel_set(r, row * _this->ndim + d, (double)c);
        }
        row++;
    }
    return r;
}

/* 沿 axis 按 index 采集（torch.gather；结果形随 index；物化）。 */
sc_tensor *sc_tensor_gather(sc_tensor *_this, int32_t axis, sc_tensor *index) {
    if (!index) return NULL;
    if (axis < 0) axis += _this->ndim;
    if (axis < 0 || axis >= _this->ndim) return NULL;
    if (index->ndim != _this->ndim) return NULL;
    sc_tensor *r = new_contig(index->ndim, index->shape, _this->dtype, 0);
    if (!r) return NULL;
    int64_t coord[TS_MAXD]; for (int i = 0; i < index->ndim; i++) coord[i] = 0;
    for (int64_t i = 0; i < index->numel; i++) {
        int32_t scoord[TS_MAXD];
        for (int d = 0; d < index->ndim; d++) scoord[d] = (int32_t)coord[d];
        int64_t k = (int64_t)tel_get(index, i);
        if (k < 0) k += _this->shape[axis];
        if (k >= 0 && k < _this->shape[axis]) {
            scoord[axis] = (int32_t)k;
            tel_set(r, i, sc_tensor_at_nd(_this, scoord));
        } else tel_set(r, i, 0.0);
        for (int d = index->ndim - 1; d >= 0; d--) { if (++coord[d] < index->shape[d]) break; coord[d] = 0; }
    }
    return r;
}

/* 沿 axis 按 index 原地写入 src（torch.scatter_）。 */
bool sc_tensor_scatter_(sc_tensor *_this, int32_t axis, sc_tensor *index, sc_tensor *src) {
    if (!index || !src) return 0;
    if (axis < 0) axis += _this->ndim;
    if (axis < 0 || axis >= _this->ndim) return 0;
    if (index->ndim != _this->ndim || src->numel != index->numel) return 0;
    int64_t coord[TS_MAXD]; for (int i = 0; i < index->ndim; i++) coord[i] = 0;
    for (int64_t i = 0; i < index->numel; i++) {
        int32_t scoord[TS_MAXD];
        for (int d = 0; d < index->ndim; d++) scoord[d] = (int32_t)coord[d];
        int64_t k = (int64_t)tel_get(index, i);
        if (k < 0) k += _this->shape[axis];
        if (k >= 0 && k < _this->shape[axis]) {
            scoord[axis] = (int32_t)k;
            sc_tensor_set_nd(_this, scoord, tel_get(src, i));
        }
        for (int d = index->ndim - 1; d >= 0; d--) { if (++coord[d] < index->shape[d]) break; coord[d] = 0; }
    }
    return 1;
}

/* ============================================================
 * 10. 逐元素一元
 * ============================================================ */

typedef double (*ts_unop)(double);

static sc_tensor *sc_tensor_unary(sc_tensor *t, ts_unop f) {
    sc_tensor *r = new_contig(t->ndim, t->shape, t->dtype, 0);
    if (r) for (int64_t i = 0; i < t->numel; i++) tel_set(r, i, f(tel_get(t, i)));
    return r;
}

static double un_neg(double x)   { return -x; }
static double un_abs(double x)   { return fabs(x); }
static double un_sqrt(double x)  { return x > 0.0 ? sqrt(x) : 0.0; }
static double un_square(double x){ return x * x; }
static double un_recip(double x) { return x != 0.0 ? 1.0 / x : 0.0; }
static double un_exp(double x)   { return exp(x); }
static double un_log(double x)   { return x > 0.0 ? log(x) : 0.0; }
static double un_floor(double x) { return floor(x); }
static double un_ceil(double x)  { return ceil(x); }
static double un_round(double x) { return nearbyint(x); }
static double un_trunc(double x) { return trunc(x); }
static double un_sign(double x)  { return (x > 0.0) - (x < 0.0); }

sc_tensor *sc_tensor_neg(sc_tensor *_this)        { return sc_tensor_unary(_this, un_neg); }
sc_tensor *sc_tensor_abs(sc_tensor *_this)        { return sc_tensor_unary(_this, un_abs); }
sc_tensor *sc_tensor_sqrt(sc_tensor *_this)       { return sc_tensor_unary(_this, un_sqrt); }
sc_tensor *sc_tensor_square(sc_tensor *_this)     { return sc_tensor_unary(_this, un_square); }
sc_tensor *sc_tensor_reciprocal(sc_tensor *_this) { return sc_tensor_unary(_this, un_recip); }
sc_tensor *sc_tensor_exp(sc_tensor *_this)        { return sc_tensor_unary(_this, un_exp); }
sc_tensor *sc_tensor_log(sc_tensor *_this)        { return sc_tensor_unary(_this, un_log); }
sc_tensor *sc_tensor_floor(sc_tensor *_this)      { return sc_tensor_unary(_this, un_floor); }
sc_tensor *sc_tensor_ceil(sc_tensor *_this)       { return sc_tensor_unary(_this, un_ceil); }
sc_tensor *sc_tensor_round(sc_tensor *_this)      { return sc_tensor_unary(_this, un_round); }
sc_tensor *sc_tensor_trunc(sc_tensor *_this)      { return sc_tensor_unary(_this, un_trunc); }
sc_tensor *sc_tensor_sign(sc_tensor *_this)       { return sc_tensor_unary(_this, un_sign); }

sc_tensor *sc_tensor_pow_scalar(sc_tensor *_this, double p) {
    sc_tensor *r = new_contig(_this->ndim, _this->shape, _this->dtype, 0);
    if (r) for (int64_t i = 0; i < _this->numel; i++) tel_set(r, i, pow(tel_get(_this, i), p));
    return r;
}

sc_tensor *sc_tensor_clip(sc_tensor *_this, double lo, double hi) {
    sc_tensor *r = new_contig(_this->ndim, _this->shape, _this->dtype, 0);
    if (r) for (int64_t i = 0; i < _this->numel; i++) {
        double v = tel_get(_this, i);
        if (v < lo) v = lo; else if (v > hi) v = hi;
        tel_set(r, i, v);
    }
    return r;
}

static double un_sin(double x)  { return sin(x); }
static double un_cos(double x)  { return cos(x); }
static double un_tan(double x)  { return tan(x); }
static double un_asin(double x) { return (x >= -1.0 && x <= 1.0) ? asin(x) : 0.0; }
static double un_acos(double x) { return (x >= -1.0 && x <= 1.0) ? acos(x) : 0.0; }
static double un_atan(double x) { return atan(x); }
static double un_sinh(double x) { return sinh(x); }
static double un_cosh(double x) { return cosh(x); }

sc_tensor *sc_tensor_sin(sc_tensor *_this)  { return sc_tensor_unary(_this, un_sin); }
sc_tensor *sc_tensor_cos(sc_tensor *_this)  { return sc_tensor_unary(_this, un_cos); }
sc_tensor *sc_tensor_tan(sc_tensor *_this)  { return sc_tensor_unary(_this, un_tan); }
sc_tensor *sc_tensor_asin(sc_tensor *_this) { return sc_tensor_unary(_this, un_asin); }
sc_tensor *sc_tensor_acos(sc_tensor *_this) { return sc_tensor_unary(_this, un_acos); }
sc_tensor *sc_tensor_atan(sc_tensor *_this) { return sc_tensor_unary(_this, un_atan); }
sc_tensor *sc_tensor_sinh(sc_tensor *_this) { return sc_tensor_unary(_this, un_sinh); }
sc_tensor *sc_tensor_cosh(sc_tensor *_this) { return sc_tensor_unary(_this, un_cosh); }

/* ============================================================
 * 11. 广播二元核
 * ============================================================ */

/* 广播形状到 rshape，返回维数；不兼容返回 -1。 */
static int bc_shape(sc_tensor *a, sc_tensor *b, int32_t *rshape) {
    int an = a->ndim, bn = b->ndim;
    int rn = an > bn ? an : bn;
    if (rn > TS_MAXD) return -1;
    for (int i = 0; i < rn; i++) {
        int ai = an - rn + i, bi = bn - rn + i;
        int32_t ad = ai >= 0 ? a->shape[ai] : 1;
        int32_t bd = bi >= 0 ? b->shape[bi] : 1;
        if      (ad == bd) rshape[i] = ad;
        else if (ad == 1)  rshape[i] = bd;
        else if (bd == 1)  rshape[i] = ad;
        else return -1;
    }
    return rn;
}

/* 操作数 t 相对结果形状（rn 维）的逐维步长（被广播维步长置 0；含 t 自身 strides）。 */
static void bc_strides(sc_tensor *t, int rn, const int32_t *rshape, int64_t *str) {
    int tn = t->ndim;
    for (int i = 0; i < rn; i++) {
        int ti = tn - rn + i;
        if (ti < 0)                                    str[i] = 0;
        else if (t->shape[ti] == 1 && rshape[i] != 1)  str[i] = 0;
        else                                           str[i] = t->strides[ti];
    }
}

typedef double (*ts_binop)(double, double);
static double op_add(double x, double y) { return x + y; }
static double op_sub(double x, double y) { return x - y; }
static double op_mul(double x, double y) { return x * y; }
static double op_div(double x, double y) { return y != 0.0 ? x / y : 0.0; }
static double op_pow(double x, double y) { return pow(x, y); }
static double op_mod(double x, double y) { return y != 0.0 ? fmod(x, y) : 0.0; }
static double op_max(double x, double y) { return x > y ? x : y; }
static double op_min(double x, double y) { return x < y ? x : y; }
static double op_atan2(double x, double y) { return atan2(x, y); }
static double op_gt(double x, double y)  { return x >  y; }
static double op_ge(double x, double y)  { return x >= y; }
static double op_lt(double x, double y)  { return x <  y; }
static double op_le(double x, double y)  { return x <= y; }
static double op_eq(double x, double y)  { return x == y; }
static double op_ne(double x, double y)  { return x != y; }
static double op_and(double x, double y) { return (x != 0.0) && (y != 0.0); }
static double op_or(double x, double y)  { return (x != 0.0) || (y != 0.0); }

/* 广播二元：out_dt 指定结果 dtype（比较/逻辑传 DT_BOOL，算术传 a->dtype）。 */
static sc_tensor *sc_tensor_bin_dt(sc_tensor *a, sc_tensor *b, ts_binop f, int out_dt) {
    if (!b) return NULL;
    int32_t rshape[TS_MAXD];
    int rn = bc_shape(a, b, rshape);
    if (rn < 0) return NULL;
    sc_tensor *r = new_contig(rn, rshape, out_dt, 0);
    if (!r) return NULL;
    int64_t astr[TS_MAXD], bstr[TS_MAXD];
    bc_strides(a, rn, rshape, astr);
    bc_strides(b, rn, rshape, bstr);
    int64_t coord[TS_MAXD]; for (int i = 0; i < rn; i++) coord[i] = 0;
    const void *ad = a->store ? a->store->data : NULL;
    const void *bd = b->store ? b->store->data : NULL;
    for (int64_t i = 0; i < r->numel; i++) {
        int64_t ai = a->offset, bi = b->offset;
        for (int d = 0; d < rn; d++) { ai += coord[d] * astr[d]; bi += coord[d] * bstr[d]; }
        tel_set(r, i, f(el_get(ad, a->dtype, ai), el_get(bd, b->dtype, bi)));
        for (int d = rn - 1; d >= 0; d--) { if (++coord[d] < rshape[d]) break; coord[d] = 0; }
    }
    return r;
}

static sc_tensor *sc_tensor_bin(sc_tensor *a, sc_tensor *b, ts_binop f) { return sc_tensor_bin_dt(a, b, f, a->dtype); }

/* 原地：结果形状须等于 _this 形状（o 可广播到 _this）。 */
static uint8_t sc_tensor_bin_(sc_tensor *a, sc_tensor *b, ts_binop f) {
    if (!b) return 0;
    int32_t rshape[TS_MAXD];
    int rn = bc_shape(a, b, rshape);
    if (rn < 0 || rn != a->ndim) return 0;
    for (int i = 0; i < rn; i++) if (rshape[i] != a->shape[i]) return 0;
    int64_t bstr[TS_MAXD];
    bc_strides(b, rn, a->shape, bstr);
    int64_t coord[TS_MAXD]; for (int i = 0; i < rn; i++) coord[i] = 0;
    const void *bd = b->store ? b->store->data : NULL;
    for (int64_t i = 0; i < a->numel; i++) {
        int64_t bi = b->offset;
        for (int d = 0; d < rn; d++) bi += coord[d] * bstr[d];
        tel_set(a, i, f(tel_get(a, i), el_get(bd, b->dtype, bi)));
        for (int d = rn - 1; d >= 0; d--) { if (++coord[d] < a->shape[d]) break; coord[d] = 0; }
    }
    return 1;
}

sc_tensor *sc_tensor_add(sc_tensor *_this, sc_tensor *o) { return sc_tensor_bin(_this, o, op_add); }
sc_tensor *sc_tensor_sub(sc_tensor *_this, sc_tensor *o) { return sc_tensor_bin(_this, o, op_sub); }
sc_tensor *sc_tensor_mul(sc_tensor *_this, sc_tensor *o) { return sc_tensor_bin(_this, o, op_mul); }
sc_tensor *sc_tensor_div(sc_tensor *_this, sc_tensor *o) { return sc_tensor_bin(_this, o, op_div); }
sc_tensor *sc_tensor_pow(sc_tensor *_this, sc_tensor *o) { return sc_tensor_bin(_this, o, op_pow); }
sc_tensor *sc_tensor_mod(sc_tensor *_this, sc_tensor *o) { return sc_tensor_bin(_this, o, op_mod); }
sc_tensor *sc_tensor_maximum(sc_tensor *_this, sc_tensor *o) { return sc_tensor_bin(_this, o, op_max); }
sc_tensor *sc_tensor_minimum(sc_tensor *_this, sc_tensor *o) { return sc_tensor_bin(_this, o, op_min); }
sc_tensor *sc_tensor_atan2(sc_tensor *_this, sc_tensor *o) { return sc_tensor_bin(_this, o, op_atan2); }

sc_tensor *sc_tensor_gt(sc_tensor *_this, sc_tensor *o) { return sc_tensor_bin_dt(_this, o, op_gt, TS_DT_BOOL); }
sc_tensor *sc_tensor_ge(sc_tensor *_this, sc_tensor *o) { return sc_tensor_bin_dt(_this, o, op_ge, TS_DT_BOOL); }
sc_tensor *sc_tensor_lt(sc_tensor *_this, sc_tensor *o) { return sc_tensor_bin_dt(_this, o, op_lt, TS_DT_BOOL); }
sc_tensor *sc_tensor_le(sc_tensor *_this, sc_tensor *o) { return sc_tensor_bin_dt(_this, o, op_le, TS_DT_BOOL); }
sc_tensor *sc_tensor_eq(sc_tensor *_this, sc_tensor *o) { return sc_tensor_bin_dt(_this, o, op_eq, TS_DT_BOOL); }
sc_tensor *sc_tensor_ne(sc_tensor *_this, sc_tensor *o) { return sc_tensor_bin_dt(_this, o, op_ne, TS_DT_BOOL); }

sc_tensor *sc_tensor_logical_and(sc_tensor *_this, sc_tensor *o) { return sc_tensor_bin_dt(_this, o, op_and, TS_DT_BOOL); }
sc_tensor *sc_tensor_logical_or(sc_tensor *_this, sc_tensor *o)  { return sc_tensor_bin_dt(_this, o, op_or, TS_DT_BOOL); }

sc_tensor *sc_tensor_logical_not(sc_tensor *_this) {
    sc_tensor *r = new_contig(_this->ndim, _this->shape, TS_DT_BOOL, 0);
    if (r) for (int64_t i = 0; i < _this->numel; i++) tel_set(r, i, tel_get(_this, i) == 0.0);
    return r;
}

bool sc_tensor_add_(sc_tensor *_this, sc_tensor *o) { return sc_tensor_bin(_this, o, op_add); }
bool sc_tensor_sub_(sc_tensor *_this, sc_tensor *o) { return sc_tensor_bin(_this, o, op_sub); }
bool sc_tensor_mul_(sc_tensor *_this, sc_tensor *o) { return sc_tensor_bin(_this, o, op_mul); }
bool sc_tensor_div_(sc_tensor *_this, sc_tensor *o) { return sc_tensor_bin(_this, o, op_div); }

/* 标量二元（新张量 / 原地） */
static sc_tensor *sc_tensor_scalar(sc_tensor *t, double s, ts_binop f) {
    sc_tensor *r = new_contig(t->ndim, t->shape, t->dtype, 0);
    if (r) for (int64_t i = 0; i < t->numel; i++) tel_set(r, i, f(tel_get(t, i), s));
    return r;
}
static uint8_t sc_tensor_scalar_(sc_tensor *t, double s, ts_binop f) {
    for (int64_t i = 0; i < t->numel; i++) tel_set(t, i, f(tel_get(t, i), s));
    return 1;
}

sc_tensor *sc_tensor_add_scalar(sc_tensor *_this, double s) { return sc_tensor_scalar(_this, s, op_add); }
sc_tensor *sc_tensor_sub_scalar(sc_tensor *_this, double s) { return sc_tensor_scalar(_this, s, op_sub); }
sc_tensor *sc_tensor_mul_scalar(sc_tensor *_this, double s) { return sc_tensor_scalar(_this, s, op_mul); }
sc_tensor *sc_tensor_div_scalar(sc_tensor *_this, double s) { return sc_tensor_scalar(_this, s, op_div); }

bool sc_tensor_add_scalar_(sc_tensor *_this, double s) { return sc_tensor_scalar_(_this, s, op_add); }
bool sc_tensor_sub_scalar_(sc_tensor *_this, double s) { return sc_tensor_scalar_(_this, s, op_sub); }
bool sc_tensor_mul_scalar_(sc_tensor *_this, double s) { return sc_tensor_scalar_(_this, s, op_mul); }
bool sc_tensor_div_scalar_(sc_tensor *_this, double s) { return sc_tensor_scalar_(_this, s, op_div); }

/* where(cond, a, b)：三方广播（先 cond⊕a 再 ⊗b），物化。 */
sc_tensor *sc_tensor_where(sc_tensor *cond, sc_tensor *a, sc_tensor *b) {
    if (!cond || !a || !b) return NULL;
    int32_t s1[TS_MAXD]; int n1 = bc_shape(cond, a, s1);
    if (n1 < 0) return NULL;
    /* 用临时形状壳计算 cond/a 与 b 的联合广播 */
    sc_tensor tmp; desc_empty(&tmp, a->dtype);
    int32_t tmpshape[TS_MAXD]; for (int i = 0; i < n1; i++) tmpshape[i] = s1[i];
    tmp.shape = tmpshape; tmp.ndim = n1;
    int32_t rshape[TS_MAXD]; int rn = bc_shape(&tmp, b, rshape);
    if (rn < 0) return NULL;
    sc_tensor *r = new_contig(rn, rshape, a->dtype, 0);
    if (!r) return NULL;
    int64_t cstr[TS_MAXD], astr[TS_MAXD], bstr[TS_MAXD];
    bc_strides(cond, rn, rshape, cstr);
    bc_strides(a, rn, rshape, astr);
    bc_strides(b, rn, rshape, bstr);
    int64_t coord[TS_MAXD]; for (int i = 0; i < rn; i++) coord[i] = 0;
    const void *cd = cond->store ? cond->store->data : NULL;
    const void *ad = a->store ? a->store->data : NULL;
    const void *bd = b->store ? b->store->data : NULL;
    for (int64_t i = 0; i < r->numel; i++) {
        int64_t ci = cond->offset, ai = a->offset, bi = b->offset;
        for (int d = 0; d < rn; d++) { ci += coord[d]*cstr[d]; ai += coord[d]*astr[d]; bi += coord[d]*bstr[d]; }
        double cv = el_get(cd, cond->dtype, ci);
        tel_set(r, i, cv != 0.0 ? el_get(ad, a->dtype, ai) : el_get(bd, b->dtype, bi));
        for (int d = rn - 1; d >= 0; d--) { if (++coord[d] < rshape[d]) break; coord[d] = 0; }
    }
    return r;
}

/* ============================================================
 * 12. 规约
 * ============================================================ */

enum { RD_SUM, RD_MEAN, RD_PROD, RD_MAX, RD_MIN, RD_ARGMAX, RD_ARGMIN,
       RD_STD, RD_VAR, RD_ANY, RD_ALL };

static int reduce_out_dt(sc_tensor *t, int kind) {
    if (kind == RD_ARGMAX || kind == RD_ARGMIN) return TS_DT_I8;
    if (kind == RD_ANY || kind == RD_ALL) return TS_DT_BOOL;
    return t->dtype;
}

static sc_tensor *sc_tensor_reduce(sc_tensor *t, int32_t axis, int kind, bool keepdim) {
    int out_dt = reduce_out_dt(t, kind);
    if (axis < 0 && axis != -1) axis += t->ndim;  /* allow -1 = full; other negatives normalized below */
    /* 全规约：axis == -1（约定） */
    if (axis < 0) {
        double acc = 0.0;
        /* 全规约按逻辑序聚合（视图非连续时物理步长非 1），用逻辑 tel_get 逐元素 */
        if (t->numel > 0) {
            if (kind == RD_STD || kind == RD_VAR) {
                double s = 0; for (int64_t i = 0; i < t->numel; i++) s += tel_get(t, i);
                double mn = s / t->numel, v = 0;
                for (int64_t i = 0; i < t->numel; i++) { double d = tel_get(t, i) - mn; v += d * d; }
                v /= t->numel; acc = kind == RD_STD ? sqrt(v) : v;
            } else {
                acc = tel_get(t, 0); int64_t arg = 0;
                if (kind == RD_ANY || kind == RD_ALL) acc = (acc != 0.0);
                for (int64_t i = 1; i < t->numel; i++) {
                    double vv = tel_get(t, i);
                    switch (kind) {
                        case RD_SUM: case RD_MEAN: acc += vv; break;
                        case RD_PROD: acc *= vv; break;
                        case RD_MAX: if (vv > acc) acc = vv; break;
                        case RD_MIN: if (vv < acc) acc = vv; break;
                        case RD_ARGMAX: if (vv > acc) { acc = vv; arg = i; } break;
                        case RD_ARGMIN: if (vv < acc) { acc = vv; arg = i; } break;
                        case RD_ANY: acc = (acc != 0.0) || (vv != 0.0); break;
                        case RD_ALL: acc = (acc != 0.0) && (vv != 0.0); break;
                    }
                }
                if (kind == RD_MEAN) acc /= (double)t->numel;
                if (kind == RD_ARGMAX || kind == RD_ARGMIN) acc = (double)arg;
            }
        }
        if (keepdim) {
            int32_t ksh[TS_MAXD]; int kn = t->ndim ? t->ndim : 1;
            for (int i = 0; i < kn; i++) ksh[i] = 1;
            sc_tensor *r = new_contig(kn, ksh, out_dt, 0);
            if (r) tel_set(r, 0, acc);
            return r;
        }
        int32_t one = 1;
        sc_tensor *r = new_contig(1, &one, out_dt, 0);
        if (r) tel_set(r, 0, acc);
        return r;
    }
    if (axis >= t->ndim) return NULL;
    int32_t alen = t->shape[axis];
    /* 输出形状 */
    int32_t rshape[TS_MAXD]; int rn = 0;
    for (int i = 0; i < t->ndim; i++) {
        if (i == axis) { if (keepdim) rshape[rn++] = 1; }
        else rshape[rn++] = t->shape[i];
    }
    if (rn == 0) { rshape[0] = 1; rn = 1; }
    sc_tensor *r = new_contig(rn, rshape, out_dt, 0);
    if (!r) return NULL;
    int64_t axstr = t->strides[axis];
    int64_t coord[TS_MAXD]; for (int i = 0; i < t->ndim; i++) coord[i] = 0;
    for (int64_t oi = 0; oi < r->numel; oi++) {
        int64_t base = t->offset;
        for (int d = 0; d < t->ndim; d++) if (d != axis) base += coord[d] * t->strides[d];
        /* reduce_run 用逻辑 tel_get(base..)，但 base 此处已是物理偏移；改写为物理聚合 */
        double acc;
        if (alen <= 0) acc = 0.0;
        else if (kind == RD_STD || kind == RD_VAR) {
            double s = 0; for (int32_t k = 0; k < alen; k++) s += el_get(t->store->data, t->dtype, base + (int64_t)k*axstr);
            double mn = s / alen, v = 0;
            for (int32_t k = 0; k < alen; k++) { double d = el_get(t->store->data, t->dtype, base + (int64_t)k*axstr) - mn; v += d*d; }
            v /= alen; acc = kind == RD_STD ? sqrt(v) : v;
        } else {
            acc = el_get(t->store->data, t->dtype, base); int64_t arg = 0;
            if (kind == RD_ANY || kind == RD_ALL) acc = (acc != 0.0);
            for (int32_t k = 1; k < alen; k++) {
                double vv = el_get(t->store->data, t->dtype, base + (int64_t)k*axstr);
                switch (kind) {
                    case RD_SUM: case RD_MEAN: acc += vv; break;
                    case RD_PROD: acc *= vv; break;
                    case RD_MAX: if (vv > acc) acc = vv; break;
                    case RD_MIN: if (vv < acc) acc = vv; break;
                    case RD_ARGMAX: if (vv > acc) { acc = vv; arg = k; } break;
                    case RD_ARGMIN: if (vv < acc) { acc = vv; arg = k; } break;
                    case RD_ANY: acc = (acc != 0.0) || (vv != 0.0); break;
                    case RD_ALL: acc = (acc != 0.0) && (vv != 0.0); break;
                }
            }
            if (kind == RD_MEAN) acc /= (double)alen;
            if (kind == RD_ARGMAX || kind == RD_ARGMIN) acc = (double)arg;
        }
        tel_set(r, oi, acc);
        for (int d = t->ndim - 1; d >= 0; d--) {
            if (d == axis) continue;
            if (++coord[d] < t->shape[d]) break;
            coord[d] = 0;
        }
    }
    return r;
}

sc_tensor *sc_tensor_sum(sc_tensor *_this, int32_t axis, bool keepdim)    { return sc_tensor_reduce(_this, axis, RD_SUM, keepdim); }
sc_tensor *sc_tensor_mean(sc_tensor *_this, int32_t axis, bool keepdim)   { return sc_tensor_reduce(_this, axis, RD_MEAN, keepdim); }
sc_tensor *sc_tensor_prod(sc_tensor *_this, int32_t axis, bool keepdim)   { return sc_tensor_reduce(_this, axis, RD_PROD, keepdim); }
sc_tensor *sc_tensor_max(sc_tensor *_this, int32_t axis, bool keepdim)    { return sc_tensor_reduce(_this, axis, RD_MAX, keepdim); }
sc_tensor *sc_tensor_min(sc_tensor *_this, int32_t axis, bool keepdim)    { return sc_tensor_reduce(_this, axis, RD_MIN, keepdim); }
sc_tensor *sc_tensor_argmax(sc_tensor *_this, int32_t axis, bool keepdim) { return sc_tensor_reduce(_this, axis, RD_ARGMAX, keepdim); }
sc_tensor *sc_tensor_argmin(sc_tensor *_this, int32_t axis, bool keepdim) { return sc_tensor_reduce(_this, axis, RD_ARGMIN, keepdim); }
sc_tensor *sc_tensor_std(sc_tensor *_this, int32_t axis, bool keepdim)    { return sc_tensor_reduce(_this, axis, RD_STD, keepdim); }
sc_tensor *sc_tensor_var(sc_tensor *_this, int32_t axis, bool keepdim)    { return sc_tensor_reduce(_this, axis, RD_VAR, keepdim); }
sc_tensor *sc_tensor_any(sc_tensor *_this, int32_t axis, bool keepdim)    { return sc_tensor_reduce(_this, axis, RD_ANY, keepdim); }
sc_tensor *sc_tensor_all(sc_tensor *_this, int32_t axis, bool keepdim)    { return sc_tensor_reduce(_this, axis, RD_ALL, keepdim); }

/* 累积（形状不变，沿 axis 前缀聚合）。 */
static sc_tensor *sc_tensor_cum(sc_tensor *t, int32_t axis, int is_prod) {
    if (axis < 0) axis += t->ndim;
    if (axis < 0 || axis >= t->ndim) return NULL;
    sc_tensor *r = materialize(t);
    if (!r) return NULL;
    int32_t alen = r->shape[axis];
    int64_t axstr = r->strides[axis];
    int64_t outer = alen > 0 ? r->numel / alen : 0;
    int64_t coord[TS_MAXD]; for (int i = 0; i < r->ndim; i++) coord[i] = 0;
    for (int64_t o = 0; o < outer; o++) {
        int64_t base = 0;
        for (int d = 0; d < r->ndim; d++) if (d != axis) base += coord[d] * r->strides[d];
        double acc = is_prod ? 1.0 : 0.0;
        for (int32_t k = 0; k < alen; k++) {
            int64_t off = base + (int64_t)k * axstr;
            double v = el_get(r->store->data, r->dtype, off);
            acc = is_prod ? acc * v : acc + v;
            el_set(r->store->data, r->dtype, off, acc);
        }
        for (int d = r->ndim - 1; d >= 0; d--) {
            if (d == axis) continue;
            if (++coord[d] < r->shape[d]) break;
            coord[d] = 0;
        }
    }
    return r;
}

sc_tensor *sc_tensor_cumsum(sc_tensor *_this, int32_t axis)  { return sc_tensor_cum(_this, axis, 0); }
sc_tensor *sc_tensor_cumprod(sc_tensor *_this, int32_t axis) { return sc_tensor_cum(_this, axis, 1); }

double sc_tensor_sum_all(sc_tensor *_this) {
    double acc = 0; for (int64_t i = 0; i < _this->numel; i++) acc += tel_get(_this, i);
    return acc;
}
double sc_tensor_mean_all(sc_tensor *_this) {
    return _this->numel > 0 ? sc_tensor_sum_all(_this) / (double)_this->numel : 0.0;
}
double sc_tensor_prod_all(sc_tensor *_this) {
    double acc = 1; for (int64_t i = 0; i < _this->numel; i++) acc *= tel_get(_this, i);
    return acc;
}
double sc_tensor_max_all(sc_tensor *_this) {
    if (_this->numel == 0) return 0.0;
    double m = tel_get(_this, 0);
    for (int64_t i = 1; i < _this->numel; i++) { double v = tel_get(_this, i); if (v > m) m = v; }
    return m;
}
double sc_tensor_min_all(sc_tensor *_this) {
    if (_this->numel == 0) return 0.0;
    double m = tel_get(_this, 0);
    for (int64_t i = 1; i < _this->numel; i++) { double v = tel_get(_this, i); if (v < m) m = v; }
    return m;
}
double sc_tensor_var_all(sc_tensor *_this) {
    if (_this->numel == 0) return 0.0;
    double mn = sc_tensor_mean_all(_this), v = 0;
    for (int64_t i = 0; i < _this->numel; i++) { double d = tel_get(_this, i) - mn; v += d * d; }
    return v / (double)_this->numel;
}
double sc_tensor_std_all(sc_tensor *_this) { return sqrt(sc_tensor_var_all(_this)); }

/* 分位数：buf 升序排序后 q01∈[0,1] 线性插值。 */
static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static double quantile_sorted(double *buf, int n, double q01) {
    if (n <= 0) return 0.0;
    if (n == 1) return buf[0];
    if (q01 < 0.0) q01 = 0.0; else if (q01 > 1.0) q01 = 1.0;
    double pos = q01 * (n - 1);
    int lo = (int)floor(pos);
    int hi = lo + 1; if (hi >= n) hi = n - 1;
    double frac = pos - lo;
    return buf[lo] + (buf[hi] - buf[lo]) * frac;
}

/* 沿 axis 取分位数（q01∈[0,1]）。axis==-1 → 全数据标量。 */
static sc_tensor *sc_tensor_quantile(sc_tensor *t, double q01, int32_t axis, bool keepdim) {
    if (axis < 0) {
        double *buf = (double *)sc_alloc((size_t)(t->numel > 0 ? t->numel : 1) * sizeof(double));
        if (!buf) return NULL;
        for (int64_t i = 0; i < t->numel; i++) buf[i] = tel_get(t, i);
        qsort(buf, (size_t)t->numel, sizeof(double), cmp_double);
        double v = quantile_sorted(buf, (int)t->numel, q01);
        sc_free(buf);
        if (keepdim) {
            int32_t ksh[TS_MAXD]; int kn = t->ndim ? t->ndim : 1;
            for (int i = 0; i < kn; i++) ksh[i] = 1;
            sc_tensor *r = new_contig(kn, ksh, t->dtype, 0);
            if (r) tel_set(r, 0, v);
            return r;
        }
        int32_t one = 1;
        sc_tensor *r = new_contig(1, &one, t->dtype, 0);
        if (r) tel_set(r, 0, v);
        return r;
    }
    if (axis >= t->ndim) return NULL;
    int32_t alen = t->shape[axis];
    int32_t rshape[TS_MAXD]; int rn = 0;
    for (int i = 0; i < t->ndim; i++) {
        if (i == axis) { if (keepdim) rshape[rn++] = 1; }
        else rshape[rn++] = t->shape[i];
    }
    if (rn == 0) { rshape[0] = 1; rn = 1; }
    sc_tensor *r = new_contig(rn, rshape, t->dtype, 0);
    if (!r) return NULL;
    double *buf = (double *)sc_alloc((size_t)(alen > 0 ? alen : 1) * sizeof(double));
    if (!buf) { sc_tensor_drop(r); sc_free(r); return NULL; }
    int64_t axstr = t->strides[axis];
    int64_t coord[TS_MAXD]; for (int i = 0; i < t->ndim; i++) coord[i] = 0;
    for (int64_t oi = 0; oi < r->numel; oi++) {
        int64_t base = t->offset;
        for (int d = 0; d < t->ndim; d++) if (d != axis) base += coord[d] * t->strides[d];
        for (int32_t k = 0; k < alen; k++) buf[k] = el_get(t->store->data, t->dtype, base + (int64_t)k * axstr);
        qsort(buf, (size_t)alen, sizeof(double), cmp_double);
        tel_set(r, oi, quantile_sorted(buf, alen, q01));
        for (int d = t->ndim - 1; d >= 0; d--) {
            if (d == axis) continue;
            if (++coord[d] < t->shape[d]) break;
            coord[d] = 0;
        }
    }
    sc_free(buf);
    return r;
}

sc_tensor *sc_tensor_median(sc_tensor *_this, int32_t axis, bool keepdim) { return sc_tensor_quantile(_this, 0.5, axis, keepdim); }
sc_tensor *sc_tensor_percentile(sc_tensor *_this, double q, int32_t axis, bool keepdim) { return sc_tensor_quantile(_this, q / 100.0, axis, keepdim); }
double sc_tensor_median_all(sc_tensor *_this) {
    sc_tensor *r = sc_tensor_quantile(_this, 0.5, -1, 0); if (!r) return 0.0;
    double v = tel_get(r, 0); sc_tensor_drop(r); sc_free(r); return v;
}
double sc_tensor_percentile_all(sc_tensor *_this, double q) {
    sc_tensor *r = sc_tensor_quantile(_this, q / 100.0, -1, 0); if (!r) return 0.0;
    double v = tel_get(r, 0); sc_tensor_drop(r); sc_free(r); return v;
}

/* ============================================================
 * 13. 线代
 * ============================================================ */

/* 2D×2D 矩乘核（写入 r 的 [ro,co] 子块；a/b 经逻辑 tel_get 支持视图）。 */
static void matmul_2d(sc_tensor *r, sc_tensor *a, sc_tensor *b,
                      int64_t ra_base, int64_t rb_base, int64_t rr_base,
                      int32_t m, int32_t k, int32_t n) {
    for (int32_t i = 0; i < m; i++)
        for (int32_t j = 0; j < n; j++) {
            double acc = 0;
            for (int32_t p = 0; p < k; p++)
                acc += el_get(a->store->data, a->dtype, ra_base + (int64_t)i * a->strides[a->ndim-2] + (int64_t)p * a->strides[a->ndim-1])
                     * el_get(b->store->data, b->dtype, rb_base + (int64_t)p * b->strides[b->ndim-2] + (int64_t)j * b->strides[b->ndim-1]);
            el_set(r->store->data, r->dtype, rr_base + (int64_t)i * n + j, acc);
        }
}

sc_tensor *sc_tensor_matmul(sc_tensor *_this, sc_tensor *o) {
    if (!o) return NULL;
    /* 2D×2D 快径 */
    if (_this->ndim == 2 && o->ndim == 2) {
        int32_t m = _this->shape[0], k = _this->shape[1];
        int32_t k2 = o->shape[0], n = o->shape[1];
        if (k != k2) return NULL;
        int32_t rshape[2] = { m, n };
        sc_tensor *r = new_contig(2, rshape, _this->dtype, 0);
        if (!r) return NULL;
#ifdef SCC_WITH_BLAS
        if (_this->dtype == TS_DT_F4 && o->dtype == TS_DT_F4 &&
            tensor_is_contiguous(_this) && tensor_is_contiguous(o)) {
            cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, m, n, k,
                        1.0f, (const float *)tensor_data(_this), k,
                        (const float *)tensor_data(o), n, 0.0f, (float *)r->store->data, n);
            return r;
        }
#endif
        matmul_2d(r, _this, o, _this->offset, o->offset, 0, m, k, n);
        return r;
    }
    /* 批量：末两维矩乘，前导维广播 */
    int an = _this->ndim, bn = o->ndim;
    if (an < 2 || bn < 2) return NULL;
    int32_t m = _this->shape[an-2], k = _this->shape[an-1];
    int32_t k2 = o->shape[bn-2], n = o->shape[bn-1];
    if (k != k2) return NULL;
    int bat = (an > bn ? an : bn) - 2;
    int32_t bshape[TS_MAXD];
    for (int i = 0; i < bat; i++) {
        int ai = an - 2 - bat + i, bi = bn - 2 - bat + i;
        int32_t ad = ai >= 0 ? _this->shape[ai] : 1;
        int32_t bd = bi >= 0 ? o->shape[bi] : 1;
        if (ad == bd) bshape[i] = ad;
        else if (ad == 1) bshape[i] = bd;
        else if (bd == 1) bshape[i] = ad;
        else return NULL;
    }
    int32_t rshape[TS_MAXD];
    for (int i = 0; i < bat; i++) rshape[i] = bshape[i];
    rshape[bat] = m; rshape[bat+1] = n;
    sc_tensor *r = new_contig(bat + 2, rshape, _this->dtype, 0);
    if (!r) return NULL;
    int64_t astr_b[TS_MAXD], bstr_b[TS_MAXD];
    for (int i = 0; i < bat; i++) {
        int ai = an - 2 - bat + i, bi = bn - 2 - bat + i;
        astr_b[i] = (ai >= 0 && _this->shape[ai] != 1) ? _this->strides[ai] : 0;
        bstr_b[i] = (bi >= 0 && o->shape[bi] != 1) ? o->strides[bi] : 0;
    }
    int64_t coord[TS_MAXD]; for (int i = 0; i < bat; i++) coord[i] = 0;
    int64_t nbat = 1; for (int i = 0; i < bat; i++) nbat *= bshape[i];
    for (int64_t bi = 0; bi < nbat; bi++) {
        int64_t abase = _this->offset, bbase = o->offset;
        for (int d = 0; d < bat; d++) { abase += coord[d]*astr_b[d]; bbase += coord[d]*bstr_b[d]; }
        matmul_2d(r, _this, o, abase, bbase, bi * m * n, m, k, n);
        for (int d = bat - 1; d >= 0; d--) { if (++coord[d] < bshape[d]) break; coord[d] = 0; }
    }
    return r;
}

double sc_tensor_dot(sc_tensor *_this, sc_tensor *o) {
    if (!o || _this->numel != o->numel) return 0.0;
    double acc = 0;
    for (int64_t i = 0; i < _this->numel; i++) acc += tel_get(_this, i) * tel_get(o, i);
    return acc;
}

sc_tensor *sc_tensor_outer(sc_tensor *_this, sc_tensor *o) {
    if (!o) return NULL;
    int32_t m = (int32_t)_this->numel, n = (int32_t)o->numel;
    int32_t rshape[2] = { m, n };
    sc_tensor *r = new_contig(2, rshape, _this->dtype, 0);
    if (!r) return NULL;
    for (int32_t i = 0; i < m; i++)
        for (int32_t j = 0; j < n; j++)
            tel_set(r, (int64_t)i * n + j, tel_get(_this, i) * tel_get(o, j));
    return r;
}

double sc_tensor_trace(sc_tensor *_this) {
    if (_this->ndim != 2) return 0.0;
    int32_t mn = _this->shape[0] < _this->shape[1] ? _this->shape[0] : _this->shape[1];
    double acc = 0;
    for (int32_t i = 0; i < mn; i++)
        acc += el_get(_this->store->data, _this->dtype,
                      _this->offset + (int64_t)i * _this->strides[0] + (int64_t)i * _this->strides[1]);
    return acc;
}

sc_tensor *sc_tensor_diagonal(sc_tensor *_this, int32_t k) {
    if (_this->ndim != 2) return NULL;
    int32_t rows = _this->shape[0], cols = _this->shape[1];
    int32_t len;
    if (k >= 0) len = (cols - k < rows) ? (cols - k) : rows;
    else        len = (rows + k < cols) ? (rows + k) : cols;
    if (len < 0) len = 0;
    int32_t shp = len;
    int32_t str = _this->strides[0] + _this->strides[1];   /* 对角步长（视图） */
    int64_t off = _this->offset + (k >= 0 ? (int64_t)k * _this->strides[1]
                                          : (int64_t)(-k) * _this->strides[0]);
    return new_view(_this, 1, &shp, &str, off);
}

sc_tensor *sc_tensor_bmm(sc_tensor *_this, sc_tensor *o) {
    if (!_this || !o || _this->ndim != 3 || o->ndim != 3) return NULL;
    /* PyTorch bmm 语义：batch 维必须一致，不做 batch 广播。 */
    if (_this->shape[0] != o->shape[0]) return NULL;
    return sc_tensor_matmul(_this, o);
}

sc_tensor *sc_tensor_addmm(sc_tensor *_this, sc_tensor *mat1, sc_tensor *mat2, double beta, double alpha) {
    if (!_this || !mat1 || !mat2) return NULL;
    if (_this->ndim != 2 || mat1->ndim != 2 || mat2->ndim != 2) return NULL;
    sc_tensor *mm = sc_tensor_matmul(mat1, mat2);
    if (!mm) return NULL;
    if (mm->ndim != 2 || mm->shape[0] != _this->shape[0] || mm->shape[1] != _this->shape[1]) {
        sc_tensor_drop(mm); sc_free(mm);
        return NULL;
    }
    sc_tensor *r = new_contig(_this->ndim, _this->shape, _this->dtype, 0);
    if (!r) { sc_tensor_drop(mm); sc_free(mm); return NULL; }
    for (int64_t i = 0; i < _this->numel; i++)
        tel_set(r, i, beta * tel_get(_this, i) + alpha * tel_get(mm, i));
    sc_tensor_drop(mm); sc_free(mm);
    return r;
}

/* --- 三角 / 范数 / 行列式 / 逆 / 解 / 分解 --- */

sc_tensor *sc_tensor_triu(sc_tensor *_this, int32_t k) {
    if (_this->ndim != 2) return NULL;
    int rows = _this->shape[0], cols = _this->shape[1];
    sc_tensor *r = materialize(_this);
    if (!r) return NULL;
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++)
            if (j < i + k) tel_set(r, (int64_t)i * cols + j, 0.0);
    return r;
}

sc_tensor *sc_tensor_tril(sc_tensor *_this, int32_t k) {
    if (_this->ndim != 2) return NULL;
    int rows = _this->shape[0], cols = _this->shape[1];
    sc_tensor *r = materialize(_this);
    if (!r) return NULL;
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++)
            if (j > i + k) tel_set(r, (int64_t)i * cols + j, 0.0);
    return r;
}

double sc_tensor_norm(sc_tensor *_this, double p) {
    if (p <= 0.0) {   /* inf 范数：最大绝对值 */
        double m = 0;
        for (int64_t i = 0; i < _this->numel; i++) { double v = fabs(tel_get(_this, i)); if (v > m) m = v; }
        return m;
    }
    double s = 0;
    for (int64_t i = 0; i < _this->numel; i++) s += pow(fabs(tel_get(_this, i)), p);
    return pow(s, 1.0 / p);
}

/* 读 2D 视图元素 (i,j) → 行主序 double 缓冲（n×n 或 m×n）。 */
static double *mat_read(const sc_tensor *t, int rows, int cols) {
    double *a = (double *)sc_alloc((size_t)rows * cols * sizeof(double));
    if (!a) return NULL;
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++)
            a[(int64_t)i * cols + j] = el_get(t->store->data, t->dtype,
                t->offset + (int64_t)i * t->strides[0] + (int64_t)j * t->strides[1]);
    return a;
}
static sc_tensor *mat_from(int rows, int cols, const double *a, int dt) {
    int32_t sh[2]; sh[0] = rows; sh[1] = cols;
    sc_tensor *r = new_contig(2, sh, dt, 0);
    if (r) for (int64_t i = 0; i < (int64_t)rows * cols; i++) tel_set(r, i, a[i]);
    return r;
}
static sc_tensor *vec_from(int n, const double *a, int dt) {
    int32_t sh = n; sc_tensor *r = new_contig(1, &sh, dt, 0);
    if (r) for (int i = 0; i < n; i++) tel_set(r, i, a[i]);
    return r;
}

double sc_tensor_det(sc_tensor *_this) {
    if (_this->ndim != 2 || _this->shape[0] != _this->shape[1]) return 0.0;
    int n = _this->shape[0];
    double *a = mat_read(_this, n, n);
    if (!a) return 0.0;
#ifdef SCC_WITH_LAPACK
    {
        lapack_int *ipiv = (lapack_int *)sc_alloc((size_t)n * sizeof(lapack_int));
        if (!ipiv) { sc_free(a); return 0.0; }
        lapack_int info = LAPACKE_dgetrf(LAPACK_ROW_MAJOR, n, n, a, n, ipiv);
        double d = 1.0;
        if (info != 0) d = 0.0;
        else for (int i = 0; i < n; i++) { d *= a[(int64_t)i*n+i]; if (ipiv[i] != i + 1) d = -d; }
        sc_free(ipiv); sc_free(a);
        return d;
    }
#endif
    double det = 1.0;
    for (int k = 0; k < n; k++) {
        int piv = k; double mx = fabs(a[(int64_t)k * n + k]);
        for (int i = k + 1; i < n; i++) { double v = fabs(a[(int64_t)i * n + k]); if (v > mx) { mx = v; piv = i; } }
        if (mx < 1e-300) { sc_free(a); return 0.0; }
        if (piv != k) { for (int j = 0; j < n; j++) { double tmp = a[(int64_t)k*n+j]; a[(int64_t)k*n+j] = a[(int64_t)piv*n+j]; a[(int64_t)piv*n+j] = tmp; } det = -det; }
        double pivv = a[(int64_t)k * n + k];
        det *= pivv;
        for (int i = k + 1; i < n; i++) {
            double f = a[(int64_t)i * n + k] / pivv;
            for (int j = k; j < n; j++) a[(int64_t)i * n + j] -= f * a[(int64_t)k * n + j];
        }
    }
    sc_free(a);
    return det;
}

sc_tensor *sc_tensor_inv(sc_tensor *_this) {
    if (_this->ndim != 2 || _this->shape[0] != _this->shape[1]) return NULL;
    int n = _this->shape[0];
    double *a = mat_read(_this, n, n);
    if (!a) return NULL;
#ifdef SCC_WITH_LAPACK
    {
        lapack_int *ipiv = (lapack_int *)sc_alloc((size_t)n * sizeof(lapack_int));
        if (!ipiv) { sc_free(a); return NULL; }
        lapack_int info = LAPACKE_dgetrf(LAPACK_ROW_MAJOR, n, n, a, n, ipiv);
        if (info == 0) info = LAPACKE_dgetri(LAPACK_ROW_MAJOR, n, a, n, ipiv);
        sc_free(ipiv);
        if (info != 0) { sc_free(a); return NULL; }
        tensor *r = mat_from(n, n, a, _this->dtype);
        sc_free(a);
        return r;
    }
#endif
    double *inv = (double *)sc_alloc((size_t)n * n * sizeof(double));
    if (!inv) { sc_free(a); return NULL; }
    for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) inv[(int64_t)i*n+j] = (i == j) ? 1.0 : 0.0;
    for (int k = 0; k < n; k++) {
        int piv = k; double mx = fabs(a[(int64_t)k*n+k]);
        for (int i = k + 1; i < n; i++) { double v = fabs(a[(int64_t)i*n+k]); if (v > mx) { mx = v; piv = i; } }
        if (mx < 1e-300) { sc_free(a); sc_free(inv); return NULL; }
        if (piv != k) for (int j = 0; j < n; j++) {
            double t1 = a[(int64_t)k*n+j]; a[(int64_t)k*n+j] = a[(int64_t)piv*n+j]; a[(int64_t)piv*n+j] = t1;
            double t2 = inv[(int64_t)k*n+j]; inv[(int64_t)k*n+j] = inv[(int64_t)piv*n+j]; inv[(int64_t)piv*n+j] = t2;
        }
        double pivv = a[(int64_t)k*n+k];
        for (int j = 0; j < n; j++) { a[(int64_t)k*n+j] /= pivv; inv[(int64_t)k*n+j] /= pivv; }
        for (int i = 0; i < n; i++) {
            if (i == k) continue;
            double f = a[(int64_t)i*n+k];
            for (int j = 0; j < n; j++) { a[(int64_t)i*n+j] -= f * a[(int64_t)k*n+j]; inv[(int64_t)i*n+j] -= f * inv[(int64_t)k*n+j]; }
        }
    }
    sc_tensor *r = mat_from(n, n, inv, _this->dtype);
    sc_free(a); sc_free(inv);
    return r;
}

sc_tensor *sc_tensor_solve(sc_tensor *_this, sc_tensor *b) {
    if (_this->ndim != 2 || _this->shape[0] != _this->shape[1] || !b) return NULL;
    int n = _this->shape[0];
    int kcols = (b->ndim == 1) ? 1 : b->shape[1];
    if (b->shape[0] != n) return NULL;
    double *a = mat_read(_this, n, n);
    if (!a) return NULL;
    double *x = (double *)sc_alloc((size_t)n * kcols * sizeof(double));
    if (!x) { sc_free(a); return NULL; }
    for (int i = 0; i < n; i++)
        for (int c = 0; c < kcols; c++)
            x[(int64_t)i * kcols + c] = (b->ndim == 1) ? tel_get(b, i) : tel_get(b, (int64_t)i * kcols + c);
#ifdef SCC_WITH_LAPACK
    {
        lapack_int *ipiv = (lapack_int *)sc_alloc((size_t)n * sizeof(lapack_int));
        if (!ipiv) { sc_free(a); sc_free(x); return NULL; }
        lapack_int info = LAPACKE_dgesv(LAPACK_ROW_MAJOR, n, kcols, a, n, ipiv, x, kcols);
        sc_free(ipiv);
        if (info != 0) { sc_free(a); sc_free(x); return NULL; }
        tensor *r = (b->ndim == 1) ? vec_from(n, x, _this->dtype) : mat_from(n, kcols, x, _this->dtype);
        sc_free(a); sc_free(x);
        return r;
    }
#endif
    for (int k = 0; k < n; k++) {
        int piv = k; double mx = fabs(a[(int64_t)k*n+k]);
        for (int i = k + 1; i < n; i++) { double v = fabs(a[(int64_t)i*n+k]); if (v > mx) { mx = v; piv = i; } }
        if (mx < 1e-300) { sc_free(a); sc_free(x); return NULL; }
        if (piv != k) {
            for (int j = 0; j < n; j++) { double t = a[(int64_t)k*n+j]; a[(int64_t)k*n+j] = a[(int64_t)piv*n+j]; a[(int64_t)piv*n+j] = t; }
            for (int c = 0; c < kcols; c++) { double t = x[(int64_t)k*kcols+c]; x[(int64_t)k*kcols+c] = x[(int64_t)piv*kcols+c]; x[(int64_t)piv*kcols+c] = t; }
        }
        double pivv = a[(int64_t)k*n+k];
        for (int i = k + 1; i < n; i++) {
            double f = a[(int64_t)i*n+k] / pivv;
            for (int j = k; j < n; j++) a[(int64_t)i*n+j] -= f * a[(int64_t)k*n+j];
            for (int c = 0; c < kcols; c++) x[(int64_t)i*kcols+c] -= f * x[(int64_t)k*kcols+c];
        }
    }
    for (int i = n - 1; i >= 0; i--) {
        for (int c = 0; c < kcols; c++) {
            double s = x[(int64_t)i*kcols+c];
            for (int j = i + 1; j < n; j++) s -= a[(int64_t)i*n+j] * x[(int64_t)j*kcols+c];
            x[(int64_t)i*kcols+c] = s / a[(int64_t)i*n+i];
        }
    }
    sc_tensor *r;
    if (b->ndim == 1) r = vec_from(n, x, _this->dtype);
    else r = mat_from(n, kcols, x, _this->dtype);
    sc_free(a); sc_free(x);
    return r;
}

sc_tensor *sc_tensor_cholesky(sc_tensor *_this) {
    if (_this->ndim != 2 || _this->shape[0] != _this->shape[1]) return NULL;
    int n = _this->shape[0];
    double *a = mat_read(_this, n, n);
    if (!a) return NULL;
#ifdef SCC_WITH_LAPACK
    {
        lapack_int info = LAPACKE_dpotrf(LAPACK_ROW_MAJOR, 'L', n, a, n);
        if (info != 0) { sc_free(a); return NULL; }   /* 非正定 */
        for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) a[(int64_t)i*n+j] = 0.0;
        sc_tensor *r = mat_from(n, n, a, _this->dtype);
        sc_free(a);
        return r;
    }
#endif
    double *L = (double *)sc_alloc((size_t)n * n * sizeof(double));
    if (!L) { sc_free(a); return NULL; }
    for (int64_t i = 0; i < (int64_t)n * n; i++) L[i] = 0.0;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j <= i; j++) {
            double s = a[(int64_t)i*n+j];
            for (int k = 0; k < j; k++) s -= L[(int64_t)i*n+k] * L[(int64_t)j*n+k];
            if (i == j) {
                if (s <= 0.0) { sc_free(a); sc_free(L); return NULL; }  /* 非正定 */
                L[(int64_t)i*n+j] = sqrt(s);
            } else {
                L[(int64_t)i*n+j] = s / L[(int64_t)j*n+j];
            }
        }
    }
    sc_tensor *r = mat_from(n, n, L, _this->dtype);
    sc_free(a); sc_free(L);
    return r;
}

/* QR（修正 Gram-Schmidt），_this 为 [m,n]，m>=n：out[0]=Q[m,n]，out[1]=R[n,n]。 */
bool sc_tensor_qr(sc_tensor *_this, void *out_) {
    sc_tensor **out = (sc_tensor **)out_;
    if (_this->ndim != 2) return 0;
    int m = _this->shape[0], n = _this->shape[1];
    if (m < n) return 0;
    double *A = mat_read(_this, m, n);
    if (!A) return 0;
#ifdef SCC_WITH_LAPACK
    {
        double *tau = (double *)sc_alloc((size_t)n * sizeof(double));
        if (!tau) { sc_free(A); return 0; }
        lapack_int info = LAPACKE_dgeqrf(LAPACK_ROW_MAJOR, m, n, A, n, tau);
        if (info != 0) { sc_free(tau); sc_free(A); return 0; }
        double *Rl = (double *)sc_alloc((size_t)n * n * sizeof(double));
        if (!Rl) { sc_free(tau); sc_free(A); return 0; }
        for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) Rl[(int64_t)i*n+j] = (j >= i) ? A[(int64_t)i*n+j] : 0.0;
        info = LAPACKE_dorgqr(LAPACK_ROW_MAJOR, m, n, n, A, n, tau);
        sc_free(tau);
        if (info != 0) { sc_free(Rl); sc_free(A); return 0; }
        out[0] = mat_from(m, n, A, _this->dtype);
        out[1] = mat_from(n, n, Rl, _this->dtype);
        sc_free(A); sc_free(Rl);
        return out[0] && out[1];
    }
#endif
    double *Q = (double *)sc_alloc((size_t)m * n * sizeof(double));
    double *R = (double *)sc_alloc((size_t)n * n * sizeof(double));
    if (!Q || !R) { sc_free(A); if (Q) sc_free(Q); if (R) sc_free(R); return 0; }
    for (int64_t i = 0; i < (int64_t)n * n; i++) R[i] = 0.0;
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < m; i++) Q[(int64_t)i*n+j] = A[(int64_t)i*n+j];
        for (int k = 0; k < j; k++) {
            double dot = 0;
            for (int i = 0; i < m; i++) dot += Q[(int64_t)i*n+k] * Q[(int64_t)i*n+j];
            R[(int64_t)k*n+j] = dot;
            for (int i = 0; i < m; i++) Q[(int64_t)i*n+j] -= dot * Q[(int64_t)i*n+k];
        }
        double nrm = 0;
        for (int i = 0; i < m; i++) nrm += Q[(int64_t)i*n+j] * Q[(int64_t)i*n+j];
        nrm = sqrt(nrm);
        R[(int64_t)j*n+j] = nrm;
        if (nrm > 1e-300) for (int i = 0; i < m; i++) Q[(int64_t)i*n+j] /= nrm;
    }
    out[0] = mat_from(m, n, Q, _this->dtype);
    out[1] = mat_from(n, n, R, _this->dtype);
    sc_free(A); sc_free(Q); sc_free(R);
    return out[0] && out[1];
}

/* 对称特征分解（Jacobi 旋转），_this 为 [n,n] 对称：out[0]=vals[n] 升序，out[1]=vecs[n,n]（列为特征向量）。 */
bool sc_tensor_eigh(sc_tensor *_this, void *out_) {
    sc_tensor **out = (sc_tensor **)out_;
    if (_this->ndim != 2 || _this->shape[0] != _this->shape[1]) return 0;
    int n = _this->shape[0];
    double *A = mat_read(_this, n, n);
    if (!A) return 0;
#ifdef SCC_WITH_LAPACK
    {
        double *w = (double *)sc_alloc((size_t)n * sizeof(double));
        if (!w) { sc_free(A); return 0; }
        lapack_int info = LAPACKE_dsyev(LAPACK_ROW_MAJOR, 'V', 'L', n, A, n, w);
        if (info != 0) { sc_free(w); sc_free(A); return 0; }
        /* dsyev：w 升序；A 列为特征向量（A[i*n+j]=向量 j 的分量 i） */
        out[0] = vec_from(n, w, _this->dtype);
        out[1] = mat_from(n, n, A, _this->dtype);
        sc_free(w); sc_free(A);
        return out[0] && out[1];
    }
#endif
    double *V = (double *)sc_alloc((size_t)n * n * sizeof(double));
    if (!V) { sc_free(A); return 0; }
    for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) V[(int64_t)i*n+j] = (i == j) ? 1.0 : 0.0;
    for (int sweep = 0; sweep < 100; sweep++) {
        double off = 0;
        for (int p = 0; p < n; p++) for (int q = p + 1; q < n; q++) off += A[(int64_t)p*n+q] * A[(int64_t)p*n+q];
        if (off < 1e-30) break;
        for (int p = 0; p < n; p++) {
            for (int q = p + 1; q < n; q++) {
                double apq = A[(int64_t)p*n+q];
                if (fabs(apq) < 1e-300) continue;
                double app = A[(int64_t)p*n+p], aqq = A[(int64_t)q*n+q];
                double phi = 0.5 * atan2(2.0 * apq, aqq - app);
                double c = cos(phi), s = sin(phi);
                for (int k = 0; k < n; k++) {
                    double akp = A[(int64_t)k*n+p], akq = A[(int64_t)k*n+q];
                    A[(int64_t)k*n+p] = c * akp - s * akq;
                    A[(int64_t)k*n+q] = s * akp + c * akq;
                }
                for (int k = 0; k < n; k++) {
                    double apk = A[(int64_t)p*n+k], aqk = A[(int64_t)q*n+k];
                    A[(int64_t)p*n+k] = c * apk - s * aqk;
                    A[(int64_t)q*n+k] = s * apk + c * aqk;
                }
                for (int k = 0; k < n; k++) {
                    double vkp = V[(int64_t)k*n+p], vkq = V[(int64_t)k*n+q];
                    V[(int64_t)k*n+p] = c * vkp - s * vkq;
                    V[(int64_t)k*n+q] = s * vkp + c * vkq;
                }
            }
        }
    }
    double *w = (double *)sc_alloc((size_t)n * sizeof(double));
    int *idx = (int *)sc_alloc((size_t)n * sizeof(int));
    if (!w || !idx) { sc_free(A); sc_free(V); if (w) sc_free(w); if (idx) sc_free(idx); return 0; }
    for (int i = 0; i < n; i++) { w[i] = A[(int64_t)i*n+i]; idx[i] = i; }
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++)
        if (w[idx[j]] < w[idx[i]]) { int t = idx[i]; idx[i] = idx[j]; idx[j] = t; }
    double *vals = (double *)sc_alloc((size_t)n * sizeof(double));
    double *vecs = (double *)sc_alloc((size_t)n * n * sizeof(double));
    if (!vals || !vecs) { sc_free(A); sc_free(V); sc_free(w); sc_free(idx); if (vals) sc_free(vals); if (vecs) sc_free(vecs); return 0; }
    for (int c = 0; c < n; c++) {
        vals[c] = w[idx[c]];
        for (int r = 0; r < n; r++) vecs[(int64_t)r*n+c] = V[(int64_t)r*n+idx[c]];
    }
    out[0] = vec_from(n, vals, _this->dtype);
    out[1] = mat_from(n, n, vecs, _this->dtype);
    sc_free(A); sc_free(V); sc_free(w); sc_free(idx); sc_free(vals); sc_free(vecs);
    return out[0] && out[1];
}

/* 瘦 SVD（经 AᵀA 的对称特征分解），_this 为 [m,n]：out[0]=U[m,r]，out[1]=S[r]，out[2]=V[n,r]，r=min(m,n)。 */
bool sc_tensor_svd(sc_tensor *_this, void *out_) {
    sc_tensor **out = (sc_tensor **)out_;
    if (_this->ndim != 2) return 0;
    int m = _this->shape[0], n = _this->shape[1];
    int r = m < n ? m : n;
    double *A = mat_read(_this, m, n);
    if (!A) return 0;
#ifdef SCC_WITH_LAPACK
    {
        double *S = (double *)sc_alloc((size_t)r * sizeof(double));
        double *U = (double *)sc_alloc((size_t)m * r * sizeof(double));
        double *VT = (double *)sc_alloc((size_t)r * n * sizeof(double));
        double *superb = (double *)sc_alloc((size_t)r * sizeof(double));
        if (!S || !U || !VT || !superb) { sc_free(A); if (S) sc_free(S); if (U) sc_free(U); if (VT) sc_free(VT); if (superb) sc_free(superb); return 0; }
        lapack_int info = LAPACKE_dgesvd(LAPACK_ROW_MAJOR, 'S', 'S', m, n, A, n, S, U, r, VT, n, superb);
        sc_free(superb); sc_free(A);
        if (info != 0) { sc_free(S); sc_free(U); sc_free(VT); return 0; }
        double *V = (double *)sc_alloc((size_t)n * r * sizeof(double));
        if (!V) { sc_free(S); sc_free(U); sc_free(VT); return 0; }
        for (int i = 0; i < n; i++) for (int c = 0; c < r; c++) V[(int64_t)i*r+c] = VT[(int64_t)c*n+i];
        out[0] = mat_from(m, r, U, _this->dtype);
        out[1] = vec_from(r, S, _this->dtype);
        out[2] = mat_from(n, r, V, _this->dtype);
        sc_free(S); sc_free(U); sc_free(VT); sc_free(V);
        return out[0] && out[1] && out[2];
    }
#endif
    /* B = AᵀA  [n,n] 对称 */
    double *B = (double *)sc_alloc((size_t)n * n * sizeof(double));
    if (!B) { sc_free(A); return 0; }
    for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) {
        double s = 0; for (int k = 0; k < m; k++) s += A[(int64_t)k*n+i] * A[(int64_t)k*n+j];
        B[(int64_t)i*n+j] = s;
    }
    sc_tensor *bt = mat_from(n, n, B, TS_DT_F8);
    sc_free(B);
    if (!bt) { sc_free(A); return 0; }
    sc_tensor *eo[2];
    uint8_t ok = sc_tensor_eigh(bt, eo);
    sc_tensor_drop(bt); sc_free(bt);
    if (!ok) { sc_free(A); return 0; }
    /* eigh 升序 → 取末 r 个降序作奇异值 */
    double *S = (double *)sc_alloc((size_t)r * sizeof(double));
    double *Vt = (double *)sc_alloc((size_t)n * r * sizeof(double));
    double *U = (double *)sc_alloc((size_t)m * r * sizeof(double));
    if (!S || !Vt || !U) { sc_free(A); sc_tensor_drop(eo[0]); sc_free(eo[0]); sc_tensor_drop(eo[1]); sc_free(eo[1]); if (S) sc_free(S); if (Vt) sc_free(Vt); if (U) sc_free(U); return 0; }
    for (int c = 0; c < r; c++) {
        int ec = n - 1 - c;                       /* 降序 */
        double ev = tel_get(eo[0], ec);
        double sv = ev > 0.0 ? sqrt(ev) : 0.0;
        S[c] = sv;
        for (int i = 0; i < n; i++) Vt[(int64_t)i*r+c] = tel_get(eo[1], (int64_t)i*n + ec);  /* V 第 ec 列 */
        /* u_c = A v_c / sv */
        for (int i = 0; i < m; i++) {
            double s = 0; for (int k = 0; k < n; k++) s += A[(int64_t)i*n+k] * Vt[(int64_t)k*r+c];
            U[(int64_t)i*r+c] = sv > 1e-300 ? s / sv : 0.0;
        }
    }
    out[0] = mat_from(m, r, U, _this->dtype);
    out[1] = vec_from(r, S, _this->dtype);
    out[2] = mat_from(n, r, Vt, _this->dtype);
    sc_tensor_drop(eo[0]); sc_free(eo[0]); sc_tensor_drop(eo[1]); sc_free(eo[1]);
    sc_free(A); sc_free(S); sc_free(Vt); sc_free(U);
    return out[0] && out[1] && out[2];
}

/* ============================================================
 * 14. nn 激活/逐点
 * ============================================================ */

static double un_relu(double x)    { return x > 0.0 ? x : 0.0; }
static double un_sigmoid(double x) { return 1.0 / (1.0 + exp(-x)); }
static double un_tanh(double x)    { return tanh(x); }

sc_tensor *sc_tensor_relu(sc_tensor *_this)    { return sc_tensor_unary(_this, un_relu); }
sc_tensor *sc_tensor_sigmoid(sc_tensor *_this) { return sc_tensor_unary(_this, un_sigmoid); }
sc_tensor *sc_tensor_tanh(sc_tensor *_this)    { return sc_tensor_unary(_this, un_tanh); }

bool sc_tensor_relu_(sc_tensor *_this) {
    for (int64_t i = 0; i < _this->numel; i++) {
        double v = tel_get(_this, i);
        if (v < 0.0) tel_set(_this, i, 0.0);
    }
    return 1;
}

sc_tensor *sc_tensor_softmax(sc_tensor *_this, int32_t axis) {
    if (axis < 0) axis += _this->ndim;
    if (axis < 0 || axis >= _this->ndim) return NULL;
    sc_tensor *r = materialize(_this);
    if (!r) return NULL;
    int32_t alen = r->shape[axis];
    if (alen <= 0) return r;
    int64_t axstr = r->strides[axis];
    int64_t outer = r->numel / alen;
    int64_t coord[TS_MAXD]; for (int i = 0; i < r->ndim; i++) coord[i] = 0;
    for (int64_t o = 0; o < outer; o++) {
        int64_t base = 0;
        for (int d = 0; d < r->ndim; d++) if (d != axis) base += coord[d] * r->strides[d];
        double mx = el_get(r->store->data, r->dtype, base);
        for (int32_t k = 1; k < alen; k++) { double v = el_get(r->store->data, r->dtype, base + (int64_t)k*axstr); if (v > mx) mx = v; }
        double sum = 0;
        for (int32_t k = 0; k < alen; k++) {
            double e = exp(el_get(r->store->data, r->dtype, base + (int64_t)k*axstr) - mx);
            el_set(r->store->data, r->dtype, base + (int64_t)k*axstr, e); sum += e;
        }
        if (sum != 0.0)
            for (int32_t k = 0; k < alen; k++)
                el_set(r->store->data, r->dtype, base + (int64_t)k*axstr,
                       el_get(r->store->data, r->dtype, base + (int64_t)k*axstr) / sum);
        for (int d = r->ndim - 1; d >= 0; d--) {
            if (d == axis) continue;
            if (++coord[d] < r->shape[d]) break;
            coord[d] = 0;
        }
    }
    return r;
}

/* 沿 axis 做 log-softmax：x - max - log(Σ exp(x-max))（数值稳定）。 */
sc_tensor *sc_tensor_log_softmax(sc_tensor *_this, int32_t axis) {
    if (axis < 0) axis += _this->ndim;
    if (axis < 0 || axis >= _this->ndim) return NULL;
    sc_tensor *r = materialize(_this);
    if (!r) return NULL;
    int32_t alen = r->shape[axis];
    if (alen <= 0) return r;
    int64_t axstr = r->strides[axis];
    int64_t outer = r->numel / alen;
    int64_t coord[TS_MAXD]; for (int i = 0; i < r->ndim; i++) coord[i] = 0;
    for (int64_t o = 0; o < outer; o++) {
        int64_t base = 0;
        for (int d = 0; d < r->ndim; d++) if (d != axis) base += coord[d] * r->strides[d];
        double mx = el_get(r->store->data, r->dtype, base);
        for (int32_t k = 1; k < alen; k++) { double v = el_get(r->store->data, r->dtype, base + (int64_t)k*axstr); if (v > mx) mx = v; }
        double sum = 0;
        for (int32_t k = 0; k < alen; k++) sum += exp(el_get(r->store->data, r->dtype, base + (int64_t)k*axstr) - mx);
        double lsum = log(sum);
        for (int32_t k = 0; k < alen; k++) {
            double v = el_get(r->store->data, r->dtype, base + (int64_t)k*axstr);
            el_set(r->store->data, r->dtype, base + (int64_t)k*axstr, v - mx - lsum);
        }
        for (int d = r->ndim - 1; d >= 0; d--) {
            if (d == axis) continue;
            if (++coord[d] < r->shape[d]) break;
            coord[d] = 0;
        }
    }
    return r;
}

sc_tensor *sc_tensor_leaky_relu(sc_tensor *_this, double slope) {
    sc_tensor *r = new_contig(_this->ndim, _this->shape, _this->dtype, 0);
    if (r) for (int64_t i = 0; i < _this->numel; i++) {
        double x = tel_get(_this, i);
        tel_set(r, i, x > 0.0 ? x : slope * x);
    }
    return r;
}

sc_tensor *sc_tensor_elu(sc_tensor *_this, double alpha) {
    sc_tensor *r = new_contig(_this->ndim, _this->shape, _this->dtype, 0);
    if (r) for (int64_t i = 0; i < _this->numel; i++) {
        double x = tel_get(_this, i);
        tel_set(r, i, x > 0.0 ? x : alpha * (exp(x) - 1.0));
    }
    return r;
}

sc_tensor *sc_tensor_silu(sc_tensor *_this) {
    sc_tensor *r = new_contig(_this->ndim, _this->shape, _this->dtype, 0);
    if (r) for (int64_t i = 0; i < _this->numel; i++) {
        double x = tel_get(_this, i);
        tel_set(r, i, x / (1.0 + exp(-x)));
    }
    return r;
}

sc_tensor *sc_tensor_gelu(sc_tensor *_this) {
    /* tanh 近似：0.5x(1+tanh(√(2/π)(x+0.044715x³))) */
    const double c = 0.7978845608028654;  /* √(2/π) */
    sc_tensor *r = new_contig(_this->ndim, _this->shape, _this->dtype, 0);
    if (r) for (int64_t i = 0; i < _this->numel; i++) {
        double x = tel_get(_this, i);
        double inner = c * (x + 0.044715 * x * x * x);
        tel_set(r, i, 0.5 * x * (1.0 + tanh(inner)));
    }
    return r;
}

/* 交叉熵：logits[N,C] + 整型 target[N] → 平均 -log_softmax(logits)[i, target[i]]。 */
double sc_tensor_cross_entropy(sc_tensor *_this, sc_tensor *target) {
    if (_this->ndim != 2 || !target) return 0.0;
    int32_t N = _this->shape[0], C = _this->shape[1];
    if (N <= 0 || C <= 0 || target->numel != N) return 0.0;
    double acc = 0.0;
    for (int32_t i = 0; i < N; i++) {
        int32_t rc[2]; rc[0] = i;
        double mx = -1e308;
        for (int32_t c = 0; c < C; c++) { rc[1] = c; double v = sc_tensor_at_nd(_this, rc); if (v > mx) mx = v; }
        double sum = 0.0;
        for (int32_t c = 0; c < C; c++) { rc[1] = c; sum += exp(sc_tensor_at_nd(_this, rc) - mx); }
        int64_t t = (int64_t)tel_get(target, i);
        if (t < 0 || t >= C) continue;
        rc[1] = (int32_t)t;
        double logit_t = sc_tensor_at_nd(_this, rc);
        acc += -(logit_t - mx - log(sum));
    }
    return acc / N;
}

double sc_tensor_mse_loss(sc_tensor *_this, sc_tensor *target) {
    if (!_this || !target || !sc_tensor_is_same_shape(_this, target) || _this->numel <= 0) return 0.0;
    double acc = 0.0;
    for (int64_t i = 0; i < _this->numel; i++) {
        double d = tel_get(_this, i) - tel_get(target, i);
        acc += d * d;
    }
    return acc / _this->numel;
}

/* NLL：input 视为 log-prob [N,C]，target 为整型类标 [N]。 */
double sc_tensor_nll_loss(sc_tensor *_this, sc_tensor *target) {
    if (!_this || !target || _this->ndim != 2) return 0.0;
    int32_t N = _this->shape[0], C = _this->shape[1];
    if (N <= 0 || C <= 0 || target->numel != N) return 0.0;
    double acc = 0.0;
    for (int32_t i = 0; i < N; i++) {
        int64_t t = (int64_t)tel_get(target, i);
        if (t < 0 || t >= C) continue;
        int32_t rc[2]; rc[0] = i; rc[1] = (int32_t)t;
        acc += -sc_tensor_at_nd(_this, rc);
    }
    return acc / N;
}

/* BCE with logits（数值稳定）：max(x,0) - x*y + log1p(exp(-abs(x))) */
double sc_tensor_bce_with_logits(sc_tensor *_this, sc_tensor *target) {
    if (!_this || !target || !sc_tensor_is_same_shape(_this, target) || _this->numel <= 0) return 0.0;
    double acc = 0.0;
    for (int64_t i = 0; i < _this->numel; i++) {
        double x = tel_get(_this, i);
        double y = tel_get(target, i);
        double z = x >= 0.0 ? x : 0.0;
        acc += z - x * y + log1p(exp(-fabs(x)));
    }
    return acc / _this->numel;
}

/* ============================================================
 * 反向数值核（backward kernels）
 *   纯数值、无图无状态，与各自前向算子成对。供 nn 自动微分引擎组合调用，
 *   不在 sc 表面暴露（仅 ts.h 的 C ABI）。所有核返回新连续 tensor*，调用方 drop。
 * ============================================================ */

/* 广播反向：将 grad（前向广播后的形状）沿被广播维 sum-reduce 回 target 形状。
   shape[ndim] 须能广播到 grad 形状（尾维对齐，长度相等或 target 维为 1 / 缺失）。 */
sc_tensor *sc_tensor_sum_to(sc_tensor *grad, int32_t ndim, int32_t *shape) {
    if (!grad || ndim < 1) return NULL;
    sc_tensor *r = new_contig(ndim, shape, grad->dtype, 1);
    if (!r) return NULL;
    int gnd = grad->ndim;
    int64_t coord[TS_MAXD];
    for (int i = 0; i < gnd; i++) coord[i] = 0;
    int32_t rc[TS_MAXD];
    for (int64_t i = 0; i < grad->numel; i++) {
        for (int d = 0; d < ndim; d++) {
            int gd = gnd - ndim + d;                 /* 尾维对齐到 grad */
            int64_t c = (gd >= 0) ? coord[gd] : 0;
            rc[d] = (shape[d] == 1) ? 0 : (int32_t)c;
        }
        sc_tensor_set_nd(r, rc, sc_tensor_at_nd(r, rc) + tel_get(grad, i));
        for (int d = gnd - 1; d >= 0; d--) { if (++coord[d] < grad->shape[d]) break; coord[d] = 0; }
    }
    return r;
}

/* relu 反向：grad * (x > 0)。x 为前向输入，grad 与 x 同形。 */
sc_tensor *sc_tensor_relu_backward(sc_tensor *grad, sc_tensor *x) {
    if (!grad || !x) return NULL;
    sc_tensor *r = new_contig(grad->ndim, grad->shape, grad->dtype, 0);
    if (r) for (int64_t i = 0; i < grad->numel; i++)
        tel_set(r, i, tel_get(x, i) > 0.0 ? tel_get(grad, i) : 0.0);
    return r;
}

/* sigmoid 反向：grad * y * (1 - y)。y 为前向输出。 */
sc_tensor *sc_tensor_sigmoid_backward(sc_tensor *grad, sc_tensor *y) {
    if (!grad || !y) return NULL;
    sc_tensor *r = new_contig(grad->ndim, grad->shape, grad->dtype, 0);
    if (r) for (int64_t i = 0; i < grad->numel; i++) {
        double yv = tel_get(y, i);
        tel_set(r, i, tel_get(grad, i) * yv * (1.0 - yv));
    }
    return r;
}

/* tanh 反向：grad * (1 - y*y)。y 为前向输出。 */
sc_tensor *sc_tensor_tanh_backward(sc_tensor *grad, sc_tensor *y) {
    if (!grad || !y) return NULL;
    sc_tensor *r = new_contig(grad->ndim, grad->shape, grad->dtype, 0);
    if (r) for (int64_t i = 0; i < grad->numel; i++) {
        double yv = tel_get(y, i);
        tel_set(r, i, tel_get(grad, i) * (1.0 - yv * yv));
    }
    return r;
}

/* mse_loss 反向（对 x）：2/N * (x - target)，N=numel。已含均值归一。 */
sc_tensor *sc_tensor_mse_backward(sc_tensor *x, sc_tensor *target) {
    if (!x || !target || !sc_tensor_is_same_shape(x, target) || x->numel <= 0) return NULL;
    sc_tensor *r = new_contig(x->ndim, x->shape, x->dtype, 0);
    if (!r) return NULL;
    double s = 2.0 / (double)x->numel;
    for (int64_t i = 0; i < x->numel; i++)
        tel_set(r, i, s * (tel_get(x, i) - tel_get(target, i)));
    return r;
}

/* cross_entropy 反向（对 logits）：(softmax(logits) - onehot(target)) / N。
   logits[N,C]，target 整型类标[N]。已含均值归一。 */
sc_tensor *sc_tensor_cross_entropy_backward(sc_tensor *logits, sc_tensor *target) {
    if (!logits || logits->ndim != 2 || !target) return NULL;
    int32_t N = logits->shape[0], C = logits->shape[1];
    if (N <= 0 || C <= 0 || target->numel != N) return NULL;
    int32_t rshape[2] = { N, C };
    sc_tensor *r = new_contig(2, rshape, logits->dtype, 0);
    if (!r) return NULL;
    for (int32_t i = 0; i < N; i++) {
        int32_t rc[2]; rc[0] = i;
        double mx = -1e308;
        for (int32_t c = 0; c < C; c++) { rc[1] = c; double v = sc_tensor_at_nd(logits, rc); if (v > mx) mx = v; }
        double sum = 0.0;
        for (int32_t c = 0; c < C; c++) { rc[1] = c; sum += exp(sc_tensor_at_nd(logits, rc) - mx); }
        int64_t t = (int64_t)tel_get(target, i);
        for (int32_t c = 0; c < C; c++) {
            rc[1] = c;
            double sm = exp(sc_tensor_at_nd(logits, rc) - mx) / sum;
            double g = (sm - ((int64_t)c == t ? 1.0 : 0.0)) / (double)N;
            sc_tensor_set_nd(r, rc, g);
        }
    }
    return r;
}

/* softmax 反向：y=softmax(x) 沿 axis。gx_k = y_k*(g_k - Σ_j g_j*y_j)。y 为前向输出。 */
sc_tensor *sc_tensor_softmax_backward(sc_tensor *grad, sc_tensor *y, int32_t axis) {
    if (!grad || !y) return NULL;
    int nd = y->ndim;
    if (axis < 0) axis += nd;
    if (axis < 0 || axis >= nd) return NULL;
    int32_t alen = y->shape[axis];
    sc_tensor *r = new_contig(nd, y->shape, y->dtype, 0);
    if (!r) return NULL;
    if (alen <= 0) return r;
    int64_t outer = y->numel / alen;
    int64_t coord[TS_MAXD];
    for (int d = 0; d < nd; d++) coord[d] = 0;
    for (int64_t oi = 0; oi < outer; oi++) {
        int32_t c[TS_MAXD];
        double dot = 0.0;
        for (int32_t j = 0; j < alen; j++) {
            coord[axis] = j;
            for (int d = 0; d < nd; d++) c[d] = (int32_t)coord[d];
            dot += sc_tensor_at_nd(grad, c) * sc_tensor_at_nd(y, c);
        }
        for (int32_t j = 0; j < alen; j++) {
            coord[axis] = j;
            for (int d = 0; d < nd; d++) c[d] = (int32_t)coord[d];
            double yv = sc_tensor_at_nd(y, c);
            sc_tensor_set_nd(r, c, yv * (sc_tensor_at_nd(grad, c) - dot));
        }
        coord[axis] = 0;
        for (int d = nd - 1; d >= 0; d--) {
            if (d == axis) continue;
            if (++coord[d] < y->shape[d]) break;
            coord[d] = 0;
        }
    }
    return r;
}

/* log_softmax 反向：out=log_softmax(x) 沿 axis。gx_k = g_k - exp(out_k)*Σ_j g_j。out 为前向输出。 */
sc_tensor *sc_tensor_log_softmax_backward(sc_tensor *grad, sc_tensor *out, int32_t axis) {
    if (!grad || !out) return NULL;
    int nd = out->ndim;
    if (axis < 0) axis += nd;
    if (axis < 0 || axis >= nd) return NULL;
    int32_t alen = out->shape[axis];
    sc_tensor *r = new_contig(nd, out->shape, out->dtype, 0);
    if (!r) return NULL;
    if (alen <= 0) return r;
    int64_t outer = out->numel / alen;
    int64_t coord[TS_MAXD];
    for (int d = 0; d < nd; d++) coord[d] = 0;
    for (int64_t oi = 0; oi < outer; oi++) {
        int32_t c[TS_MAXD];
        double sg = 0.0;
        for (int32_t j = 0; j < alen; j++) {
            coord[axis] = j;
            for (int d = 0; d < nd; d++) c[d] = (int32_t)coord[d];
            sg += sc_tensor_at_nd(grad, c);
        }
        for (int32_t j = 0; j < alen; j++) {
            coord[axis] = j;
            for (int d = 0; d < nd; d++) c[d] = (int32_t)coord[d];
            double gv = sc_tensor_at_nd(grad, c);
            sc_tensor_set_nd(r, c, gv - exp(sc_tensor_at_nd(out, c)) * sg);
        }
        coord[axis] = 0;
        for (int d = nd - 1; d >= 0; d--) {
            if (d == axis) continue;
            if (++coord[d] < out->shape[d]) break;
            coord[d] = 0;
        }
    }
    return r;
}

/* leaky_relu 反向：gx = g*(x>0?1:slope)。x 为前向输入。 */
sc_tensor *sc_tensor_leaky_relu_backward(sc_tensor *grad, sc_tensor *x, double slope) {
    if (!grad || !x) return NULL;
    sc_tensor *r = new_contig(grad->ndim, grad->shape, grad->dtype, 0);
    if (r) for (int64_t i = 0; i < grad->numel; i++)
        tel_set(r, i, tel_get(grad, i) * (tel_get(x, i) > 0.0 ? 1.0 : slope));
    return r;
}

/* elu 反向：x>0 → g；否则 g*alpha*exp(x)。x 为前向输入。 */
sc_tensor *sc_tensor_elu_backward(sc_tensor *grad, sc_tensor *x, double alpha) {
    if (!grad || !x) return NULL;
    sc_tensor *r = new_contig(grad->ndim, grad->shape, grad->dtype, 0);
    if (r) for (int64_t i = 0; i < grad->numel; i++) {
        double xv = tel_get(x, i);
        double d = xv > 0.0 ? 1.0 : alpha * exp(xv);
        tel_set(r, i, tel_get(grad, i) * d);
    }
    return r;
}

/* silu 反向：silu=x*σ(x)，导数 σ(x)*(1+x*(1-σ(x)))。x 为前向输入。 */
sc_tensor *sc_tensor_silu_backward(sc_tensor *grad, sc_tensor *x) {
    if (!grad || !x) return NULL;
    sc_tensor *r = new_contig(grad->ndim, grad->shape, grad->dtype, 0);
    if (r) for (int64_t i = 0; i < grad->numel; i++) {
        double xv = tel_get(x, i);
        double s = 1.0 / (1.0 + exp(-xv));
        double d = s * (1.0 + xv * (1.0 - s));
        tel_set(r, i, tel_get(grad, i) * d);
    }
    return r;
}

/* gelu 反向（tanh 近似，与前向一致）。x 为前向输入。 */
sc_tensor *sc_tensor_gelu_backward(sc_tensor *grad, sc_tensor *x) {
    const double c = 0.7978845608028654;  /* √(2/π) */
    if (!grad || !x) return NULL;
    sc_tensor *r = new_contig(grad->ndim, grad->shape, grad->dtype, 0);
    if (r) for (int64_t i = 0; i < grad->numel; i++) {
        double xv = tel_get(x, i);
        double inner = c * (xv + 0.044715 * xv * xv * xv);
        double th = tanh(inner);
        double dinner = c * (1.0 + 3.0 * 0.044715 * xv * xv);
        double d = 0.5 * (1.0 + th) + 0.5 * xv * (1.0 - th * th) * dinner;
        tel_set(r, i, tel_get(grad, i) * d);
    }
    return r;
}

/* layer_norm 反向（无仿射）：沿 axis，N=alen。
   x_hat_i=(x_i-μ)*inv；dx_i = inv*(g_i - mean(g) - x_hat_i*mean(g*x_hat))。x 为前向输入。 */
sc_tensor *sc_tensor_layer_norm_backward(sc_tensor *grad, sc_tensor *x, int32_t axis, double eps) {
    if (!grad || !x || x->ndim <= 0) return NULL;
    int nd = x->ndim;
    if (axis < 0) axis += nd;
    if (axis < 0 || axis >= nd) return NULL;
    int32_t alen = x->shape[axis];
    sc_tensor *r = new_contig(nd, x->shape, x->dtype, 0);
    if (!r) return NULL;
    if (alen <= 0) return r;
    int64_t outer = x->numel / alen;
    int64_t coord[TS_MAXD];
    for (int d = 0; d < nd; d++) coord[d] = 0;
    for (int64_t oi = 0; oi < outer; oi++) {
        int32_t c[TS_MAXD];
        double mean = 0.0;
        for (int32_t j = 0; j < alen; j++) {
            coord[axis] = j;
            for (int d = 0; d < nd; d++) c[d] = (int32_t)coord[d];
            mean += sc_tensor_at_nd(x, c);
        }
        mean /= alen;
        double var = 0.0;
        for (int32_t j = 0; j < alen; j++) {
            coord[axis] = j;
            for (int d = 0; d < nd; d++) c[d] = (int32_t)coord[d];
            double v = sc_tensor_at_nd(x, c) - mean;
            var += v * v;
        }
        var /= alen;
        double inv = 1.0 / sqrt(var + eps);
        double sg = 0.0, sgx = 0.0;
        for (int32_t j = 0; j < alen; j++) {
            coord[axis] = j;
            for (int d = 0; d < nd; d++) c[d] = (int32_t)coord[d];
            double g = sc_tensor_at_nd(grad, c);
            double xhat = (sc_tensor_at_nd(x, c) - mean) * inv;
            sg += g; sgx += g * xhat;
        }
        sg /= alen; sgx /= alen;
        for (int32_t j = 0; j < alen; j++) {
            coord[axis] = j;
            for (int d = 0; d < nd; d++) c[d] = (int32_t)coord[d];
            double g = sc_tensor_at_nd(grad, c);
            double xhat = (sc_tensor_at_nd(x, c) - mean) * inv;
            sc_tensor_set_nd(r, c, inv * (g - sg - xhat * sgx));
        }
        coord[axis] = 0;
        for (int d = nd - 1; d >= 0; d--) {
            if (d == axis) continue;
            if (++coord[d] < x->shape[d]) break;
            coord[d] = 0;
        }
    }
    return r;
}

/* conv2d 反向（对输入）：dX[n,ci,iy,ix] += Σ_{co,ky,kx} grad[n,co,oy,ox]*w[co,ci,ky,kx]。
   x 仅用于形状 [N,Cin,H,W]；grad 为输出梯度 [N,Cout,Hout,Wout]。 */
sc_tensor *sc_tensor_conv2d_backward_input(sc_tensor *grad, sc_tensor *x, sc_tensor *w,
        int32_t stride_h, int32_t stride_w, int32_t pad_h, int32_t pad_w) {
    if (!grad || !x || !w || x->ndim != 4 || w->ndim != 4 || grad->ndim != 4) return NULL;
    int32_t N = x->shape[0], Cin = x->shape[1], H = x->shape[2], W = x->shape[3];
    int32_t Cout = w->shape[0], Kh = w->shape[2], Kw = w->shape[3];
    int32_t Hout = grad->shape[2], Wout = grad->shape[3];
    sc_tensor *r = new_contig(4, x->shape, x->dtype, 1);
    if (!r) return NULL;
    int32_t cg[4], cw[4], cr[4];
    for (int32_t n = 0; n < N; n++)
        for (int32_t co = 0; co < Cout; co++)
            for (int32_t oy = 0; oy < Hout; oy++)
                for (int32_t ox = 0; ox < Wout; ox++) {
                    cg[0] = n; cg[1] = co; cg[2] = oy; cg[3] = ox;
                    double gv = sc_tensor_at_nd(grad, cg);
                    if (gv == 0.0) continue;
                    int32_t by = oy * stride_h - pad_h, bx = ox * stride_w - pad_w;
                    for (int32_t ci = 0; ci < Cin; ci++)
                        for (int32_t ky = 0; ky < Kh; ky++)
                            for (int32_t kx = 0; kx < Kw; kx++) {
                                int32_t iy = by + ky, ix = bx + kx;
                                if (iy < 0 || iy >= H || ix < 0 || ix >= W) continue;
                                cw[0] = co; cw[1] = ci; cw[2] = ky; cw[3] = kx;
                                cr[0] = n; cr[1] = ci; cr[2] = iy; cr[3] = ix;
                                sc_tensor_set_nd(r, cr, sc_tensor_at_nd(r, cr) + gv * sc_tensor_at_nd(w, cw));
                            }
                }
    return r;
}

/* conv2d 反向（对权重）：dW[co,ci,ky,kx] += Σ_{n,oy,ox} grad[n,co,oy,ox]*x[n,ci,iy,ix]。w 仅用于形状。 */
sc_tensor *sc_tensor_conv2d_backward_weight(sc_tensor *grad, sc_tensor *x, sc_tensor *w,
        int32_t stride_h, int32_t stride_w, int32_t pad_h, int32_t pad_w) {
    if (!grad || !x || !w || x->ndim != 4 || w->ndim != 4 || grad->ndim != 4) return NULL;
    int32_t N = x->shape[0], Cin = x->shape[1], H = x->shape[2], W = x->shape[3];
    int32_t Cout = w->shape[0], Kh = w->shape[2], Kw = w->shape[3];
    int32_t Hout = grad->shape[2], Wout = grad->shape[3];
    sc_tensor *r = new_contig(4, w->shape, w->dtype, 1);
    if (!r) return NULL;
    int32_t cg[4], cx[4], cr[4];
    for (int32_t n = 0; n < N; n++)
        for (int32_t co = 0; co < Cout; co++)
            for (int32_t oy = 0; oy < Hout; oy++)
                for (int32_t ox = 0; ox < Wout; ox++) {
                    cg[0] = n; cg[1] = co; cg[2] = oy; cg[3] = ox;
                    double gv = sc_tensor_at_nd(grad, cg);
                    if (gv == 0.0) continue;
                    int32_t by = oy * stride_h - pad_h, bx = ox * stride_w - pad_w;
                    for (int32_t ci = 0; ci < Cin; ci++)
                        for (int32_t ky = 0; ky < Kh; ky++)
                            for (int32_t kx = 0; kx < Kw; kx++) {
                                int32_t iy = by + ky, ix = bx + kx;
                                if (iy < 0 || iy >= H || ix < 0 || ix >= W) continue;
                                cx[0] = n; cx[1] = ci; cx[2] = iy; cx[3] = ix;
                                cr[0] = co; cr[1] = ci; cr[2] = ky; cr[3] = kx;
                                sc_tensor_set_nd(r, cr, sc_tensor_at_nd(r, cr) + gv * sc_tensor_at_nd(x, cx));
                            }
                }
    return r;
}

/* conv2d 反向（对偏置）：dB[co] = Σ_{n,oy,ox} grad[n,co,oy,ox]。 */
sc_tensor *sc_tensor_conv2d_backward_bias(sc_tensor *grad) {
    if (!grad || grad->ndim != 4) return NULL;
    int32_t N = grad->shape[0], Cout = grad->shape[1], Hout = grad->shape[2], Wout = grad->shape[3];
    sc_tensor *r = new_contig(1, &Cout, grad->dtype, 1);
    if (!r) return NULL;
    int32_t cg[4];
    for (int32_t co = 0; co < Cout; co++) {
        double s = 0.0;
        for (int32_t n = 0; n < N; n++)
            for (int32_t oy = 0; oy < Hout; oy++)
                for (int32_t ox = 0; ox < Wout; ox++) {
                    cg[0] = n; cg[1] = co; cg[2] = oy; cg[3] = ox;
                    s += sc_tensor_at_nd(grad, cg);
                }
        sc_tensor_set_at(r, co, s);
    }
    return r;
}

/* max_pool2d 反向：把 grad 路由到各窗口的 argmax 位置（首个最大，和前向一致）。x 为前向输入。 */
sc_tensor *sc_tensor_max_pool2d_backward(sc_tensor *grad, sc_tensor *x,
        int32_t kh, int32_t kw, int32_t sh, int32_t sw, int32_t ph, int32_t pw) {
    if (!grad || !x || x->ndim != 4 || grad->ndim != 4) return NULL;
    int32_t N = x->shape[0], C = x->shape[1], H = x->shape[2], W = x->shape[3];
    int32_t Hout = grad->shape[2], Wout = grad->shape[3];
    sc_tensor *r = new_contig(4, x->shape, x->dtype, 1);
    if (!r) return NULL;
    int32_t cx[4], cg[4], cr[4];
    for (int32_t n = 0; n < N; n++)
        for (int32_t c = 0; c < C; c++)
            for (int32_t oy = 0; oy < Hout; oy++)
                for (int32_t ox = 0; ox < Wout; ox++) {
                    int32_t by = oy * sh - ph, bx = ox * sw - pw;
                    double m = -1e308; int32_t my = -1, mx = -1;
                    for (int32_t ky = 0; ky < kh; ky++)
                        for (int32_t kx = 0; kx < kw; kx++) {
                            int32_t iy = by + ky, ix = bx + kx;
                            if (iy < 0 || iy >= H || ix < 0 || ix >= W) continue;
                            cx[0] = n; cx[1] = c; cx[2] = iy; cx[3] = ix;
                            double v = sc_tensor_at_nd(x, cx);
                            if (v > m) { m = v; my = iy; mx = ix; }
                        }
                    if (my < 0) continue;
                    cg[0] = n; cg[1] = c; cg[2] = oy; cg[3] = ox;
                    cr[0] = n; cr[1] = c; cr[2] = my; cr[3] = mx;
                    sc_tensor_set_nd(r, cr, sc_tensor_at_nd(r, cr) + sc_tensor_at_nd(grad, cg));
                }
    return r;
}

/* avg_pool2d 反向：把每个窗口的 grad 均分回窗口内有效位置（除数=有效计数，和前向一致）。x 为前向输入。 */
sc_tensor *sc_tensor_avg_pool2d_backward(sc_tensor *grad, sc_tensor *x,
        int32_t kh, int32_t kw, int32_t sh, int32_t sw, int32_t ph, int32_t pw) {
    if (!grad || !x || x->ndim != 4 || grad->ndim != 4) return NULL;
    int32_t N = x->shape[0], C = x->shape[1], H = x->shape[2], W = x->shape[3];
    int32_t Hout = grad->shape[2], Wout = grad->shape[3];
    sc_tensor *r = new_contig(4, x->shape, x->dtype, 1);
    if (!r) return NULL;
    int32_t cg[4], cr[4];
    for (int32_t n = 0; n < N; n++)
        for (int32_t c = 0; c < C; c++)
            for (int32_t oy = 0; oy < Hout; oy++)
                for (int32_t ox = 0; ox < Wout; ox++) {
                    int32_t by = oy * sh - ph, bx = ox * sw - pw;
                    int32_t cnt = 0;
                    for (int32_t ky = 0; ky < kh; ky++)
                        for (int32_t kx = 0; kx < kw; kx++) {
                            int32_t iy = by + ky, ix = bx + kx;
                            if (iy < 0 || iy >= H || ix < 0 || ix >= W) continue;
                            cnt++;
                        }
                    if (cnt == 0) continue;
                    cg[0] = n; cg[1] = c; cg[2] = oy; cg[3] = ox;
                    double g = sc_tensor_at_nd(grad, cg) / cnt;
                    for (int32_t ky = 0; ky < kh; ky++)
                        for (int32_t kx = 0; kx < kw; kx++) {
                            int32_t iy = by + ky, ix = bx + kx;
                            if (iy < 0 || iy >= H || ix < 0 || ix >= W) continue;
                            cr[0] = n; cr[1] = c; cr[2] = iy; cr[3] = ix;
                            sc_tensor_set_nd(r, cr, sc_tensor_at_nd(r, cr) + g);
                        }
                }
    return r;
}

/* embedding 前向：weight[V,D] + idx[...]（整型类标，浮点存储按 round 取整）→ out[...,D]。 */
sc_tensor *sc_tensor_embedding(sc_tensor *weight, sc_tensor *idx) {
    if (!weight || !idx || weight->ndim != 2 || idx->ndim < 1) return NULL;
    int32_t V = weight->shape[0], D = weight->shape[1];
    int ind = idx->ndim;
    int32_t osh[TS_MAXD];
    for (int i = 0; i < ind; i++) osh[i] = idx->shape[i];
    osh[ind] = D;
    sc_tensor *r = new_contig(ind + 1, osh, weight->dtype, 0);
    if (!r) return NULL;
    int64_t n = idx->numel;
    for (int64_t i = 0; i < n; i++) {
        int64_t id = (int64_t)(tel_get(idx, i) + 0.5);
        if (id < 0) id = 0; else if (id >= V) id = V - 1;
        for (int32_t d = 0; d < D; d++)
            tel_set(r, i * D + d, tel_get(weight, id * D + d));
    }
    return r;
}

/* embedding 反向：dW[V,D] scatter-add；grad 形状 [...,D]，idx 同前向，V 为词表大小。 */
sc_tensor *sc_tensor_embedding_backward(sc_tensor *grad, sc_tensor *idx, int32_t V) {
    if (!grad || !idx || grad->ndim < 1) return NULL;
    int32_t D = grad->shape[grad->ndim - 1];
    int32_t wsh[2]; wsh[0] = V; wsh[1] = D;
    sc_tensor *r = new_contig(2, wsh, grad->dtype, 1);
    if (!r) return NULL;
    int64_t n = idx->numel;
    for (int64_t i = 0; i < n; i++) {
        int64_t id = (int64_t)(tel_get(idx, i) + 0.5);
        if (id < 0) id = 0; else if (id >= V) id = V - 1;
        for (int32_t d = 0; d < D; d++)
            tel_set(r, id * D + d, tel_get(r, id * D + d) + tel_get(grad, i * D + d));
    }
    return r;
}

/* batch_norm 前向（训练，无仿射）：通道=dim1，对每通道在其余所有位置统计 μ/σ²。 */
sc_tensor *sc_tensor_batch_norm(sc_tensor *x, double eps) {
    if (!x || x->ndim < 2) return NULL;
    int32_t C = x->shape[1];
    int64_t post = 1;
    for (int d = 2; d < x->ndim; d++) post *= x->shape[d];
    int64_t M = x->numel / C;               /* 每通道元素数 */
    if (M <= 0) return materialize(x);
    sc_tensor *r = new_contig(x->ndim, x->shape, x->dtype, 0);
    if (!r) return NULL;
    double *mean = (double *)sc_alloc((size_t)C * sizeof(double));
    double *inv = (double *)sc_alloc((size_t)C * sizeof(double));
    if (!mean || !inv) { if (mean) sc_free(mean); if (inv) sc_free(inv); sc_tensor_drop(r); sc_free(r); return NULL; }
    for (int32_t c = 0; c < C; c++) { mean[c] = 0.0; inv[c] = 0.0; }
    for (int64_t i = 0; i < x->numel; i++) { int32_t c = (int32_t)((i / post) % C); mean[c] += tel_get(x, i); }
    for (int32_t c = 0; c < C; c++) mean[c] /= (double)M;
    for (int64_t i = 0; i < x->numel; i++) {
        int32_t c = (int32_t)((i / post) % C);
        double v = tel_get(x, i) - mean[c]; inv[c] += v * v;
    }
    for (int32_t c = 0; c < C; c++) inv[c] = 1.0 / sqrt(inv[c] / (double)M + eps);
    for (int64_t i = 0; i < x->numel; i++) {
        int32_t c = (int32_t)((i / post) % C);
        tel_set(r, i, (tel_get(x, i) - mean[c]) * inv[c]);
    }
    sc_free(mean); sc_free(inv);
    return r;
}

/* batch_norm 反向（无仿射）：每通道组 M 元素，
   dx_i = inv_c*(g_i - mean_c(g) - xhat_i*mean_c(g*xhat))。x 为前向输入。 */
sc_tensor *sc_tensor_batch_norm_backward(sc_tensor *grad, sc_tensor *x, double eps) {
    if (!grad || !x || x->ndim < 2) return NULL;
    int32_t C = x->shape[1];
    int64_t post = 1;
    for (int d = 2; d < x->ndim; d++) post *= x->shape[d];
    int64_t M = x->numel / C;
    if (M <= 0) return new_contig(x->ndim, x->shape, x->dtype, 1);
    sc_tensor *r = new_contig(x->ndim, x->shape, x->dtype, 0);
    if (!r) return NULL;
    size_t cb = (size_t)C * sizeof(double);
    double *mean = (double *)sc_alloc(cb), *inv = (double *)sc_alloc(cb);
    double *sg = (double *)sc_alloc(cb), *sgx = (double *)sc_alloc(cb);
    if (!mean || !inv || !sg || !sgx) {
        if (mean) sc_free(mean); if (inv) sc_free(inv); if (sg) sc_free(sg); if (sgx) sc_free(sgx);
        sc_tensor_drop(r); sc_free(r); return NULL;
    }
    for (int32_t c = 0; c < C; c++) { mean[c] = inv[c] = sg[c] = sgx[c] = 0.0; }
    for (int64_t i = 0; i < x->numel; i++) { int32_t c = (int32_t)((i / post) % C); mean[c] += tel_get(x, i); }
    for (int32_t c = 0; c < C; c++) mean[c] /= (double)M;
    for (int64_t i = 0; i < x->numel; i++) {
        int32_t c = (int32_t)((i / post) % C);
        double v = tel_get(x, i) - mean[c]; inv[c] += v * v;
    }
    for (int32_t c = 0; c < C; c++) inv[c] = 1.0 / sqrt(inv[c] / (double)M + eps);
    for (int64_t i = 0; i < x->numel; i++) {
        int32_t c = (int32_t)((i / post) % C);
        double g = tel_get(grad, i);
        double xhat = (tel_get(x, i) - mean[c]) * inv[c];
        sg[c] += g; sgx[c] += g * xhat;
    }
    for (int32_t c = 0; c < C; c++) { sg[c] /= (double)M; sgx[c] /= (double)M; }
    for (int64_t i = 0; i < x->numel; i++) {
        int32_t c = (int32_t)((i / post) % C);
        double g = tel_get(grad, i);
        double xhat = (tel_get(x, i) - mean[c]) * inv[c];
        tel_set(r, i, inv[c] * (g - sg[c] - xhat * sgx[c]));
    }
    sc_free(mean); sc_free(inv); sc_free(sg); sc_free(sgx);
    return r;
}

/* rms_norm 前向（无仿射）：沿 axis，y_i = x_i / sqrt(mean(x²)+eps)。 */
sc_tensor *sc_tensor_rms_norm(sc_tensor *x, int32_t axis, double eps) {
    if (!x || x->ndim <= 0) return NULL;
    int nd = x->ndim;
    if (axis < 0) axis += nd;
    if (axis < 0 || axis >= nd) return NULL;
    int32_t alen = x->shape[axis];
    sc_tensor *r = new_contig(nd, x->shape, x->dtype, 0);
    if (!r) return NULL;
    if (alen <= 0) return r;
    int64_t outer = x->numel / alen;
    int64_t coord[TS_MAXD];
    for (int d = 0; d < nd; d++) coord[d] = 0;
    for (int64_t oi = 0; oi < outer; oi++) {
        int32_t c[TS_MAXD];
        double ms = 0.0;
        for (int32_t j = 0; j < alen; j++) {
            coord[axis] = j;
            for (int d = 0; d < nd; d++) c[d] = (int32_t)coord[d];
            double v = sc_tensor_at_nd(x, c); ms += v * v;
        }
        double inv = 1.0 / sqrt(ms / alen + eps);
        for (int32_t j = 0; j < alen; j++) {
            coord[axis] = j;
            for (int d = 0; d < nd; d++) c[d] = (int32_t)coord[d];
            sc_tensor_set_nd(r, c, sc_tensor_at_nd(x, c) * inv);
        }
        coord[axis] = 0;
        for (int d = nd - 1; d >= 0; d--) {
            if (d == axis) continue;
            if (++coord[d] < x->shape[d]) break;
            coord[d] = 0;
        }
    }
    return r;
}

/* rms_norm 反向（无仿射）：inv=(ms+eps)^(-1/2)，ms=mean(x²)，N=alen，sgx=Σ g_k x_k。
   dx_i = inv*g_i - inv³*x_i*sgx/N。x 为前向输入。 */
sc_tensor *sc_tensor_rms_norm_backward(sc_tensor *grad, sc_tensor *x, int32_t axis, double eps) {
    if (!grad || !x || x->ndim <= 0) return NULL;
    int nd = x->ndim;
    if (axis < 0) axis += nd;
    if (axis < 0 || axis >= nd) return NULL;
    int32_t alen = x->shape[axis];
    sc_tensor *r = new_contig(nd, x->shape, x->dtype, 0);
    if (!r) return NULL;
    if (alen <= 0) return r;
    int64_t outer = x->numel / alen;
    int64_t coord[TS_MAXD];
    for (int d = 0; d < nd; d++) coord[d] = 0;
    for (int64_t oi = 0; oi < outer; oi++) {
        int32_t c[TS_MAXD];
        double ms = 0.0, sgx = 0.0;
        for (int32_t j = 0; j < alen; j++) {
            coord[axis] = j;
            for (int d = 0; d < nd; d++) c[d] = (int32_t)coord[d];
            double v = sc_tensor_at_nd(x, c); ms += v * v; sgx += sc_tensor_at_nd(grad, c) * v;
        }
        double inv = 1.0 / sqrt(ms / alen + eps);
        double inv3 = inv * inv * inv;
        for (int32_t j = 0; j < alen; j++) {
            coord[axis] = j;
            for (int d = 0; d < nd; d++) c[d] = (int32_t)coord[d];
            double xv = sc_tensor_at_nd(x, c);
            sc_tensor_set_nd(r, c, inv * sc_tensor_at_nd(grad, c) - inv3 * xv * sgx / alen);
        }
        coord[axis] = 0;
        for (int d = nd - 1; d >= 0; d--) {
            if (d == axis) continue;
            if (++coord[d] < x->shape[d]) break;
            coord[d] = 0;
        }
    }
    return r;
}

sc_tensor *sc_tensor_layer_norm(sc_tensor *_this, int32_t axis, double eps) {
    if (!_this || _this->ndim <= 0) return NULL;
    int nd = _this->ndim;
    if (axis < 0) axis += nd;
    if (axis < 0 || axis >= nd) return NULL;
    int32_t alen = _this->shape[axis];
    if (alen <= 0) return materialize(_this);

    sc_tensor *r = new_contig(nd, _this->shape, _this->dtype, 0);
    if (!r) return NULL;

    int64_t outer = _this->numel / alen;
    int64_t coord[TS_MAXD];
    for (int d = 0; d < nd; d++) coord[d] = 0;

    for (int64_t oi = 0; oi < outer; oi++) {
        double mean = 0.0;
        for (int32_t j = 0; j < alen; j++) {
            coord[axis] = j;
            int32_t c[TS_MAXD];
            for (int d = 0; d < nd; d++) c[d] = (int32_t)coord[d];
            mean += sc_tensor_at_nd(_this, c);
        }
        mean /= alen;

        double var = 0.0;
        for (int32_t j = 0; j < alen; j++) {
            coord[axis] = j;
            int32_t c[TS_MAXD];
            for (int d = 0; d < nd; d++) c[d] = (int32_t)coord[d];
            double v = sc_tensor_at_nd(_this, c) - mean;
            var += v * v;
        }
        var /= alen;
        double inv = 1.0 / sqrt(var + eps);

        for (int32_t j = 0; j < alen; j++) {
            coord[axis] = j;
            int32_t c[TS_MAXD];
            for (int d = 0; d < nd; d++) c[d] = (int32_t)coord[d];
            double v = sc_tensor_at_nd(_this, c);
            sc_tensor_set_nd(r, c, (v - mean) * inv);
        }

        /* 迭代“除 axis 外”的坐标（axis 维固定为 0）。 */
        coord[axis] = 0;
        for (int d = nd - 1; d >= 0; d--) {
            if (d == axis) continue;
            if (++coord[d] < _this->shape[d]) break;
            coord[d] = 0;
        }
    }
    return r;
}

sc_tensor *sc_tensor_dropout(sc_tensor *_this, double p, bool train) {
    if (!_this) return NULL;
    if (!train || p <= 0.0) return materialize(_this);
    if (p >= 1.0) {
        sc_tensor *z = new_contig(_this->ndim, _this->shape, _this->dtype, 1);
        return z;
    }
    double scale = 1.0 / (1.0 - p);
    sc_tensor *r = new_contig(_this->ndim, _this->shape, _this->dtype, 0);
    if (!r) return NULL;
    for (int64_t i = 0; i < _this->numel; i++) {
        double keep = rng_double() >= p ? 1.0 : 0.0;
        tel_set(r, i, keep ? tel_get(_this, i) * scale : 0.0);
    }
    return r;
}

/* dropout 掩码（训练用，供 autograd 保存以保证前/反向一致）：保留→1/(1-p)，丢弃→0。 */
sc_tensor *sc_tensor_dropout_mask(sc_tensor *_this, double p) {
    if (!_this) return NULL;
    sc_tensor *m = new_contig(_this->ndim, _this->shape, _this->dtype, 0);
    if (!m) return NULL;
    double scale = (p < 1.0) ? 1.0 / (1.0 - p) : 0.0;
    for (int64_t i = 0; i < _this->numel; i++)
        tel_set(m, i, rng_double() >= p ? scale : 0.0);
    return m;
}

/* scaled dot-product attention（前向）：
 * Q:[B,T,D], K:[B,S,D], V:[B,S,Dv] -> O:[B,T,Dv]
 * causal=1 时，对每个 t 屏蔽 s>t。 */
sc_tensor *sc_tensor_sdpa(sc_tensor *_this, sc_tensor *k, sc_tensor *v, bool causal) {
    if (!_this || !k || !v) return NULL;
    if (_this->ndim != 3 || k->ndim != 3 || v->ndim != 3) return NULL;
    int32_t B = _this->shape[0], T = _this->shape[1], D = _this->shape[2];
    int32_t Bk = k->shape[0], S = k->shape[1], Dk = k->shape[2];
    int32_t Bv = v->shape[0], Sv = v->shape[1], Dv = v->shape[2];
    if (B != Bk || B != Bv || D != Dk || S != Sv) return NULL;
    if (B <= 0 || T <= 0 || D <= 0 || S <= 0 || Dv <= 0) return NULL;

    int32_t osh[3]; osh[0] = B; osh[1] = T; osh[2] = Dv;
    sc_tensor *out = new_contig(3, osh, _this->dtype, 0);
    if (!out) return NULL;

    double *score = (double *)sc_alloc((size_t)S * sizeof(double));
    if (!score) { sc_tensor_drop(out); sc_free(out); return NULL; }
    double scale = 1.0 / sqrt((double)D);

    int32_t cq[3], ck[3], cv[3], co[3];
    for (int32_t b = 0; b < B; b++) {
        for (int32_t t = 0; t < T; t++) {
            double mx = -1e308;
            for (int32_t s = 0; s < S; s++) {
                if (causal && s > t) { score[s] = -1e308; continue; }
                double dot = 0.0;
                for (int32_t d = 0; d < D; d++) {
                    cq[0] = b; cq[1] = t; cq[2] = d;
                    ck[0] = b; ck[1] = s; ck[2] = d;
                    dot += sc_tensor_at_nd(_this, cq) * sc_tensor_at_nd(k, ck);
                }
                score[s] = dot * scale;
                if (score[s] > mx) mx = score[s];
            }

            double denom = 0.0;
            for (int32_t s = 0; s < S; s++) {
                if (score[s] <= -1e307) continue;
                denom += exp(score[s] - mx);
            }
            if (denom <= 0.0) denom = 1.0;

            for (int32_t dv = 0; dv < Dv; dv++) {
                double acc = 0.0;
                for (int32_t s = 0; s < S; s++) {
                    if (score[s] <= -1e307) continue;
                    double w = exp(score[s] - mx) / denom;
                    cv[0] = b; cv[1] = s; cv[2] = dv;
                    acc += w * sc_tensor_at_nd(v, cv);
                }
                co[0] = b; co[1] = t; co[2] = dv;
                sc_tensor_set_nd(out, co, acc);
            }
        }
    }

    sc_free(score);
    return out;
}

sc_tensor *sc_tensor_conv1d(sc_tensor *_this, sc_tensor *w, sc_tensor *bias, int32_t stride, int32_t padding) {
    if (!_this || !w || !bias) return NULL;
    if (_this->ndim != 3 || w->ndim != 3 || bias->ndim != 1) return NULL;
    if (stride <= 0 || padding < 0) return NULL;
    int32_t N = _this->shape[0], Cin = _this->shape[1], L = _this->shape[2];
    int32_t Cout = w->shape[0], Cin2 = w->shape[1], K = w->shape[2];
    if (Cin != Cin2 || bias->shape[0] != Cout || K <= 0) return NULL;
    int32_t Lout = (L + 2 * padding - K) / stride + 1;
    if (Lout <= 0) return NULL;
    int32_t sh[3]; sh[0] = N; sh[1] = Cout; sh[2] = Lout;
    sc_tensor *r = new_contig(3, sh, _this->dtype, 0);
    if (!r) return NULL;
    int32_t cx[3], cw[3], cr[3];
    for (int32_t n = 0; n < N; n++)
        for (int32_t co = 0; co < Cout; co++)
            for (int32_t xo = 0; xo < Lout; xo++) {
                double acc = tel_get(bias, co);
                int32_t base = xo * stride - padding;
                for (int32_t ci = 0; ci < Cin; ci++)
                    for (int32_t kx = 0; kx < K; kx++) {
                        int32_t xi = base + kx;
                        if (xi < 0 || xi >= L) continue;
                        cx[0] = n; cx[1] = ci; cx[2] = xi;
                        cw[0] = co; cw[1] = ci; cw[2] = kx;
                        acc += sc_tensor_at_nd(_this, cx) * sc_tensor_at_nd(w, cw);
                    }
                cr[0] = n; cr[1] = co; cr[2] = xo;
                sc_tensor_set_nd(r, cr, acc);
            }
    return r;
}
    
sc_tensor *sc_tensor_conv2d(sc_tensor *_this, sc_tensor *w, sc_tensor *bias, int32_t stride_h, int32_t stride_w, int32_t pad_h, int32_t pad_w) {
    if (!_this || !w || !bias) return NULL;
    if (_this->ndim != 4 || w->ndim != 4 || bias->ndim != 1) return NULL;
    if (stride_h <= 0 || stride_w <= 0 || pad_h < 0 || pad_w < 0) return NULL;
    int32_t N = _this->shape[0], Cin = _this->shape[1], H = _this->shape[2], W = _this->shape[3];
    int32_t Cout = w->shape[0], Cin2 = w->shape[1], Kh = w->shape[2], Kw = w->shape[3];
    if (Cin != Cin2 || bias->shape[0] != Cout || Kh <= 0 || Kw <= 0) return NULL;
    int32_t Hout = (H + 2 * pad_h - Kh) / stride_h + 1;
    int32_t Wout = (W + 2 * pad_w - Kw) / stride_w + 1;
    if (Hout <= 0 || Wout <= 0) return NULL;
    int32_t sh[4]; sh[0] = N; sh[1] = Cout; sh[2] = Hout; sh[3] = Wout;
    sc_tensor *r = new_contig(4, sh, _this->dtype, 0);
    if (!r) return NULL;
    int32_t cx[4], cw[4], cr[4];
    for (int32_t n = 0; n < N; n++)
        for (int32_t co = 0; co < Cout; co++)
            for (int32_t oy = 0; oy < Hout; oy++)
                for (int32_t ox = 0; ox < Wout; ox++) {
                    double acc = tel_get(bias, co);
                    int32_t by = oy * stride_h - pad_h;
                    int32_t bx = ox * stride_w - pad_w;
                    for (int32_t ci = 0; ci < Cin; ci++)
                        for (int32_t ky = 0; ky < Kh; ky++)
                            for (int32_t kx = 0; kx < Kw; kx++) {
                                int32_t iy = by + ky, ix = bx + kx;
                                if (iy < 0 || iy >= H || ix < 0 || ix >= W) continue;
                                cx[0] = n; cx[1] = ci; cx[2] = iy; cx[3] = ix;
                                cw[0] = co; cw[1] = ci; cw[2] = ky; cw[3] = kx;
                                acc += sc_tensor_at_nd(_this, cx) * sc_tensor_at_nd(w, cw);
                            }
                    cr[0] = n; cr[1] = co; cr[2] = oy; cr[3] = ox;
                    sc_tensor_set_nd(r, cr, acc);
                }
    return r;
}

sc_tensor *sc_tensor_max_pool1d(sc_tensor *_this, int32_t kernel, int32_t stride, int32_t padding) {
    if (!_this || _this->ndim != 3 || kernel <= 0 || stride <= 0 || padding < 0) return NULL;
    int32_t N = _this->shape[0], C = _this->shape[1], L = _this->shape[2];
    int32_t Lout = (L + 2 * padding - kernel) / stride + 1;
    if (Lout <= 0) return NULL;
    int32_t sh[3]; sh[0] = N; sh[1] = C; sh[2] = Lout;
    sc_tensor *r = new_contig(3, sh, _this->dtype, 0);
    if (!r) return NULL;
    int32_t cx[3], cr[3];
    for (int32_t n = 0; n < N; n++)
        for (int32_t c = 0; c < C; c++)
            for (int32_t o = 0; o < Lout; o++) {
                double m = -1e308;
                int32_t base = o * stride - padding;
                for (int32_t kx = 0; kx < kernel; kx++) {
                    int32_t ix = base + kx;
                    if (ix < 0 || ix >= L) continue;
                    cx[0] = n; cx[1] = c; cx[2] = ix;
                    double v = sc_tensor_at_nd(_this, cx);
                    if (v > m) m = v;
                }
                cr[0] = n; cr[1] = c; cr[2] = o;
                sc_tensor_set_nd(r, cr, m);
            }
    return r;
}

sc_tensor *sc_tensor_avg_pool1d(sc_tensor *_this, int32_t kernel, int32_t stride, int32_t padding) {
    if (!_this || _this->ndim != 3 || kernel <= 0 || stride <= 0 || padding < 0) return NULL;
    int32_t N = _this->shape[0], C = _this->shape[1], L = _this->shape[2];
    int32_t Lout = (L + 2 * padding - kernel) / stride + 1;
    if (Lout <= 0) return NULL;
    int32_t sh[3]; sh[0] = N; sh[1] = C; sh[2] = Lout;
    sc_tensor *r = new_contig(3, sh, _this->dtype, 0);
    if (!r) return NULL;
    int32_t cx[3], cr[3];
    for (int32_t n = 0; n < N; n++)
        for (int32_t c = 0; c < C; c++)
            for (int32_t o = 0; o < Lout; o++) {
                double s = 0.0; int32_t cnt = 0;
                int32_t base = o * stride - padding;
                for (int32_t kx = 0; kx < kernel; kx++) {
                    int32_t ix = base + kx;
                    if (ix < 0 || ix >= L) continue;
                    cx[0] = n; cx[1] = c; cx[2] = ix;
                    s += sc_tensor_at_nd(_this, cx);
                    cnt++;
                }
                if (cnt == 0) cnt = 1;
                cr[0] = n; cr[1] = c; cr[2] = o;
                sc_tensor_set_nd(r, cr, s / cnt);
            }
    return r;
}

sc_tensor *sc_tensor_max_pool2d(sc_tensor *_this, int32_t kh, int32_t kw, int32_t sh, int32_t sw, int32_t ph, int32_t pw) {
    if (!_this || _this->ndim != 4 || kh <= 0 || kw <= 0 || sh <= 0 || sw <= 0 || ph < 0 || pw < 0) return NULL;
    int32_t N = _this->shape[0], C = _this->shape[1], H = _this->shape[2], W = _this->shape[3];
    int32_t Hout = (H + 2 * ph - kh) / sh + 1;
    int32_t Wout = (W + 2 * pw - kw) / sw + 1;
    if (Hout <= 0 || Wout <= 0) return NULL;
    int32_t osh[4]; osh[0] = N; osh[1] = C; osh[2] = Hout; osh[3] = Wout;
    sc_tensor *r = new_contig(4, osh, _this->dtype, 0);
    if (!r) return NULL;
    int32_t cx[4], cr[4];
    for (int32_t n = 0; n < N; n++)
        for (int32_t c = 0; c < C; c++)
            for (int32_t oy = 0; oy < Hout; oy++)
                for (int32_t ox = 0; ox < Wout; ox++) {
                    double m = -1e308;
                    int32_t by = oy * sh - ph, bx = ox * sw - pw;
                    for (int32_t ky = 0; ky < kh; ky++)
                        for (int32_t kx = 0; kx < kw; kx++) {
                            int32_t iy = by + ky, ix = bx + kx;
                            if (iy < 0 || iy >= H || ix < 0 || ix >= W) continue;
                            cx[0] = n; cx[1] = c; cx[2] = iy; cx[3] = ix;
                            double v = sc_tensor_at_nd(_this, cx);
                            if (v > m) m = v;
                        }
                    cr[0] = n; cr[1] = c; cr[2] = oy; cr[3] = ox;
                    sc_tensor_set_nd(r, cr, m);
                }
    return r;
}

sc_tensor *sc_tensor_avg_pool2d(sc_tensor *_this, int32_t kh, int32_t kw, int32_t sh, int32_t sw, int32_t ph, int32_t pw) {
    if (!_this || _this->ndim != 4 || kh <= 0 || kw <= 0 || sh <= 0 || sw <= 0 || ph < 0 || pw < 0) return NULL;
    int32_t N = _this->shape[0], C = _this->shape[1], H = _this->shape[2], W = _this->shape[3];
    int32_t Hout = (H + 2 * ph - kh) / sh + 1;
    int32_t Wout = (W + 2 * pw - kw) / sw + 1;
    if (Hout <= 0 || Wout <= 0) return NULL;
    int32_t osh[4]; osh[0] = N; osh[1] = C; osh[2] = Hout; osh[3] = Wout;
    sc_tensor *r = new_contig(4, osh, _this->dtype, 0);
    if (!r) return NULL;
    int32_t cx[4], cr[4];
    for (int32_t n = 0; n < N; n++)
        for (int32_t c = 0; c < C; c++)
            for (int32_t oy = 0; oy < Hout; oy++)
                for (int32_t ox = 0; ox < Wout; ox++) {
                    double s = 0.0; int32_t cnt = 0;
                    int32_t by = oy * sh - ph, bx = ox * sw - pw;
                    for (int32_t ky = 0; ky < kh; ky++)
                        for (int32_t kx = 0; kx < kw; kx++) {
                            int32_t iy = by + ky, ix = bx + kx;
                            if (iy < 0 || iy >= H || ix < 0 || ix >= W) continue;
                            cx[0] = n; cx[1] = c; cx[2] = iy; cx[3] = ix;
                            s += sc_tensor_at_nd(_this, cx);
                            cnt++;
                        }
                    if (cnt == 0) cnt = 1;
                    cr[0] = n; cr[1] = c; cr[2] = oy; cr[3] = ox;
                    sc_tensor_set_nd(r, cr, s / cnt);
                }
    return r;
}

/* ============================================================
 * 15. 拼接 / 堆叠 / 拆分 / 平铺
 * ============================================================ */

sc_tensor *sc_tensor_concat(void *arr_, int32_t n, int32_t axis) {
    sc_tensor **arr = (sc_tensor **)arr_;
    if (!arr || n <= 0) return NULL;
    sc_tensor *a0 = arr[0];
    if (axis < 0) axis += a0->ndim;
    if (axis < 0 || axis >= a0->ndim) return NULL;
    int32_t rshape[TS_MAXD];
    for (int i = 0; i < a0->ndim; i++) rshape[i] = a0->shape[i];
    int32_t axsum = 0;
    for (int32_t t = 0; t < n; t++) {
        if (arr[t]->ndim != a0->ndim) return NULL;
        for (int i = 0; i < a0->ndim; i++)
            if (i != axis && arr[t]->shape[i] != a0->shape[i]) return NULL;
        axsum += arr[t]->shape[axis];
    }
    rshape[axis] = axsum;
    sc_tensor *r = new_contig(a0->ndim, rshape, a0->dtype, 0);
    if (!r) return NULL;
    /* 沿 axis 逐张量拷贝：对每个张量遍历其逻辑元素，映射到 r 的坐标。 */
    int32_t off_axis = 0;
    for (int32_t t = 0; t < n; t++) {
        sc_tensor *s = arr[t];
        int64_t coord[TS_MAXD]; for (int i = 0; i < s->ndim; i++) coord[i] = 0;
        for (int64_t i = 0; i < s->numel; i++) {
            int64_t ro = 0, mul = 1;
            for (int d = r->ndim - 1; d >= 0; d--) {
                int64_t c = (d == axis) ? coord[d] + off_axis : coord[d];
                ro += c * mul; mul *= r->shape[d];
            }
            tel_set(r, ro, tel_get(s, i));
            for (int d = s->ndim - 1; d >= 0; d--) { if (++coord[d] < s->shape[d]) break; coord[d] = 0; }
        }
        off_axis += s->shape[axis];
    }
    return r;
}

sc_tensor *sc_tensor_stack(void *arr_, int32_t n, int32_t axis) {
    sc_tensor **arr = (sc_tensor **)arr_;
    if (!arr || n <= 0) return NULL;
    sc_tensor *a0 = arr[0];
    int nd = a0->ndim + 1;
    if (axis < 0) axis += nd;
    if (axis < 0 || axis >= nd) return NULL;
    int32_t rshape[TS_MAXD]; int rn = 0;
    for (int i = 0; i < nd; i++) {
        if (i == axis) rshape[rn++] = n;
        else rshape[rn++] = a0->shape[i < axis ? i : i - 1];
    }
    sc_tensor *r = new_contig(nd, rshape, a0->dtype, 0);
    if (!r) return NULL;
    for (int32_t t = 0; t < n; t++) {
        sc_tensor *s = arr[t];
        if (s->ndim != a0->ndim || s->numel != a0->numel) { sc_tensor_drop(r); sc_free(r); return NULL; }
        int64_t coord[TS_MAXD]; for (int i = 0; i < s->ndim; i++) coord[i] = 0;
        for (int64_t i = 0; i < s->numel; i++) {
            int64_t ro = 0, mul = 1;
            int sd = s->ndim - 1;
            for (int d = nd - 1; d >= 0; d--) {
                int64_t c = (d == axis) ? t : coord[sd--];
                ro += c * mul; mul *= r->shape[d];
            }
            tel_set(r, ro, tel_get(s, i));
            for (int d = s->ndim - 1; d >= 0; d--) { if (++coord[d] < s->shape[d]) break; coord[d] = 0; }
        }
    }
    return r;
}

int32_t sc_tensor_split(sc_tensor *_this, int32_t parts, int32_t axis, sc_tensor **out) {
    if (parts <= 0 || !out) return 0;
    if (axis < 0) axis += _this->ndim;
    if (axis < 0 || axis >= _this->ndim) return 0;
    int32_t total = _this->shape[axis];
    int32_t base = total / parts, rem = total % parts;
    int32_t start = 0, cnt = 0;
    for (int32_t p = 0; p < parts; p++) {
        int32_t len = base + (p < rem ? 1 : 0);
        out[cnt++] = sc_tensor_narrow(_this, axis, start, len);
        start += len;
    }
    return cnt;
}

sc_tensor *sc_tensor_tile(sc_tensor *_this, int32_t reps) {
    if (reps <= 0) return NULL;
    int32_t rshape[TS_MAXD];
    for (int i = 0; i < _this->ndim; i++) rshape[i] = _this->shape[i];
    rshape[0] = _this->shape[0] * reps;
    sc_tensor *r = new_contig(_this->ndim, rshape, _this->dtype, 0);
    if (!r) return NULL;
    int64_t block = _this->numel;
    for (int32_t t = 0; t < reps; t++)
        for (int64_t i = 0; i < block; i++) tel_set(r, t * block + i, tel_get(_this, i));
    return r;
}

sc_tensor *sc_tensor_repeat(sc_tensor *_this, int32_t reps, int32_t axis) {
    if (reps <= 0) return NULL;
    if (axis < 0) axis += _this->ndim;
    if (axis < 0 || axis >= _this->ndim) return NULL;
    int32_t rshape[TS_MAXD];
    for (int i = 0; i < _this->ndim; i++) rshape[i] = _this->shape[i];
    rshape[axis] = _this->shape[axis] * reps;
    sc_tensor *r = new_contig(_this->ndim, rshape, _this->dtype, 0);
    if (!r) return NULL;
    int64_t coord[TS_MAXD]; for (int i = 0; i < r->ndim; i++) coord[i] = 0;
    for (int64_t i = 0; i < r->numel; i++) {
        int32_t scoord[TS_MAXD];
        for (int d = 0; d < r->ndim; d++) scoord[d] = (int32_t)coord[d];
        scoord[axis] = (int32_t)(coord[axis] / reps);
        tel_set(r, i, sc_tensor_at_nd(_this, scoord));
        for (int d = r->ndim - 1; d >= 0; d--) { if (++coord[d] < r->shape[d]) break; coord[d] = 0; }
    }
    return r;
}

/* 常量填充：pads[2*ndim]，每维 (before, after)（物化）。 */
sc_tensor *sc_tensor_pad(sc_tensor *_this, int32_t *pads, double value) {
    int32_t rshape[TS_MAXD];
    for (int d = 0; d < _this->ndim; d++)
        rshape[d] = _this->shape[d] + pads[2*d] + pads[2*d+1];
    sc_tensor *r = new_contig(_this->ndim, rshape, _this->dtype, 0);
    if (!r) return NULL;
    for (int64_t i = 0; i < r->numel; i++) tel_set(r, i, value);
    int64_t coord[TS_MAXD]; for (int i = 0; i < _this->ndim; i++) coord[i] = 0;
    for (int64_t i = 0; i < _this->numel; i++) {
        int32_t rcoord[TS_MAXD];
        for (int d = 0; d < _this->ndim; d++) rcoord[d] = (int32_t)coord[d] + pads[2*d];
        sc_tensor_set_nd(r, rcoord, tel_get(_this, i));
        for (int d = _this->ndim - 1; d >= 0; d--) { if (++coord[d] < _this->shape[d]) break; coord[d] = 0; }
    }
    return r;
}

/* 循环位移（axis<0 → 展平后整体滚动；物化）。 */
sc_tensor *sc_tensor_roll(sc_tensor *_this, int64_t shift, int32_t axis) {
    if (axis >= _this->ndim) return NULL;
    sc_tensor *r = new_contig(_this->ndim, _this->shape, _this->dtype, 0);
    if (!r) return NULL;
    if (axis < 0) {
        int64_t n = _this->numel;
        if (n == 0) return r;
        int64_t s = ((shift % n) + n) % n;
        for (int64_t i = 0; i < n; i++) tel_set(r, (i + s) % n, tel_get(_this, i));
        return r;
    }
    int32_t alen = _this->shape[axis];
    if (alen == 0) return r;
    int64_t s = ((shift % alen) + alen) % alen;
    int64_t coord[TS_MAXD]; for (int i = 0; i < _this->ndim; i++) coord[i] = 0;
    for (int64_t i = 0; i < _this->numel; i++) {
        int32_t rcoord[TS_MAXD];
        for (int d = 0; d < _this->ndim; d++) rcoord[d] = (int32_t)coord[d];
        rcoord[axis] = (int32_t)((coord[axis] + s) % alen);
        sc_tensor_set_nd(r, rcoord, tel_get(_this, i));
        for (int d = _this->ndim - 1; d >= 0; d--) { if (++coord[d] < _this->shape[d]) break; coord[d] = 0; }
    }
    return r;
}

/* ============================================================
 * 16. 比较 / 杂项 / 打印
 * ============================================================ */

bool sc_tensor_equal(sc_tensor *_this, sc_tensor *o) {
    if (!sc_tensor_is_same_shape(_this, o)) return 0;
    for (int64_t i = 0; i < _this->numel; i++)
        if (tel_get(_this, i) != tel_get(o, i)) return 0;
    return 1;
}

bool sc_tensor_allclose(sc_tensor *_this, sc_tensor *o, double rtol, double atol) {
    if (!sc_tensor_is_same_shape(_this, o)) return 0;
    for (int64_t i = 0; i < _this->numel; i++) {
        double a = tel_get(_this, i), b = tel_get(o, i);
        if (fabs(a - b) > atol + rtol * fabs(b)) return 0;
    }
    return 1;
}

void sc_tensor_print(sc_tensor *_this) {
    static const char *dn[] = { "f4", "f8", "i4", "i8", "bool" };
    const char *dname = (_this->dtype >= 0 && _this->dtype <= 4) ? dn[_this->dtype] : "?";
    printf("tensor(shape=[");
    for (int i = 0; i < _this->ndim; i++) printf("%s%d", i ? "," : "", _this->shape[i]);
    printf("], dtype=%s, numel=%lld) [", dname, (long long)_this->numel);
    int64_t lim = _this->numel < 64 ? _this->numel : 64;
    for (int64_t i = 0; i < lim; i++) {
        if (_this->dtype == TS_DT_I4 || _this->dtype == TS_DT_I8 || _this->dtype == TS_DT_BOOL)
            printf("%s%lld", i ? ", " : "", (long long)tel_get(_this, i));
        else
            printf("%s%g", i ? ", " : "", tel_get(_this, i));
    }
    if (_this->numel > lim) printf(", ...");
    printf("]\n");
}

/* ============================================================
 * 17. 随机 / 序列化
 * ============================================================ */

/* xorshift128+ 模块级状态 */
static uint64_t rng_s0 = 0x9E3779B97F4A7C15ULL, rng_s1 = 0xBF58476D1CE4E5B9ULL;
static uint64_t rng_next(void) {
    uint64_t x = rng_s0, y = rng_s1;
    rng_s0 = y;
    x ^= x << 23;
    rng_s1 = x ^ y ^ (x >> 17) ^ (y >> 26);
    return rng_s1 + y;
}
static double rng_double(void) {   /* [0,1) */
    return (double)(rng_next() >> 11) * (1.0 / 9007199254740992.0);
}
static double rng_normal_std(void) {   /* Box-Muller，标准正态 */
    double u1 = rng_double(); if (u1 < 1e-300) u1 = 1e-300;
    double u2 = rng_double();
    return sqrt(-2.0 * log(u1)) * cos(6.283185307179586 * u2);
}

void sc_rand_seed(int64_t seed) {
    rng_s0 = (uint64_t)seed ^ 0x9E3779B97F4A7C15ULL;
    rng_s1 = ((uint64_t)seed << 1) ^ 0xBF58476D1CE4E5B9ULL;
    if (rng_s0 == 0 && rng_s1 == 0) rng_s1 = 1;
    for (int i = 0; i < 16; i++) rng_next();   /* 预热 */
}

sc_tensor *sc_rand_uniform(int32_t ndim, int32_t *shape, double lo, double hi, int32_t dtype) {
    sc_tensor *r = new_contig(ndim, shape, dtype, 0);
    if (!r) return NULL;
    for (int64_t i = 0; i < r->numel; i++) tel_set(r, i, lo + (hi - lo) * rng_double());
    return r;
}

sc_tensor *sc_rand_normal(int32_t ndim, int32_t *shape, double mean, double std, int32_t dtype) {
    sc_tensor *r = new_contig(ndim, shape, dtype, 0);
    if (!r) return NULL;
    for (int64_t i = 0; i < r->numel; i++) tel_set(r, i, mean + std * rng_normal_std());
    return r;
}

sc_tensor *sc_rand_randint(int32_t ndim, int32_t *shape, int64_t lo, int64_t hi, int32_t dtype) {
    sc_tensor *r = new_contig(ndim, shape, dtype, 0);
    if (!r) return NULL;
    int64_t range = hi - lo;
    if (range <= 0) range = 1;
    for (int64_t i = 0; i < r->numel; i++)
        tel_set(r, i, (double)(lo + (int64_t)(rng_next() % (uint64_t)range)));
    return r;
}

sc_tensor *sc_permutation(int32_t n, int32_t dtype) {
    int32_t sh = n;
    sc_tensor *r = new_contig(1, &sh, dtype, 0);
    if (!r) return NULL;
    for (int i = 0; i < n; i++) tel_set(r, i, (double)i);
    for (int i = n - 1; i > 0; i--) {
        int j = (int)(rng_next() % (uint64_t)(i + 1));
        double t = tel_get(r, i); tel_set(r, i, tel_get(r, j)); tel_set(r, j, t);
    }
    return r;
}

bool sc_tensor_shuffle_(sc_tensor *_this) {
    if (_this->ndim < 1) return 0;
    int32_t n = _this->shape[0];
    if (n <= 1) return 1;
    int64_t row = _this->numel / n;   /* 每个首维切片的元素数 */
    /* Fisher-Yates 交换首维切片 */
    for (int i = n - 1; i > 0; i--) {
        int j = (int)(rng_next() % (uint64_t)(i + 1));
        if (i == j) continue;
        int32_t ci[TS_MAXD], cj[TS_MAXD];
        for (int64_t e = 0; e < row; e++) {
            /* 把扁平偏移 e 解成除首维外的坐标 */
            int64_t rem = e;
            for (int d = _this->ndim - 1; d >= 1; d--) { ci[d] = (int32_t)(rem % _this->shape[d]); cj[d] = ci[d]; rem /= _this->shape[d]; }
            ci[0] = i; cj[0] = j;
            double vi = sc_tensor_at_nd(_this, ci), vj = sc_tensor_at_nd(_this, cj);
            sc_tensor_set_nd(_this, ci, vj); sc_tensor_set_nd(_this, cj, vi);
        }
    }
    return 1;
}

/* 下三角 1 矩阵 tri(n,m,k)：i >= j-k 处为 1 */
sc_tensor *sc_tri(int32_t n, int32_t m, int32_t k, int32_t dtype) {
    if (m <= 0) m = n;
    int32_t sh[2]; sh[0] = n; sh[1] = m;
    sc_tensor *r = new_contig(2, sh, dtype, 1);
    if (!r) return NULL;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++)
            if (j <= i + k) tel_set(r, (int64_t)i * m + j, 1.0);
    return r;
}

/* meshgrid：N 个 1D 坐标 arr[0..n-1] → N 个 N 维网格 out[0..n-1]。
 * indexing 0=ij（out[k] 沿轴 k 变化，形状 (len0,len1,…,lenN-1)）；
 * indexing 1=xy（仅交换前两轴：形状 (len1,len0,len2,…)，out[0] 沿轴 1、out[1] 沿轴 0、其余沿轴 k）。 */
bool sc_meshgrid(void *arr_, int32_t n, int32_t indexing, void *out_) {
    sc_tensor **arr = (sc_tensor **)arr_;
    sc_tensor **out = (sc_tensor **)out_;
    if (n <= 0 || n > TS_MAXD) return 0;
    int32_t shape[TS_MAXD];     /* 输出网格形状 */
    int axis_of[TS_MAXD];       /* out[k] 的变化轴 */
    for (int k = 0; k < n; k++) {
        if (!arr[k] || arr[k]->ndim != 1) return 0;
    }
    if (indexing == 1 && n >= 2) {
        shape[0] = arr[1]->shape[0];
        shape[1] = arr[0]->shape[0];
        for (int d = 2; d < n; d++) shape[d] = arr[d]->shape[0];
        axis_of[0] = 1; axis_of[1] = 0;
        for (int k = 2; k < n; k++) axis_of[k] = k;
    } else {
        for (int d = 0; d < n; d++) { shape[d] = arr[d]->shape[0]; axis_of[d] = d; }
    }
    for (int k = 0; k < n; k++) out[k] = NULL;
    for (int k = 0; k < n; k++) {
        sc_tensor *g = new_contig(n, shape, arr[k]->dtype, 0);
        if (!g) { for (int j = 0; j < k; j++) { sc_tensor_drop(out[j]); sc_free(out[j]); out[j] = NULL; } return 0; }
        int ax = axis_of[k];
        int64_t coord[TS_MAXD];
        for (int d = 0; d < n; d++) coord[d] = 0;
        for (int64_t i = 0; i < g->numel; i++) {
            tel_set(g, i, tel_get(arr[k], coord[ax]));
            for (int d = n - 1; d >= 0; d--) { if (++coord[d] < shape[d]) break; coord[d] = 0; }
        }
        out[k] = g;
    }
    return 1;
}

/* ---------- NumPy .npy 兼容序列化（主路径） + 旧 SCTS 向后读取 ---------- */

static const char *npy_descr_from_dtype(int dt) {
    if (dt == TS_DT_F4) return "<f4";
    if (dt == TS_DT_F8) return "<f8";
    if (dt == TS_DT_I4) return "<i4";
    if (dt == TS_DT_I8) return "<i8";
    if (dt == TS_DT_BOOL) return "|b1";
    return NULL;
}

static int dtype_from_npy_descr(const char *d) {
    const char *p = d;
    char endian = *p;
    if (endian == '<' || endian == '>' || endian == '|' || endian == '=') p++;
    if (strcmp(p, "f4") == 0) return TS_DT_F4;
    if (strcmp(p, "f8") == 0) return TS_DT_F8;
    if (strcmp(p, "i4") == 0) return TS_DT_I4;
    if (strcmp(p, "i8") == 0) return TS_DT_I8;
    if (strcmp(p, "b1") == 0 || strcmp(p, "?") == 0) return TS_DT_BOOL;
    return -1;
}

static uint8_t sc_tensor_save_npy(sc_tensor *_this, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    sc_tensor *c = sc_tensor_is_contiguous(_this) ? _this : materialize(_this);
    if (!c) { fclose(f); return 0; }

    const char *descr = npy_descr_from_dtype(c->dtype);
    if (!descr) { if (c != _this) { sc_tensor_drop(c); sc_free(c); } fclose(f); return 0; }

    char shape_buf[512];
    int pos = 0;
    if (c->ndim == 0) {
        pos = snprintf(shape_buf, sizeof(shape_buf), "()");
    } else if (c->ndim == 1) {
        pos = snprintf(shape_buf, sizeof(shape_buf), "(%d,)", c->shape[0]);
    } else {
        pos += snprintf(shape_buf + pos, sizeof(shape_buf) - (size_t)pos, "(");
        for (int i = 0; i < c->ndim; i++) {
            pos += snprintf(shape_buf + pos, sizeof(shape_buf) - (size_t)pos,
                            "%s%d", i ? ", " : "", c->shape[i]);
        }
        pos += snprintf(shape_buf + pos, sizeof(shape_buf) - (size_t)pos, ",)");
    }
    if (pos <= 0 || (size_t)pos >= sizeof(shape_buf)) { if (c != _this) { sc_tensor_drop(c); sc_free(c); } fclose(f); return 0; }

    char dict[1024];
    int dlen = snprintf(dict, sizeof(dict),
                        "{'descr': '%s', 'fortran_order': False, 'shape': %s, }",
                        descr, shape_buf);
    if (dlen <= 0 || (size_t)dlen >= sizeof(dict)) { if (c != _this) { sc_tensor_drop(c); sc_free(c); } fclose(f); return 0; }

    /* .npy v1.0: magic(6)+ver(2)+hlen(2) + header，要求总头长度按 16 对齐 */
    int base = dlen;
    int pad = (16 - ((10 + base + 1) % 16)) % 16;
    int hlen = base + pad + 1; /* + '\n' */
    if (hlen > 65535) { if (c != _this) { sc_tensor_drop(c); sc_free(c); } fclose(f); return 0; }

    unsigned char magic[6] = { 0x93, 'N', 'U', 'M', 'P', 'Y' };
    unsigned char ver[2] = { 1, 0 };
    unsigned char h2[2] = { (unsigned char)(hlen & 0xff), (unsigned char)((hlen >> 8) & 0xff) };

    size_t ok = 1;
    ok &= fwrite(magic, 1, 6, f) == 6;
    ok &= fwrite(ver, 1, 2, f) == 2;
    ok &= fwrite(h2, 1, 2, f) == 2;
    ok &= fwrite(dict, 1, (size_t)base, f) == (size_t)base;
    for (int i = 0; i < pad; i++) ok &= fwrite(" ", 1, 1, f) == 1;
    ok &= fwrite("\n", 1, 1, f) == 1;

    size_t nb = (size_t)c->numel * dt_size(c->dtype);
    ok &= fwrite((char *)c->store->data + c->offset * dt_size(c->dtype), 1, nb, f) == nb;

    fclose(f);
    if (c != _this) { sc_tensor_drop(c); sc_free(c); }
    return ok ? 1 : 0;
}

/* 旧格式：magic("SCTS") + ndim + dtype + shape + numel + bytes（仅用于向后读取） */
static sc_tensor *sc_ts_load_scts(FILE *f) {
    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || magic[0] != 'S' || magic[1] != 'C' || magic[2] != 'T' || magic[3] != 'S') return NULL;
    int32_t ndim = 0, dtype = 0;
    int64_t numel = 0;
    if (fread(&ndim, sizeof(int32_t), 1, f) != 1 || ndim < 0 || ndim > TS_MAXD) return NULL;
    if (fread(&dtype, sizeof(int32_t), 1, f) != 1) return NULL;
    int32_t shape[TS_MAXD];
    if (ndim > 0 && fread(shape, sizeof(int32_t), (size_t)ndim, f) != (size_t)ndim) return NULL;
    if (fread(&numel, sizeof(int64_t), 1, f) != 1) return NULL;
    sc_tensor *r = new_contig(ndim, shape, dtype, 0);
    if (!r || r->numel != numel) { if (r) { sc_tensor_drop(r); sc_free(r); } return NULL; }
    size_t nb = (size_t)numel * dt_size(dtype);
    if (nb && fread(r->store->data, 1, nb, f) != nb) { sc_tensor_drop(r); sc_free(r); return NULL; }
    return r;
}

static sc_tensor *sc_ts_load_npy(FILE *f) {
    unsigned char magic[6];
    if (fread(magic, 1, 6, f) != 6) return NULL;
    if (!(magic[0] == 0x93 && magic[1] == 'N' && magic[2] == 'U' && magic[3] == 'M' && magic[4] == 'P' && magic[5] == 'Y')) return NULL;

    unsigned char ver[2];
    if (fread(ver, 1, 2, f) != 2) return NULL;

    uint32_t hlen = 0;
    if (ver[0] == 1) {
        unsigned char h2[2];
        if (fread(h2, 1, 2, f) != 2) return NULL;
        hlen = (uint32_t)h2[0] | ((uint32_t)h2[1] << 8);
    } else if (ver[0] == 2 || ver[0] == 3) {
        unsigned char h4[4];
        if (fread(h4, 1, 4, f) != 4) return NULL;
        hlen = (uint32_t)h4[0] | ((uint32_t)h4[1] << 8) | ((uint32_t)h4[2] << 16) | ((uint32_t)h4[3] << 24);
    } else {
        return NULL;
    }
    if (hlen == 0 || hlen > (1u << 20)) return NULL;

    char *hdr = (char *)sc_alloc((size_t)hlen + 1);
    if (!hdr) return NULL;
    if (fread(hdr, 1, (size_t)hlen, f) != (size_t)hlen) { sc_free(hdr); return NULL; }
    hdr[hlen] = '\0';

    char *pd = strstr(hdr, "'descr'");
    char *pf = strstr(hdr, "'fortran_order'");
    char *ps = strstr(hdr, "'shape'");
    if (!pd || !pf || !ps) { sc_free(hdr); return NULL; }
    if (strstr(pf, "True")) { sc_free(hdr); return NULL; } /* 仅支持 C-order */

    char *q1 = strchr(pd, '\'');
    if (!q1) { sc_free(hdr); return NULL; }
    q1 = strchr(q1 + 1, '\''); /* 跳过 'descr' 键 */
    if (!q1) { sc_free(hdr); return NULL; }
    q1 = strchr(q1 + 1, '\''); /* 值起始引号 */
    if (!q1) { sc_free(hdr); return NULL; }
    char *q2 = strchr(q1 + 1, '\'');
    if (!q2) { sc_free(hdr); return NULL; }

    char desc[16];
    int dl = (int)(q2 - (q1 + 1));
    if (dl <= 0 || dl >= (int)sizeof(desc)) { sc_free(hdr); return NULL; }
    memcpy(desc, q1 + 1, (size_t)dl);
    desc[dl] = '\0';

    int dtype = dtype_from_npy_descr(desc);
    if (dtype < 0) { sc_free(hdr); return NULL; }
    if (desc[0] == '>') { sc_free(hdr); return NULL; } /* 暂不做大端交换 */

    int32_t shape[TS_MAXD];
    int ndim = 0;
    char *lp = strchr(ps, '(');
    char *rp = lp ? strchr(lp, ')') : NULL;
    if (!lp || !rp) { sc_free(hdr); return NULL; }

    char *p = lp + 1;
    while (p < rp) {
        while (p < rp && (*p == ' ' || *p == '\t' || *p == ',')) p++;
        if (p >= rp) break;
        char *e = p;
        while (e < rp && *e >= '0' && *e <= '9') e++;
        if (e == p) { sc_free(hdr); return NULL; }
        int64_t v = 0;
        for (char *t = p; t < e; t++) v = v * 10 + (*t - '0');
        if (v < 0 || v > 2147483647LL || ndim >= TS_MAXD) { sc_free(hdr); return NULL; }
        shape[ndim++] = (int32_t)v;
        p = e;
    }
    sc_free(hdr);

    sc_tensor *r = new_contig(ndim, shape, dtype, 0);
    if (!r) return NULL;
    size_t nb = (size_t)r->numel * dt_size(dtype);
    if (nb && fread(r->store->data, 1, nb, f) != nb) { sc_tensor_drop(r); sc_free(r); return NULL; }
    return r;
}

bool sc_tensor_save(sc_tensor *_this, const char *path) {
    /* 新写入统一采用 NumPy .npy（C-order） */
    return sc_tensor_save_npy(_this, path);
}

sc_tensor *sc_ts_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    unsigned char head6[6];
    size_t n = fread(head6, 1, 6, f);
    rewind(f);

    sc_tensor *r = NULL;
    if (n == 6 && head6[0] == 0x93 && head6[1] == 'N' && head6[2] == 'U' && head6[3] == 'M' && head6[4] == 'P' && head6[5] == 'Y') {
        r = sc_ts_load_npy(f);
    } else if (n >= 4 && head6[0] == 'S' && head6[1] == 'C' && head6[2] == 'T' && head6[3] == 'S') {
        r = sc_ts_load_scts(f);  /* 兼容旧文件 */
    }
    fclose(f);
    return r;
}
