
## sc 空间计算扩展——设计决策与路线图（syntax-s-design）

> **文档分工**（ 2026-07-10 拆分）：本文档保留 syntax-s 的**设计决策、编译管线、
> 路线图与历史依据**；纯语言语法参考已独立为 [syntax-s.md](syntax-s.md)（只收录已实现
> 语法）。编译器源码注释中的 `syntax-s §N` 锚点指本文档章节（编号未变）。

本手册是 [syntax.md](syntax.md)（sc 语言主手册）的**独立配套文档**，专门描述 sc 的
GPU **空间计算**（渲染着色 + 并行计算）开发扩展。定位——**设计依据**与**演进路线图**；
作为独立特性单独维护，避免与语言核心混为一谈。

> **命名旨趣（syntax-g → syntax-s）**
>
> sc 的 **s = space（空间）**：cpu 是串行·逻辑·时间，gpu 是并行·变换·空间——正好一对。
> 现代 GPU 已超越图像渲染（graphics），扩展到空间计算/AI 并行计算；本方言同时
> 承载 `vert`/`frag`（渲染）与 `comp`（计算），故从“g”改名“s”。源文件扩展名同步
> 由 `.sg` 改为 **`.ss`**（space source）。运行时消费侧对应三个模块：
> [builtins/gfx](builtins/gfx/)（渲染）、[builtins/spc](builtins/spc/)（计算），
> 共同架在 [builtins/gpu](builtins/gpu/)（运行环境）之上。

> **当前状态（重要）**
>
> **一期（最小可用）已落地**：`.ss` 文件路由、`vert`/`frag`/`comp` stage 解析、
> `shader_sema` 子集强制、`codegen_glsl` 产 Vulkan-GLSL 文本 + 反射清单 JSON、
> 属性语法（`loc`/`builtin`/`uniform`/`storage`/`push` + `set`/`binding`）、stage I/O
> 成员改写、std140/std430 布局偏移均已实现并回归通过。
>
> **原 §13 的 P0–P2 已全部落地成模块体系**：GL 版本适配（`tar` 多目标，§13.1）、
> 运行环境与渲染（builtins/gpu + builtins/gfx，Metal/GL 双后端三角形实机验证）、
> 窗口层（utils/wsi）。`comp` 计算链路亦已打通（计算内建映射、local_size、
> 反射携带），由 builtins/spc 消费（Metal compute 实机验证）。文中未落地部分仍为
> **提案**，标注「（待定）」处实现前可调整。

> **两条硬约束（贯穿全文）**
>
> 1. **语言仍是 sc**：不引入新语言。shader 是 sc 的一个**方言子集**——复用 sc 的词法、
>    parser、类型系统与模块机制，只增加 stage 关键字、向量/矩阵类型与资源绑定语义。
> 2. **能力对齐 SPIR-V**：SPIR-V（Khronos 着色器中枢 IR）支持什么，sc-shader 才开放什么。
>    这是「反向裁剪」——不是先设计语言再找后端，而是以 SPIR-V 能力集为上界，用语义分析 +
>    语法插件把 sc 在 shader 语境下**收窄**到可安全下译的子集。

---

## 1. 设计原则

- **中枢 IR 是 SPIR-V**。整个着色器工具链在业界已收敛到 SPIR-V 作为交换格式；sc 不重新发明，而是把它当作事实标准的目标中枢（与 sc→C99→系统 cc 的哲学同构：生成中间表示，交给成熟后端）。
- **三期（当前）：AST 直发 SPIR-V**（`codegen_spirv`），去除 glslang 中转；自研 GLSL 文本发射保留（双 emit：gl/gles 目标产物 + 调试选项）。理由见 §2.2。
- **Mac 优先**：作者主力平台为 macOS，Metal 后端优先级高于 D3D/GLES。落地路径见 §2.3。
- **依赖最小化**：scc 只链 SPIRV-Cross（跨后端扇出）；SPIR-V 生成自研，不再需要 glslang/shaderc。
- **子集而非超集**：shader 方言是 sc 的严格子集——禁用堆分配、裸/自动指针、递归、函数指针
  等 GPU 无法表达的构造（见 §4、§9）；完备性以 SPIR-V 能力为反向对齐目标，按优先级渐进（见 §16）。

### 1.1 技术栈边界（自研 vs 开源）

**一句话**：前端全自研；后端自研到 **SPIR-V 二进制发射**（GLSL 文本发射并存，服务 gl/gles
目标与交叉验证，跑稳后再评估退役）；SPIR-V→各平台语言的扇出用成熟开源件（SPIRV-Cross），**跨后端翻译不重复造轮子**。

| 层 | 归属 | 对应件 | 理由 |
|----|------|--------|------|
| 词法 / 语法 / 语义 | **自研** | lexer / parser / `shader_sema` | 复用 sc 现有前端，这是「语言是 sc」的根本 |
| codegen（AST → SPIR-V 二进制） | **自研** | `codegen_spirv.cpp` | ss 是**受控子集**：只需为自己发射的模式生成 SPIR-V，不需要支持全 GLSL 规范；与 AST→GLSL 文本的语义映射工作量同级，只是编码形式不同 |
| codegen（AST → GLSL 文本） | **自研** | `codegen_glsl.cpp` | gl/gles 目标产物（含 ES100 遗留形态/平铺 uniform 等运行时契约）+ 可读调试通道（--emit-glsl） |
| SPIR-V → MSL / HLSL / GLSL | **开源** | SPIRV-Cross | 各后端绑定/精度/寄存器怪癖已被它解决 |
| Vulkan+SPIR-V → Metal（运行时） | **开源** | MoltenVK | Mac 开发路径，无需自研 |

~~GLSL 文本 → SPIR-V（glslang）~~：**已移除**。一期理由是「跟踪全 GLSL 规范的成熟编译器，
自研 = 数年工作」——但实践证明前提不成立：我们不需要编译「任意 GLSL」，只需编译
**自己发射器产出的受控模式**；而经 GLSL 文本中转会丢失 AST 语义（反向重推）、引入双重
映射维护（sc→GLSL→SPIR-V 两张语义表），且 glslang 作为 vendored 静态库已是 scc 最重的
构建依赖。AST→SPIR-V 与 AST→GLSL 在语义层面等价，发射目标不同而已。

两点澄清：

- **自研范围最多到「发射 SPIR-V」，绝不下探到跨后端翻译**——SPIR-V→MSL/HLSL/各版本 GLSL
  的扇出永远用 SPIRV-Cross。
- **验证体系外部化**：发射器正确性靠 spirv-val（SPIRV-Tools，外部工具非链接依赖）+
  SPIRV-Cross 反译 round-trip + 旧链产物对照 + demo 实机渲染四重验证。

---

## 2. 编译管线与路线

### 2.1 一期管线（sc → Vulkan-GLSL 文本）——**历史，已被 §2.2 取代**

```mermaid
flowchart LR
    A[.ss 源码<br/>vert / frag / comp] --> B[scc 前端<br/>parser + shader_sema]
    B --> C[shader AST]
    C --> D[codegen_glsl]
    D --> E[Vulkan-GLSL 文本<br/>+ 反射清单 .json]
    E --> F[运行时: shaderc / glslang]
    F --> G[SPIR-V]
    G --> H{目标后端}
    H -->|Vulkan| I[直接使用 SPIR-V]
    H -->|Metal| J[SPIRV-Cross → MSL<br/>或 MoltenVK 直吃 SPIR-V]
    H -->|GL / GLES| K[SPIRV-Cross → GLSL]
    H -->|D3D| L[SPIRV-Cross → HLSL]
```

关键点：**scc 只负责到「Vulkan-GLSL 文本 + 反射清单」为止**。SPIR-V 生成与跨后端扇出全部
交给成熟的开源件（glslang / shaderc / SPIRV-Cross），sc 一个后端都不用自己写。

### 2.2 三期管线（AST → SPIR-V 直发，当前，进行中）

