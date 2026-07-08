# GPU 模块体系核心机制/原理

> 本文是 GPU 模块体系（builtins 内的 gpu/gfx/spc + 外部适配层 wsi）的**统一规格文档**，
> 与根目录 [AGENTS.md](../../AGENTS.md)（开发上下文/平台矩阵）互补：AGENTS.md 管「现状与待办」，
> 本文管「**怎么做、怎么实现、背后的技术和流程是什么**」——各 GPU 设备技术的原理、
> 以及本项目对它们的适配机制，均从实际源码推导总结。风格对齐
> [builtins/MECHANISM.md](../MECHANISM.md)。
>
> 模块归属：gpu/gfx/spc 是语言内置模块（builtins，`inc gpu.sc` 即用，平台
> 框架链接由编译器自动注入）；wsi（窗口库）留在 templates/utils，按 gpu.h
> 定义的「平台原生句柄标准」适配——gpu 不依赖任何窗口库。

---

## 机制全景

| 机制 | 源码落点 | 运行时载体 | 一句话 |
|------|----------|-----------|--------|
| **分层与边界契约** | gpu.h / gfx.h / spc.h | device·frame_acquire·frame_end 三握手 | env 层交付设备与帧目标，渲染/计算层只消费句柄 |
| **多后端 vtable** | gpu/src/internal.h、gfx/src/internal.h | `gpu_env_api` / `gfx_backend_api` 函数表 | 每后端一张表，编译宏裁剪，运行时显式选择不静默降级 |
| **Metal 适配** | metal_env.m / metal_gfx.m | CAMetalLayer + 命令缓冲 + 信号量帧控 | drawable 即交换链影像，retained references 保释放安全 |
| **GL 上下文家族** | gl_ctx.c（NSGL/WGL/GLX）+ gl_egl.c（EGL） | 每 surface 一上下文 + 全局 VAO | 上下文即环境：make current 后 gfx 直接发 GL 命令 |
| **EGL/GBM 无屏** | gl_egl.c | GBM 平台 display + surfaceless 上下文 | 不开窗口拿 GPU：DRM 渲染节点 → GBM → EGL |
| **memimg 可导出图像** | metal_env.m / gl_egl.c / gl_env.c | IOSurface（mac）/ dma-buf（linux） | 跨进程/跨引擎共享的 GPU 内存原语，双向（导出/导入） |
| **MEMORY surface** | gpu/src/gpu.c（公共层） | memimg 环 + 四态状态机 | 无表面交换链：渲染循环与窗口场景同构，帧出口是编码器 |
| **着色管线（scc）** | compiler/src/codegen_glsl.cpp、shader_caps.h | 每目标一份文本产物 + 反射 JSON | .ss 离线转多方言，能力表当契约，反射清单驱动运行时绑定 |
| **spc 三入口** | metal_spc.m / mpsg_spc.m / coreml_spc.m | kernel（自写）/ graph（算子）/ model（整图） | 可编程性递减、调度自动化递增的三级计算面 |

四模块依赖：`wsi →(native_window) gpu ←(C API) gfx / spc`；gfx 与 spc 平级互不依赖。

---

## 1. 分层与边界契约：environment / swapchain 两握手

**技术背景**：GPU 编程有两类平台性负担——「环境」（设备/上下文怎么来）和「交换链」
（画完的像素去哪）。sokol 的经验是把这两件事从渲染 API 中剥离成 environment 与
swapchain 两个注入点，渲染层就能做到平台无关。本项目按同样思路拆为 gpu（env 层）
与 gfx（渲染层），但比 sokol 更进一步：**env 层是活模块而非一次性描述体**，因为
我们还要管理 MEMORY surface（内存交换链）这类动态资源。

**契约**（gfx/spc 只允许调用这几个 gpu API，编译期单向依赖）：

```c
void* sc_gpu_device(void);                    // 一次性：原生设备（Metal=MTLDevice；GL=NULL）
int   sc_gpu_frame_acquire(sc_gpu_frame*);    // 每帧：渲染目标原生句柄 + 真实像素尺寸
void  sc_gpu_frame_end(void);                 // 每帧收尾：GL swap / Metal 释放 drawable
int   sc_gpu_query_surface_info(0, &sd);      // 交换链像素格式/MSAA（gfx 管线缺省对齐用）
int   sc_gpu_query_backend(void);             // 后端种类（gfx 选对应翻译器）
```

