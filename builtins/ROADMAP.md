# ts —— 张量模块路线图与设计文档

> sc 内置张量（tensor）模块：以 **numpy ndarray** 的存储/视图/广播/规约模型为骨架，
> 叠加 **PyTorch** 的 nn 算子核与原地（`_` 尾缀）习惯。autograd 引擎 **不**在本模块内自建；
> ts 只提供成对的前向 + 反向**数值核**，由上层 `builtins/nn`（define-by-run tape + backward）组合
> （见 `templates/dnn-framework`）。
>
> 本文是 ts 的**设计与进度事实源**（类 `syntax.md` 风格）。三件套实现：
> `ts.sc`（唯一接口源）/ `ts.h`（C ABI）/ `ts_impl.c`（默认实现）。

---

## 0. 设计哲学与边界

| 维度 | 取向 |
|------|------|
| 存储模型 | numpy 式 **存储（storage）与视图（view）分离**：多视图共享一块引用计数缓冲 |
| 视图 | strides + offset 表达；transpose/slice/reshape(连续) 等**零拷贝** |
| 广播 | numpy 规则（尾维对齐，维相等或为 1） |
| 命名 | 数组操作随 numpy（zeros/reshape/sum…）；nn 算子随 PyTorch（matmul/relu/softmax…） |
| 原地 | PyTorch 习惯：`_` 尾缀变体写回自身、返回成功标志 |
| 语法 | **不扩展 sc 语法**——切片/索引/掩码一律走**成员函数**（`a.slice(...)` 而非 `a[1:3]`） |
| autograd | 引擎不内建（落在 `builtins/nn`）；ts 提供成对的前向 + 反向数值核（matmul/逐元素/激活/mse/ce backward + sum_to）供 nn 组合 |
| 精度 | 通用算子经 f8 中转；DNN 主路径用 DT_F4（对齐 PyTorch float32 默认） |

**非目标（明确不做）**：Python 式花式下标语法糖、惰性计算图、GPU 后端、复数（暂缓）、
稀疏张量、自动求导引擎。

### 0.1 关于代码量：为什么 ts 千行级即可对齐 numpy 常用子集

numpy 本体几十万行，但其体量**绝大部分不在算子逻辑本身**，而在以下三类工程化铺陈——
ts 经设计取舍**大多绕过**：

| numpy 的体量来源 | 占比 | ts 的对策 |
|------------------|------|-----------|
| **多 dtype 代码特化**：每算子 × 每 dtype（bool/int8…64/uint×4/float16/32/64/128/complex×3/datetime…）由代码生成器铺出专门 C 循环 | 最大头（数万行） | **f8 中转**一份代码覆盖全 dtype；代价是 i8 超 2^53 精度损失、无 float16/complex |
| **数值算法库**：linalg 对接 LAPACK（数十万行 Fortran）、random 多引擎多分布、fft 整套 | 大头 | 自研轻量实现（P3/P4，各数百行级，不追 LAPACK 的数值健壮性） |
| **海量便利函数 + 边角语义**：多算法 sort、histogram、polynomial、masked array、结构化数组、字节序、非对齐内存、npy/npz IO、文本解析… | 长尾 | **有意精简**，只挑常用 |

结论：做到「接口习惯对齐 numpy、覆盖常用子集」的目标，总量约 **2500~4000 行**即可。
「上万行」是要追 numpy 全 dtype 特化 + 数值库工程化健壮性，那不属于本模块
（f8 中转 + 不自建 autograd + 不改语法）的取向。

---

## 1. 数据模型（核心架构，P0 落地）

### 1.1 存储层：引用计数缓冲

```c
typedef struct ts_store {   /* 多视图共享的元素缓冲（模块内私有 refcount） */
    void   *data;           /* 扁平字节缓冲（dt_size * cap 字节） */
    int64_t nbytes;         /* 缓冲字节数 */
    int32_t refcnt;         /* 视图引用数；减到 0 释放 data 与本结构 */
    int32_t _pad;
} ts_store;
```

- 不接入 op 的 `sc_ref` 语言指针图：`tensor` 是 C 内部实现，自管 refcount 更简单直接。
- `data` 经 op 层 `sc_alloc/sc_free` 分配（可经 `-DSC_POOL` 切池化）。

### 1.2 张量：视图描述符