```mermaid
flowchart LR
    A[.ss 源码] --> B[scc 前端<br/>parser + shader_sema]
    B --> C[shader AST]
    C --> D[codegen_spirv<br/>自研直发]
    C -."--emit-glsl / gl·gles 目标".-> E[codegen_glsl<br/>GLSL 文本]
    D --> G[SPIR-V 中枢 IR]
    G -->|vulkan 目标| I[.spv 产物直落盘]
    G -->|metal 目标| J[SPIRV-Cross → MSL]
    G -."--emit-spv".-> S[.spv 中间文件]
    G -."二期评估".-> K[SPIRV-Cross → GLSL<br/>统一 gl/gles 路径]
```

要点：

- **AST 直发 SPIR-V**：发射形式与 glslang 同构（Logical 寻址、OpVariable + Load/Store、
  不做 SSA/phi，结构化控制流 OpSelectionMerge/OpLoopMerge，数学库走 GLSL.std.450
  扩展指令集）——驱动与 SPIRV-Cross 均消费无碍，无需优化器。
- **双 emit**：`codegen_spirv` 是默认链唯一发射器（全目标）；`codegen_glsl` 保留作
  对照/兜底，经 `--emit-glsl` 输出（产物同名可替换，见 §14.3）。`--emit-spv` 落盘
  SPIR-V 中间文件。
- **默认产物链**：metal 目标 = SPIR-V→SPIRV-Cross→MSL；vulkan 目标 = **直落 `.spv`**
  （不再输出 GLSL 文本要求用户自跑 glslangValidator）；glcore/gles = SPIRV-Cross 反译
  GLSL（ES100 legacy 形态）。
- **glslang 整体移除**（vendor/glslang-src、SCC_WITH_GLSLANG、shader_spv 封装）；
  SPIRV-Cross 保留（SCC_WITH_SPIRV_CROSS）。
- 反射清单仍由 AST 侧自研生成（emitReflectionJson，不变）。

历史：~~一期「Vulkan-GLSL 文本 + 运行时 glslang」→ 二期「glslang 静态链入 scc 离线完成」~~
均已被本期取代；迁移期内旧链产物作为黄金对照。

### 2.3 Mac 优先的落地路径

macOS 无原生 Vulkan，两条推荐路径（一期均可用，运行时侧集成，scc 无差别）：

| 路径 | 链路 | 适用 |
|------|------|------|
| **MoltenVK（推荐用于开发）** | Vulkan-GLSL → SPIR-V →（MoltenVK 运行时翻译）→ Metal | 一套 Vulkan 代码直接在 Metal 上跑，零额外离线转译 |
| **SPIRV-Cross → MSL（推荐用于发行）** | Vulkan-GLSL → SPIR-V → SPIRV-Cross → MSL → Metal 编译 | 产出原生 `.metal`/`.metallib`，无 MoltenVK 依赖 |

远期可加 `codegen_msl` 直接产 MSL（跳过 SPIRV-Cross），但一期不做。

### 2.4 文件模型与扩展名 `.ss`

shader 代码放在**独立的 `.ss` 文件**（sc graphics），**不与宿主 `.sc`（编向 C）混编**；
一个 `.ss` 文件内可**混编多个 stage**（vert/frag/comp）+ 辅助函数 + 共享类型，stage 由
关键字标出入口。这与现代着色语言的共识一致：

| 语言 | 文件 | stage 区分 | 与宿主混编 |
|------|------|-----------|-----------|
| GLSL（传统） | `.vert` / `.frag` 各一 | **靠扩展名** | 否 |
| HLSL | 单个 `.hlsl` | 多入口函数 | 否 |
| Metal | 单个 `.metal` | `vertex`/`fragment`/`kernel` 限定词 | 否 |
| WGSL | 单个 `.wgsl` | `@vertex`/`@fragment`/`@compute` | 否 |
| Slang | 单个 `.slang` | `[shader("vertex")]` 属性 | 否 |
| **sc** | **单个 `.ss`** | **`vert`/`frag`/`comp` 关键字** | **否** |

**扩展名与关键字职责不同，二者不冗余**：
- `.ss` 扩展名 → 编译器/编辑器识别「整文件是 shader 方言」，路由到 `codegen_glsl`、全文件
  套用 §9 子集规则、LSP 用 shader 语义。
- `vert`/`frag`/`comp` 关键字 → 在文件内标出**哪个函数是入口**（一个 `.ss` 可含多个入口）。

这正是 Metal 的模型（`.metal` 扩展名已确定是 shader 文件，函数前照样写 `vertex`/`kernel`
限定词）。**只有传统 GLSL「一个 `.vert` 一个 `.frag`」的模型才让 stage 关键字冗余**——而它
还强行拆开几乎总要共享同一 varying 结构体（`VsOut`）的 vert 与 frag，故不采用。

**为何不选 host + shader 同文件混编**：一个 TU 同时产出 C 与 GLSL 两种目标、语义域要在文件
内劈开，复杂度陡增；且无主流生态如此做；类型共享用共享模块即可达成（见下）。

**host↔shader 类型共享**：宿主 `.sc` 与着色 `.ss` 各自 `inc` 同一个**共享 `@def` 模块**——
host 侧编成 C 结构体、shader 侧编成 GLSL block，编译期校验 std140 偏移一致（见 §7）。分文件
不妨碍共享，边界反而更干净。

---

## 3. 顶层语法：stage 关键字

`vert` / `frag` / `comp` 是与 `fnc` / `rpc` 平级的**新顶层关键字**，声明着色器阶段入口
（位于 `.ss` 文件，一个文件可混编多个 stage，见 §2.4）：

| 关键字 | 阶段 | SPIR-V ExecutionModel | GLSL 阶段 |
|--------|------|----------------------|-----------|
| `vert` | 顶点着色 | Vertex | `.vert` |
| `frag` | 片元着色 | Fragment | `.frag` |
| `comp` | 计算着色 | GLCompute | `.comp` |

> **另有顶层指令 `tar`**（与 stage 关键字平级）声明该 `.ss` 的 GPU 目标与版本，如
> `tar vulkan@450` 或 `tar gles@100, gles@300`；它同时驱动 codegen 输出形态与 sema 能力
> 裁剪（源码内声明而非 CLI，以支撑编辑期诊断与整体构建），详见 §13.1。

阶段入口形似函数，但语义受限（无副作用堆操作、参数即阶段 I/O）：

```
# —— 提案语法（待定）——

# 顶点属性输入（按 location 绑定到顶点缓冲）
@def VsIn: {
    pos: vec3   loc 0
    uv:  vec2   loc 1
}

# 传给片元阶段的 varying（内建 position + 自定义插值量）
@def VsOut: {
    clip: vec4   builtin position   # → gl_Position / SV_Position / [[position]]
    uv:   vec2
}

# uniform 块（set/binding 绑定；见 §6）
@def Camera: { mvp: mat4 }   uniform set 0 binding 0

vert vs_main: VsOut, in: VsIn
    var o: VsOut
    o.clip = Camera.mvp * vec4(in.pos, 1.0)
    o.uv = in.uv
    return o

frag fs_main: vec4, in: VsOut
    return texture(albedo, in.uv)
```

> `loc N` / `builtin X` / `uniform set..binding..` 的**属性附着语法待定**。sc 的 `@` 前缀已
> 用于导出（见主手册 §8），故 shader 属性倾向用**后缀限定词**（如上）或专门的属性括号语法，
> 具体在实现前定稿。此处仅示意语义。

---

## 4. shader 类型系统（SPIR-V 能力子集）

在 shader 语境下，sc 的类型系统按 SPIR-V 能力**收窄并扩展**：

### 4.1 新增：向量 / 矩阵

| sc 类型 | 含义 | GLSL |
|---------|------|------|
| `vec2/3/4` | f4 向量 | `vec2/3/4` |
| `ivec2/3/4` | i4 向量 | `ivec*` |
| `uvec2/3/4` | u4 向量 | `uvec*` |
| `bvec2/3/4` | bool 向量 | `bvec*` |
| `mat2/3/4` | f4 方阵 | `mat*` |
| `mat3x4` 等 | 非方阵 | `matCxR` |

- **Swizzle**：`v.xyz` / `v.rgba` / `v.st`（读写皆可，遵循 GLSL 规则）。
- **构造**：`vec4(v3, 1.0)`、`vec3(1.0)`（标量广播）。
- 标量沿用 sc：`f4`(float) / `i4`(int) / `u4`(uint) / `bool`；`f8`(double) 仅在后端支持
  时开放（SPIR-V `Float64` 能力，多数移动 GPU 不支持——语义分析按目标裁剪）。