`sc_gpu_frame` 是**平面 union 结构**（Metal 塞 `mtl_drawable/mtl_texture`，GL 塞
`gl_fbo`），不做抽象句柄——渲染后端本来就按后端写，抽象在这里只添乱。像素格式
`sc_gpu_pixel_format` 是两层共同词汇（gfx 有自己的资源格式枚举，但交换链格式必须
与 env 层一致，所以由 env 层定义）。

**帧流**：`gpu_init → gfx_init → { begin_pass(内部 frame_acquire) → draw →
commit(内部 frame_end) } → gfx_shutdown → gpu_shutdown`。gfx 的交换链 pass 不
持有 surface 概念，每帧向 env 要一次目标——resize、drawable 轮转、环槽位选择全部
被 env 层吸收。

---

## 2. 多后端 vtable：编译宏裁剪 + 运行时显式选择

与 wsi（glfw 同构）同一套机制：每个后端实现一张函数表，公共层只做句柄池与分派。

```c
// gpu/src/internal.h
typedef struct gpu_env_api {
    const char* name;  sc_gpu_backend kind;
    bool (*init)(const sc_gpu_desc*);  void (*shutdown)(void);  void* (*device)(void);
    bool (*surface_create)(gpu_surface_t*);   /* …destroy/activate/resize */
    bool (*frame_acquire)(gpu_surface_t*, sc_gpu_frame*);  void (*frame_end)(void);
    /* memimg 七操作 + surface_dequeue：可选，不支持置 NULL */
} gpu_env_api;
```

- **编译宏**：`SC_GPU_METAL` / `SC_GPU_GL`（darwin 两者全开，linux 仅 GL）+
  `SC_GPU_GLES`（GL 后端的 ES 编译形态，见 §4）。宏由两库的 build.sh 同步注入——
  gfx 的翻译器选择必须与 gpu 的 env 选择一致。
- **运行时选择**：`desc.backend` 显式指定或平台默认（mac=Metal）。**不静默降级**：
  要 Metal 没 Metal 就是失败，不偷偷换 GL——图形后端的行为差异（坐标系、精度、
  格式支持）大到不允许「悄悄换一个」。
- 命名规约：公开 API `sc_gpu_*`（sc 语言 C 侧域前缀）；模块内部 `gpu_*`/`gfx_*`/
  `gl_*`/`spc_*`；全局状态 `g_gpu`/`g_gfx`/`g_wsi`。

---

## 3. Metal 适配：drawable 生命周期 + 信号量帧控

**技术原理**：Metal 没有「默认帧缓冲」概念——交换链是 `CAMetalLayer`（Core Animation
图层），每帧 `nextDrawable` 借出一张纹理（drawable），画完 `presentDrawable` 归还给
合成器。命令通过 `MTLCommandBuffer` 显式编码提交，GPU 完成回调驱动 CPU/GPU 同步。

**env 层适配**（metal_env.m）：
- `MTLCreateSystemDefaultDevice()` 建设备（Apple Silicon 即 GPU+ANE 同 die 的统一内存）。
- WINDOW surface：把 `CAMetalLayer` 挂到 wsi 交付的 NSView 上。**主线程约束**：
  layer 操作必须在主线程——`[NSThread isMainThread]` 判断后直接执行或
  `dispatch_sync(main_queue)`，先判断是防止已在主线程时 dispatch_sync 自锁。
- resize 竞态：`frame_acquire` 时校验 drawable 尺寸与 desc 尺寸，不符即跳帧重试
  ——窗口拖拽时 layer 尺寸变更与渲染循环异步。
- MEMORY surface：环成员 = memimg 的 IOSurface 纹理（§6），`ring_cur` 槽位由公共层
  状态机给出。

**gfx 层适配**（metal_gfx.m）：
- **帧控**：in-flight = 2 的 `dispatch_semaphore`——CPU 最多领先 GPU 两帧，
  commit 时命令缓冲挂 `addCompletedHandler` signal。**shutdown 坑**：
  `dispatch_semaphore` 释放时值低于初值会故意 crash（GCD 设计），必须先排空
  在飞命令再把信号量 signal 回填到初值。
