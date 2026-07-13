# syntax-s —— sc 空间计算方言（`.ss`）语法参考

本文档是 `.ss` 着色器方言的**语言语法参考**：只收录**已实现并通过回归验证**的语法。
配套文档：

- [syntax.md](syntax.md) —— sc 语言主手册（本方言是其严格子集 + 空间计算扩展）
- [syntax-s-design.md](syntax-s-design.md) —— 设计决策·编译管线·路线图（编译器注释中的
  `syntax-s §N` 锚点指该文档章节）
- [spec.md](spec.md) —— spec 编译期特化维度系统的完整设计

> **方言定位**：cpu = 串行·逻辑·时间，gpu = 并行·变换·空间。`.ss`（space source）
> 复用 sc 的词法/解析器，在 shader 语境下收窄到可安全下译 SPIR-V 的子集，
> 并扩展向量/矩阵/采样器类型与阶段/绑定语义。能力上界 = SPIR-V（Shader 能力档）。

---

## 1. 文件模型

- shader 代码放**独立的 `.ss` 文件**，不与宿主 `.sc` 混编。
- 一个 `.ss` 文件可混编**多个阶段入口**（`vert`/`frag`/`comp`）+ 辅助函数 + 共享类型。
- 编译：`scc file.ss -o outdir/stem`（产物形态见 §13）。

## 2. 顶层声明

| 声明 | 语法示意 | 说明 |
|------|---------|------|
| 目标 | `tar vulkan@450` | GPU 目标与版本（§3），必须至少一条 |
| 特化维度 | `spec K in [a, b]` / `spec K:` + 分支 | 编译期变体（§11） |
| 实例白名单 | `use K1, K2` + 缩进数据行 | spec 组合收窄（§11.3） |
| 结构体 | `@def Name: { ... }` [资源词] | I/O 结构 / 资源块（§4.5、§6） |
| 采样器 | `var tex: sampler2D uniform set 0 binding 1` | 全局资源（§6.4） |
| 常量 | `let PI: f4 = 3.14159` | 编译期常量（含常量数组） |
| 特化常量 | `let TILE: u4 = 64 spec 0` | 管线创建期可调参（§6.6） |
| 辅助函数 | `fnc name: Ret, p: T, ...` | 阶段内可调用（§10） |
| 阶段入口 | `vert` / `frag` / `comp` | 着色阶段（§5） |

## 3. 目标声明 `tar`

```
tar vulkan@450                 # 单目标
tar glcore@410, gles@300       # 多目标（逗号分隔）
tar metal@2.0                  # Metal（major.minor 写法）
tar "boards/rk3588.caps"       # 外部设备能力档案
```

- **版本必须显式指定，无默认**。多目标 = 兼容性契约：任一目标不支持所用能力即硬报错
  （不降级、不静默跳过）；产物逐目标各出一份。
- 版本白名单（写错当场拒绝并提示）：

| api | 合法版本 | 备注 |
|-----|---------|------|
| `vulkan` | 450 / 460 | 直落 `.spv` |
| `glcore`（别名 `gl`） | 110–150 / 330 / 400–460 | 桌面 core profile |
| `gles` | 100 / 300 / 310 / 320 | ES2.0 的着色语言 = `gles@100` |
| `webgl` | 100 / 300 | WebGL1 / WebGL2 |
| `metal`（别名 `msl`） | 1.0–4.x（`@major.minor`） | 经 SPIRV-Cross 产 MSL |
| `cpu`（别名 `c`） | 99 / 11 / 17（C 标准年份） | SPMD 循环化 C 直发（仅 comp；向量化交目标 C 编译器，见设计文档 §17） |

### 3.1 设备能力档案（caps profile）

`tar "file.caps"` 把目标锚定到具体设备（基线版本 + 扩展）。行式协议：

```
# rk3588.caps（# 行注释）
api gles
version 3.1              # 或 310，与 tar 版本规则同样归一化
ext GL_EXT_texture_border_clamp
ext GL_OES_standard_derivatives
```

