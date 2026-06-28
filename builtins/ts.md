# 张量 `ts`：numpy 存储-视图分离 + 广播/规约 + PyTorch nn 算子核

> sc 的数值张量基建。本文是**规格 + API 参考 + 示例**，
> 既面向用户讲清怎么用，也作为编译器/运行时实现的对照基准。
> **进度矩阵与实现路线**（已完成/待完成、分期 P0–P4）见 `ts/ROADMAP.md`（syntax.md 式跟踪文档）。
> 配套源码：`builtins/ts/ts.sc`（事实源，类型与方法声明）、`builtins/ts/ts.h`（C ABI 契约）、
> `builtins/ts/ts_impl.c`（默认实现，拼接进同 TU）。

---

## 0. 设计目标与边界

**目标**：在不引入 GC、不引入编译器特殊知识的前提下，提供一套**接口习惯对齐 numpy/PyTorch**
的稠密张量类型，降低学习成本：

1. numpy 式存储与数组操作：`zeros/ones/full/arange/eye/reshape/transpose/permute/sum/mean/argmax…`；
2. numpy 式广播：尾维对齐，维须相等或为 1；
3. PyTorch 式 nn 算子核：`matmul/relu/sigmoid/tanh/softmax/exp/log…`；
4. 可选 BLAS 加速（`-DSCC_WITH_BLAS`，仿 op 的 `-DSCC_WITH_UV`），未开启则纯 C 可移植实现。

**明确不做**（设计边界，写进文档当承诺）：

- **不自建 autograd（自动微分）图**。`ts` 只提供成对的**前向 + 反向数值核**（纯数学：给定上游
  梯度与前向张量，算出本算子的输入梯度），**不**持有计算图、不决定回传次序。把这些数值核
  组合成「按拓扑序回传 + 梯度累加」的自动微分引擎，是上层 **`builtins/nn`**（进程内录带 tape +
  backward 引擎）与 `tok`（step / 分布式宏调度）的事。详见 `builtins/nn/nn.sc`。
  反向数值核包括：`matmul`（经转置组合）、逐元素加减乘（配 `sum_to` 广播归约）、
  `relu/sigmoid/tanh` 反向、`mse` / `cross_entropy` 反向。它们只走 C-ABI 供 nn 调用，
  不在 sc 表面暴露（保持 `ts.sc` 接口聚焦前向）。
- **不做惰性/计算图**。算子立即求值（eager）。
- **不做运算符重载糖 / 不改 sc 语法**。一切以成员函数表达：`a->add(b)`、`a->slice(ax,...)`，
  不提供 `a + b`、`a[1:3]` 这类下标/切片语法糖（无需为张量改动 sc 语法）。

**做了什么**（P0/P1 已落地）：

- **存储-视图分离**：底层 `ts_store`（带 refcount 的连续缓冲）+ 上层 `tensor`（shape/strides/offset 元信息）。
  `reshape/transpose/permute/squeeze/unsqueeze/ravel/broadcast_to/flip/slice/select/narrow` 等
  在可行时返回**零拷贝视图**（与源共享 `store`，refcount 保护），否则静默物化。
- **strides 广播**：被广播维 stride 置 0，二元/比较/where 不物化中间结果。
- **批量 matmul**：>2D 走前导维广播；2D 可选 BLAS。

---

## 1. 数据模型与所属权

```sc
@def tensor&: {          # 堆专属（同 adt string& / mt pool&）
    store: &            # ts_store*：带 refcount 的连续缓冲（多视图共享，空张量为 nil）
    shape: i4&          # ndim 个维长
    strides: i4&        # ndim 个步长（元素为单位；视图可为 0/负）
    offset: i8          # 起始元素偏移（视图入口）
    ndim: i4            # 维数（0 = 标量/空）
    dtype: i4           # DT_* 元素类型
    numel: i8           # 元素总数 = ∏ shape[i]
}
```

- **元素物理偏移** `= offset + Σ coord[d]·strides[d]`；行主序连续时 `strides[i] = ∏_{j>i} shape[j]`，
  但视图可有任意（含 0 或负）步长。`is_contiguous()` 判定是否连续，`contiguous()` 物化为连续副本。
- **堆专属 `tensor&`**：只能作指针使用。工厂函数返回新 `tensor&`，**调用方负责 `drop()`**。
  其析构遵循堆专属类型约定——`t->drop()` 由编译器发射为「`store` 减引用（归零才真正释放缓冲）、
  释放 `shape/strides`，再 `sc_free` 结构体盒子本身」，故用户只需一次 `t->drop()`。
  视图与源各自 `drop()` 安全：共享 `store` 由 refcount 保护，不会重复释放。
