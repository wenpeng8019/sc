# gpu —— GPU 设备操作统一接口

驱动各平台 GPU 硬件、**执行 scc 编译转义后的 GPU 代码**的运行时模块。

## 1. 定位与边界

本模块**不是**"消除平台差异性的统一图形抽象层"（那是引擎的事），而是
gfx-hal / sokol_gfx 式的**薄硬件访问层**：

- 核心职责：把 scc 编译 `.sg`（syntax-g 方言）的产物——各后端目标代码
  （MSL / GLSL / SPIR-V）+ 反射清单 JSON——喂给对应平台的图形 API 执行。
- 不做：场景图、材质系统、渲染图、资源缓存等上层概念。
- 上游依赖：wsi（取 `native_window` / `native_display`）；着色器由 scc
  离线或运行时编译（syntax-g §10）。

```
 .sg ──scc──▶ vs.metal / fs.metal + reflect.json      （macOS）
              vs.vert / fs.frag  + reflect.json       （GL 平台）
                     │
                     ▼
 wsi 窗口 ─▶ sc_gpu_init ─▶ make_shader（直接吃产物）─▶ pipeline ─▶ 帧循环
```

## 2. 多后端机制（同 wsi / glfw）

一个 `libgpu.a` 可同时编入多个后端，运行时按 `sc_gpu_desc.backend`
显式选择或按平台默认（不做静默降级——显式请求的后端未编入即失败）：

| 平台    | 默认后端 | 备选        | 编译宏          | 状态   |
|---------|---------|-------------|-----------------|--------|
| macOS   | Metal   | GL          | `SC_GPU_METAL` + `SC_GPU_GL` | ✅ 均已首光 |
| Linux   | GL      | Vulkan(远期) | `SC_GPU_GL`     | 已编入待验证 |
| Windows | GL      | D3D(远期)   | —               | 待补（GL 需加载器） |
| 任意    | Null    | —（测试用） | 恒编入          | ✅     |

后端以 `_sc_gpu_backend_api` 函数表（vtable）注册，公共层
（[src/gpu.c](src/gpu.c)）持有资源池 / 句柄 / 缺省值解析 / 校验，后端只做
纯图形 API 翻译（[src/metal_dev.m](src/metal_dev.m) / [src/gl_dev.c](src/gl_dev.c)）。

## 3. 资源与帧模型（参考 sokol_gfx）

- 六类资源：buffer / image / sampler / shader / pipeline / **surface**。
  32 位句柄（低 16 位池索引 + 高 16 位代数防悬垂；0 = 无效），
  desc 结构一次性描述、不可变创建，零值字段自动解析为合理默认。
- **surface = 呈现目标（交换链）**：gpu 的子概念。`sc_gpu_init` 若提供
  `desc.surface.native_window` 自动建默认 surface 并置为当前；多窗口用
  `sc_gpu_make_surface` + `sc_gpu_make_current` 切换。交换链 pass 渲染到
  当前 surface。
- 帧模型：`begin_pass`（附件全空 = 交换链）→ `apply_pipeline` →
  `apply_bindings` → `apply_uniforms` → `draw` / `dispatch` →
  `end_pass` → `commit`。
- 计算：`sc_gpu_pass.compute = 1` 开计算 pass，管线带 `compute` 标志与
  `threads_per_group`（对应 .sg comp 的 local_size）。Metal 已支持；
  GL 4.1 无 compute（macOS 上限）。

## 4. 与 scc 的整合（核心）

`sc_gpu_shader_desc` 直接消费 scc 产物：

```c
sc_gpu_shader_desc sd = {0};
sd.vs.code  = (sc_gpu_range){ msl_text, msl_len };
sd.vs.entry = "vs_main";          /* scc 产物入口 = .sg 阶段函数名 */
sd.fs.code  = ...;
sd.reflect_json = reflect_text;   /* <stem>.reflect.json 内容 */
```

反射清单提供 uniform/storage 块的 set/binding/size、顶点属性 location、
sampler 绑定——运行时自动建立管线绑定，无须手工填
`uniform_blocks[]` 等（无清单时仍可手工描述）。

### Metal 后端绑定约定（与 scc 的 SPIR-V→MSL 转译对齐）