### 4.2 新增：不透明资源类型

`sampler2D` / `sampler3D` / `samplerCube` / `image2D` / `texture2D` + `sampler` 等
（对齐 Vulkan-GLSL / SPIR-V 的分离式 texture+sampler 模型）。

### 4.3 禁用（GPU 无法表达）

- 堆分配、`chunk`/`recycle`、`mem` 模块。
- 裸指针 `T&`、自动指针 `T@`/`object@`、瘦指针 `T*`、单例指针 `T@1`。
- 递归、函数指针字段、`rpc`、`async`、可变参数。
- 大部分 C 互通 `::`（除非映射到 GPU 内置）。

违反即**编译期报错**，报错信息指明「shader 方言不支持 X」（见 §9）。

---

## 5. 阶段 I/O 与 varying

阶段间数据流用**结构体**表达（比 GLSL 的全局 `in`/`out` 更结构化）：

- **顶点属性**：`vert` 入参结构体，字段带 `loc N` → GLSL `layout(location=N) in`。
- **varying**：`vert` 返回结构体 → 下译为 `out` block；`frag` 同名入参 → `in` block，
  location 自动配对。
- **内建变量映射**（跨后端由 SPIRV-Cross 处理，sc 只需标注语义）：

| sc 标注 | GLSL | HLSL | MSL |
|---------|------|------|-----|
| `builtin position` | `gl_Position` | `SV_Position` | `[[position]]` |
| `builtin vertex_id` | `gl_VertexIndex` | `SV_VertexID` | `[[vertex_id]]` |
| `builtin frag_coord` | `gl_FragCoord` | `SV_Position` | `[[position]]` |
| `builtin frag_depth` | `gl_FragDepth` | `SV_Depth` | `[[depth]]` |

---

## 6. 资源绑定

对齐 Vulkan 描述符模型（descriptor set / binding）：

- **Uniform 块**：`@def X: {...} uniform set S binding B` → `layout(set=S, binding=B) uniform`。
- **Storage 块（SSBO）**：`... storage set S binding B` → `buffer`。
- **Push constant**：`... push` → `layout(push_constant)`。
- **纹理/采样器**：`albedo: sampler2D  set S binding B`。

内存布局：uniform 默认 `std140`，storage 默认 `std430`；由 codegen 显式发射
`layout(std140/std430)`，并在反射清单里导出每字段偏移（供 §7 校验）。

---

## 7. CPU↔GPU 数据共享（`@def` 复用，opt-in）

**决策：默认分开**（CPU 侧 `@def` 与 shader 侧类型互不可见），**但对 uniform/SSBO 结构体
提供可选的「共享布局」能力**。

**为什么值得提供**：shader 开发最高频、最难查的一类 bug 是——CPU 侧上传 uniform 的 C 结构体
与 GPU 侧 shader 里的 block 声明，二者 `std140/std430` 字节偏移不一致（错位后画面错乱但不
报错、不崩溃）。让**同一份 `@def` 同时生成 C 布局和 GLSL block**，编译期校验偏移一致，能从
根上消灭它——这是 GLSL/HLSL 手写做不到、而 sc（同时能编 CPU 和 GPU）天然能做的差异化价值。

一期先把语法「门」留好、默认分开；共享作为二期特性：

```
# —— 提案（二期）——
@def Camera: { mvp: mat4; eye: vec3; _pad: f4 }  shared uniform binding 0
# scc 同时产出：
#   · C 侧 struct sc_Camera（供 CPU 填充上传）
#   · GLSL uniform block（供 shader 使用）
#   · 编译期断言两者 std140 偏移逐字段一致，否则报错
```

---

## 8. 内置函数库

shader 内置函数映射到各后端的原生内置（由 codegen 转名，SPIRV-Cross 保证跨后端一致）：

- **数学**：`dot` / `cross` / `normalize` / `length` / `mix` / `clamp` / `pow` / `min` /
  `max` / `abs` / `floor` / `fract` / `mod` …
- **纹理**：`texture(sampler, uv)` / `textureLod` / `texelFetch` …
- **导数（frag）**：`dFdx` / `dFdy` / `fwidth`。
- **计算（comp）**：`barrier` / `groupMemoryBarrier` 等。计算内建已支持（经 `builtin`
  属性标注，见 §5）：`global_invocation_id` / `local_invocation_id` / `workgroup_id` /
  `num_workgroups`（uvec3；字段声明为标量 u4/i4 时自动取 `.x`，1D 调度惯用）、
  `local_invocation_index`（uint）。工作组尺寸暂无 `.ss` 语法，编译器固定发射
  `local_size = 64×1×1` 并在反射清单 stages[] 携带，运行时（builtins/spc）据此设
  线程组；自定义 local_size 语法属二期。

考虑作为 shader 专属 builtins 模块（如 `builtins/shader/` 或 `templates/shader/`）暴露给
用户，声明这些内置的 sc 签名 + 到 GLSL 内置的桥接（细节待定）。

---

## 9. 语义限制与语法插件

按 Q4 决策——**SPIR-V 支持什么，sc-shader 才开放什么**，用两层机制强制：

1. **语义分析层（`shader_sema`）**：在 stage 函数体内做子集检查——遇到禁用构造（堆/指针/
   递归/rpc/…）即报错，报错文案对齐主手册「零误报」风格，明确指出「shader 方言不支持」。
   向量/矩阵/资源类型的类型检查、swizzle 合法性、stage I/O 配对、binding 冲突检测均在此。
2. **语法插件（vscode-sg / vscode-ast）**：在 shader 上下文中高亮 stage 关键字、
   向量类型、内建变量，并对禁用构造给出编辑期提示（与编译期一致，早失败）。

---

## 10. 运行时集成

按 Q3 决策——**一期运行时嵌 glslang/shaderc 动态编译**，scc 只产文本：

- scc 产物：每个 stage 一份 `.vert/.frag/.comp` GLSL 文本 + 一份**反射清单**（JSON）：
  stage 类型、入口名、各 uniform/纹理的 set/binding、顶点属性 location、uniform 布局偏移。
- 运行时：应用链接 `libshaderc`（或 glslang），加载时把 GLSL 文本编成 SPIR-V，按反射清单
  建立管线绑定；Mac 上再经 MoltenVK 或 SPIRV-Cross→MSL 落到 Metal（见 §2.3）。
- scc 一期**完全不碰 SPIR-V 二进制**，也不链接任何 shader 库——保持单二进制、零膨胀。

---

## 11. 与业界方案的关系

按 Q5 决策——**有成熟方案就不重新发明轮子**：

| 方案 | 角色 | sc 的取用 |
|------|------|-----------|
| **SPIR-V** | 中枢 IR | 直接作为目标中枢 |
| **glslang / shaderc** | GLSL→SPIR-V | 运行时依赖（一期） |
| **SPIRV-Cross** | SPIR-V→MSL/GLSL/HLSL | 跨后端扇出，尤其 Mac 的 MSL |
| **MoltenVK** | Vulkan+SPIR-V→Metal（运行时） | Mac 开发期首选 |
| **Slang** | 「一语言多后端」着色语言（Khronos 托管） | **借鉴其资源/参数模型概念**；远期可作**可选后端**（sc→Slang 白嫖其 autodiff/泛型多后端），一期不依赖 |
| **WGSL** | WebGPU 着色语言 | 参照其可移植子集；能力被 WebGPU 最小公共集限制，非一期目标 |

**为何一期选 Vulkan-GLSL 而非直连 Slang/SPIR-V**：Vulkan-GLSL 是 glslang/SPIRV-Cross 直接
消费的格式、文本可读可调试、工作量最小，完全复刻 sc「先产文本、后接成熟工具」的既有哲学。
Slang 的 autodiff/泛型多后端很诱人，但引入它是一个大依赖，留待远期评估。

---

## 12. 编译器模块划分

按用户要求——**shader 相关功能独立成模块，被整合而非混入现有文件**。落地时的模块规划：

