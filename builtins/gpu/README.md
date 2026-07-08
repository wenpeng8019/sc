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

## 2. surface（呈现目标）与无表面渲染

surface = 一块可呈现的目标面（交换链），两种形态：

- **WINDOW**：原生窗口面——Metal 为 CAMetalLayer（挂 wsi NSView），
  GL 为平台上下文（NSGL / WGL / GLX）。
- **MEMORY**：无表面——N 张可导出内存图像（memimg）的环，渲染循环与
  窗口场景完全同构；commit 后 `sc_gpu_memory_dequeue` 取帧（dma-buf fd /
  IOSurface）送编码器，`enqueue` 归还。视频流 / 嵌入式无屏场景。

`sc_gpu_init` 提供 `desc.surface.native_window` 时自动建默认 WINDOW
surface；传 NULL 即 headless 初始化（随后自建 MEMORY surface）。

### memimg（可导出/可导入内存图像，平台原语）

双向复用、双驱动模式共享的边界原语（linux = GBM BO / dma-heap →
EGLImage；mac = IOSurface）：

- **Mode A（gpu 驱动环，定频流水线）**：MEMORY surface 内置 memimg 环，
  交换链语义，dequeue/enqueue 交接消费线程。
- **Mode B（gfx 驱动，按需绘制）**：`sc_gpu_memimg_alloc` + 绑定到
  gfx image（`sc_gfx_image_desc.memimg`）作离屏渲染目标，应用自管环，
  `sc_gpu_memimg_export` 导出。嵌入式按需绘制省算力。
- **导入方向**：`sc_gpu_memimg_import` 包装外部 dma-buf（如 v4l2 相机），
  绑定 gfx image 作采样纹理（零拷贝）。

同步：linux 用 EGL native fence 导出 `sync_fd` 随帧交付；无 fence 扩展
退化 glFinish；mac 验证版 `sync_fd=-1`，消费前 `sc_gfx_finish()`。
fd/native 生命周期 = 借用（dequeue→enqueue 间有效）；sync_fd 归调用方关闭。

消费链路（v4l2 编码 / RTP 等）在 gpu/gfx 之外——边界止于交出 fd + fence。

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
gpu.h                 公开 C API（backend/像素格式/surface/memimg/frame 契约）
gpu.sc                sc FFI 描述
.sc                   scc 本地构建配置（macOS 框架 ldflags）
build.sh              静态库构建（多后端宏在此选择）
src/internal.h        env 后端 vtable、surface/memimg 池、环调度
src/gpu.c             公共层：池/句柄/缺省值/环状态机/分派
src/metal_env.m       Metal env（ARC；device + CAMetalLayer + IOSurface memimg）
src/gl_ctx.h/.c       GL 窗口上下文平台层（NSGL / WGL / GLX；mac 按 ObjC 编译）
src/gl_egl.h/.c       Linux EGL headless（GBM/DRM/dma-heap/EGLImage/fence）
src/gl_env.c          GL env（窗口上下文 + 内存 surface + 帧交付）
src/null_env.c        空 env（无硬件/测试）
```