- 搜索顺序：绝对路径 → 相对 `.ss` 源文件目录 → `builtins/gpu/caps/`（标准档案库）。
- 版本不够但档案声明了替代扩展 → 能力成立，产物自动发射 `#extension ... : require`。
- 产物/反射 tag 用档案文件 stem（如 `cs_main.rk3588.comp`）。

## 4. 类型系统

### 4.1 标量

名字即字节数（与 CPU 侧 sc 一致）；窄/宽标量（P3）为真位宽，能力表门控。

| 类型 | 含义 | 备注 |
|------|------|------|
| `f4` | 32 位浮点 | GLSL `float` |
| `f2` | 16 位半精度（P3） | SPIR-V Float16 + 16bit_storage；MSL `half`；GLSL `float16_t` |
| `f8` | 64 位双精度 | 仅 GLSL 文本链（DoubleType 门控）；Metal 永不支持 |
| `i4` / `u4` | 32 位整数 | GLSL `int`/`uint` |
| `i1` / `u1` | 8 位整数（P3） | Int8 + 8bit_storage；MSL `char`；GLSL `int8_t` |
| `i2` / `u2` | 16 位整数（P3） | Int16；MSL `short`；GLSL `int16_t` |
| `i8` / `u8` | 64 位整数（P3） | Int64；MSL `long`（buffer 内需 MSL 2.3+）；GLES 无途径 |
| `bool` | 布尔 | |

- 窄/宽标量支持：局部变量与算术、资源块成员（含数组）、顶层 let（需数值
  字面量初值）、标量转换 `f2(x)`/`i8(x)` 与强转 `(x: f2)`。字面量默认 32 位，
  混型算术/比较自动向较宽侧对齐。
- 限制：无窄/宽分量向量（f16vec* 等待后续）；原子操作仍限 32 位；spec
  特化常量限 32 位。

### 4.2 向量 / 矩阵

| 类型 | 分量 | GLSL 对应 |
|------|------|-----------|
| `vec2` `vec3` `vec4` | f4 | `vec*` |
| `ivec2` `ivec3` `ivec4` | i4 | `ivec*` |
| `uvec2` `uvec3` `uvec4` | u4 | `uvec*` |
| `bvec2` `bvec3` `bvec4` | bool | `bvec*` |
| `mat2` `mat3` `mat4` | f4 方阵 | `mat*` |

### 4.3 数组

```
var a[4]: vec2            # 定长数组（维度须编译期常量）
let LUT[3]: vec2 = { vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.5, 1.0) }
y[]: f4                   # 运行时数组（仅 storage 块的最后一个字段）
```

多维数组暂不支持。

### 4.4 采样器（19 种，opaque 类型）

| 族 | 类型 |
|----|------|
| 浮点 | `sampler2D` `sampler3D` `samplerCube` `sampler2DArray` `samplerCubeArray` |
| 深度对比（shadow） | `sampler2DShadow` `samplerCubeShadow` `sampler2DArrayShadow` `samplerCubeArrayShadow` |
| 有符号整数 | `isampler2D` `isampler3D` `isamplerCube` `isampler2DArray` `isamplerCubeArray` |
| 无符号整数 | `usampler2D` `usampler3D` `usamplerCube` `usampler2DArray` `usamplerCubeArray` |
| 多重采样 | `sampler2DMS` `sampler2DMSArray` |

采样器只能作全局资源（§6.4）或纯 sampler 资源块成员，**不能进 uniform/storage 数据块**。

### 4.5 结构体

```
@def VsOut: {
    clip: vec4 builtin position
    uv:   vec2
}
```

`@def` 结构体用作阶段 I/O（§5.2）或资源块（§6）。字段可带属性（§5.3）。

## 5. 阶段入口与 I/O

### 5.1 三种阶段

```
vert vs_main: VsOut, in: VsIn      # 顶点：返回 varying 结构，入参顶点属性结构
frag fs_main: vec4, in: VsOut      # 片元：返回 vec4（→ location 0 输出）或结构体
comp cs_main: in: CompIn           # 计算：无返回值（省略即 void），入参计算内建结构
comp cs_tile: in: CompIn, local 8 8   # 签名尾 local X [Y [Z]] = 工作组尺寸
comp cs_solo                        # 无 I/O 的入口可省略签名
```