```c
typedef struct tensor {
    ts_store *store;        /* 共享缓冲（nil = 空张量） */
    int32_t  *shape;        /* [ndim] 每视图私有 */
    int32_t  *strides;      /* [ndim] 步长（元素为单位，可负——支持 flip） */
    int64_t   offset;       /* 进入 store->data 的起始元素偏移 */
    int32_t   ndim;         /* 维数（0 = 标量/空） */
    int32_t   dtype;        /* DT_* 元素类型 */
    int64_t   numel;        /* 元素总数 = ∏ shape[i] */
} tensor;
```

- 元素 `i`（多维坐标 `c`）的物理地址：`store->data + (offset + Σ c[d]·strides[d]) · dt_size`。
- **连续（C-contiguous）** 判定：`strides[d] == ∏_{k>d} shape[k]` 且 `offset==0`，缓冲恰好 numel 个元素。
- box（结构体本身）仍由编译器在 `drop()` 后 `sc_free`（堆专属 tensor& 约定不变）。

### 1.3 视图 vs 物化

| 零拷贝视图（共享 store） | 物化拷贝（新建连续 store） |
|--------------------------|----------------------------|
| transpose / T / permute  | clone / contiguous         |
| reshape（仅连续时）       | reshape（非连续时自动 fallback） |
| squeeze / unsqueeze       | 所有逐元素 / 规约 / matmul 结果 |
| slice / select / narrow   | copy_from（写入既有缓冲）   |
| broadcast_to / expand     | concatenate / stack         |
| flip（负步长）/ diagonal   | tile / repeat               |

- 迭代统一经 **stride-aware 遍历**；连续张量走 `memcpy`/线性快径。
- `is_contiguous()` 查询；`contiguous()` 在需要时物化。

---

## 2. dtype

| 枚举 | C 类型 | 用途 |
|------|--------|------|
| `DT_F4=0` | float | float32，DNN 默认 |
| `DT_F8=1` | double | float64 |
| `DT_I4=2` | int32_t | int32 |
| `DT_I8=3` | int64_t | int64 / 索引 |
| `DT_BOOL=4` ✅ | uint8_t | 比较/逻辑/掩码输出 |

复数 `c8/c16`、小整型 `i1/i2/u1`：⬜ 暂缓（P4+，f8 中转架构下非常用子集）。

---

## 3. API 进度矩阵

图例：✅ 已完成 ｜ 🚧 进行中 ｜ ⬜ 规划（P2+）

### 3.1 构造 / 工厂
| API | 状态 | 备注 |
|-----|------|------|
| zeros / ones / full | ✅ | |
| arange / eye | ✅ | |
| from_data | ✅ | 逐字节拷入 |
| linspace / logspace | ✅ | 等差/等比 |
| empty | ✅ | 未初始化 |
| zeros_like / ones_like / full_like / empty_like | ✅ | 形状随源 |
| diag | ✅ | 1D→对角阵 / 2D→对角线 |
| random.* (uniform/normal/randint/permutation/shuffle) | ✅ | P4（xorshift128+） |
| tri / triu / tril / meshgrid | ✅ | P3（meshgrid：`out:&` 多输出，支持 ij/xy） |

### 3.2 元信息 / 访问
| API | 状态 | 备注 |
|-----|------|------|
| ndim / numel / dtype / dim / is_same_shape | ✅ | |
| item / at / set_at（扁平） | ✅ | |
| data / fill / clone / copy_from | ✅ | clone 物化 |
| is_contiguous / contiguous | ✅ | P0 架构产物 |
| at_nd / set_nd（多维坐标） | ✅ | |
| astype（dtype 转换拷贝） | ✅ | |

### 3.3 形变（视图优先）
| API | 状态 | 备注 |
|-----|------|------|
| reshape | ✅ | 连续时视图、否则物化（支持 -1 推断） |
| transpose / T | ✅ | 零拷贝视图 |
| permute | ✅ | 零拷贝视图 |
| squeeze / unsqueeze | ✅ | 零拷贝视图 |
| ravel / flatten | ✅ | ravel 视图 / flatten 拷贝 |
| broadcast_to / expand | ✅ | 零拷贝（stride=0） |
| flip | ✅ | 负步长视图 |
| concatenate / stack | ✅ | 物化 |
| split / chunk | ✅ | 视图数组 |
| tile / repeat | ✅ | 物化 |
| pad / roll | ✅ | 常量填充 / 循环位移（物化） |