- 缓冲走 op 层 `sc_alloc/sc_realloc/sc_free`（默认 libc，`-DSC_POOL` 可切内存池）。

### 所属权速记

| 操作 | 返回 | 谁释放 |
|---|---|---|
| 工厂 `zeros/ones/full/empty/arange/linspace/logspace/eye/diag/from_data/*_like` | 新 `tensor&` | 调用方 `drop()` |
| 逐元素/规约/线代/激活/形变/索引（非 `_` 后缀） | **新** `tensor&`（视图或物化） | 调用方 `drop()` |
| 原地变体（`_` 后缀，如 `add_`/`relu_`） | `bool` 成功标志 | 写回 `_this`，无新对象 |
| 标量规约 `sum_all/mean_all/max_all/min_all/prod_all/item/at/dot` | `f8` | 无对象 |

> 凡返回 `tensor&` 的算子都产生**新堆对象**（视图或物化拷贝，均各自 `drop()`）；
> 视图与源共享 `store`，由 refcount 保护，互不影响释放。训练内循环宜优先用 `_` 原地变体避免反复分配。

---

## 2. dtype

| 常量 | 值 | C 类型 | 用途 |
|---|---|---|---|
| `DT_F4` | 0 | `float`   | float32，DNN 主路径，对齐 PyTorch 默认 |
| `DT_F8` | 1 | `double`  | float64 |
| `DT_I4` | 2 | `int32_t` | 索引/整型 |
| `DT_I8` | 3 | `int64_t` | 大整型 / `argmax` 结果 |
| `DT_BOOL` | 4 | `uint8_t` | 比较/逻辑/掩码输出（0/1） |

- **dtype 为各工厂的必填末参**（严格语法，不省略）。`inc ts.sc` 后按裸名引用 `DT_F4` 等。
- **数值经 f8 中转**：通用算子用 `double` 读写元素，故 `DT_I8` 超过 `2^53` 的整数会损失精度；
  DNN 主路径用 `DT_F4` 不受影响。

---

## 3. API 参考

> 方法形如 `t->name(...)`（C 侧 `tensor_<name>`）；工厂为自由函数 `name(...)`（同 `default_pool`）。

### 工厂（返回新 `tensor&`，调用方 drop）

| 声明 | 说明 |
|---|---|
| `zeros(ndim, shape, dtype)` | 全 0 |
| `ones(ndim, shape, dtype)` | 全 1 |
| `full(ndim, shape, value, dtype)` | 全填充 `value` |
| `empty(ndim, shape, dtype)` | 未初始化 |
| `arange(start, stop, step, dtype)` | `[start, stop)` 步进 `step` 的 1D |
| `linspace(start, stop, num, dtype)` | `[start, stop]` 等差 `num` 点 |
| `logspace(start, stop, num, base, dtype)` | `base^linspace` 等比 |
| `eye(n, dtype)` | `n×n` 单位阵 |
| `diag(src, k)` | 1D→以 `src` 为第 `k` 对角线的方阵 / 2D→取第 `k` 对角线 1D |
| `from_data(ndim, shape, data, dtype)` | 由现有缓冲逐元素拷入（`data` 须与 dtype 同型） |
| `zeros_like/ones_like/full_like/empty_like(src[, value])` | 形状/类型随 `src` |

### 元信息

| 方法 | 返回 | 说明 |
|---|---|---|
| `ndim()` / `numel()` / `dtype()` | `i4`/`i8`/`i4` | 维数 / 元素数 / 类型 |
| `dim(axis)` | `i4` | 第 `axis` 维长（越界返回 0） |
| `is_same_shape(o)` | `bool` | 形状是否一致 |

### 元素访问（扁平索引，经 f8 中转）

| 方法 | 返回 | 说明 |
|---|---|---|
| `item()` | `f8` | 取唯一/首元素 |
| `at(idx)` / `set_at(idx, v)` | `f8`/`bool` | 扁平（逻辑）读 / 写（越界返回 0） |
| `at_nd(coord)` / `set_nd(coord, v)` | `f8`/`bool` | 多维坐标读 / 写（`coord` 为 `ndim` 个下标） |
| `data()` | `&` | 缓冲区基址（`base + offset`，视图慎用） |
| `fill(v)` | — | 全填充 |
| `clone()` | `tensor&` | 深拷贝（物化） |
| `contiguous()` | `tensor&` | 已连续则返回拷贝，否则物化为连续 |
| `is_contiguous()` | `bool` | 是否行主序连续 |
| `astype(dtype)` | `tensor&` | dtype 转换拷贝 |
| `copy_from(o)` | `bool` | 形状一致，逐元素拷入（dtype 自动转换） |

