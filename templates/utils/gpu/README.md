# gpu —— GPU 运行环境（env 层）

为渲染层（[utils/gfx](../gfx/)）搭好"GPU 运行环境"的模块，
类比 **sokol_app 去掉窗口维护的部分**。

## 1. 定位与边界

两层分工（参考 sokol_gfx 的 environment / swapchain 契约）：

| 层 | 模块 | 职责 |
|----|------|------|
| 运行环境 | **gpu（本模块）** | 后端选择、设备创建、surface（交换链）、交换链 MSAA/深度目标、resize、vsync、每帧渲染目标交付、呈现收尾 |
| 渲染 | [gfx](../gfx/) | 资源（buffer/image/sampler/shader/pipeline）、pass/draw/commit、执行 scc 编译的 GPU 代码 |

边界交付物（gfx 消费，性能零热路径开销——每帧 O(1) 次指针交接）：

```c
void* sc_gpu_device(void);              /* 一次性：Metal 的 MTLDevice；GL 为 NULL */
int   sc_gpu_frame_acquire(sc_gpu_frame*);  /* 每帧：当前 surface 的渲染目标句柄 + 真实像素尺寸 */
void  sc_gpu_frame_end(void);           /* 帧收尾：GL swap / Metal 释放 drawable 引用 */
```

| 后端 | env 一次性交付 | 每帧交付（sc_gpu_frame） |
|------|--------------|------------------------|
| Metal | `id<MTLDevice>` | drawable 的 color/msaa/depth 纹理 + drawable + 尺寸 |
| GL | "上下文已 current" | 默认 FBO id（0）+ 像素尺寸 |

resize 竞态在 env 内解决：`frame_acquire` 校验附属纹理与 drawable
尺寸，不一致立刻重建——gfx 拿到的恒为本帧真实尺寸。

## 2. surface（呈现目标）

surface = 一块可呈现的原生窗口面（交换链）：Metal 为 CAMetalLayer
（挂 wsi NSView），GL 为平台上下文（NSGL / WGL / GLX）。
`sc_gpu_init` 提供 `desc.surface.native_window` 时自动建默认 surface
并置为当前；多窗口用 `sc_gpu_make_surface` + `sc_gpu_make_current`。

## 3. 多后端机制（同 wsi / glfw）

一个 `libgpu.a` 可同时编入多个后端，运行时按 `sc_gpu_desc.backend`
显式选择或按平台默认（不静默降级）：

| 平台    | 默认后端 | 备选        | 编译宏          | 状态   |
|---------|---------|-------------|-----------------|--------|
| macOS   | Metal   | GL          | `SC_GPU_METAL` + `SC_GPU_GL` | ✅ 均已验证 |
| Linux   | GL      | Vulkan(远期) | `SC_GPU_GL`     | 已编入待验证 |
| Windows | GL      | D3D(远期)   | —               | 待补（GL 需加载器） |
| 任意    | Null    | —（测试用） | 恒编入          | ✅     |

## 4. 构建

```sh
./build.sh                        # 宿主构建 → libgpu.<triple>.a + libgpu.a
./build.sh --target <triple>      # 交叉编译
```

macOS 链接需系统框架（本目录 [.sc](.sc) 配置文件已带）：
`-framework Metal -framework QuartzCore -framework OpenGL -framework Cocoa`

## 5. sc 侧用法

[gpu.sc](gpu.sc) 为 FFI 描述；帧交付接口是 C 侧 gfx 后端消费的内部
契约，sc 侧一般只用生命周期与 surface 接口。完整示例见
[templates/demo/gpu_demo.sc](../../demo/gpu_demo.sc)（gpu + gfx 两层配合）。

## 6. 源码结构

```
gpu.h                 公开 C API（backend/像素格式/surface/frame 契约）
gpu.sc                sc FFI 描述
.sc                   scc 本地构建配置（macOS 框架 ldflags）
build.sh              静态库构建（多后端宏在此选择）
src/internal.h        env 后端 vtable、surface 池
src/gpu.c             公共层：surface 池/句柄/缺省值/分派
src/metal_env.m       Metal env（ARC；device + CAMetalLayer + drawable 交付）
src/gl_ctx.h/.c       GL 上下文平台层（NSGL / WGL / GLX；mac 按 ObjC 编译）
src/gl_env.c          GL env（上下文 + VAO + 帧交付/swap）
src/null_env.c        空 env（无硬件/测试）
```
