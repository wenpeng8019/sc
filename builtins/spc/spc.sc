# spc —— 多维空间并行计算（space compute）
#
# 命名旨趣：cpu = 串行·逻辑·时间；gpu/spc = 并行·变换·空间。
# spc 是 gpu（运行环境）之下与 gfx（渲染）平级的"计算路"，三个入口：
#   kernel 面：执行 scc 编译 .ss comp 的产物（自定义并行算法，Metal）
#   graph 面 ：高性能张量算子（mac = MPSGraph；nn GPU 加速的着力点）
#   model 面 ：整图推理（mac = CoreML，可调度 ANE 推理芯片）
#
# 依赖：须先 gpu_init（headless 即可）再 spc_init；张量互操作依赖 ts
#（张量须 C-连续；spc 只读其内存，不改 ts 语义）。
#
# 说明：各接口的功能、参数与约束注释见 spc.h。

inc ts.sc
inc spc.h

# 实现：源码动态编译（同 gpu.sc：平台自守卫 + .m 自动 ObjC）。
# kernel 面按 gpu 后端派发：darwin=Metal、linux/android=GL·Vulkan、win=Vulkan；
# graph/model 面仍 darwin 专属（非命中平台源文件全部空化为空 .o）。
add src/spc.c
add src/metal_spc.m
add src/vulkan_spc.c
add src/gl_spc.c
add src/mpsg_spc.m
add src/coreml_spc.m

# === 生命周期 ===
@fnc spc_init:: i4, desc: const ::sc_spc_desc&
@fnc spc_shutdown::
@fnc spc_isvalid:: i4
@fnc spc_finish::

# === kernel 面：buffer（GPU 缓冲；句柄 u4，0 无效） ===
@fnc spc_make_buffer:: u4, desc: const ::sc_spc_buffer_desc&
@fnc spc_buffer_from_tensor:: u4, t: tensor&
@fnc spc_buffer_to_tensor:: i4, buf: u4, t: tensor&
@fnc spc_buffer_read:: i4, buf: u4, dst: &, size: u8, offset: u8
@fnc spc_buffer_write:: i4, buf: u4, src: const &, size: u8, offset: u8
@fnc spc_destroy_buffer:: buf: u4

# === kernel 面：内核与调度（gx/gy/gz = 全局线程数） ===
# 特化常量传值：kernel_desc.spec_values/spec_count（见 spc.h；仅 Metal/Vulkan）
@fnc spc_make_kernel:: u4, desc: const ::sc_spc_kernel_desc&
@fnc spc_destroy_kernel:: k: u4
@fnc spc_dispatch:: i4, k: u4, gx: i4, gy: i4, gz: i4, bindings: const ::sc_spc_bindings&

# === graph 面：张量算子（一期 matmul，2D DT_F4 连续） ===
@fnc spc_mm:: i4, a: tensor&, b: tensor&, out: tensor&

# === model 面：整图推理（mac = CoreML/.mlmodelc；ANE 见 spc.h） ===
@fnc spc_model_load:: u4, path: const char&, units: i4
@fnc spc_destroy_model:: m: u4
@fnc spc_model_run1:: i4, m: u4, in: tensor&, out: tensor&
@fnc spc_model_ane_ratio:: i4, m: u4