### 形变（连续时零拷贝视图，否则物化）

| 方法 | 说明 |
|---|---|
| `reshape(ndim, shape)` | `numel` 须一致（支持单个 `-1` 推断）；连续→视图，否则物化 |
| `transpose()` / `t()` | 2D 转置视图（`ndim!=2` 返回 nil） |
| `permute(ndim, axes)` | 维重排零拷贝视图（`axes` 为 `ndim` 的排列） |
| `squeeze()` | 去掉所有长度 1 的维（视图） |
| `unsqueeze(axis)` | 在 `axis` 处插入长度 1 的维（视图） |
| `ravel()` / `flatten()` | 展平为 1D：`ravel` 连续时视图、否则物化；`flatten` 总物化 |
| `broadcast_to(ndim, shape)` | 广播到目标形状（被广播维 stride=0，零拷贝视图） |
| `flip(axis)` | 沿 `axis` 翻转（负步长视图） |
| `tile(reps...)` / `repeat(...)` | 平铺 / 重复（物化） |

### 索引（全走成员函数，不改 sc 语法）

| 方法 | 说明 |
|---|---|
| `slice(ax, start, stop, step)` | 沿 `ax` 切片 `[start, stop)` 步进 `step`（视图） |
| `select(ax, idx)` | 取 `ax==idx` 降一维（视图） |
| `narrow(ax, start, len)` | 截取 `[start, start+len)`（视图） |
| `take(idx_tensor)` | 花式索引按扁平下标取（物化 1D） |
| `masked_select(mask)` | 布尔掩码筛选（物化 1D） |
| `nonzero()` | 非零元素坐标 `[k, ndim]`（`DT_I8`，物化） |
| `gather(axis, index)` | 沿 `axis` 按整型 `index` 采集（torch.gather，结果形随 `index`，物化） |
| `scatter_(axis, index, src)` | 沿 `axis` 按 `index` 原地写入 `src`（返回 `bool`） |
| `pad(pads, value)` | 常量填充（`pads` 为 `[2*ndim]` 每维 before/after，物化） |
| `roll(shift, axis)` | 循环位移（`axis<0` 展平后整体滚动，物化） |
| `where(cond, a, b)`（自由函数） | 三元选择，三方广播（物化） |

### 逐元素一元（返回新 `tensor&`）

`neg` / `abs` / `sqrt` / `square` / `reciprocal` / `exp` / `log` /
`floor` / `ceil` / `round` / `trunc` / `sign` / `pow_scalar(p)` / `clip(min, max)`。

三角族：`sin` / `cos` / `tan` / `asin` / `acos` / `atan` / `sinh` / `cosh`（均经 f8 中转）。

### 逐元素二元（numpy 广播）

- 新对象：`add` / `sub` / `mul` / `div` / `pow` / `mod` / `maximum` / `minimum` / `atan2(o)` /
  `add_scalar(s)` / `sub_scalar(s)` / `mul_scalar(s)` / `div_scalar(s)`
- 原地（返回 `bool`，`o` 须可广播到自身形状）：`add_` / `sub_` / `mul_` / `div_` /
  `add_scalar_(s)` / `sub_scalar_(s)` / `mul_scalar_(s)` / `div_scalar_(s)`
- 比较（→ `DT_BOOL`）：`gt` / `ge` / `lt` / `le` / `eq` / `ne`
- 逻辑（→ `DT_BOOL`）：`logical_and` / `logical_or` / `logical_not`

### 规约（`axis<0` → 全规约得标量张量 `[1]`；否则沿 `axis` 降一维；`keepdim` 保留该维为 1）

- 张量结果：`sum` / `mean` / `max` / `min` / `prod` / `argmax` / `argmin`（`arg*` 结果 dtype `DT_I8`）/
  `std` / `var` / `any` / `all`（`any/all` → `DT_BOOL`）；均带 `(axis, keepdim)` 签名
- 累积：`cumsum(axis)` / `cumprod(axis)`（同形）
- 标量 `f8`：`sum_all` / `mean_all` / `max_all` / `min_all` / `prod_all`
- 分位数：`median(axis, keepdim)` / `percentile(q, axis, keepdim)`（线性插值；`axis<0` 全规约）；
  标量 `f8`：`median_all` / `percentile_all(q)`