### 3.4 索引（全走成员函数，不改语法）
| API | 状态 | 备注 |
|-----|------|------|
| slice(ax,start,stop,step) | ✅ | 视图 |
| select(ax,idx) / narrow | ✅ | 降维视图 |
| take(idx_tensor) | ✅ | 花式索引（物化） |
| masked_select(mask) | ✅ | 布尔掩码 → 1D |
| where(cond, a, b) | ✅ | 三元选择 |
| nonzero | ✅ | 非零坐标 [k, ndim]（DT_I8） |
| gather / scatter_ | ✅ | 沿 axis 采集 / 原地散写 |

### 3.5 逐元素一元
| API | 状态 | 备注 |
|-----|------|------|
| neg / exp / log | ✅ | |
| abs / sqrt / square / reciprocal | ✅ | |
| floor / ceil / round / trunc / sign | ✅ | |
| pow_scalar / clip(min,max) | ✅ | |
| sin / cos / tan / asin… / atan2 | ✅ | P3 三角族 |

### 3.6 逐元素二元（广播）
| API | 状态 | 备注 |
|-----|------|------|
| add / sub / mul / div | ✅ | |
| add_scalar / mul_scalar | ✅ | |
| 原地 add_ / sub_ / mul_ / div_ / *_scalar_ | ✅ | |
| pow / mod / maximum / minimum | ✅ | |
| sub_scalar / div_scalar (+原地) | ✅ | 补齐对称 |
| 比较 gt/ge/lt/le/eq/ne → bool | ✅ | |
| 逻辑 logical_and/or/not → bool | ✅ | |

### 3.7 规约
| API | 状态 | 备注 |
|-----|------|------|
| sum / mean / max / min / argmax（axis） | ✅ | |
| sum_all / mean_all / max_all | ✅ | |
| argmin / prod | ✅ | |
| std / var | ✅ | |
| cumsum / cumprod | ✅ | |
| any / all（→ bool） | ✅ | |
| min_all / prod_all | ✅ | |
| keepdim 选项 | ✅ | 各轴规约支持保留维 |
| median / percentile | ✅ | P3 |

### 3.8 线代
| API | 状态 | 备注 |
|-----|------|------|
| matmul（2D） / dot（1D） | ✅ | 可选 BLAS |
| matmul（>2D 批量广播） | ✅ | |
| outer / trace / diagonal | ✅ | |
| inv / solve / det | ✅ | P3 linalg |
| eigh / svd / qr / cholesky / norm | ✅ | P3 linalg（Jacobi/Gram-Schmidt/瘦 SVD） |

### 3.9 nn 算子
| API | 状态 | 备注 |
|-----|------|------|
| relu / sigmoid / tanh / softmax（+relu_） | ✅ | |
| leaky_relu / gelu / elu / silu | ✅ | 参数化激活 |
| log_softmax / cross_entropy | ✅ | cross_entropy: logits[N,C]+target[N] |
| mse_loss / bce_with_logits / nll_loss | ✅ | P5 第一批已完成 |
| layer_norm / dropout(train) | ✅ | P5 第二批已完成 |
| bmm / addmm / scaled_dot_product_attention | ✅ | P5 第三批已完成 |
| conv1d/2d + max/avg pool | ✅ | P5 第四批已完成 |
| embedding / batch_norm / rms_norm（前向 + 反向） | ✅ | P6 补全（embedding 散加、batch_norm 训练态、rms_norm） |
| 反向数值核（第一批：matmul/逐元素/relu/sigmoid/tanh/mse/ce backward + sum_to） | ✅ | 与前向成对；仅 C-ABI 供 nn 自动微分引擎组合，不在 ts.sc 暴露 |
| 反向数值核（P6 补全：softmax/log_softmax/leaky_relu/elu/silu/gelu/layer_norm/rms_norm/batch_norm/conv2d(input/weight/bias)/max_pool2d/avg_pool2d/embedding + dropout_mask） | ✅ | P6 全量补齐：所有 P5 nn 算子均有成对反向核，使 CNN/Transformer 可端到端训练 |
| autograd 引擎 | → `builtins/nn` | val/tape/backward + linear/conv/embed 模块 + SGD/Adam；P6 接线全部反向核并新增 conv/embed 高层模块 |

### 3.10 杂项
| API | 状态 | 备注 |
|-----|------|------|
| print | ✅ | |
| equal / allclose（张量比较） | ✅ | |
| save / load（IO） | ✅ | P4（NumPy `.npy`，兼容读取旧 `.scts`） |