| 模块 | 职责 | 关系 |
|------|------|------|
| `lexer.cpp`（既有，微改） | 新增 `vert`/`frag`/`comp` 及向量类型关键字 token | 仅加 token，主体不变 |
| `parser.cpp`（既有，挂钩） | stage 顶层声明的解析入口，转调 shader 专属解析 | 只加一个分发钩子 |
| `shader_ast.h`（新） | shader 专属 AST 节点（stage 入口、向量/矩阵类型、绑定属性、swizzle） | 独立头，最小侵入 ast.h |
| `shader_sema.cpp/.h`（新） | shader 方言子集强制、类型检查、stage I/O 配对、binding 冲突检测 | 独立于 semantic.cpp |
| `codegen_glsl.cpp/.h`（新） | shader AST → Vulkan-GLSL 文本 + 反射清单 | 独立于 codegen_c.cpp |
| `codegen_spirv.cpp/.h`（新，二期） | shader AST → SPIR-V 二进制直发 | 二期 |
| `codegen_msl.cpp/.h`（新，远期） | 直产 MSL（跳过 SPIRV-Cross） | 远期 |

**整合方式**：`main.cpp` 按**文件扩展名**分派——`.ss` 走 shader 子管线
（`shader_sema` → `codegen_glsl`），`.sc` 走现有 sc→C 管线，二者并列而非交织；共享
lexer/parser 基础设施但各自的语义/代码生成完全独立。CMakeLists 相应新增这些独立编译单元。

---

## 13. 待做能力（近期优先）——**已全部落地**（保留作设计依据）

> 本节三项能力已实现并模块化：P0 = `tar` 多目标（§13.1）；P1 = builtins/gpu +
> builtins/gfx（运行环境/渲染，Metal+GL 双后端）；P2 = utils/wsi（自研窗口层）。
> 计算路另落地为 builtins/spc（kernel/graph/model 三入口，含 ANE 推理）。
> 下文保留当时的设计取舍作历史依据，细节以各模块 README 为准。

一期已把「`.ss` → Vulkan-GLSL 450 → SPIR-V → MoltenVK 三角形」的**最小闭环**跑通，但要达到
「写 `.ss` + 少量 sc 胶水就能出画面、且能跑在不止一个平台」的**无感好用**，还差三块能力。
按优先级排列，前置依赖关系为：**P0 编译器侧（版本/能力）** 先行，**P1/P2 主机侧模块** 随后
（后两者合起来正是要替换掉 demo 里手写的 C + glfw + Vulkan 样板）。

### 13.1 GL 语言版本适配与能力裁剪（P0，最高优先级；已实现）

**现状**：`tar <api>@<version>` 顶层指令声明转义目标；`shader_caps.h` 统一维护能力表
（能力 × api/version），`shader_sema` 按契约门控，`codegen_glsl` 按目标发射对应
`#version`、profile、precision、绑定语法与（多目标）产物命名。

**要做的**：让「输出什么 GL 版本 / profile」成为一个**目标（target）参数**，贯穿 codegen 与 sema。

- **目标矩阵（至少覆盖）**：

  | target | 版本头 | 典型场景 |
  |--------|--------|----------|
  | `vulkan` | `#version 450`（或 460） | 一期现状；glslang/MoltenVK |
  | `gl-core` | `#version 330/410/430 core` | 桌面 OpenGL（Linux/Win/Mac 兼容 profile） |
  | `gles` | `#version 300 es` / `310 es` | 移动端 / 嵌入式 |
  | `webgl` | `#version 300 es`（WebGL2）/ `100`（WebGL1） | 浏览器 |

- **codegen 按 target 分叉的差异点**（已在 `codegen_glsl` 参数化；legacy ES = gles@100/
  webgl@100 整体切换发射形态）：
  - **版本头 + profile**：`#version N [core|es]`（ES 100 / GL<150 无 profile 词）。
  - **精度限定符**：ES 发 `precision`；legacy frag 用 `mediump`（ES2 硬件不保证片元
    highp），其余 `highp`。
  - **绑定语法**：Vulkan 用 `layout(set=,binding=)`；桌面 GL 无 `set`、`binding` 需 GL≥4.2；
    ES3.1 才有显式 binding；更低版本退化为运行时按名字 `glGetUniformLocation` → 反射清单需带名字。
  - **uniform 块平铺（legacy，已实现）**：ES 100 无 uniform 块——块字段平铺为普通
    uniform（名 = `块_字段`，如 `Params_tint`），块成员访问自动改写；反射清单 target 增
    `"flattenUniforms": true`，运行时按名逐字段 `glUniform*` 上传。
  - **内建变量名**：`gl_VertexIndex`↔`gl_VertexID`（Vulkan vs GL/ES）；legacy frag_depth →
    `gl_FragDepthEXT`（GL_EXT_frag_depth）。
  - **I/O 关键字（legacy，已实现）**：现代用 `in`/`out` + `layout(location=)`；
    legacy 用 `attribute`（vert 入）/`varying`（vert 出 + frag 入）无 layout；
    frag 输出写 `gl_FragColor`（单）/`gl_FragData[i]`（MRT，需 GL_EXT_draw_buffers）。
  - **采样（legacy，已实现）**：`texture()` → `texture2D()`。
  - **数组构造器（legacy，已实现）**：ES 100 无 `vec2[3](...)` —— 初始化列表降级为
    声明 + 逐元素赋值（`const` 随之丢弃）。

- **shader_sema 按 target 收窄能力**（已实现，能力行见 `shader_caps.h`）：
  - `storage`/SSBO、`comp` → 需 GL≥4.3 / ES≥3.1（或 ARB 后向移植扩展）。
  - `f8`（double）→ 仅桌面 GL≥4.0（或 GL_ARB_gpu_shader_fp64）；ES/WebGL/Metal 无。
  - `u*`/`uvec*` 无符号整型 → 需 GL≥1.30 / ES≥3.0（ES 100 无 uint）。
  - `builtin vertex_id/instance_id` → 需 ES≥3.0（ES 100 无 gl_VertexID，无标准扩展）。
  - `builtin frag_depth` → ES 100 经 GL_EXT_frag_depth（发射名改 gl_FragDepthEXT）。
  - frag 多输出（MRT）→ ES 100 经 GL_EXT_draw_buffers（gl_FragData[i]）。
  - 校验产出**明确的版本相关报错**：「目标 `gles100` 不支持 vertex_id/instance_id 内建
    （需 gles≥300）」；有替代扩展途径时报错文案告知两条补救方式。

- **目标声明（已定稿）——源码内 `tar` 指令，不走 CLI**：

  目标是**构建契约**而非命令行临时参数。三条理由决定它必须内嵌源码：
  ① 编辑期语法插件解析源码即得目标，才能给出目标相关的**实时诊断**（CLI 传参插件看不到 →
  报不了错，开发实用性掉一个量级）；② `.ss` 作为 `.sc` 依赖**整体构建**时，源码自描述、免透传
  flag；③ 避开与**交叉编译 target** 的语义歧义（`.ss` 内的 `tar` 作用域隔离，专指 GPU 目标）。

  语法（`.ss` 顶部，`tar` 是与 `vert`/`frag`/`comp` 平级的顶层指令）：

  ```
  tar vulkan@450                 # 单目标
  tar gles@100, gles@300         # 多目标（逗号分隔，可同 api 多版本）
  tar "boards/rk3588.caps"       # 外部设备能力档案（见下），可与内联目标混用
  ```

  - `<api>@<version>`：**版本必须显式指定，无默认**；`@N` 为**精确锚定**，codegen 直接发
    `#version N`，能力判定按「该 api 起始支持版本 ≤ N」。
  - api 取值：`vulkan` / `glcore` / `gles` / `webgl`。
  - **版本白名单（已实现）**：GLSL 版本号非连续数，写错当场拒绝并提示。
    gles 只有 `100(ES2.0)/300(ES3.0)/310(ES3.1)/320(ES3.2)`——典型误区
    `gles@200` 会报「ES2.0 的着色语言是 gles@100」；glcore 限
    `110–150/330/400–460`；webgl 限 `100/300`；vulkan 限 `450/460`。
  - **多目标 = 兼容性契约**：声明的每个目标都必须支持所用能力，任一不满足即**硬报错**
    （不降级、不静默跳过）；codegen 逐目标各出一份产物。
  - 共享：一期只支持**每文件声明**（将来再加 inc 共享 / 项目级默认，见 §15）。
  - 反射清单 JSON 增 `"target": {"api","version"}`；无显式 binding 的低版本目标，resources
    项保留 `"name"` 供运行时按名绑定。

