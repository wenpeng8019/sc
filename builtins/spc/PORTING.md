# spc 后端移植验证手册（PORTING）

> 面向场景：**换设备调试 spc kernel 面后端**（Vulkan / GLES3.1 / 桌面 GL4.3）。
> 全部后端代码已在 macOS 侧写完并交叉编译验证（Android NDK aarch64 +
> Windows mingw 通过；Metal 后端 + 特化常量传值已实机端到端）。
> 板端只需按本文档逐节验证，改动预期集中在「坑」清单所列的点。
>
> 配套端到端 demo：[templates/demo/spc_p2_demo.sc](../../templates/demo/spc_p2_demo.sc)
>（跨后端自适应，数值自校验，通过即后端 OK）。

## 0. 架构速览（读完再动手）

```
spc.c（公共层：句柄池/反射解析/校验）
  └─ K（spc_kernel_api vtable，init 时按 gpu_query_backend() 选定，不静默降级）
       ├─ metal_spc.m   darwin   （✅ 实机验证）
       ├─ vulkan_spc.c  linux/android/win（盲写，待板验）
       └─ gl_spc.c      linux/android    （盲写，待板验；GLES3.1+ / 桌面 GL4.3+）
graph（mpsg_spc.m）/ model（coreml_spc.m）仍 darwin 直连——非 kernel 面，别动。
```

- **后端跟随 gpu env**：`sc_gpu_init(desc.backend=...)` 决定一切；spc_init 查
  `sc_gpu_query_backend()`（1=Metal 2=GL 3=Vulkan）选 vtable，无对应实现即报错。
- **内核产物形态按后端**：Metal=MSL 文本、Vulkan=`.spv` 二进制、GL=GLSL 文本。
  同一 .ss 多 tar 产出全部形态，消费侧按后端选条目（见 §4）。
- **反射清单（reflect JSON）是运行时契约**：binding 对位 + local_size +
  spec_constants 类型，三个后端同一份。

## 1. 板端前置

| 后端 | 前置 |
|---|---|
| Vulkan | gpu env 以 `SC_GPU_BACKEND_VULKAN(3)` 初始化成功（libvulkan.so 动态加载，见 gpu/vk_loader）；无表面（MEMORY/headless）即可 |
| GLES3.1 | gpu env GL 后端 + EGL 上下文 current（headless pbuffer/surfaceless 即可）；`glGetString(GL_VERSION)` ≥ ES 3.1 |
| 桌面 GL | 上下文 ≥ GL 4.3（mac 上限 4.1 无 compute——mac 恒走 Metal） |

构建（模块随源码动态编译，无需预构建；交叉目标示例）：

```sh
# Android（已验证编译通过）：
./compiler/build/scc builtins/spc --build --env templates/.scenv --target android
# 板上原生编译则直接消费：inc spc.sc 即可（无需 --build）
```

## 2. 验证步骤（每后端相同流程）

```sh
# 1) 编内核（host 侧或板上均可；产物是 C 资源文件，平台无关）
./compiler/build/scc templates/demo/spc_kernel/reduce.ss     -o templates/demo/spc_kernel/out/reduce
./compiler/build/scc templates/demo/spc_kernel/scale_spec.ss -o templates/demo/spc_kernel/out/scale_spec

# 2) 跑 demo（按需在 demo 里把 gd.backend 显式设 3=Vulkan；GL 是 linux 默认）
./compiler/build/scc templates/demo/spc_p2_demo.sc
```

**期望输出**（数值必须逐字一致）：

```
spc 就绪(P2/P3 计算深化;gpu 后端 <N>: 1=Metal 2=GL 3=Vulkan)
[P2] shared+barrier+atomic 规约:通过(total=8386560, 16 组 x 256 线程)
[P3] spec 特化常量传值:通过(SCALE 默认 1.0 → 运行时 3.0)   ← GL 后端此行为"跳过"
spc_p2_demo 全部通过
```

- `total=8386560` = Σ(0..4095)，精确整数；不对 = shared/barrier/atomic 链有 bug。
- Vulkan 后端要验 [P3] 行（VkSpecializationInfo 路径）；GL 后端跳过属正常。

## 3. 各后端实现要点与坑（板端调试首查清单）

### 3.1 Vulkan（vulkan_spc.c）

设计：单 host-visible|coherent 内存池化缓冲（常驻映射）；每 dispatch 一个
一次性命令缓冲 + fence + 临时描述符池（挂在 16 槽在飞环，finish 统一回收）；
组数 = ceil(gx/local)（LocalSize 已编进 .spv，反射 local 只做换算）；
uniform 小参数每次 dispatch 建临时 uniform buffer。

已知设计取舍/潜在坑：
1. **入口名恒 "main"**（`ci.stage.pName`）——scc SPIR-V 的 OpEntryPoint 就叫
   main（entry 字段只用于产物文件名）。若板上报 "entry not found"，
   用 `spirv-dis xx.spv | grep OpEntryPoint` 核对。
2. **code 甄别**：喂 MSL/GLSL 文本会立即报「不是 SPIR-V」——检查 demo 的
   base 选择是否与 tar 声明序一致（§4）。
3. **内存类型**：`vkspcFindMemType` 要求 HOST_VISIBLE|HOST_COHERENT。
   个别嵌入式驱动的 device-local 才快——一期先跑通正确性，性能后议。
4. **subgroup 内核**（用了 subgroup_* 的 .ss）需要 Vulkan 1.1+ 实例/设备：
   gpu env 的 vulkan_env.c 目前建 1.0 实例的话，SPIR-V 1.3 模块会被
   vkCreateShaderModule 拒——先跑非 subgroup 内核；subgroup 板验时若报
   版本错，把 vulkan_env.c 的 apiVersion 提到 VK_API_VERSION_1_1。