| 关键字 | SPIR-V ExecutionModel | 产物扩展名 |
|--------|----------------------|-----------|
| `vert` | Vertex | `.vert` |
| `frag` | Fragment | `.frag` |
| `comp` | GLCompute | `.comp` |

- **`comp` 工作组尺寸**：签名尾 `local X [Y [Z]]`（缺省维度补 1）；未声明默认
  `64×1×1`。写入 SPIR-V `ExecutionMode LocalSize` 与反射清单 `local_size`，
  运行时（spc）据此设 threadsPerThreadgroup。仅 comp 可用。
- **comp 输入收敛**：至多一个入参，且须为纯内建字段结构体（无 loc 输入，
  数据走 storage/uniform 块）；comp 无阶段返回值（输出走 storage 块）。

### 5.2 I/O 结构体（成员改写模型）

- **顶点属性**：`vert` 入参结构体字段（`loc N` 或按字段序自动分配 location）。
- **varying**：`vert` 返回结构体 = `frag` 入参结构体——**用同一份 `@def`**，location
  自动配对；`builtin` 字段映射内建变量，不占 location。
- **片元输出**：`frag` 返回 `vec4` → `location 0` 单输出；返回结构体 → MRT 多输出。
- 函数体内经参数名访问（如 `in.uv`），返回值即阶段输出。

### 5.3 字段属性（后缀限定词）

```
pos: vec3 loc 0                  # 显式 location
clip: vec4 builtin position      # 内建语义（不占 location）
n: vec3 noperspective            # 插值限定词
k: i4 flat                       # 整数 varying 自动补 flat，也可显式
c: vec4 centroid                 # 质心采样
```

### 5.4 内建语义名（`builtin X`）全表

| 阶段·方向 | 语义名 | 类型 | GLSL 对应 |
|-----------|--------|------|-----------|
| vert 输出 | `position` | vec4 | `gl_Position` |
| vert 输入 | `vertex_id` | i4 | `gl_VertexIndex` |
| vert 输入 | `instance_id` | i4 | `gl_InstanceIndex` |
| frag 输入 | `frag_coord` | vec4 | `gl_FragCoord` |
| frag 输入 | `front_facing` | bool | `gl_FrontFacing` |
| frag 输入 | `sample_id` | i4 | `gl_SampleID` |
| frag 输入 | `point_coord` | vec2 | `gl_PointCoord` |
| frag 输出 | `frag_depth` | f4 | `gl_FragDepth` |
| comp 输入 | `global_invocation_id` | uvec3 | `gl_GlobalInvocationID` |
| comp 输入 | `local_invocation_id` | uvec3 | `gl_LocalInvocationID` |
| comp 输入 | `workgroup_id` | uvec3 | `gl_WorkGroupID` |
| comp 输入 | `num_workgroups` | uvec3 | `gl_NumWorkGroups` |
| comp 输入 | `local_invocation_index` | u4 | `gl_LocalInvocationIndex` |
| 任意阶段 | `subgroup_size` | u4 | `gl_SubgroupSize`（SPIR-V 1.3） |
| 任意阶段 | `subgroup_invocation_id` | u4 | `gl_SubgroupInvocationID`（SPIR-V 1.3） |

计算内建字段声明为标量（`u4`/`i4`）时自动取 `.x`（1D 调度惯用）：

```
@def CompIn: {
    gid: u4 builtin global_invocation_id    # uvec3 自动取 .x
}
```

## 6. 资源绑定

对齐 Vulkan 描述符模型（set / binding）；低版本目标（无显式 binding）自动退化为
反射清单按名绑定。

### 6.1 uniform 块（UBO，std140）

```
@def Camera: {
    mvp: mat4
} uniform set 0 binding 0
```

### 6.2 storage 块（SSBO，std430）

```
@def YBuf: {
    n: u4
    y[]: f4          # 运行时数组须是最后一个字段
} storage set 0 binding 2
```

### 6.3 push 常量

```
@def Push: {
    tint: vec4
} push               # 无 set/binding
```

### 6.4 全局采样器

