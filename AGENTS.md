# AGENTS.md —— sc 项目开发上下文（跨机器交接事实源）

> 本文件是 GPU/图形/计算模块体系的**开发上下文导出**,供远程环境
> (WSL / Windows / Linux)继续开发时的 AI 代理与人类共同消费。
> 事实截至 2026-07-08(macOS 侧 Metal/GL/ANE 全链路已验证)。
> 下一阶段目标:**Windows/Linux 平台实现(Vulkan、D3D 等)**。

## 1. 项目速览

- **sc**:自研语言(s = space);`scc` 编译器(C++,`compiler/`,产 C99 交 cc)。
  `./compiler/build/scc file.sc` = 编译并立即运行(二进制在 `/tmp/scc_units_*/run.out`)。
- **syntax-s**(`.ss` 文件,语法参考 [syntax-s.md](syntax-s.md),设计/路线
  [syntax-s-design.md](syntax-s-design.md)):GPU 空间计算方言
  (vert/frag/comp),sc 的严格子集。命名旨趣:**cpu = 串行·逻辑·时间,
  gpu = 并行·变换·空间**。旧名 syntax-g/.sg 已全量更名,无兼容。
- 构建配置：gpu/gfx/spc/wsi 的平台框架链接由编译器自动注入（main.cpp 按
  scStem 特判，零 SCC_LDFLAGS）；临时链接标志仍可用环境变量 `SCC_LDFLAGS`。

## 2. GPU 模块体系(builtins 内置：gpu/gfx/spc；外部适配：wsi)

```
wsi(窗口/输入,templates/.scenv/modules/,glfw 同构自研)──按 gpu.h 句柄标准适配
  │ native_window/native_display（平台原生句柄，标准定义在 gpu.h 文件头）
gpu(builtins/gpu,≈sokol_app 去窗口)── device·surface交换链·memimg·帧交付
  ├── gfx(builtins/gfx,≈sokol_gfx)──资源/管线/pass/draw,执行 scc 产物
  └── spc(builtins/spc)──kernel(.ss comp)/graph(算子)/model(整图推理)
```

- **依赖方向（2026-07-08 反转）**：gpu 不依赖任何窗口库——gpu.h 文件头定义
  「平台原生句柄标准」（mac=NSView*/win=HWND/x11=Window/wl=wl_surface* 等），
  wsi 等窗口库是适配者；无窗口场景（MEMORY surface）两句柄均 NULL。
- **builtins 集成**：`inc gpu.sc` 裸名即用（子项目形态搜索 builtins/<名>/<名>.sc）；
  `add lib<mod>.a` 按 <名>.<triple>.a 变体匹配交叉目标；apple 的 ObjC 构建与
  C 工具链同源（xcode clang 同时发行两者），故 .m 后端可作为 builtins 默认集成。
- **能力档案库**：builtins/gpu/caps/ = 标准设备能力档案（.ss 里 `tar "名.caps"`
  搜索顺序：相对源文件 → builtins/gpu/caps/）；随 `--builtins` 目录整体替换。

- **边界契约**(参考 sokol 的 environment/swapchain):gpu 一次性交付
  `sc_gpu_device()`(Metal=MTLDevice;GL=NULL);每帧 `sc_gpu_frame_acquire()`
  交付渲染目标原生句柄+真实像素尺寸;`sc_gpu_frame_end()` 收尾(GL swap/Metal 释放 drawable)。
- **软边界**:gfx/spc 编译期单向依赖 gpu 的 C API;像素格式 `sc_gpu_pixel_format`
  是共同词汇。gfx 热路径(draw/bindings)不跨模块。
- **surface 两形态**:WINDOW(原生窗口交换链)/ MEMORY(无表面,memimg 图像环,
  dequeue/enqueue 供编码器,环状态机在 gpu.c 公共层,`__atomic` 跨线程)。
- **memimg**(可导出/导入内存图像,双向原语):linux = GBM BO/dma-heap→EGLImage,
  mac = IOSurface。输出方向送编码器(fence:linux EGL native fence→sync_fd,退化
  glFinish);输入方向 v4l2 相机零拷贝导入。fd/native 借用语义(不 dup)。
- **多后端机制**:每后端一张 vtable(`gpu_env_api` / `gfx_backend_api`),
  编译宏 `SC_GPU_METAL`/`SC_GPU_GL`(两库同套,internal.h 按目标平台自推导，
  形态宏由模块 .sc 段配置注入),运行时按
  `desc.backend` 显式选或平台默认,不静默降级。spc 一期单实现未抽 vtable
  (`#ifdef __APPLE__` 直连),加 Vulkan/RKNN 时再抽。