- **设备能力档案（caps profile，已实现）——外部文件配置目标 + 扩展**：

  「API@版本」网格点不足以描述真实设备：嵌入式板卡常见「基线版本低但带关键扩展」
  （如 GLES2 + `GL_OES_standard_derivatives`，GL3.3 + ARB 后向移植扩展）。
  `tar "file.caps"` 把一个目标锚定到具体设备的能力档案（行式通用协议）：

  ```
  # rk3588.caps —— 设备能力档案（# 行注释）
  api gles
  version 3.1            # 或 310；同 tar 版本规则归一化
  ext GL_EXT_texture_border_clamp
  ext GL_OES_sample_variables
  ```

  - 路径相对 `.ss` 源文件解析；产物/反射 tag 用文件 stem（如 `cs_main.rk3588.comp`），
    不同板卡档案互不覆盖。
  - **扩展参与能力判定**：能力矩阵每格除核心起始版本外可挂**替代扩展**（含扩展版本
    下限）；版本不够但档案声明了该扩展 → 能力成立，codegen 在 `#version` 后自动发射
    `#extension <名> : require`。报错文案告知两条途径：
    「目标 rk3588 不支持 storage 缓冲（需 glcore≥430 或 扩展 GL_ARB_...（caps profile 声明，需版本≥400））」。
  - 解析器（`parseCapsProfile`）住 `shader_caps.h`，纯文本无 I/O；文件加载在
    `compileShaderSource` 里、能力门控前完成。

- **能力表架构（单一事实源，sema 与 codegen 共用）**：

  用一张二维表取代散落各处的版本 `if` 判断——**行 = sg 能力全集（含方言构造）**，
  **列 = API 族**，格值 = `{ 核心起始版本, 替代扩展, 扩展版本下限 }`。
  新增能力加一行、新增目标加一列，天然不易漏、易扩展。

  ```cpp
  // shader_caps.h —— sg 能力 × 目标 的需求矩阵（核心版本 或 替代扩展）
  enum class Cap { StorageBuffer, ComputeStage, PushConstant, DoubleType,
                   DescriptorSet, ExplicitBinding, /* … */ CapCount };
  struct CapReq { int core; const char* ext; int extFrom; };  // -1/NULL = 无该途径
  struct CapRow { const char* name; CapReq vulkan, glcore, gles, webgl, metal; };
  // 例：StorageBuffer.glcore = {430, "GL_ARB_shader_storage_buffer_object", 400}
  //   → 核心 430 直接支持；400–429 若档案声明该扩展亦成立（发 #extension）
  enum class CapVia { No, Core, Ext };
  CapVia capResolve(Cap c, const GlslTarget& t, const char** outExt);
  bool   capSupported(Cap c, const GlslTarget& t);   // 任一途径成立
  ```

  - **sema 用法**：遍历 AST 收集「已用能力集」（`storage` 属性→StorageBuffer、出现 `comp`
    →ComputeStage、用到 `f8`→DoubleType…）并存入 `prog.shaderUsedCaps`；对**每个声明目标**
    查表，不满足即报错并点名「能力 × 目标 × 两条补救途径」。
  - **codegen 用法**：查「策略类能力」（DescriptorSet/ExplicitBinding/精度/内建名）决定**怎么发**
    ——同一份 AST，vulkan 发 `set=/binding=`、glcore@410 省 `set` 只留名字绑定等；
    对 `capResolve == Ext` 的已用能力在 `#version` 后发 `#extension : require`。

- **落点**：新增独立 `shader_caps.*`（能力表）；`codegen_glsl` 引入
  `struct GlslTarget { enum class Api{Vulkan,GLCore,GLES,WebGL}; Api api; int version; }`，
  `emitStage`/`emitResources`/`builtinGlsl`/`#version` 头均接收它；`shader_sema` 接收同一目标
  列表、查同一张 `CAP_TABLE` 做能力门控；`lexer`/`parser` 在 shaderMode 识别 `tar` 顶层指令。

### 13.2 GL/EGL 上下文环境模块（P1）——类 glfw 的「上下文/表面」层

**现状**：`examples/shader-tri` 的 host 是手写 C（实例→设备→交换链→管线→渲染循环），且
运行时靠环境变量硬指 MoltenVK/loader。用户要「无感好用」，这套样板必须被**可复用的 sc 模块**取代。

**要做的**：一个 sc 侧 GPU 上下文模块（暂名 `templates/gpu/` 或未来 stdlib `gfx`），职责：

- **上下文/设备创建**：
  - Vulkan 路径（MoltenVK）：实例 + 物理/逻辑设备 + 队列 + 交换链，封装 portability 相关细节。
  - EGL 路径（GLES）：`eglGetDisplay`/`eglCreateContext`/`eglCreateWindowSurface`。
  - 桌面 GL 路径：CGL(Mac)/GLX(X11)/WGL(Win) 或直接复用窗口库给的 context。
- **管线自动装配**：读 scc 产出的**反射清单 JSON**，据此建立管线布局、描述符集/绑定、顶点属性
  绑定——把 §10「运行时按反射清单建管线」这段做成模块，而不是每个应用重抄一遍。
- **着色器加载**：按 target 加载 `.spv`（Vulkan）或 GLSL 源（GL/ES，运行时 `glShaderSource`）。
- **分层**：底层用 sc 的 C 互操作（`inc` Vulkan/EGL 头）做 FFI 绑定；上层给出 sc 友好的
  「创建 context → 载入 .ss 产物 → 拿到可用管线」三步 API。

对标 glfw 的「context」部分（不含窗口），但聚焦「把 `.ss` 产物变成可直接 draw 的管线」这条链。

### 13.3 UI / 窗口跨平台环境模块（P2）——类 SDL 的「窗口/输入/事件」层

**现状**：demo 直接依赖 glfw 建窗口、收输入、跑事件循环。

**要做的**：一个跨平台窗口/输入的 sc 模块，与 §13.2 的上下文模块**解耦**（窗口只负责产出
native handle / surface，交给上下文模块消费）：

- **能力**：窗口创建/尺寸/全屏、事件循环、键鼠/触摸输入、时间/帧率、DPI。
- **平台**：macOS(Cocoa + CAMetalLayer)、Linux(X11/Wayland)、Windows(Win32)、Web(canvas)。
- **策略（待定）**：
  - **(a) 薄封装现成 glfw/SDL**：sc 封装其 C API——快、省事，契合「不重复造轮子」，一期倾向此。
  - **(b) 自研跨平台窗口层**：重，远期再评估。
- 这是「无感好用」的最外层：用户写 `.ss` + 少量 sc 胶水（窗口 + 上下文两个模块）即可出画面。

### 13.4 三者关系与推进顺序

```mermaid
flowchart LR
    P0["§13.1 GL 版本适配<br/>(codegen + sema)"] --> P1["§13.2 GPU 上下文模块<br/>(类 glfw context)"]
    W["§13.3 窗口模块<br/>(类 SDL)"] -->|native surface| P1
    P1 --> APP["无感好用:<br/>.ss + 少量 sc 胶水 → 出画面"]
    W --> APP
```

- **P0（§13.1）先做**：没有多 target，产物只能喂 Vulkan，谈不上跨平台。且它纯编译器侧、
  与现有 `codegen_glsl`/`shader_sema` 同构，风险最低、收益最直接。
- **P1（§13.2）次之**：把 demo 的手写 C host 沉淀为模块，是「好用」的核心。
- **P2（§13.3）最后**：窗口层可先薄封装 glfw/SDL，快速替掉 demo 的裸 glfw 依赖。

---

## 14. 路线图

### 14.1 一期（最小可用）——**已完成**
- ✅ `vert`/`frag`/`comp` 关键字 + stage I/O 结构体 + swizzle + `vec/mat` 透传。
- ✅ `codegen_glsl`：产 Vulkan-GLSL 文本 + 反射清单 JSON。
- ✅ `shader_sema`：子集强制 + 基础类型检查 + `(set,binding)` 冲突检测。
- ✅ 运行时样例（`examples/shader-tri`）：GLSL 经 `glslangValidator`→SPIR-V→MoltenVK 在 Mac 上
  跑通彩色三角形。