### 线代

| 方法 | 说明 |
|---|---|
| `matmul(o)` | 2D×2D（`[m,k]×[k,n]→[m,n]`）；>2D 走前导维批量广播；`DT_F4`+`-DSCC_WITH_BLAS` 走 `cblas_sgemm` |
| `dot(o)` | 1D·1D 内积（长度须一致），返回 `f8` |
| `outer(o)` | 1D 外积 → 2D |
| `trace()` | 主对角线之和，返回 `f8` |
| `diagonal(k)` | 取第 `k` 对角线（视图） |
| `bmm(o)` | 3D 批量矩乘：`[B,M,K]×[B,K,N]→[B,M,N]`（不做 batch 广播） |
| `addmm(mat1,mat2,beta,alpha)` | `beta*self + alpha*(mat1@mat2)`（2D） |
| `triu(k)` / `tril(k)` | 上/下三角（第 `k` 对角线为界，物化） |
| `norm(p)` | 元素 `p`-范数（`p=2` 为 Frobenius；`p<=0` 取 ∞-范数=最大绝对值），返回 `f8` |
| `det()` | 方阵行列式（部分主元高斯消元），返回 `f8` |
| `inv()` | 方阵逆（Gauss-Jordan） |
| `solve(b)` | 解 `Ax=b`（`b` 可为 `[n]` 或 `[n,k]`，高斯消元 + 回代） |
| `cholesky()` | 对称正定 `A=LLᵀ` 的下三角 `L`（非正定返回 nil） |
| `qr(out)` | `[m,n]`(m≥n) → `out[0]=Q[m,n]`、`out[1]=R[n,n]`（修正 Gram-Schmidt） |
| `eigh(out)` | 对称矩阵 → `out[0]=特征值[n]`(升序)、`out[1]=特征向量[n,n]`(列向量)（Jacobi 旋转） |
| `svd(out)` | 瘦 SVD → `out[0]=U[m,r]`、`out[1]=S[r]`、`out[2]=V[n,r]`，`r=min(m,n)`（经 `AᵀA` 特征分解） |

> 多输出（`qr/eigh/svd`）：`out` 为 `tensor&[]` 数组，结果各元素由调用方 `drop()`。
> 这些数值核为纯自研轻量实现，不追 LAPACK 的数值健壮性（病态/大规模慎用）。
> 定义 `SCC_WITH_LAPACK` 可启用 LAPACKE 路径（det/inv/solve/cholesky/qr/eigh/svd），
> 默认仍为自研 fallback（零外部依赖）。
>
> 用法：`var out[2]: &` → `m->eigh((out: &))` → `var vals: tensor& = (out[0]: tensor&)`。

### nn 激活/逐点（返回新 `tensor&`）

`relu` / `sigmoid` / `tanh` / `exp` / `log` / `neg` / `softmax(axis)`（数值稳定）；原地 `relu_`（返回 `bool`）。
`log_softmax(axis)` / `leaky_relu(slope)` / `elu(alpha)` / `silu()` / `gelu()`（tanh 近似）。
`cross_entropy(target)`：`logits[N,C]` + 整型 `target[N]` → 平均交叉熵（返回 `f8`）。
`mse_loss(target)`：同形张量均方误差（返回 `f8`）。
`nll_loss(target)`：输入视为 `log-prob[N,C]`，整型 `target[N]`（返回 `f8`）。
`bce_with_logits(target)`：同形 logits + target 的稳定 BCE（返回 `f8`）。
`layer_norm(axis, eps)`：沿指定轴逐切片标准化（返回 `tensor&`）。
`dropout(p, train)`：训练态随机置零并按 `1/(1-p)` 缩放；推理态返回拷贝。
`sdpa(k, v, causal)`：scaled dot-product attention（`Q/K/V` 均为 3D，返回 3D）。
`conv1d(w,bias,stride,padding)`：`x[N,Cin,L]` 与 `w[Cout,Cin,K]` 卷积（返回 `[N,Cout,Lout]`）。
`conv2d(w,bias,sh,sw,ph,pw)`：`x[N,Cin,H,W]` 与 `w[Cout,Cin,Kh,Kw]` 卷积。
`max_pool1d/avg_pool1d(kernel,stride,padding)` 与
`max_pool2d/avg_pool2d(kh,kw,sh,sw,ph,pw)`：池化下采样（avg 为有效元素平均）。

### 拼接 / 比较 / 杂项