---

## 4. 广播与视图规则

### 4.1 广播（二元 / where / 比较）
1. 右对齐两形状（尾维对齐），短者前补 1。
2. 各维：相等 → 取该值；一方为 1 → 取另一方；否则报错（返回 nil）。
3. 实现：被广播维 stride 置 0（零拷贝迭代），不物化中间结果。

### 4.2 视图安全
- 视图与源共享 `store`，源 `drop` 不影响仍存活的视图（refcount 保护）。
- 原地算子（`_` 尾缀）写入视图会改动共享缓冲——**有意为之**（如 slice 后原地填充）；
  文档显式提示。需独立副本时先 `clone()`。
- `reshape` 非连续时静默物化（返回独立张量），保证语义正确。

---

## 5. 分期路线

| 期 | 范围 | 状态 |
|----|------|------|
| **P0** | 核心架构重构：ts_store + strides + offset + 视图/物化 + stride 迭代器；现有算子迁移；回归 ts_basic + dnn 收敛 | ✅ 完成 |
| **P1 (=B)** | 实用 numpy 子集：构造扩展、一元/二元/比较/逻辑、规约扩展(std/var/prod/cumsum/keepdim)、形状(concat/stack/split/flip/broadcast_to)、批量 matmul、索引(slice/select/take/masked_select/where) | ✅ 完成 |
| **P2** | 高级索引(nonzero/gather/scatter)、pad/roll、nn 扩展(log_softmax/cross_entropy/gelu…) | ✅ 完成 |
| **P3** | linalg(inv/solve/det/eigh/svd/qr/cholesky/norm)、三角函数族(sin…/atan2)、tri/triu/tril/meshgrid、median/percentile | ✅ 完成 |
| **P4** | random(xorshift128+：uniform/normal/randint/permutation/shuffle)、IO(save/load `.npy`) | ✅ 完成 |
| **P5** | PyTorch nn 常用核心补齐：`mse/bce_with_logits/nll`、`layer_norm/dropout`、`bmm/addmm/sdpa`、`conv/pool` | ✅ 完成 |
| **P6** | 反向数值核全量补齐 + nn autograd 接线 + conv/embed 高层模块：使 **CNN / Transformer 可端到端训练**；新增 embedding/batch_norm/rms_norm 前向+反向 | ✅ 完成 |
| **P4+** | dtype 扩展(复数 c8/c16、小整型 i1/i2/u1)、fft、GPU 后端探索 | ⬜ 待完善 |

### 5.1 待完善（Backlog）

P0–P4 已覆盖 numpy 常用子集。以下为已知缺口，按需推进：

| 项目 | 现状 / 搁置原因 | 未来思路 |
|------|----------------|----------|
| 复数 `c8/c16` | f8 中转架构不覆盖复数；非数值计算常用子集 | 新增 dtype + 复数中转层（re/im 双 f8），或引入 `_Complex` 后端 |
| 小整型 `i1/i2/u1` | f8 中转可表达但物理布局未实现；收益主要在内存占用 | 扩 `dt_size`/`el_get`/`el_set` 分支即可，逻辑层零改动 |
| `fft` / `ifft` | 需整套频域算法核（radix-2/Bluestein）+ 复数支持 | 依赖复数 dtype 先落地；自研轻量 FFT |
| GPU 后端 | 当前纯 CPU；ts_store 已抽象 data 指针，具备扩展基础 | 探索 Metal/CUDA store 变体 + 算子分派 |
| linalg 数值健壮性（已部分完成） | 已提供 `SCC_WITH_LAPACK` opt-in：det/inv/solve/cholesky/qr/eigh/svd 可走 LAPACKE；默认仍保留自研 fallback | 后续按需补齐更多 LAPACK 例程与平台链接模板 |
| PyTorch nn 常用核心（P5） | 已补 `mse_loss/nll_loss/bce_with_logits/layer_norm/dropout/bmm/addmm/sdpa/conv/pool` | 下一步按模型需求细化（如 group/depthwise conv、mask/flash attention） |

### 5.2 跨模块待办

| 项目 | 现状 | 思路 |
|------|------|------|
| list/dict/tok 元素瘦化省内存 | item 用裸 `@`（sc_afat 32B）+ `SC_OWN_RAW`，own 不记账但仍占 8B；曾试标注 `@^` 但 sc_afat 仍 32B、零收益已回退 | 真省内存须单态化：按元素类型生成 24B 专属容器（去 dtor/own），非指针标注可达 |