```
var tex: sampler2D uniform set 0 binding 1
var tex: sampler2D set 0 binding 1        # 简写：默认 uniform
```

### 6.5 访问与校验

- 块成员经块名访问：`Camera.mvp`、`YBuf.y[i]`。
- std140/std430 字段偏移由编译器发射并导出到反射清单（§13.3）。
- 编译期检查：`(set, binding)` 冲突报错；sampler 与非 sampler 成员不得混入同一块。

### 6.6 shared 共享内存（comp 专属，P2）

```
@def Tile: {
    data[256]: f4        # 定长成员（不支持运行时数组）
} shared                 # 无 set/binding（非描述符资源）
```

- Workgroup 存储类（MSL threadgroup / GLSL shared），同工作组线程共享；
  访问同资源块：`Tile.data[i]`。跨线程读写间须 `barrier()` 同步（§9.4）。
- 不入反射清单（无 host 可见面）。

### 6.7 特化常量（let … spec N，P2）

```
let TILE: u4 = 64 spec 0     # constant_id = 0，默认值 64
let GAIN: f4 = 1.5 spec 1
```

- 顶层 `let` 尾缀 `spec N` → SPIR-V `OpSpecConstant`（SpecId N）/ MSL
  `function_constant(N)`：**管线创建期可覆写默认值**，无需重编译。
- 初值须为数值字面量，类型限 `i4`/`u4`/`f4` 标量。
- 反射清单导出 `spec_constants: [{name, id, type, default}]` 供运行时传值；
  仅 vulkan/metal 目标支持（glcore/gles 目标声明即报错）。
- 与 §11 的 `spec` 编译期特化维度是两套机制：§11 在编译期展开多份产物，
  本节在运行时管线创建期调参（同一份产物）。

## 7. 语句

```
var v: vec4                        # 局部变量（可带初值）
let k: f4 = 2.0                    # 常量
v.xy = in.uv                       # 赋值（含 swizzle 写入）

if cond                            # 分支
    ...
else
    ...

while cond                         # while 循环
    ...

do                                 # do-while 循环
    ...
while cond

for i = 0; i < 8; i++              # C 风格 for（无 for-in）
    ...

break                              # 跳出循环
continue                           # 下一次迭代

case x                             # 多路分支（自动 break；through 显式贯穿）
    0, 1:
        ...
    2:
        ...
        through                    # 贯穿到下一 arm
    default:
        ...

discard                            # 丢弃片元（仅 frag）
return expr                        # 返回（阶段输出 / 函数返回）
```

## 8. 表达式

### 8.1 运算符

| 类 | 运算符 |
|----|--------|
| 算术 | `+ - * / %`（`*` 含 mat×mat、mat×vec、vec×mat） |
| 比较 | `< > <= >= == !=` |
| 逻辑 | `&& \|\| !` |
| 位 | `& \| ^ ~ << >>` |
| 赋值 | `= += -= *= /=` |
| 自增减 | 前缀/后缀 `++` `--` |
| 三元 | `cond ? a : b` |

### 8.2 swizzle（读写）

```
p = v.xyz          # 读：任意 1–4 分量组合
v.xy = uv          # 写：多分量
v.x = 1.0          # 写：单分量
```

分量字母须同属 `xyzw` / `rgba` / `stpq` 之一，不可混用，最多 4 个。

### 8.3 构造与转换

```
vec3(1.0)                  # 标量广播（splat）
vec4(v3, 1.0)              # 嵌套向量拆分拼接
mat2(c0, c1)               # 矩阵按列构造
float(i)  int(f)  uint(i)  # 标量转换
(f4)(x)                    # sc 转换语法亦可
```

## 9. 内建函数

### 9.1 数学（GLSL.std.450）

| 类别 | 函数 |
|------|------|
| 舍入 | `round` `trunc` `floor` `ceil` `fract` |
| 符号 | `abs` `sign` |
| 三角 | `sin` `cos` `tan` `asin` `acos` `atan`（一元或 `atan(y,x)`）`sinh` `cosh` `tanh` |
| 指数 | `exp` `log` `exp2` `log2` `pow` `sqrt` `inversesqrt` |
| 极值/限制 | `min` `max` `clamp`（标量↔向量自动广播） |
| 插值 | `mix` `step` `smoothstep` `fma` |
| 几何 | `length` `distance` `dot` `cross` `normalize` `reflect` `refract` |
| 矩阵 | `transpose` `determinant` `inverse` |
| 取模 | `mod` |