| 资源                | MSL 槽位                               |
|---------------------|----------------------------------------|
| uniform/storage 块  | `[[buffer(binding)]]`（spirv-cross 从 0 顺序分配，与清单 binding 一致） |
| 顶点数据缓冲        | `[[buffer(8 + slot)]]`（8 = `SC_GPU_MAX_UNIFORM_BLOCKS`，避让低编号区） |
| 纹理 / 采样器       | `[[texture(binding)]]` / `[[sampler(binding)]]` |
| 入口名              | `desc.entry`；NULL 默认 `"main0"`（spirv-cross 惯例） |

`apply_uniforms` 走 4 MB 共享环缓冲（256 字节对齐），每帧复位；
in-flight = 2（信号量节流，uniform 环 / 动态缓冲双份轮转）。

### GL 后端约定（GL 4.1 core，macOS 上限；scc `tar glcore@410`）

- 4.1 无 shader 内 explicit binding：链接后按反射清单名字解析
  uniform 块索引 / sampler location，手动指派绑定点。
- 绑定点分配（GL 全局命名空间 vs Metal 每阶段槽位）：
  uniform 块 = `stage * 8 + slot`；纹理单元 = `stage * 12 + slot`。
- GLSL 入口恒为 `main`（`desc.entry` 忽略）；顶点属性 location 即
  管线 `attrs[]` 下标。
- 每 surface 一个 GL 上下文 + VAO（core 必需）；`make_current` 即
  GL 上下文切换。当前限制：对象未跨上下文共享，资源须在使用它的
  surface 上下文中创建。
- 离屏 pass 用临时 FBO；MSAA 解析走 `glBlitFramebuffer`；交换链 MSAA
  暂不支持。无 compute / SSBO（4.1 限制，报错不降级）。

## 5. 构建

```sh
./build.sh                        # 宿主构建 → libgpu.<triple>.a + libgpu.a
./build.sh --target <triple>      # 交叉编译
```

macOS 链接需系统框架（本目录 [.sc](.sc) 配置文件已带，从本目录跑 scc 自动生效）：
`-framework Metal -framework QuartzCore -framework OpenGL -framework Cocoa`

## 6. sc 侧用法

[gpu.sc](gpu.sc) 为 FFI 描述（`inc gpu.h` + `@fnc gpu_*::`）；句柄即 `u4`。
完整示例见 [templates/demo/gpu_demo.sc](../../demo/gpu_demo.sc)：

```sh
./templates/utils/wsi/build.sh
./templates/utils/gpu/build.sh
./compiler/build/scc templates/demo/gpu_shader/gpu_tri.sg -o templates/demo/gpu_shader/out/gpu_tri
SCC_LDFLAGS="-framework Cocoa -framework IOKit -framework CoreFoundation \
             -framework Metal -framework QuartzCore -framework OpenGL" \
    ./compiler/build/scc templates/demo/gpu_demo.sc     # 编译并运行，三角形上屏（Metal）
# GPU_BACKEND=gl 前缀同一命令 → 切 OpenGL 后端（同一三角形）
```

注意（FFI 陷阱，实测踩过）：
- sc 局部声明的 C 结构体**不零初始化**——desc/pass/bindings 用前须
  `::memset(&d, 0, sizeof(::sc_gpu_desc))`。
- 公开签名避免 `size_t`（macOS 上 = unsigned long ≠ `uint64_t`，会与
  FFI 生成的 extern 冲突）；本 API 一律用 `uint64_t`。
- 句柄是纯 `uint32_t` typedef（非单成员结构体），与 sc 的 `u4` 直接对应。

## 7. 源码结构

```
gpu.h                 公开 C API（枚举 / desc / 函数）
gpu.sc                sc FFI 描述
.sc                   scc 本地构建配置（macOS 框架 ldflags）
build.sh              静态库构建（多后端宏在此选择）
src/internal.h        后端 vtable、资源池、反射结构
src/gpu.c             公共层：池/句柄/缺省值/校验/分派
src/gpu_reflect.c     反射清单 JSON 极简解析器（无外部依赖）
src/null_dev.c        空后端（无硬件/测试）
src/metal_dev.m       Metal 后端（ARC；CAMetalLayer 挂 surface NSView）
src/gl_ctx.h/.c       GL 上下文平台层（NSGL / WGL / GLX；mac 按 ObjC 编译）
src/gl_dev.c          GL 后端（4.1 core 命令翻译）
```
