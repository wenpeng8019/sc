# spc —— 多维空间并行计算（space compute）

**命名旨趣**:cpu = 串行·逻辑·时间;gpu/spc = 并行·变换·空间——一对。
现代 GPU 已超越图像渲染,扩展到空间计算 / AI 并行计算;spc 就是
[utils/gpu](../gpu/)(运行环境)之下与 [utils/gfx](../gfx/)(渲染)
平级的**计算路**。

## 1. 三个入口(一个模块,按工作负载选引擎)

| 入口 | 定位 | mac 引擎(一期) | 板子(二期) |
|------|------|----------------|------------|
| **kernel**(可编程) | 执行 scc 编译 `.sg comp` 的产物;自定义并行算法/图像处理主场 | Metal compute | Vulkan / GLES3.1 |
| **graph**(算子) | 高性能张量算子,nn GPU 加速的着力点 | MPSGraph | — |
| **model**(整图推理) | 加载编译好的模型执行 | CoreML(**可调度 ANE 推理芯片**) | RKNN 等厂商栈 |

### ANE(Apple 推理芯片)的技术现实

ANE **不可直接编程**——kernel 面在 mac 恒为 GPU;ANE 唯一公开路径是
CoreML 整图执行(model 面)。`sc_spc_model_ane_ratio()` 用
MLComputePlan(macOS 14.4+)逐算子查询实际调度设备,**程序化确证**
而非猜测。实测:128×128×64ch 卷积网 86% 算子跑在 ANE(余下为仅支持
CPU 的 I/O cast);注意 CoreML 有成本模型,算量太小的网络会被调去
CPU——这是正确行为,不是集成失败。

## 2. 与其他模块的关系

- 须先 `sc_gpu_init`(headless 即可)再 `sc_spc_init`;共享
  `sc_gpu_device()`,但命令队列独立于 gfx(计算不依赖渲染)。
- **ts/nn 不动**:spc 接口收 `sc_tensor*`(须 C-连续),只读其字段,
  不调用 ts 函数——零链接依赖。nn 热点算子接 GPU 属二期。
- gfx 里的图形语境 compute(粒子/后效)保留在 gfx;spc 面向 AI/通用。

## 3. kernel 面机制

- 内核 = scc `.sg comp` 产物(MSL 文本)+ 反射清单。
- **槽位对位**:spirv-cross 会重排 MSL `[[buffer(N)]]`,与清单
  binding 不一致——运行时经 Metal 管线反射按**块实例名**对位。
- 绑定模型:`sc_spc_bindings.buffers[binding]`(storage 块)+
  `uniforms[binding]`(uniform 块,内联字节 setBytes)。
- dispatch 是**全局线程数**(N 维网格,精确 dispatchThreads);
  线程组尺寸取清单 `local_size`(现为编译器固定 64×1×1)。

## 4. 构建与验证

```sh
./templates/utils/gpu/build.sh && ./templates/utils/spc/build.sh
./compiler/build/scc templates/demo/spc_kernel/saxpy.sg -o templates/demo/spc_kernel/out/saxpy
python3 templates/demo/spc_model/gen.py     # 需 pip install coremltools
SCC_LDFLAGS="-framework Cocoa -framework Metal -framework QuartzCore \
  -framework IOSurface -framework OpenGL -framework MetalPerformanceShaders \
  -framework MetalPerformanceShadersGraph -framework CoreML -framework Foundation" \
  ./compiler/build/scc templates/demo/spc_demo.sc
```

[spc_demo.sc](../../demo/spc_demo.sc) 三重验证:kernel saxpy GPU vs
ts CPU 允差、graph matmul GPU vs CPU、model 推理数值 + ANE 占比。

## 5. 源码结构

```
spc.h                 公开 C API(三入口:buffer/kernel/dispatch、mm、model)
spc.sc                sc FFI 描述(inc ts.sc,张量互操作)
.sc                   scc 本地构建配置(macOS 框架 ldflags)
build.sh              静态库构建(一期仅 darwin)
src/internal.h        资源体、张量字段助手、darwin 函数声明
src/spc.c             公共层:池/句柄/反射解析/校验(单实现暂不抽 vtable)
src/metal_spc.m       kernel 面:Metal compute(独立队列、管线反射对位)
src/mpsg_spc.m        graph 面:MPSGraph 算子(一期 matmul F4)
src/coreml_spc.m      model 面:CoreML 加载/推理/MLComputePlan 查证
```

## 6. 二期方向

- nn 热点算子(matmul/conv2d/sdpa)经 graph 面 GPU 加速(tape 不动)
- Linux:Vulkan/GLES3.1 compute kernel、RKNN model 后端(经 memimg
  与相机/编码器零拷贝衔接)
- .sg comp 的 local_size 语法(现编译器固定 64×1×1)