- **释放安全**：Metal 命令缓冲默认 retained references（持有所引用资源），资源
  重赋值/销毁通常安全；gfx 仍保留逐帧槽位的延迟释放队列兜底。动态缓冲双副本轮转
  （每帧首次更新时切换），避免 GPU 在读时 CPU 在写。
- **绑定约定**：uniform/storage 块 `[[buffer(binding)]]`、顶点数据
  `[[buffer(8+slot)]]`、纹理/采样器 `[[texture/sampler(binding)]]`——顶点缓冲
  从 slot 8 起避开资源绑定区。4MB uniform 环 ×2 副本承载 apply_uniforms。

---

## 4. GL 上下文家族：一 API 五平台形态

**技术原理**：OpenGL 没有设备对象——「上下文」就是环境，make current 之后线程上的
GL 调用全部作用于该上下文。平台差异全在「上下文怎么创建、挂到哪」：

| 形态 | 平台 | 创建路径 | 交换 |
|------|------|---------|------|
| NSGL | macOS | `NSOpenGLContext` + setView（view=NULL 即无屏） | `flushBuffer` |
| GLX | Linux/X11 桌面 | `glXCreateContextAttribsARB` | `glXSwapBuffers` |
| WGL | Windows | `wglCreateContextAttribsARB`（已写未实测） | `SwapBuffers` |
| EGL window | Linux 嵌入式/Wayland | `eglCreateWindowSurface`（SC_GPU_GLES 形态） | `eglSwapBuffers` |
| EGL surfaceless | Linux 无屏 | GBM display + `EGL_KHR_surfaceless_context` | 无（FBO 渲染） |

**适配机制**：
- gl_ctx.c 管前三种（每 surface 一个上下文 + 上下文级全局 VAO——core profile 必须
  有 VAO 绑定才能画）；gl_egl.c 管后两种。`SC_GPU_GLES` 编译形态下 gl_ctx.c 整个
  不编译（GLX 是桌面 GL 的装载协议，GLES 世界只有 EGL）。
- **按名绑定退化**：GL 4.1（mac 上限）无 shader 内 explicit binding——gfx 链接后按
  反射清单的名字 `glGetUniformBlockIndex`/`glGetUniformLocation` 解析，手动指派
  绑定点：uniform 块 = `stage*8+slot`、纹理单元 = `stage*12+slot`（GL 绑定点是
  全局命名空间，Metal 是每阶段槽位——乘 stage 错开）。
- **GLES 形态差异**（SC_GPU_GLES，交叉编译验证）：头文件用入库的 Khronos 官方头
  （gpu/khr/，免交叉 sysroot）；border wrap 编译期退化 CLAMP_TO_EDGE（ES3.2 才有
  border）；MSAA 纹理走 `glTexStorage2DMultisample`（ES3.1 immutable 形式）。
  其余 VAO/UBO/sampler 对象/instancing/blit 在 ES3.0 与桌面 4.1 同名同义。
- uniform 数据走 1MB UBO 环，每帧 `glBufferData(NULL)` 孤儿化（GL 经典手法：旧存储
  交给在飞命令，拿新存储继续写，避免同步等待）。

---

## 5. EGL/GBM/DRM 无屏：不开窗口拿 GPU

**技术原理**（Linux 图形栈三层）：
- **DRM**（Direct Rendering Manager）：内核 GPU 接口，`/dev/dri/card0`（含模式设置）
  与 `renderD128`（纯渲染节点）。
- **GBM**（Generic Buffer Management）：Mesa 的缓冲分配器，从 DRM 设备分配可扫描/
  可渲染的 BO（buffer object），BO 可导出 dma-buf fd。
- **EGL**：把 GL 上下文装到任意「平台 display」上——窗口系统（X11/Wayland）是平台，
  **GBM 设备本身也是平台**（`EGL_PLATFORM_GBM_KHR`）。配合
  `EGL_KHR_surfaceless_context` 扩展，make current 时两个 surface 参数都传
  `EGL_NO_SURFACE`——上下文存在但没有默认帧缓冲，渲染全走 FBO。

**适配**（gl_egl.c，盲写已过交叉编译，板验待做）：打开 DRM 设备 → `gbm_create_device`
→ `eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_KHR)` → 桌面 GL 4.1 优先（回落 GLES3）
→ surfaceless make current。无 1×1 pbuffer 技巧（那是 surfaceless 扩展出现前的
老办法）。