- 自由函数：`concat(arr, n, axis)` 沿 `axis` 拼接、`stack(arr, n, axis)` 新增 `axis` 堆叠（`arr` 为 `tensor*[]`，物化）
- 网格构造：`meshgrid(arr, n, indexing, out)`（`arr`/`out` 均为 `tensor&[]`）
  - 输入 `arr[i]` 必须为 1D；输出 `out[i]` 为 N 维网格
  - `indexing=0` 为 `ij`（矩阵索引），`indexing=1` 为 `xy`（仅交换前两轴）
- 成员：`split(parts, axis, out)` 均分为视图写入 `out[]`
- `equal(o)` / `allclose(o, rtol, atol)` 张量比较 → `bool`
- `print()` — 形状 + 元素（大张量省略），`drop()` — 释放。

### 随机（xorshift128+ 模块级 RNG）

- `rand_seed(seed)`（自由函数）：设种子（并预热）。
- 自由函数（返回新 `tensor&`）：`rand_uniform(ndim, shape, lo, hi, dtype)` →`[lo,hi)` 均匀；
  `rand_normal(ndim, shape, mean, std, dtype)`（Box-Muller 正态）；
  `rand_randint(ndim, shape, lo, hi, dtype)` →`[lo,hi)` 整数；`permutation(n, dtype)` →`0..n-1` 随机排列。
- 成员：`shuffle_()` 沿首维原地 Fisher-Yates 洗牌（返回 `bool`）。
- 构造：`tri(n, m, k, dtype)`（自由函数）→ `[n,m]` 下三角 1 矩阵（`m<=0` 取方阵）。

### 序列化（NumPy `.npy`，C-order）

- 成员 `save(path)` → `bool`：写 **NumPy `.npy` v1.0**（`descr/fortran_order/shape` 头 + 连续原始字节）。
- 自由函数 `ts_load(path)` → `tensor&`（失败返回 nil）：读取 `.npy`（当前支持 C-order，`fortran_order=False`）。
- dtype 对齐：`DT_F4/F8/I4/I8/BOOL` 对应 `<f4/<f8/<i4/<i8/|b1`。
- 兼容性：`ts_load` 仍保留对旧 `.scts` 文件的向后读取；`save` 新写入统一为 `.npy`。

### 反向数值核（与前向算子成对，仅 C-ABI）

`ts` 为关键前向算子配套提供**纯数值反向核**（不持图、不定回传序），供 `builtins/nn` 的
自动微分引擎组合调用，不在 `ts.sc` 表面暴露：

- `tensor_sum_to(grad, ndim, shape)`：广播归约——把 `grad` 按尾维对齐累加回目标形状（广播反向）。
- `tensor_relu_backward(grad, x)` = `grad * (x > 0)`；
  `tensor_sigmoid_backward(grad, y)` = `grad * y * (1-y)`（`y` 为前向输出）；
  `tensor_tanh_backward(grad, y)` = `grad * (1-y²)`。
- `tensor_mse_backward(x, target)` = `2/N * (x-target)`；
  `tensor_cross_entropy_backward(logits, target)` = `(softmax(logits) - onehot(target)) / N`。
- `matmul` 反向、逐元素加/减/乘反向由 nn 用 `tensor_transpose` / `tensor_neg` / 逐元素乘 +
  `tensor_sum_to` 组合得到（无需独立核）。

### autograd 引擎（落在 builtins/nn）

`ts` 保持纯数值内核（前向 + 上述反向核），**不**在张量库内自建 autograd 图。PyTorch 风格的
自动微分引擎（`val` 节点 + define-by-run `tape` + `backward` 回传 + `linear` 模块 + `SGD`/`Adam`
优化器）落在独立的 **`builtins/nn`** 模块——nn = ts 数值（含反向核） + tok 调度 之上的组合层。
用法 `inc nn.sc` 后 `nn_linear(in, out)` / `nn_sgd(lr)` / `loss->backward()` / `opt->step()`，
详见 `builtins/nn/nn.sc` 与 `templates/dnn-framework`。

---

## 4. 广播规则（numpy 语义）

两操作数从**尾维对齐**逐维比较，每维须**相等**或**其一为 1**（为 1 的维沿该维复制），
缺维按 1 补齐。结果维数取较大者。

```
[2,3] + [3]    → [2,3]   # 右操作数补成 [1,3]，沿 0 轴复制
[2,1] + [1,3]  → [2,3]   # 各自沿被 1 的维复制
[2,3] + [2,4]  → 错误（3≠4 且都不为 1）→ 返回 nil
```