### 9.2 纹理采样（全族）

| 函数 | 形态 | 说明 |
|------|------|------|
| `texture(s, p)` | 隐式 lod | frag 隐式导数；其他阶段 lod=0 |
| `texture(s, p, bias)` | lod 偏置 | 仅 frag |
| `textureLod(s, p, lod)` | 显式 lod | |
| `textureProj(s, p)` | 投影（末分量除） | `textureProjLod` / `textureProjGrad` / `textureProjOffset` 同族 |
| `textureGrad(s, p, dx, dy)` | 显式梯度 | `textureGradOffset` 同族 |
| `textureOffset(s, p, off)` | 纹素偏移 | `off` 须编译期常量；`textureLodOffset` 同族 |
| `textureGather(s, p[, comp])` | 4 邻点采集 | `comp`=0–3；shadow 采样器第三参为 dref |
| `texelFetch(s, ip, lod)` | 无过滤直读 | 整数坐标；MS 采样器第三参为 sample 索引 |
| `textureSize(s, lod)` | 尺寸查询 | 返回 `i4`/`ivec*` |
| `textureQueryLod(s, p)` | lod 查询 | 返回 vec2；仅 frag |
| `textureQueryLevels(s)` | mip 层数 | 返回 i4 |

- **shadow 采样器**：坐标末分量为对比参考值（dref），返回 `f4`。
- **整数/无符号采样器**：返回 `ivec4` / `uvec4`。

### 9.3 导数（仅 frag）

`dFdx(e)` `dFdy(e)` `fwidth(e)`

### 9.4 计算原语（comp，P2）

| 函数 | 语义 | SPIR-V |
|------|------|--------|
| `barrier()` | 工作组内控制+内存同步（仅 comp） | `OpControlBarrier` |
| `memory_barrier()` | 设备级内存屏障 | `OpMemoryBarrier` |
| `atomic_add(m, v)` / `atomic_sub` | 原子加/减，返回旧值 | `OpAtomicIAdd/ISub` |
| `atomic_min` / `atomic_max` | 原子极值（按符号选指令） | `OpAtomicS/UMin·Max` |
| `atomic_and` / `atomic_or` / `atomic_xor` | 原子位运算 | `OpAtomicAnd/Or/Xor` |
| `atomic_exchange(m, v)` | 原子交换 | `OpAtomicExchange` |
| `atomic_cas(m, cmp, v)` | 比较交换（等于 cmp 才写 v，返回旧值） | `OpAtomicCompareExchange` |

- 原子首参 `m` 须为 **storage 块或 shared 成员**的 `i4`/`u4` 标量（含数组元素）。
- GLSL 对照链：`atomic_sub` 发 `atomicAdd(m, -(v))`（GLSL 无 atomicSub）。

### 9.5 subgroup 基础（P2，SPIR-V 1.3）

| 函数 | 返回 | 语义 |
|------|------|------|
| `subgroup_all(b)` / `subgroup_any(b)` | bool | 子组全体/任一投票 |
| `subgroup_ballot(b)` | uvec4 | 谓词分布位图 |
| `subgroup_shuffle(v, lane)` | 同 v | 跨通道取值 |

- 用到任一 subgroup 能力（含 §5.4 的 subgroup 内建）时，SPIR-V 版本自动升
  1.3（Vulkan 1.1）并发 `GroupNonUniform*` Capability；其余产物保持 1.0。
- 能力门控：vote/ballot/内建需 metal≥2.1；shuffle metal≥2.0；glcore/gles 经
  `GL_KHR_shader_subgroup_*` 扩展（caps 档案声明）；webgl 永不支持。

## 10. 辅助函数 `fnc`

```
fnc lerp3: vec3, a: vec3, b: vec3, t: f4
    return a + (b - a) * t
```