### 14.2 紧接一期（原近期优先，§13）——**已全部落地**
- ✅ **P0**：GL 版本/profile 适配（`tar metal@2.0` / `glcore@410` / `gles@300` 多目标，
  多目标产物带标签名）+ 按版本的 sema 能力裁剪。
- ✅ **P1**：GPU 运行环境与渲染模块：[builtins/gpu](builtins/gpu/)（device/surface/
  memimg，含无表面渲染）+ [builtins/gfx](builtins/gfx/)（读反射清单自动装配管线，
  Metal/GL 双后端）。
- ✅ **P2**：跨平台窗口/输入模块 [modules/wsi](templates/.scenv/modules/wsi/)（自研，glfw 同构）。

### 14.3 三期（SPIR-V 直发 + 产物资源化，**当前，进行中**）

目标：去除 glslang，AST 直发 SPIR-V（§2.2）；产物从散文件改为可直接链入应用的资源文件。

| 里程碑 | 内容 | 验收 |
|---|---|---|
| **M1 发射器骨架** | `codegen_spirv.cpp`：模块头/类型池/常量池/装饰/入口点；线性代码（表达式/赋值/return/swizzle/向量构造）；vert/frag I/O 变量 + uniform/sampler | 三角形 .ss 产物过 spirv-val；SPIRV-Cross→MSL 与 glslang 旧链产物语义对照 |
| **M2 控制流与内建** | if/else、while/for（SelectionMerge/LoopMerge）；GLSL.std.450 数学库；纹理采样；comp 阶段（workgroup/计算内建/SSBO）；辅助函数（OpFunctionCall） | 全部现有 .ss 用例（含 saxpy comp）新链产物过验证 + demo 实机渲染 |
| **M3 管线切换 + glslang 移除** | metal 目标走 AST→SPIR-V→SPIRV-Cross；vulkan 目标直落 .spv；`--emit-glsl`/`--emit-spv` 选项；删 vendor/glslang-src + SCC_WITH_GLSLANG + shader_spv 封装 | 244 回归 + 三 demo；scc 构建时间/体积显著下降 |
| **M4 产物资源化** ✅ | 默认输出 `<stem>.shader.h/.c`：字节数组（文本带尾 NUL）+ 自动 enum id + 反射 JSON 内嵌 + `_get(i)` 非 inline 取条目（FFI 用）；条目布局每目标三连 [reflect, 逐 stage]；`--files` 落散文件 | ✅ 三 demo（gpu/headless/spc）全改资源模式（零运行时文件路径）实机验证 |
| **M5 语言完备性** | 按 §16 优先级逐批实现，反向对齐 SPIR-V 能力 | 每批能力：用例 + 验证链 + 能力矩阵行更新 |

已决（2026-07-09，细化）：**默认产物链全目标统一 SPIR-V 中枢**——vulkan 直落 .spv，
metal 经 SPIRV-Cross→MSL，**glcore/gles 也经 SPIRV-Cross 反译 GLSL**（ES100 走 legacy
形态）。codegen_glsl **保留**但退出默认链：作对照/兜底/`--emit-glsl` 特殊输出
（自研文本发射，产物同名可直接替换）。实测存档：SPIRV-Cross 反译全版本无硬障碍
（glcore410 过 glslangValidator + GL 实机；ES100 legacy 含 struct uniform 与
`--flatten-ubo` 整块上传两形态）。**退役评估**：跑稳一段时间后若对照价值衰减，
再移除 codegen_glsl（ES2 运行时 uniform 上传契约届时按反译输出形态对齐）。

### 14.4 四期（原二期存目，部分已提前落地）
- ✅ `comp` 计算链路：计算内建映射 + `local_size` 发射/反射携带 + storage/SSBO；
  消费侧 [builtins/spc](builtins/spc/)（多维空间并行计算：kernel=Metal compute、
  graph=MPSGraph、model=CoreML/ANE）实机验证。待深化：`local_size` 语法、
  barrier/共享内存语法（已列入 §16 P2）。
- `@def` 共享布局（CPU↔GPU uniform 一致性校验，见 §7）。
- ✅ SPIRV-Cross→MSL 离线路径（`tar metal@2.0` 直产 MSL，已是默认发行路径）。

### 14.5 远期
- `codegen_msl` 直产 MSL（跳过 SPIRV-Cross；仅当 SPIRV-Cross 成为痛点时）。
- 评估 Slang 作为可选后端（autodiff/泛型多后端）。
- 与 sc 的 `tok` 依赖图 / dnn 模板联动（compute shader 加速训练？——研究向）。

---

## 15. 当前未决 / 待定

- ~~GL 版本适配的接口形态~~：已定——`.ss` 内 `tar` 顶层指令（可多目标）；多目标
  产物带标签名（如 `vs_main.glcore410.vert`）。
- ~~上下文/窗口模块的边界与命名~~：已定——utils/wsi（窗口）、builtins/gpu（运行环境）、
  builtins/gfx（渲染）、builtins/spc（计算），均自研。
- 向量/矩阵类型是内置关键字还是 shader builtins 模块提供（见 §4、§8）。
- `local_size` 的 `.ss` 语法形态（现为编译器固定 64×1×1）。
- 计算着色器与 sc `tok`/dnn 体系的结合边界（远期研究；builtins/spc 的 graph/model
  面已提供运行时路径）。
- **已定**（一期落地时确定，此处存档）：属性附着语法（`loc N` / `builtin X` 跟在字段后；
  `uniform|storage|push [set S binding B]` 跟在 `@def` 结构后）；反射清单 JSON schema
  （`stages[].{inputs,outputs,location,builtin}` + `resources[].{kind,set,binding,layout,members[].{offset,size}}`）。

---

## 16. SPIR-V 反向对齐优先级列表（M5 路线）

原则：**SPIR-V（Shader 能力档）支持什么，ss 就应能表达什么**——以 SPIR-V 规范的
能力/指令集为完备性清单反向核对，按「现有用例的直接缺口 → 图形完备 → 计算深化 →
按需远期」排批。每项标注 SPIR-V 落点（指令/OpCapability），作为可核查的对齐依据。
实现节奏：每批 = 语法（如需）+ shader_sema 门控 + codegen_spirv/glsl 双后端 +
用例 + 能力矩阵（shader_caps.h）行。

### P0 语言核心完备（现有用例的直接缺口）

| 能力 | SPIR-V 落点 | 现状/备注（2026-07-13 复盘） |
|---|---|---|
| `for` / `do-while` / `break` / `continue` | OpLoopMerge + OpBranch* | ✅ |
| `switch`/`case` | OpSwitch | ✅（if-else 链等价降级，含 through 贯穿；未用 OpSwitch 指令，语义等价） |
| `discard`（frag） | OpKill | ✅ |
| swizzle 写入（`v.xy = ...`） | OpVectorShuffle + OpStore | ✅ |
| 结构体值类型完备（嵌套/函数参数/返回/构造） | OpTypeStruct / OpCompositeConstruct | ✅ |
| 数组完备（多维/函数参数/动态索引） | OpTypeArray / OpAccessChain | ✅（多维结构体数组仍拒） |
| 矩阵运算完备（mat*vec、vec*mat、mat*mat、转置/逆/行列式） | OpMatrixTimesVector 等 + GLSL.std.450 | ✅ |
| 内建变量补全（front_facing/point_size/sample_id/…） | BuiltIn 装饰 | ✅（13+ 个，含 subgroup 两个） |
| 编译期常量折叠 / `let` 常量 | OpConstant / OpSpecConstant | ✅（含 P3 窄/宽常量） |

### P1 图形能力补全

