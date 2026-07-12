# gfx —— 渲染层（执行 scc 编译转义后的 GPU 代码）

在 [utils/gpu](../gpu/)（GPU 运行环境）之上的**薄硬件访问层**，
对应 sokol_gfx 的渲染部分：驱动 GPU 硬件、执行 scc 编译 `.ss`
（syntax-s 方言）的产物。不做场景图/材质/渲染图等上层概念。

## 1. 与 gpu（env 层）的边界

软边界：本库编译期单向依赖 libgpu.a 的 C API。

- `sc_gfx_init` 前须 `sc_gpu_init` 成功；后端种类跟随 gpu
  （`sc_gpu_query_backend()` 选配对的命令翻译 vtable）。
- 一次性消费 `sc_gpu_device()`；交换链 pass 每帧消费
  `sc_gpu_frame_acquire()`；`sc_gfx_commit` 末尾调 `sc_gpu_frame_end()`。
- Metal 呈现由本层把 frame.drawable 挂到命令缓冲（最佳呈现节拍）；
  GL 的 swapBuffers 在 env 侧（frame_end）。
- 像素格式沿用 `sc_gpu_pixel_format`（两层共同词汇）。

```
 .ss ──scc──▶ vs.metal / vs.vert + reflect.json
                     │
 gpu_init（env）─▶ gfx_init ─▶ make_shader（直接吃产物）─▶ pipeline ─▶ 帧循环
```

## 2. 资源与帧模型（参考 sokol_gfx）

- 五类资源：buffer / image / sampler / shader / pipeline。
  32 位句柄（低 16 位池索引 + 高 16 位代数防悬垂；0 = 无效），
  desc 一次性描述、不可变创建，零值字段自动解析为合理默认。
- 帧模型：`begin_pass`（附件全空 = 交换链，渲染到 gpu 当前 surface）
  → `apply_pipeline` → `apply_bindings` → `apply_uniforms` →
  `draw` / `dispatch` → `end_pass` → `commit`。
- 计算：`sc_gfx_pass.compute = 1` 开计算 pass。Metal 已支持；
  GL 4.1 无 compute（macOS 上限）。
- 外部内存绑定：`image_desc.memimg` 非 0 时，纹理存储来自 gpu 的
  memimg（dma-buf/IOSurface）——既可作离屏渲染目标（按需绘制导出），
  也可作导入采样纹理（相机零拷贝）。限 2D 单 mip。
- `sc_gfx_finish()`：等 GPU 全部完成（glFinish 语义）——无表面导出帧
  的 `sync_fd` 为 -1 时，消费前调此同步。

## 3. 与 scc 的整合（核心）

`sc_gfx_shader_desc` 直接消费 scc 产物：

```c
sc_gfx_shader_desc sd = {0};
sd.vs.code  = (sc_gfx_range){ msl_text, msl_len };
sd.vs.entry = "vs_main";          /* Metal 产物入口 = .ss 阶段函数名；GL 恒 main */
sd.fs.code  = ...;
sd.reflect_json = reflect_text;   /* <stem>.reflect.json 内容 */
```

反射清单提供 uniform/storage 块的 binding/size、顶点属性 location、
sampler 绑定——运行时自动建立管线绑定（无清单时可手工描述）。

### Metal 后端绑定约定（与 scc 的 SPIR-V→MSL 转译对齐）

| 资源                | MSL 槽位                               |
|---------------------|----------------------------------------|
| uniform/storage 块  | `[[buffer(binding)]]` |
| 顶点数据缓冲        | `[[buffer(8 + slot)]]`（避让低编号 uniform 区） |
| 纹理 / 采样器       | `[[texture(binding)]]` / `[[sampler(binding)]]` |

对齐 sokol_gfx 已解决的问题：in-flight=2 信号量节流、延迟释放队列、
DYNAMIC/STREAM 缓冲双副本轮转、4MB uniform 环×2（256 对齐）、
viewport/scissor 钳制。

### GL 后端约定（GL 4.1 core，macOS 上限；scc `tar glcore@410`）

- 4.1 无 shader 内 explicit binding：链接后按反射清单名字解析，
  手动指派绑定点：uniform 块 = `stage * 8 + slot`；纹理单元 =
  `stage * 12 + slot`。
- 离屏 pass 用临时 FBO；MSAA 解析走 `glBlitFramebuffer`；交换链
  MSAA 暂不支持。无 compute / SSBO（报错不降级）。
- uniform 走 1MB UBO 环，每帧孤儿化。

## 4. 构建

无需预构建：`inc gfx.sc` 即用（`add src/*` 源码动态编译）。模块库按需：
`../../compiler/build/scc . --build` → libgfx.a。

后端宏与 gpu 同套（internal.h 自推导：darwin: Metal+GL；linux: GL）。
macOS 链接框架见 [.sc](.sc)（编译器自动注入）。

## 5. sc 侧用法

[gfx.sc](gfx.sc) 为 FFI 描述（句柄即 `u4`）。完整示例：
窗口渲染见 [templates/demo/gpu_demo.sc](../../demo/gpu_demo.sc)；
无表面渲染（MEMORY surface / memimg 绑定双模）见
[templates/demo/gpu_headless_demo.sc](../../demo/gpu_headless_demo.sc)。

```sh
./templates/.scenv/modules/wsi/build.sh    # 仅 wsi 需预编（libwsi.a 已入库）；gpu/gfx 源码动态编译
./compiler/build/scc templates/demo/gpu_shader/gpu_tri.ss -o templates/demo/gpu_shader/out/gpu_tri
./compiler/build/scc templates/demo/gpu_demo.sc     # Metal 三角形（框架链接自动注入，零 SCC_LDFLAGS）
# GPU_BACKEND=gl 前缀同一命令 → OpenGL 后端（同一三角形）
```

注意（FFI 陷阱，实测踩过）：
- sc 局部声明的 C 结构体**不零初始化**——desc/pass/bindings 用前须
  `::memset(&d, 0, sizeof(::sc_gfx_desc))`。
- 公开签名避免 `size_t`（macOS 上 ≠ `uint64_t`，与 FFI extern 冲突）；
  本 API 一律用 `uint64_t`。
- 句柄是纯 `uint32_t` typedef，与 sc 的 `u4` 直接对应。

## 6. 源码结构

```
gfx.h                 公开 C API（枚举 / desc / 函数）
gfx.sc                sc FFI 描述（add src/*.c 动态编译交付）
.sc                   模块构建/链接配置（按目标段注入，见 compiler.md §7.4）
src/internal.h        后端 vtable、资源池、反射结构
src/gfx.c             公共层：池/句柄/缺省值/校验/分派
src/gfx_reflect.c     反射清单 JSON 极简解析器（无外部依赖）
src/metal_gfx.m       Metal 命令翻译（ARC；消费 gpu 的 device/frame）
src/gl_gfx.c          GL 4.1 core 命令翻译（假定上下文已 current）
src/null_gfx.c        空后端（无硬件/测试）
```