---

## 6. 参考与对照

- **numpy**：ndarray 存储/strides/广播/规约语义的事实标准。
  - 存储-视图分离、strides 表达、广播规则、`keepdims`、`np.where`、布尔掩码。
- **PyTorch**：nn 算子核与原地习惯、`Tensor.view/contiguous/permute`、`matmul` 批量广播。
- **BLAS（可选）**：`SCC_WITH_BLAS` + DT_F4 时 matmul 走 `cblas_sgemm`。

API 命名优先与 numpy/PyTorch 同名同义，降低迁移学习成本；语法差异（无下标重载）一律以成员函数表达。

---

## 7. 三件套契约

| 文件 | 角色 |
|------|------|
| `ts.sc` | 唯一接口事实源：`@def` 数据布局 + `fnc::` 方法声明 + `@fnc` 工厂 |
| `ts.h` | C ABI 契约（结构体 + 函数原型），字段顺序须与 ts.sc 一致 |
| `ts_impl.c` | 默认实现（编译器自动编译并拼接进消费 TU） |

替换实现：按 `ts.h` 实现全部函数，编译为 `.o/.a` 随工程链接即可覆盖默认实现。

---

## 8. 变更记录

- **2026-06-28** 立项真·numpy 级重构；确立存储-视图分离架构（P0）；本文档建立。
- **2026-06-28** P0 完成：`ts_store`(refcount) + `strides` + `offset` + 视图/物化 + stride 迭代器落地；
  三件套(ts.sc/ts.h/ts_impl.c, 1430 行)重写；`ts_basic` 运行通过、`dnn` 收敛不变、回归 216/0。