原地 `_` 变体不得改变 `_this` 的形状：`o` 可广播**到** `_this`，反之不行。

**实现**：广播经 strides 表达——被广播维 stride 置 0（零拷贝迭代），不物化中间结果。
`broadcast_to` 暴露该机制，返回与源共享 `store` 的零拷贝视图。

---

## 5. 示例

```sc
inc ts.sc

fnc main: i4
    var shp[2]: i4
    shp[0] = 2
    shp[1] = 3

    # 构造 + 全规约
    var a: tensor& = zeros(2, shp, DT_F4)
    a->fill(2.0)
    printf("sum=%g\n", a->sum_all())          # 12

    # arange + reshape（连续拷贝）
    var b: tensor& = arange(0.0, 6.0, 1.0, DT_F4)
    var b2: tensor& = b->reshape(2, shp)       # [[0,1,2],[3,4,5]]

    # 广播：[2,3] + [1,3]
    var rsh[2]: i4
    rsh[0] = 1
    rsh[1] = 3
    var row: tensor& = ones(2, rsh, DT_F4)
    var bc: tensor& = b2->add(row)             # 每元素 +1

    # matmul [2,3]×[3,2]
    var sh32[2]: i4
    sh32[0] = 3
    sh32[1] = 2
    var w: tensor& = ones(2, sh32, DT_F4)
    var mm: tensor& = b2->matmul(w)            # [[3,3],[12,12]]

    # nn：softmax 沿末维
    var sm: tensor& = mm->softmax(-1)

    a->drop()
    b->drop()
    b2->drop()
    row->drop()
    bc->drop()
    w->drop()
    mm->drop()
    sm->drop()
    return 0
```

---

## 6. 与 `tok` 的关系：autograd 编排

`ts` 不含反向图。在 `templates/dnn-framework` 中，张量作为 `i8` 句柄在 `tok` 节点间流转：

- 前向（`follow` else 分支）：从上游 `tok` 取张量句柄，调 `ts` 算子核算出激活，`set` 给下游；
- 反向（`follow TOK_BACK` 分支）：`back loss` 触发，按 `tok` 依赖图的**反拓扑序**回传，
  各节点用 `ts` 的逐元素/线代算子累加梯度。fan-out 的梯度汇聚时序由 `back` 自动保证。

> 即：`tok` 负责「**何时、按什么序**」算反向；`ts` 负责「**算什么**」（算子核）。两层正交。

---

## 7. 三件套契约与接入

| 文件 | 角色 | 面向谁 |
|---|---|---|
| `ts.sc` | sc 侧接口声明（`@def` 布局 + `@fnc T::m` 方法声明，无体），决定 sc 怎么用 | scc 编译器 |
| `ts.h` | C ABI 契约（结构体/原型 + dtype 枚举裸名），与 `ts.sc` 手工同步 | C 编译器 |
| `ts_impl.c` | 默认实现，拼接进 `ts.sc` 生成的单元 `.c`（同一 TU）；dist 下预编译 `.a` 单独链接 | C 编译器/链接器 |

- **接入零改编译器/CMake**：`inc ts.sc` 经 `resolveModulePath` 自动解析子项目形态 `builtins/ts/ts.sc`，
  并直接 `#include "builtins/ts/ts.h"`；`ts_impl.c` 自动拼入同 TU。
- **dist 嵌入**：`CMakeLists` 的 `GLOB_RECURSE *.sc/*.h` + `GLOB <sub>/<sub>_impl.c` 自动收录，
  新增 `builtins/ts/` 文件重跑 cmake 即被发现，预编译为 `ts.a`。
- **自定义实现**：按 `ts.h` 重新实现全部函数、编译为 `.o/.a` 随工程链接即可替换默认实现。

### 默认名一致性约定（重要）

`ts.sc` 的 `@def dtype` 枚举发射裸名 `DT_F4..DT_I8`；消费单元 `inc ts.sc` 时由编译器直接
`#include "ts.h"` 提供这些常量定义，故 **`ts.h` 必须以同样的裸名定义枚举**（另给 `TS_DT_*` 宏别名）。

---

## 8. 可选 BLAS 加速

定义 `SCC_WITH_BLAS` 且 `matmul` 两操作数均为 `DT_F4` 时，走 `cblas_sgemm`（`#include <cblas.h>`，
链接 `-lblas` 或 OpenBLAS）；否则回退可移植 `double` 三重循环。其余 dtype 始终走可移植路径。