| 能力 | SPIR-V 落点 | 备注（2026-07-13 复盘） |
|---|---|---|
| 纹理类型族：3D/Cube/Array/Shadow/MS | OpTypeImage 维度/Depth/MS 位 | ✅（19 种采样器） |
| 采样变体：textureLod/Grad/texelFetch/textureSize/Proj | OpImageSample*/OpImageFetch/OpImageQuery* | ✅（含 Gather/Offset/QueryLod/QueryLevels） |
| image load/store（无采样读写） | OpImageRead/OpImageWrite + Capability StorageImage* | ❌ **未做**（spc kernel 面的图像算子需要时再做；当前用 SSBO 表达图像数据可绕行） |
| derivative：dFdx/dFdy/fwidth | OpDPdx/OpDPdy/OpFwidth | ✅（frag 门控） |
| 分离 texture/sampler（SPIR-V 原生形态） | OpTypeSampler + OpSampledImage | ❌ **未做**（combined 形态足够现有用例；D3D 后端时再评估） |
| MRT 完备 + 深度输出组合 | Location/BuiltIn FragDepth | ✅ |
| UBO/SSBO 运行时数组 + 动态索引 | OpTypeRuntimeArray + Capability | ✅ |
| push constant 完备（嵌套/数组） | PushConstant storage class | ✅（含存储类修复，spirv-val 验证） |

### P2 计算深化（✅ 2026-07-13 全部落地，mac Metal 端到端验证）

| 能力 | SPIR-V 落点 | 状态 |
|---|---|---|
| `local_size` 的 .ss 语法 | ExecutionMode LocalSize | ✅ 签名尾 `local X [Y [Z]]`，反射驱动 spc 调度 |
| shared 共享内存 | Workgroup storage class | ✅ `@def X: {...} shared`，MSL threadgroup 验证 |
| barrier / memoryBarrier | OpControlBarrier / OpMemoryBarrier | ✅ `barrier()`/`memory_barrier()` |
| 原子操作 | OpAtomic* | ✅ add/sub/min/max/and/or/xor/exchange/cas，SSBO+shared |
| 特化常量 | OpSpecConstant* | ✅ `let X: T = v spec N`，反射 spec_constants；运行时传值面待后端适配期 |
| subgroup 基础（vote/ballot/shuffle） | Capability GroupNonUniform*（SPIR-V 1.3+） | ✅ 版本按需 1.0/1.3 动态；metal vote/ballot 需 2.1 已门控 |

验证：tests/cases/shader_p2_*.ss 四用例（golden 286/0）+ templates/demo/spc_p2_demo.sc
（Metal host：shared 树形规约 + barrier + atomic_add，n=4096 数值精确）。
待后续：Vulkan/GLES31 后端适配（spc kernel 面）、spec 常量运行时传值 API。

### P3 按需远期

| 能力 | SPIR-V 落点 | 触发条件/状态 |
|---|---|---|
| f16 / i64 / i8 标量 | Capability Float16/Int64/Int8/Int16 | ✅ 2026-07-13 落地（嵌入式先行）：名字即字节数 f2/i8·u8/i1·u1/i2·u2 真位宽；块成员经 SPV_KHR_16bit/8bit_storage；MSL half/long(2.3+)/char/short 验证；限制：无窄分量向量、原子/spec 仍 32 位 |
| 几何/细分/mesh/task 阶段 | Capability Geometry/Tessellation/MeshShadingEXT | 有真实用例再做 |
| buffer_device_address（物理指针） | PhysicalStorageBuffer | Vulkan 高级路径 |
| ray tracing / ray query | Capability RayTracingKHR | 平台成熟后 |
| 多视图/多采样高级（sample shading 等） | 相应 Capability | XR 需求 |

### spc kernel 面后端矩阵（2026-07-13，三后端代码全部就位）

- 公共层抽 vtable（spc_kernel_api，按 sc_gpu_query_backend 选定）；
  graph/model 面仍 darwin 直连。特化常量运行时传值面
  （kernel_desc.spec_values）同批落地。
- Metal（metal_spc.m）✅ 实机；Vulkan（vulkan_spc.c，VkSpecializationInfo）
  与 GL（gl_spc.c，GLES3.1/GL4.3）盲写完成，Android NDK + mingw 交叉编译
  验证；板端验证手册 = builtins/spc/PORTING.md（换设备必读）。

---

## 17. CPU/DSP 后端路线（`tar cpu`，规划 2026-07-13）

> 结论先行：.ss 的 SPMD 执行模型与 CPU SIMD 的最佳编程抽象（ISPC 模式）
> 天然同构——**CPU/DSP 是 .ss 的合法 tar 后端**，且实现路线与 sc 体系
> 完全同构：scc 直发 C 源码，向量化交给目标 C 编译器。

### 17.1 动机与定位

1. **嵌入式无 GPU / GPU 不可用场景的兜底**：同一份 .ss kernel 在纯 CPU
   板子上跑通（正确性优先，性能靠自动向量化）。
2. **全平台数值对拍参考**：板端调 Vulkan/GLES 后端时用 CPU 后端对答案
  （确定性、可单步调试）。
3. **DSP 的唯一现实入口**：Hexagon HVX / TI C7000 / CEVA / Cadence 的
   入口都是「厂商 C 工具链 + 自动向量化/pragma」——纯 C 产物 + scc 交叉
   编译子系统（target 档 + caps 档案）原样复用。

### 17.2 语义映射（SPMD → 标量循环 + 自动向量化）

| .ss / GPU 概念 | CPU/DSP 对映 |
|---|---|
| invocation | SIMD lane / 标量循环迭代（编译器向量化） |
| workgroup | 一个线程处理的迭代片（cache 亲和） |
| workgroup 间并行 | 线程池（mt 模块现成） |
| `shared` | workgroup 局部数组（栈上；DSP 映射 scratchpad/TCM，语义比 GPU 更贴合） |
| `barrier()` | **相位分裂点**（见 17.5 难点） |
| `atomic_*` | C11 atomics |
| `local_size` | 内层循环 trip count |
| spec 常量 | 运行时参数（或编译期宏特化） |
| f2/i1/u1 等窄标量 | `_Float16`/`int8_t`——窄类型打满向量宽度（NEON 一条 16×i8） |

### 17.3 路线选型

- **路线 A（零成本，已可用）**：SPIR-V → CPU Vulkan 实现（lavapipe/
  SwiftShader，内部 LLVM 向量化）。Android 模拟器 SwiftShader 已实测跑通
  我们的 Vulkan 产物。定位：开发期兜底，依赖大、DSP 不可行，非产品路线。
- **路线 B（选定）**：`tar cpu@99` → codegen_cpu（AST→C，SPMD 循环化，
  体量对标 codegen_glsl）→ `<stem>.cpu.c` → 目标 C 编译器。运行时 = spc
  kernel 面第四张 vtable（buffer=malloc、dispatch=mt 线程池分 workgroup）。
- **路线 C（不做，按需重议）**：自研向量化直发 intrinsics/LLVM IR = 重造
  LLVM 向量化器，违反自研/开源边界原则；仅当路线 B 实测性能不够时评估。

### 17.4 SIMD ISA 覆盖策略——不逐个适配后端

ISA 全景：ARM NEON（v7/v8 标配）/ SVE·SVE2（armv9 可变长）/ Helium-MVE
（Cortex-M）；x86 SSE4/AVX2/AVX-512；RISC-V RVV 1.0；DSP 专有 HVX（1024-bit）
/ TI C7000 / CEVA / Cadence HiFi。

**路线 B 下一份 SPMD C 产物，N 个 ISA 由各自编译器兑现**（clang `-march`
挑 NEON/SVE/AVX/RVV；DSP 厂商编译器挑各自向量指令）。每个新 ISA 只需两层
薄配置（均为现成机制）：target 档（`target_flags`）+ caps 档案（能力差异，
如 f16 硬件支持）。可变长向量（SVE/RVV）对 SPMD 循环天然免疫向量长度问题
（编译器按 VL 切），比手写 intrinsics 更先进。

### 17.5 语法覆盖度与缺口（诚实清单）

| 指令类别 | 覆盖 | 说明 |
|---|---|---|
| 垂直运算（逐 lane 算术/比较/转换，实际 kernel 90%+） | ✅ 完全 | SPMD 天然表达，自动向量化最擅长 |
| 水平运算（归约/点积） | ✅ 良好 | shared+barrier 树形归约 + subgroup 三件；编译器 reduction idiom 识别 |
| 数据重排（permute/zip/查表） | ⚠️ 部分 | subgroup_shuffle 是通路；任意 permute 无直接语法，多数可 gather 表达 |
| DSP 专用（饱和/定点 Q 格式、打包点积 SDOT/VNNI） | ❌ 缺口 | 见下方补法 |