- **2026-06-28** P1(=B) 完成：补齐构造(linspace/logspace/empty/*_like/diag)、一元(abs/sqrt/square/
  reciprocal/floor/ceil/round/trunc/sign/pow_scalar/clip)、二元(pow/mod/maximum/minimum/标量对称)、
  比较→bool、逻辑、规约(prod/argmin/std/var/cumsum/cumprod/any/all/keepdim)、形变(ravel/flatten/
  broadcast_to/flip/tile/repeat/concat/stack/split)、索引(slice/select/narrow/take/masked_select/
  where)、线代(批量 matmul/outer/trace/diagonal)、misc(equal/allclose)。
  注：`dim` 为 sc 关键字，切片轴参数命名为 `ax`。- **2026-06-28** P2 完成：高级索引(nonzero/gather/scatter_)、形变(pad/roll)、
  nn 扩展(log_softmax/cross_entropy/leaky_relu/elu/silu/gelu)；`ts_basic` 覆盖、回归 216/0。
  代码量说明：本模块采用 **f8 中转**覆盖全 dtype（而非 numpy 逐 dtype 代码生成），故
  功能覆盖 numpy 常用子集仅需千行量；P3/P4 的 linalg 数值核/random 才会显著增加代码。
- **2026-06-28** P3 完成：三角族(sin/cos/tan/asin/acos/atan/sinh/cosh/atan2)、
  median/percentile(全规约+按轴，线性插值)、tri/triu/tril、linalg(norm/det/inv/solve/
  cholesky/qr=Gram-Schmidt/eigh=Jacobi/svd=瘦 SVD via AᵀA 特征分解)；纯自研轻量数值核，
  不追 LAPACK 数值健壮性。`ts_basic` 覆盖、回归 216/0。
- **2026-06-28** P4 完成：random(xorshift128+ 模块级 RNG：rand_seed/rand_uniform/
  rand_normal=Box-Muller/rand_randint/permutation/shuffle_)、IO(save/ts_load，NumPy `.npy`
  C-order；`ts_load` 兼容旧 `.scts`)。`ts_basic` 往返校验、回归 216/0。
- **2026-06-28** 增量实现：`meshgrid(arr,n,indexing,out)` 多输出网格构造（`ij/xy`），
  并补齐 linalg 数值健壮性 opt-in：定义 `SCC_WITH_LAPACK` 时 det/inv/solve/cholesky/
  qr/eigh/svd 走 LAPACKE（默认仍保留自研 fallback）。
  剩余暂缓：复数 c8/c16、小整型 i1/i2/u1、fft、GPU 后端（见 P4+）。
- **2026-06-28** 路线图细化：新增 **P5（PyTorch nn 常用核心补齐）**，按
  `loss(mse/bce_with_logits/nll)` → `layer_norm/dropout` → `bmm/addmm/sdpa` → `conv/pool`
  的优先级推进；目标是从“MLP 可用”提升到“Transformer/CNN 常用功能可用”。
- **2026-06-28** P5 第一批完成：新增 `mse_loss` / `nll_loss` / `bce_with_logits`（前向损失），
  `ts_basic` 已覆盖，回归 216/0。
- **2026-06-28** P5 第二批完成：新增 `layer_norm(axis, eps)` / `dropout(p, train)`，
  `ts_basic` 已覆盖（含训练/推理路径），回归 216/0。
- **2026-06-28** P5 第三批完成：新增 `bmm` / `addmm` / `sdpa`（scaled dot-product attention 前向），
  `ts_basic` 已覆盖，回归 216/0。
- **2026-06-28** P5 第四批完成：新增 `conv1d/conv2d` 与 `max_pool/avg_pool`（1D/2D）前向实现，
  `ts_basic` 已覆盖，回归 216/0。P5（PyTorch nn 常用核心补齐）标记完成。
- **2026-06-28** 增补并修正：`autograd` 组件（`autograd_new/init/bind/set_sample/step/y/loss`）
  最初由弱模型误置于 `ts` 内（`ts → tok` 分层倒置，且 drop 不解绑 tok 致 use-after-free）。
  已迁出至独立 **`builtins/nn`** 模块（nn = ts 数值 + tok 调度 之上的组合层，`ts` 恢复纯数值），
  并修复绑定生命周期：内部状态绑定后由 tok 图持有、同 id 幂等复用、drop 仅解绑句柄。
- **2026-06-28** 自动微分 P1 落地（全新架构，非打补丁）：`ts` 新增成对的**反向数值核**
  （`tensor_sum_to` 广播归约、`relu/sigmoid/tanh/mse/cross_entropy` backward，纯数学、仅 C-ABI）；
  旧 `autograd` 组件被 **`builtins/nn`** 的 PyTorch 风格自动微分引擎取代——`val`（define-by-run
  张量节点）+ 进程内录带 `tape` + `backward` 回传 + `linear` 模块 + `SGD`/`Adam`。
  验证：单样本 MLP（2→8(relu)→2，MSE+SGD）收敛、AddressSanitizer 全程无内存错误；
  新增 golden `tests/cases/nn_train.sc`，回归 218/0。
- **2026-06-28** P6 完成（反向核全量补齐 + autograd 接线 + 高层模块，使 CNN/Transformer 可端到端训练）：
  · `ts` 新增反向数值核：`softmax/log_softmax/leaky_relu/elu/silu/gelu/layer_norm/rms_norm/
    batch_norm/conv2d(input/weight/bias)/max_pool2d/avg_pool2d/embedding` backward + `dropout_mask`，
    并补 `embedding/batch_norm/rms_norm` 前向（均仅 C-ABI，配对前向核，不在 ts.sc 暴露）。
  · `builtins/nn` autograd 接线全部新核：`val_` 新增 `div/scale/exp/log/reshape/permute/transpose`
    及 `softmax/log_softmax/leaky_relu/elu/silu/gelu/layer_norm/rms_norm/batch_norm/bmm/dropout/
    conv2d/max_pool2d/avg_pool2d/embedding`；`ag_node` 增 `ia[8]/da[2]` 承载算子标量参数；
    backward 在 tape_clear 前直读 `in[i]->data`/`out->data`（dropout 例外，保存 mask）。
  · 新增高层模块 `conv`（`nn_conv2d`→forward/w/b/drop）与 `embed`（`nn_embedding`→forward/w/drop），
    `optim` 增 `track_conv2d/track_embedding`。注：模块类型名用 `conv/embed`（避与 `val` 同名方法冲突）；
    `nn_embedding` 维度参数命名 `edim`（`dim` 为 sc 关键字）。
  · 验证：MLP(layer_norm+gelu)/CNN(conv→relu→pool→reshape→linear→ce)/Transformer block
    (embedding→bmm→transpose→scale→softmax→bmm→rms_norm→dropout) 三条路径训练均收敛；
    新增 golden `tests/cases/cnn_train.sc` 与 `tests/cases/attn_train.sc`，回归 224/0。