- 阶段函数体内直接调用；同签名函数共享一个函数类型。
- 不支持递归、函数指针、可变参数（见 §12）。

## 11. spec 编译期特化维度

同一份 `.ss` 按有限枚举维度在编译期展开为多个单态实例。完整设计见 [spec.md](spec.md)。

### 11.1 无体 spec（类型/符号直代）

```
spec TEX_KIND in [sampler2D, samplerExternalOES]

var tex: TEX_KIND uniform set 0 binding 1    # 维度名出现在类型/符号位置
```

### 11.2 有体 spec（分支标签即取值）

```
spec BLEND:
    ADD:
        fnc blend: vec3, a: vec3, b: vec3
            return a + b
    MUL:
        fnc blend: vec3, a: vec3, b: vec3
            return a * b
```

约束：分支体内同名 `@def` 须接口同构（字段名集合一致，类型可不同）。

### 11.3 use 白名单

```
use TONE, BLEND          # 表头：维度序
    tm_a, ADD            # 每行一个实例组合
    tm_b, MUL
```

- 默认输出**全笛卡尔积**（组合数 >32 报错）；声明 `use` 后仅输出白名单组合。
- 未列维度取全集与已列行做积。
- 产物带实例标签后缀（如 `fs_main.sampler2D.ADD.vulkan450.spv`），反射清单附
  `"spec": {维度: 取值}` 字段供运行时按设备能力选实例。

## 12. 方言子集限制（编译期报错）

| 禁用构造 | 原因 |
|----------|------|
| 堆分配、`chunk`/`recycle`、`mem` | GPU 无堆 |
| 指针族：`&` 取址、`*` 解引用、`->`、指针类型转换 | GPU 逻辑寻址 |
| `rpc` / `run` / `sync` / `async` / `await` / `done` | 无线程/异步 |
| `tls` / `form` / `back` / `mix` 宏 / `inl` / `final` / `goto` / 标签 | 方言外构造 |
| `print` / `assert` | 无控制台/运行时断言 |
| 函数字面量 / 闭包、递归、可变参数 | SPIR-V 不可表达 |
| `discard`、导数、`texture` bias 用于非 frag 阶段 | 阶段专属 |
| `for-in`、多维数组 | 暂未支持（路线见设计文档 §16） |

能力门控类（按 `tar` 目标裁剪，报错点名目标与补救途径）：`storage`/`comp`
需 glcore≥430 / gles≥310；`u*` 无符号需 gles≥300；`vertex_id`/`instance_id`
需 gles≥300；`frag_depth` 在 gles100 经 `GL_EXT_frag_depth`；P2 计算深化：
`shared`/`barrier`/`atomic_*` 同 comp 档（webgl 永不支持），`spec` 特化常量仅
vulkan/metal，subgroup 见 §9.5；P3 窄/宽标量：`f2` 需 vulkan/metal（gl 经
GL_EXT_shader_explicit_arithmetic_types 扩展声明），`i8/u8` 需 vulkan/metal≥2.3，
`i1/u1`/`i2/u2` 同 f2 档；等（单一事实源 `shader_caps.h` 能力表）。

## 13. 编译与产物

### 13.1 命令行

```sh
scc file.ss -o out/stem              # 默认：资源化产物 stem.shader.h/.c
scc file.ss -o out/stem --files      # 散文件：逐实例逐目标落盘
scc file.ss --emit-glsl              # 自研 GLSL 文本发射（对照/兜底通道）
scc file.ss -o out/stem --emit-spv   # 非 vulkan 目标额外落 .spv 中间文件
scc file.ss --ast                    # AST JSON（spec 以首实例呈现）
```

### 13.2 产物链

全目标统一 SPIR-V 中枢（AST 直发）：`vulkan` 直落 `.spv`；`metal` 经
SPIRV-Cross → MSL；`glcore`/`gles` 经 SPIRV-Cross 反译 GLSL（ES100 legacy 形态，
uniform 块平铺 + `attribute`/`varying`/`gl_FragColor`）。
**例外：`cpu` 目标不过 SPIR-V**，由 codegen_cpu 直发 C 源码（见 §13.4）。