**显式同步**：`EGL_ANDROID_native_fence_sync`——GPU 命令流里插 fence，
`eglDupNativeFenceFDANDROID` 导出 sync_fd（内核 sync_file）。消费者（编码器/另一
进程）poll 该 fd 即知渲染完成——这是 dma-buf 跨设备零拷贝管线的标准同步原语。
扩展不可用时退化 `glFinish()`（CPU 同步，正确但慢）。

---

## 6. memimg：可导出/可导入内存图像（跨引擎共享原语）

**问题**：渲染结果要送硬件编码器（V4L2/VideoToolbox/MediaCodec）、相机帧要零拷贝
进 GPU 采样——都需要「GPU 能画/能采、其他引擎能直接访问」的内存。各平台给这类内存
起了不同名字，但语义完美对偶：

| 平台 | 原语 | 导出形式 | GPU 包装 |
|------|------|---------|---------|
| Linux | **dma-buf**（GBM BO / dma-heap CMA） | fd + stride/offset/fourcc | `EGLImage` → `glEGLImageTargetTexture2DOES` |
| Apple | **IOSurface** | IOSurfaceRef（XPC 可传） | Metal：`newTextureWithDescriptor:iosurface:`；NSGL：`CGLTexImageIOSurface2D`→`GL_TEXTURE_RECTANGLE` |
| Android | **AHardwareBuffer**（规划） | binder 可传 | EGLImage / Vulkan external memory |

**适配机制**：
- `sc_gpu_memimg` 句柄统一双向操作：`alloc`（自分配，输出方向）/ `import`（借用
  外部帧，输入方向）/ `export`（导出 `sc_gpu_memory_frame`：planes/fd[4]/stride/
  offset/fourcc/sync_fd/native）/ `native`（gfx 后端消费的 GPU 句柄）/ `map/unmap`
  （CPU 访问）。
- **fd/native 借用语义**：导入不 dup、导出不转移所有权——生命周期由分配方管理，
  接口零隐式拷贝。sync_fd 是唯一例外（dequeue 转移给调用方 close）。
- Linux 分配二选一：GBM BO（设备最优 tiling）或 dma-heap CMA（物理连续，某些编码器
  硬性要求）；`fourcc`（DRM 四字符码，如 AR24=BGRA8）是跨引擎格式词汇。
- mac 的 NSGL 包装限制：`CGLTexImageIOSurface2D` 只支持 `GL_TEXTURE_RECTANGLE`
  目标——作 FBO 附件渲染没问题，但 shader 采样需 `sampler2DRect`（非归一化坐标），
  普通 sampler2D 不适用。Metal 无此限制。

---

## 7. MEMORY surface：内存交换链（无表面渲染）

**场景**：视频流编码（持续帧循环）与嵌入式按需绘制——渲染循环希望与窗口场景**同构**
（begin_pass/draw/commit 一模一样），只是帧的出口不是屏幕而是编码器。

**机制**（公共层 gpu.c，后端无关）：surface 两形态之一 `SC_GPU_SURFACE_MEMORY`，
创建时公共层自动分配 N 张 memimg 组成环，后端按槽位建渲染目标（Metal=IOSurface
纹理、GL=FBO 包装）。环槽四态状态机：

```
FREE ──frame_acquire──▶ ACQUIRED ──frame_end──▶ RENDERED
  ▲                                                │
  └──enqueue（归还）── DEQUEUED ◀──dequeue（消费）──┘
```

- 状态用 `__atomic` 读写：渲染线程（acquire/end）与消费线程（dequeue/enqueue）
  是单生产者/单消费者对。
- `dequeue` 交付 `sc_gpu_memory_frame`（含 frame_end 时存下的 sync_fd），真实链路
  把 fd/native 交给 V4L2/VideoToolbox；验证链路 `map` 后写 PPM 肉眼比对。
- 双驱动模式：**Mode A**（gpu 驱动环——上述状态机）；**Mode B**（gfx 驱动——memimg
  直接绑定 gfx image 作离屏渲染目标，`export` 导出，环由应用自管）。

---

## 8. 着色管线：scc 离线多方言 + 能力表契约 + 反射驱动绑定

**流程**（全部离线，运行时零着色器编译依赖）：