- **命名规约**:`sc_` 前缀 = sc 语言在 C 侧的域命名(仅公开 API,如
  `sc_gpu_init`);模块内部私有符号直接用模块名前缀(`gpu_*`/`gfx_*`/
  `spc_*`/`gl_*`),全局状态变量 `g_gpu`/`g_gfx`/`g_wsi`。**禁用 `_sc_` 前缀**
  (glfw 遗留风格,已全量清除)。

## 3. 各平台现状矩阵(诚实标注)

| 组件 | macOS | Linux | Windows |
|------|-------|-------|---------|
| wsi | ✅ cocoa 实测 | x11/wayland 已写未实测 | win32 已写未实测 |
| gpu env | ✅ Metal(metal_env.m)+ GL(NSGL 含无屏) | GLX(gl_ctx.c)+ EGL headless/memimg(gl_egl.c)+ EGL window(**盲写，GLES 形态交叉编译验证**) | WGL 已写未实测；**无 D3D** |
| gfx | ✅ Metal + GL4.1 双后端三角形实测 | gl_gfx.c 双形态：桌面 GL / **GLES3.0+(SC_GPU_GLES，交叉编译验证)**；**无 Vulkan** | **缺 GL 加载器**(gl_gfx.c #error)；**无 D3D** |
| spc | ✅ Metal kernel + MPSGraph + CoreML/ANE(86% 实测) | 待补(Vulkan/GLES31 kernel、RKNN model) | 待补 |
| 无表面渲染 | ✅ IOSurface 双后端双模验证（Metal + NSGL 无屏） | dma-buf 路径盲写 | 无 |

## 4. 新后端接入指南(Vulkan / D3D)

1. **文件落位与命名**:`gpu/src/<api>_env.c(pp)` + `gfx/src/<api>_gfx.c(pp)`
   (对照 metal_env.m/metal_gfx.m 逐函数移植);宏 `SC_GPU_VULKAN`/`SC_GPU_D3D11`;
   internal.h 自推导块与两模块 .sc 段配置同步加。backend 枚举加 `SC_GPU_BACKEND_VULKAN/D3D`
   + gpu.c/gfx.c 的 pickBackend 分支。
2. **env 要实现的 vtable**(gpu/src/internal.h):init/shutdown/device、
   surface 四操作、frame_acquire/frame_end、memimg 七操作(可 NULL)、surface_dequeue。
   Vulkan:device()=聚合体(instance/physdev/device/queue 打包结构指针);
   frame 结构可能需扩字段(image view/信号量)——扩 sc_gpu_frame 时保持 Metal/GL 兼容。
3. **gfx 要实现的 vtable**(gfx/src/internal.h):资源五类 + pass/draw/commit +
   finish + query_pixelformat。交换链 pass 内部调 sc_gpu_frame_acquire();
   commit 末尾调 sc_gpu_frame_end()。
4. **着色器供给**:scc `tar vulkan@450` 产 Vulkan-GLSL 文本(+反射 JSON);
   SPIR-V 经 glslangValidator 离线编译(见 examples/shader-tri/build.sh),
   Vulkan 后端的 shader_desc.code 吃 SPIR-V 二进制。D3D 远期需 HLSL 转译
   (SPIRV-Cross 支持,scc 侧待加 tar 目标)。
5. **绑定约定**:反射清单是 Vulkan set/binding 风格,Vulkan 后端最自然
   (descriptor set 直映)。Metal 卷 [[buffer(binding)]]+顶点缓冲 8+slot;
   GL4.1 无显式 binding,链接后按名指派(uniform 块=stage*8+slot,纹理=stage*12+slot)。
6. **WSL 注意**:WSLg 走 Wayland(wsi 有 wl 后端);/dev/dri 可能是 dxg 虚拟
   GPU(mesa d3d12 驱动),EGL/GBM 路径行为与真板不同;Vulkan 在 WSL 可用
   dzn(D3D12 上的 Vulkan)或 lavapipe(CPU)。真验证以 Windows 原生/实板为准。

## 5. scc 着色管线事实（2026-07-09 三期：SPIR-V 直发 + 产物资源化）

- 管线：AST → codegen_spirv（自研 SPIR-V 1.0 直发，glslang 已移除）→
  vulkan 直落 .spv / metal 经 SPIRV-Cross→MSL / glcore·gles 经 SPIRV-Cross 反译
  GLSL（ES100 legacy）。codegen_glsl 保留作对照/兜底（--emit-glsl，产物同名
  可替换）；--emit-spv 额外落 .spv。验证链：spirv-val/spirv-dis/xcrun metal。
- 编译：`./compiler/build/scc file.ss -o outdir/stem` → **默认资源化产物**
  `<stem>.shader.h/.c`：字节数组（文本带尾 NUL）+ enum id + 反射 JSON 内嵌 +
  `sc_shader_<sym>_get(i)` 非 inline 取条目（FFI 用）；条目布局每目标三连
  [reflect, 逐 stage]。`--files` 落旧式散文件（带目标标签）。demo 消费：
  `add out/x.shader.c` + sc 侧同布局 def + `@fnc shader_x_get::` 绑定。
- 目标:`tar metal@2.0`(MSL,入口=stage 函数名)/ `glcore@410` / `gles@300` /
  `vulkan@450`；`tar "x.caps"` 能力档案。
- 反射 JSON:`resources[]{name,kind:uniform|storage|push|sampler,set,binding,size}` +
  `stages[]{name,stage,entry,inputs[]{name,type,location},local_size(comp)}`。
- comp 已通(计算内建映射如 `global_invocation_id`,标量声明自动 `.x`;
  local_size 编译器固定 64×1×1 并写入反射;`.ss` 语法待二期)。
- **坑**:spirv-cross 会重排 MSL `[[buffer(N)]]` 槽位——运行时须按块实例名对位
  (metal_gfx 用清单顺序即 binding,spc 用管线反射按名;新后端别信 MSL 槽=binding)。
  SPIR-V 块类型名带 `_blk` 后缀、实例名=块名（保住按名对位契约）。
- 语言完备性路线：syntax-s-design.md §16（SPIR-V 反向对齐 P0-P3）。

## 6. 构建与验证工作流

> **.scenv 虚拟环境（2026-07-12 新增）**：templates 下建 `.scenv/` 域基（类 python venv）——
> `utils`（原 templates/utils）与 `targets`（原 templates/targets）已移入其下。scc 从源文件/cwd
> 向上自动发现 `.scenv`，令 `inc wsi.sc` 裸名（→ `.scenv/modules/`）、`--target android` 裸名
> （→ `.scenv/targets/`）、`-L`（`.scenv/lib/`）、环境配置（`.scenv/env.sc`）、缓存中间物
> （`.scenv/cache/`）自动生效。`--env <dir>` / `SCC_ENV` 覆盖发现。完整规则见 compiler.md §4.4。

```sh
# gpu/gfx/spc 为源码动态编译交付（inc 即用，无预构建；build.sh 已删）；模块库按需：
#   ./compiler/build/scc builtins/gpu --build          # → libgpu[.<triple>].a
./templates/.scenv/modules/wsi/build.sh   # wsi/ui 仍预编交付(lib*.a 入库)，改源码后重跑
# 编译器:cd compiler/build && make scc

# 三个 demo(从仓库根跑;平台框架链接由编译器自动注入,零 SCC_LDFLAGS):
#  gpu_demo.sc         窗口三角形(GPU_BACKEND=gl 前缀切 GL 后端——环境变量要
#                      加在 scc 命令前,因为程序在 /tmp 运行)
#  gpu_headless_demo.sc 无表面双模(MEMORY surface + memimg 绑定)→ 写 PPM 校验
#  spc_demo.sc         kernel/graph/model 三重验证(数值 allclose 对照 ts CPU)
```

- 窗口验证模式(mac):后台跑 demo → sleep → `screencapture -x` 截图肉眼比对
  (截图前 osascript 把 run.out 置前,防遮挡);Linux 桌面可类比 grim/import。
- 静态库：wsi/ui 的 .a **随 git 入库**（预编交付）；gpu/gfx/spc 源码动态编译无 .a；build/ 目录 gitignore。

## 7. sc FFI 规则(踩坑实录,写 demo/绑定必读)

- sc 局部声明的 C 结构体**不零初始化**:一律 `::memset(&d, 0, sizeof(::sc_gpu_desc))`。
- 公开签名禁 `size_t`(mac 上 ≠ uint64_t 会与 FFI extern 冲突),一律 `uint64_t`。
- 句柄 = 纯 `uint32_t` typedef(≠单成员结构体),sc 侧 `u4`;desc 参数
  `const ::type&`;`@fnc name::` 绑定 C 的 `sc_<name>`。
- `print` 不能直接打印函数调用返回值,先存变量;字符串比较用数值
  (`envb[0] == (103: char)`)。
- ObjC/ARC:`.m` 用 `-fobjc-arc -x objective-c` 编;calloc 结构体存强引用 OK
  (free 前置 nil);出参声明 `id<X> __strong*`;主线程 UI 操作要
  `[NSThread isMainThread]` 判断防 dispatch_sync 死锁。

## 8. 已知坑与技术判断(勿重蹈)

- `dispatch_semaphore` 释放时值低于初值会**故意 crash**:shutdown 排空后须 signal 回填。
- Metal 命令缓冲默认 retained references:资源释放通常安全,但 gfx 仍保留
  延迟释放队列(逐帧槽位)。动态缓冲双副本轮转(每帧首更切换)。
- NSGL 弃用告警已 pragma 静默;retina 需 `wantsBestResolutionOpenGLSurface=YES`。
- CoreML 有成本模型:**小网络被调去 CPU 是正确行为**(实测 128×128×64ch 卷积网
  86% 上 ANE);ANE 不可编程,唯一路径 CoreML 整图;`MLComputePlan`(macOS14.4+)
  程序化查证调度。
- GL 多 surface 上下文未共享对象(shareContext 传 nil,TODO);GL 交换链 MSAA 未支持。
- invo-avm(嵌入式全景参考项目,~/Downloads/invo-t60m/...)已消化:gbm_egl→gl_egl.c、
  frame_queue→MEMORY surface、post_pass/stash→pass 模型吸收;其源码有三处笔误勿抄
  (eglDestroySurface 参数、gbm_bo_destroy(display)、bo!=NULL 反判)。

## 9. 优先待办(Windows/Linux 方向)

1. **GLES 化（已基本完成，待板验）**：编译器侧 — 版本白名单（gles 仅
   100/300/310/320）、GLSL ES 100 发射模式（attribute/varying、gl_FragColor、
   uniform 块平铺+反射 flattenUniforms、数组降级、texture2D）、新能力行
   （UintType/VertexIdBuiltin/FragDepthBuiltin/MRT）；运行时侧 — SC_GPU_GLES
   编译形态（build.sh --gles，入库 Khronos 头 gpu/khr/，交叉编译验证通过）：
   gl_gfx.c ES3.0/3.1 分支、gl_egl.c EGL window surface、gl_env.c 窗口路径切
   EGL。**剩余**：ES2 运行时路径（flattenUniforms 按名 glUniform 上传、
   glBindAttribLocation、无 VAO/UBO/sampler 对象退化）、实板验证。
2. Linux 实板验证：gl_egl.c（EGL headless/GBM/dma-heap/fence）+ gl_gfx/gl_env、
   wsi x11/wayland。
3. **Vulkan 后端**（gpu env + gfx）：SPIR-V 直消费，反射即 set/binding，
   frame 契约可能需扩（image view/semaphore 字段）；建议先无屏后窗口（§4.1）。
4. **Windows**：wsi win32 验证；GL 加载器（gl_gfx.c 现 #error）或直接跳 D3D11；
   D3D 后端 + scc 的 HLSL tar 目标（SPIRV-Cross）。
5. spc 二期：Vulkan/GLES31 compute kernel、RKNN model 后端、nn 热点算子接
   graph 面、local_size 的 .ss 语法。
6. ts/nn 保持 CPU 纯数值不动（spc 只读 sc_tensor 字段，要求 C-连续，零链接依赖）。

## 10. 移动端路线（规划，wsi 将加移动后端）

- **iOS（离现有代码最近）**：GLES 在 iOS 已弃用，直接 Metal——metal_env/
  metal_gfx（CAMetalLayer/IOSurface 同款）与 spc 的 CoreML/ANE 几乎原样复用；
  只差 wsi 的 uikit 后端（UIWindow/触控事件）。
- **Android**：wsi 加 android 后端（ANativeWindow/ALooper）；gpu env 复用
  EGL（gl_egl 的 window 路径）或 Vulkan；**memimg = AHardwareBuffer**
  （与 dma-buf/IOSurface 完美对偶，可导 EGLImage/Vulkan external memory，
  直送 MediaCodec 编码）；spc model = NNAPI/厂商栈。
- memimg 三元组对偶：dma-buf(linux) / IOSurface(apple) / AHardwareBuffer(android)。

## 11. Web 路线评估（结论，2026-07-08）

- **WebGL = GLES 的同类**（非独立后端）：浏览器 WebGL2 ≈ GLES3.0、WebGL1 ≈
  GLES2；emscripten 把 GLES/EGL 调用直译到 WebGL——**SC_GPU_GLES 编译形态
  就是 Web 路径的主体**（gl_egl window 路径换 emscripten 的 EGL 模拟，
  wsi 换 emscripten HTML5 API）。scc 侧 webgl 目标已在能力表（webgl@100/300，
  多数能力对齐 gles 列）；ES2 运行时路径（flattenUniforms 等）同时服务
  WebGL1。**结论：不单做，随 GLES 板验后加 emscripten 构建形态验证**。
- **WebGPU = Vulkan 一档的独立后端**（非 GLES 同类）：WGSL 着色语言 +
  bind group 描述符模型 + 显式命令编码，抽象层级与 Vulkan/Metal 同代。
  接入路径：scc 加 `tar wgsl` 目标（SPIRV-Cross/naga 转译 WGSL）+
  gpu env/gfx 各一张 vtable（浏览器 JS 互操作或原生 Dawn/wgpu-native）。
  反射清单的 set/binding 直映 bind group——与 Vulkan 后端高度同构。
  **结论：排在 Vulkan 后端之后做，两者共享大部分设计决策**。
- 优先级不变：GLES 板验 → Vulkan → （D3D | WebGPU/emscripten 按需）。