资源化产物（默认）：`<stem>.shader.h/.c` —— 字节数组（文本带尾 NUL）+ enum id +
反射 JSON 内嵌 + `sc_shader_<sym>_get(i)` / `_find(entry, target, ext)` 查询；
`add out/stem.shader.c` 直接链入应用，零运行时文件路径。

散文件命名：`<entry>[.<spec标签>][.<目标tag>].<ext>`，如
`fs_main.sampler2D.tm_a.metal20000.metal`。

### 13.3 反射清单 JSON

```json
{
  "target": {"api": "vulkan", "version": 450,
              "explicitBinding": true, "flattenUniforms": false},
  "spec": {"TEX_KIND": "sampler2D"},
  "stages": [{
    "name": "vs_main", "stage": "vert", "entry": "main",
    "inputs": [{"name": "pos", "type": "vec3", "location": 0}],
    "outputs": [{"name": "uv", "type": "vec2", "location": 0},
                 {"name": "clip", "builtin": "position"}],
    "local_size": [64, 1, 1]
  }],
  "resources": [{
    "name": "Camera", "kind": "uniform", "set": 0, "binding": 0,
    "layout": "std140",
    "members": [{"name": "mvp", "type": "mat4", "offset": 0, "size": 64}],
    "size": 64
  }]
}
```

- `kind`：`uniform` / `storage` / `push` / `sampler`。
- 低版本目标（无显式 binding）`set`/`binding` 为 -1，运行时按 `name` 绑定；
  legacy ES 目标 `flattenUniforms: true`（块字段平铺为 `块_字段` 普通 uniform）。
- `spec` 字段仅在该实例来自 spec 展开时出现。

### 13.4 CPU/SIMD 后端（`tar cpu@99`）

同一份 comp kernel 也可编到 **CPU 并行执行**（无 GPU 的嵌入式/DSP 场景、
或作为 GPU 结果的数值对拍基准）。设计详见设计文档 §17。

- **执行模型**：SPMD 循环化——invocation → 循环迭代，**SIMD 向量化交给
  目标 C 编译器**（NEON/SVE/AVX/RVV/DSP 向量指令由 `-march`/厂商工具链
  自动兑现，无需逐 ISA 适配；产物是带 `restrict` + 向量化提示的规整 C99）。
  含 `barrier()` 的 kernel 经相位分裂变换（按 barrier 切相位；`shared` =
  工作组局部数组；`atomic_*` = C11 原子）；workgroup 间多线程分片。
- **产物形态**：`<stem>.cpu.c` 散文件（待编译源码，不进资源字节数组）——
  宿主 `add out/stem.cpu.c` 编入后构造器自注册；运行时经 spc 的
  `desc.kernel_backend = SC_SPC_KERNEL_CPU` 选用（同一套 dispatch API）。
- **能力集**（能力表 cpu 列）：计算面全支持（storage/uniform/shared/
  barrier/atomic/spec 传值/f2/i8·u8/i1·u1/i2·u2）；图形面（采样器/导数/
  MRT）与 subgroup 不支持（硬门控报错）。
- **当前限制**（超出编译期报错）：仅标量与标量数组（无 vec/mat）、
  uniform 块限标量成员（数组用 storage）、无 push 块、`barrier()` 仅
  kernel 顶层或顶层 `while` 内、含 barrier 的工作组限 1D。
- 运行时消费与板端验证：[builtins/spc/PORTING.md](builtins/spc/PORTING.md) §8。

---

## 附：最小完整示例

```
tar vulkan@450, metal@2.0

@def Camera: {
    mvp: mat4
} uniform set 0 binding 0

var albedo: sampler2D uniform set 0 binding 1

@def VsIn: {
    pos: vec3 loc 0
    uv:  vec2 loc 1
}

@def VsOut: {
    clip: vec4 builtin position
    uv:   vec2
}

vert vs_main: VsOut, in: VsIn
    var o: VsOut
    o.clip = Camera.mvp * vec4(in.pos, 1.0)
    o.uv = in.uv
    return o

frag fs_main: vec4, in: VsOut
    return texture(albedo, in.uv)
```