5. **16bit/8bit storage**（f2/i1 进块）需设备 feature：
   VkPhysicalDevice16BitStorageFeatures.storageBuffer16BitAccess。gpu env
   建 device 时未启——板验 f2 内核报 validation 错时，在 vulkan_env.c 的
   vkCreateDevice pNext 链加该 feature（PORTING 后续项）。
6. 在飞环满（>16 未 finish 的 dispatch）会同步回收最老一槽——高频提交
   场景想提性能再改环大小。

### 3.2 GL（gl_spc.c）

设计：SSBO=glBufferData(GL_DYNAMIC_COPY)；uniform=每 kernel 每 binding 常驻
UBO，dispatch 时 glBufferData 重灌；显式 binding（GLES3.1/GL4.3 有
layout(binding=N)，scc 产物已发射）直接 glBindBufferBase；读回 =
glMemoryBarrier + glFinish + glMapBufferRange(READ)。

已知坑：
1. **上下文必须 current**：spc 不建上下文，依赖 gpu env 的 GL 上下文在当前
   线程 current（gpu_init 后就是）。多线程调 spc_dispatch 会炸——GL 后端
   单线程使用。
2. **init 探测**：`glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT,0,...)`
   出错 = 上下文低于 3.1/4.3 → 检查 EGL config 请求的 client version
  （gpu env gl_egl.c 请求 ES3，个别驱动给 3.0 上下文——需显式
   EGL_CONTEXT_MAJOR/MINOR 3.1，这是 gl_egl.c 侧待验项）。
3. **SSBO/UBO 绑定命名空间独立**：反射 binding 直接用（uniform binding 0 与
   storage binding 0 不冲突）。若板上驱动怪异对不上，用
   glGetProgramResourceIndex 按名对位兜底（未实现，报错再说）。
4. **spec 常量**：GL 无此机制，GLSL 文本已固化默认值。传 spec_values 会
   警告并忽略——不是 bug。
5. 桌面 Linux 的 `#else` 分支（GL/gl.h + GL_GLEXT_PROTOTYPES）只在交叉
   编译层面验证过 Android 分支，桌面 glibc 分支若头文件报错，检查
   sysroot 的 libglvnd 头版本。

### 3.3 Metal（metal_spc.m，参照实现）

已实机验证（含 spec 传值 MTLFunctionConstantValues）。板端调 Vulkan/GL 时
对拍参照：同一 .ss、同一 demo、同一期望数值。

## 4. 内核产物条目布局（消费侧契约）

`scc x.ss -o out/x` 默认产 `x.shader.c/h`：条目 = **对每个 tar 目标**
`[reflect JSON, stage1, stage2, ...]` 顺序展开，目标顺序 = tar 声明序。

- reduce.ss（`tar vulkan@450, metal@2.0, gles@310`，单 comp）：
  条目 0/1 = vulkan reflect/.spv，2/3 = metal，4/5 = gles
- scale_spec.ss（`tar vulkan@450, metal@2.0`）：0/1 vulkan、2/3 metal

demo 的 `reduce_base()/scale_base()` 按 `gpu_query_backend()` 映射 base；改
tar 声明序时必须同步改这两个函数（或改用条目 target 字段串比对）。

## 5. spec 特化常量传值 API（Metal/Vulkan）

```c
sc_spc_spec_value sv = { .id = 0, .value = <32位值> };  /* f4 传位模式 */
kd.spec_values = &sv;  kd.spec_count = 1;
```

- id = 反射 `spec_constants[].id`（.ss 里 `let X: T = v spec N` 的 N）。
- 类型对位：Metal 需要类型（从反射 type 字段取 'f'/'i'/'u'）；Vulkan 按位
  传 4 字节，类型无关。
- 不传 = 用 .ss 默认值；GL 后端传了警告忽略。

## 6. P2/P3 语法能力 × 后端矩阵（板验核对表）

| 能力 | Metal | Vulkan | GLES3.1 | 备注 |
|---|---|---|---|---|
| local X Y Z / shared / barrier / atomic_* | ✅ 实测 | 待板验 | 待板验 | gles 门控 310 |
| spec 常量传值 | ✅ 实测 | 待板验（VkSpecializationInfo 已写） | ❌ 无机制 | |
| subgroup 三件 | ✅ 编译实测（2.1+） | 待板验（需 VK 1.1 实例，见 §3.1-4） | ❌（无核心途径） | SPIR-V 1.3 |
| f2(f16) | ✅ 编译实测（half） | 待板验（需 16bit_storage feature，见 §3.1-5） | ❌ 门控 | |
| i8/u8(int64) | ✅ 编译实测（2.3+，long） | 待板验 | ❌ | Metal buffer 内 long 需 MSL 2.3 |
| i1/u1(int8)、i2/u2(int16) | ✅ 编译支持 | 待板验（8bit_storage） | ❌ | |

## 7. 板端排错工具链

```sh
spirv-val --target-env vulkan1.1 x.spv     # SPIR-V 合法性
spirv-dis x.spv | less                     # 反汇编核对 entry/binding/LocalSize
# Vulkan validation layer（板上有的话）：
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation ./run.out
# GL：spc_log 已带 shader 编译/链接 info log；再看
LIBGL_DEBUG=verbose / EGL_LOG_LEVEL=debug
```

失败时的最小化二分：先跑最简 saxpy（spc_kernel/saxpy.ss，无 shared/atomic），
再上 reduce（shared+barrier+atomic），最后 spec/f16——逐能力定位。