缺口补法遵循「SPIR-V 反向对齐」原则，不为 CPU 单独造语法：
- **打包点积**（int8 量化 ML 核心）：SPIR-V 有 `SPV_KHR_integer_dot_product`
 （OpSDotKHR 系）——跟随加 `dot4_i8(...)` 内建，GPU/CPU 双端同源兑现。
- **饱和/定点算术**：SPIR-V 无对应，等真实 DSP 用例再议专用内建/类型。
- 「最大驱动并行能力」的杠杆排序：算法级并行度（SPMD 已给足）≫ 数据布局
  与别名保证（codegen_cpu 发射 `restrict`/对齐/`#pragma omp simd`——自动
  向量化成败的真正关键）≫ 指令级特调（编译器兑现大半）。前两层做满即达
  这类硬件 90%+ 可达性能。

**barrier 相位分裂**（实现上最难的一处）：barrier 语义 = 全体到齐 → 把
kernel 体按 barrier 切相位，每相位一个 lid 循环，跨相位存活的私有变量扩展
为 local_size 份数组；循环内 barrier（如 reduce 的 while 内）用循环交换
（lid 循环推到最内）消解。一期语法限制：barrier 仅允许出现在 kernel 顶层
或顶层循环内（sema 报错超出者），覆盖实际 kernel 绝大多数。

### 17.6 实施路线图（2026-07-13 立项，任务级分解）

**M1 最小版（无 barrier kernel 端到端，兼板验对拍参考）——✅ 2026-07-13 完成（mac 实测：saxpy CPU 对拍通过，clang 向量化 width 4；golden 290/0）**

| # | 任务 | 落点 |
|---|---|---|
| 1 | `tar cpu@99` 目标：GlApi 加 CPU、版本归一（99）、能力表加 cpu 列（sampler/subgroup/spec 一期 -1 门控；f16 经 `_Float16`） | shader_caps.h + parser |
| 2 | codegen_cpu：AST→C 的 SPMD 循环化发射——kernel 函数签名 `(gx0,gx1,gy,gz, bind[])`（函数内自带 gid 循环，非逐 invocation 调用）；资源块→字节偏移访问（std140/430 布局同源）；`restrict` + 对齐 + `#pragma omp simd` 提示 | 新 codegen_cpu.cpp |
| 3 | 产物形态：`<stem>.cpu.c` = kernel 函数 + 注册表 `sc_spc_cpu_kernel[]`（entry 名→函数指针）；host 经 `add out/x.cpu.c` 编译链接（sc 的 add 机制直接吃） | codegen_cpu + main.cpp 分派 |
| 4 | spc cpu 后端（第五张 vtable）：buffer=malloc、kernel=按 entry 查注册表、dispatch=切 workgroup 段循环；`sc_spc_desc` 加 kernel_backend 字段（0=跟随 gpu，SC_SPC_KERNEL_CPU=强制 CPU——有 GPU 也可对拍） | spc.h/spc.c + cpu_spc.c |
| 5 | demo/用例：saxpy/scale CPU 路数值对照（与 Metal 同答案）；.cpu.c 文本入 golden | spc_p2_demo + tests |

**M2 完整版（P2 语义全兑现）——✅ 2026-07-13 完成（mac 实测：reduce 相位分裂 +
spec 传值 + 多线程分片全部对拍通过；关键点：uniform 赋值外提防 lid 循环重复执行）**

| # | 任务 | 落点 |
|---|---|---|
| 6 | barrier 相位分裂：按 barrier 切相位 + 跨相位私有变量数组扩展 + 循环内 barrier 的循环交换；一期 sema 限制 barrier 位置（顶层/顶层循环） | codegen_cpu |
| 7 | shared（workgroup 局部数组）+ atomic_*（C11 atomics）→ reduce kernel 通 | codegen_cpu |
| 8 | mt 线程池 workgroup 间并行（dispatch 分段派发） | cpu_spc.c |
| 9 | spec 常量运行时参数化（kernel 函数加 spec 参数或查表） | codegen_cpu + cpu_spc |

**M3 DSP/优化（有真实板子触发）**

| # | 任务 | 落点 |
|---|---|---|
| 10 | DSP target/caps 档（Hexagon/TI 优先）+ 交叉编译验证 | .scenv/targets |
| 11 | dot4_i8 内建（SPV_KHR_integer_dot_product 双端同源）；饱和算术按需求评估 | codegen_spirv/cpu |

排期：M1 立即（与 GPU 板验并行，兼对拍参考）；M2 紧随；M3 有板触发。

---

## 18. spc graph 面算子路线（nn 加速，规划 2026-07-13）

> 目的：给 ts/nn 提供**张量算子级 GPU 加速**（nn 现全跑 ts 纯 CPU 数值）。
> 定位区分：kernel 面 = 自定义并行算法（已完备）；graph 面 = 现成算子库；
> model 面 = 整图交 NPU（RKNN 板触发，与本节无关）。

### 18.1 架构决策：.ss kernel 算子打底 + 厂商框架作快路径

| 路线 | 通用性 | 取舍 |
|---|---|---|
| (a) 厂商图框架 | 每平台一套 | mac=MPSGraph（已有 matmul）——性能最优，适配面大 |
| (b) **.ss kernel 算子**（选定打底） | **全平台**（骑 kernel 面四后端：Metal/Vulkan/GL/CPU） | 一份 .ss 全平台生效；性能靠 tile 调优，MPSGraph 作上限参照 |

- 派发策略：`sc_spc_mm` 等算子 API 不变；实现层 **mac Metal → MPSGraph
  快路径（保留），其余后端 → 内部 .ss kernel**；desc 加强制开关
  （SC_SPC_GRAPH_KERNEL）令 mac 也走 .ss 路（对拍/调优）。
- **算子 kernel 交付**：builtins/spc/kernels/*.ss + 预编产物
  （shader.c/cpu.c）随 git 入库（同 wsi .a 预编交付范式；改 .ss 后重跑
  kernels/build.sh）；spc.sc `add` 编入，运行时按后端选条目。
- 这是 P2 语法（shared/barrier/tile）+ 四后端矩阵的第一个生产级消费者。

### 18.2 实施路线图（任务级）

**M1 matmul 通用化（打通 graph→kernel 内部管道）——✅ 2026-07-13 完成（mac 实测：
Metal 双路（MPSGraph vs mm_tiled）+ CPU（mm_1d）均与 ts CPU 对拍通过；
Vulkan/GLES 条目已预编入库，板验照 PORTING §9）**

| # | 任务 | 落点 |
|---|---|---|
| 1 | kernels/matmul.ss：tiled matmul（f4，16×16 shared tile，四目标 tar）+ kernels/build.sh 预编入库 | builtins/spc/kernels/ |
| 2 | graph→kernel 内部管道：spc.c 内部句柄惰性创建（graph 面复用 K vtable 的 make_kernel/dispatch，不经公开句柄池） | spc.c/internal.h |
| 3 | sc_spc_mm 派发通用化：mac Metal=MPSGraph 快路径；其余后端=.ss kernel；SC_SPC_GRAPH_KERNEL=1 强制 .ss（对拍） | spc.c/mpsg_spc.m |
| 4 | 验证：四后端 mm vs ts CPU 对拍（spc_demo 扩展）+ mac 上 MPSGraph vs .ss kernel 双路对拍 | demo/golden |
| 5 | 文档：REFERENCE spc 段、PORTING §9、本节标注 | docs |

**M2 算子批量扩展（nn 前向热点）**

| # | 任务 |
|---|---|
| 6 | conv2d（im2col+matmul 复用 或 direct tiled） |
| 7 | softmax / layernorm（行归约类，shared 树形规约复用） |
| 8 | elementwise 族（relu/gelu/add/mul，无 barrier 直发） |
| 9 | nn 模块接线：热点前向路径经 spc graph 面（带 CPU 回退，无 gpu 时零依赖不变） |

**M3 量化与融合（性能深水区，需求触发）**

| # | 任务 |
|---|---|
| 10 | i8 量化 matmul（dot4_i8 内建，联动 §17-M3 与 SPV_KHR_integer_dot_product） |
| 11 | attention 融合算子（flash-attention 简化形态）；conv+bias+relu 融合 |

排期：M1 立即；M2 随 nn 需求；M3 需求触发。

---