```
.ss 源 ─parse─▶ AST ─shader_sema(能力门控)─▶ codegen_glsl ─┬─▶ vulkan: GLSL 450 文本
                                                          ├─▶ glcore/gles/webgl: 对应方言文本
                                                          └─▶ metal: GLSL→glslang→SPIR-V→SPIRV-Cross→MSL
                                            每目标另产 <stem>.reflect.json（反射清单）
```

- **能力表**（shader_caps.h，单一事实源）：行 = 能力（StorageBuffer/ComputeStage/
  UintType/VertexIdBuiltin/…），列 = API 族，格值 = `{核心起始版本, 替代扩展,
  扩展版本下限}`。sema 收集「已用能力集」对每个 `tar` 目标门控（不满足硬报错，
  文案给两条补救途径）；codegen 按表决定发射形态（set/binding 语法、`#extension`
  指令、legacy ES 的 attribute/varying 全套）。
- **设备能力档案**（caps profile）：`tar "rk3588.caps"` 外部文件锚定具体板卡
  （api/version/ext 行式协议）——「基线版本低但带关键扩展」的真实设备不再受限于
  API@版本网格点。
- **反射清单**：`resources[]{name,kind,set,binding,size,members[]}` +
  `stages[]{entry,inputs[],local_size}`。运行时按清单建管线：Vulkan set/binding
  直映；Metal 按清单顺序即 binding；GL 按名解析。**坑**：SPIRV-Cross 会重排 MSL
  `[[buffer(N)]]` 槽位——运行时绝不能假设 MSL 槽号 = 清单 binding，metal_gfx 按
  清单顺序、spc 用 Metal 管线反射按名对位。
- legacy ES（gles@100）的清单带 `flattenUniforms: true`：uniform 块已平铺为
  `块_字段` 普通 uniform，运行时须按名逐字段 `glUniform*` 上传（ES2 无 UBO）。

---

## 9. spc：多维空间并行计算的三级入口

**设计**：可编程性与调度自动化不可兼得，按粒度分三个面，同一 `sc_spc_buffer` 词汇：

| 入口 | 载体 | 可编程性 | 典型负载 |
|------|------|---------|---------|
| `kernel` | .ss comp → Metal compute pipeline | 完全自写 | 自定义算子（saxpy/图像处理） |
| `graph` | MPSGraph（图优化 + kernel 融合） | 组合算子 | matmul/卷积等标准张量运算 |
| `model` | CoreML 整图（.mlmodelc） | 无（声明式） | 整网推理，唯一能上 ANE 的路径 |

- **kernel**（metal_spc.m）：dispatch 用精确全局线程数（Metal non-uniform
  threadgroups）；绑定用 `MTLPipelineOptionBindingInfo` 管线反射按名对位（解
  SPIRV-Cross 槽位重排）；local_size 从 .ss 反射清单读取。
- **graph**（mpsg_spc.m）：MPSGraph 张量图即时构建，NSData no-copy 包装输入。
- **model**（coreml_spc.m）：**ANE 不可编程**，唯一路径是 CoreML 整图交给
  Apple 调度器。**成本模型是特性不是缺陷**：小网络被调去 CPU 是正确行为（启动
  开销 > 收益），128×128×64ch 卷积网实测 86% 算子上 ANE。`MLComputePlan`
  （macOS 14.4+）可程序化查证每算子实际派发设备。
- ts/nn（CPU 张量库）保持纯数值零链接依赖：spc 只读 `sc_tensor` 字段（要求
  C-连续），CPU 参考实现即数值 allclose 的对照组。

---

## 附录：验证方法论

- **窗口链路**（mac）：后台跑 demo → `screencapture -x` 截图肉眼比对（截图前
  osascript 置前防遮挡）。
- **无屏链路**：MEMORY surface / memimg 渲染 → `map` → 写 PPM → 逐像素断言
  （角落 = 清屏色、中心 = 三角形色）。
- **计算链路**：GPU 结果 vs ts CPU 参考实现 allclose；ANE 占比 MLComputePlan。
- **盲写代码**（无板期间的 linux/GLES 路径）：入库 Khronos/mesa 头 + aarch64
  交叉编译当「真编译器审查」——能抓类型/签名/常量错误，运行时行为板验兜底。
- 着色产物：glslangValidator 全目标校验（含 ES 100 legacy 形态）。
