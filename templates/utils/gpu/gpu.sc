# gpu —— GPU 设备操作统一接口（执行 scc 编译转义后的 GPU 代码）
#
# 定位（区别于常见 gfx 抽象层）：
#   不追求"消除平台差异性的统一图形抽象"，而是**驱动 GPU 硬件、执行 scc
#   编译 .sg 产物（MSL/GLSL/SPIR-V + 反射清单）**的薄硬件访问层，
#   概念对应 gfx-hal / sokol_gfx。
#
# 多后端机制（同 wsi/glfw）：一个 libgpu.a 可同时编入多个后端，运行时
#   按平台默认或 sc_gpu_desc.backend 显式选择：
#     macOS → Metal（默认）/ GL     linux/win → GL（默认）
#
# 依赖：wsi（native_window / native_display 来自 sc_wsi_win_get_native_*）。
#
# 句柄：C 侧为纯 u4 typedef（低 16 位池索引 + 高 16 位代数），绑定声明
#   为 u4；0 = 无效句柄。desc / 绑定 / pass 等大结构体经指针传递（&）。
#
# 说明：各接口的功能、参数与默认值注释见 gpu.h（本文件仅按功能分区罗列绑定）。

inc gpu.h
add libgpu.a

# === 生命周期 ===
@fnc gpu_init:: i4, desc: const ::sc_gpu_desc&
@fnc gpu_shutdown::
@fnc gpu_isvalid:: i4
@fnc gpu_query_backend:: i4
@fnc gpu_resize:: width: i4, height: i4

# === surface（呈现目标；多窗口用 make_current 切换） ===
@fnc gpu_make_surface:: u4, desc: const ::sc_gpu_surface_desc&
@fnc gpu_destroy_surface:: surf: u4
@fnc gpu_make_current:: surf: u4
@fnc gpu_query_current_surface:: u4
@fnc gpu_surface_resize:: surf: u4, width: i4, height: i4

# === 资源创建 / 销毁（句柄 = u4，0 无效） ===
@fnc gpu_make_buffer:: u4, desc: const ::sc_gpu_buffer_desc&
@fnc gpu_make_image:: u4, desc: const ::sc_gpu_image_desc&
@fnc gpu_make_sampler:: u4, desc: const ::sc_gpu_sampler_desc&
@fnc gpu_make_shader:: u4, desc: const ::sc_gpu_shader_desc&
@fnc gpu_make_pipeline:: u4, desc: const ::sc_gpu_pipeline_desc&
@fnc gpu_destroy_buffer:: buf: u4
@fnc gpu_destroy_image:: img: u4
@fnc gpu_destroy_sampler:: smp: u4
@fnc gpu_destroy_shader:: shd: u4
@fnc gpu_destroy_pipeline:: pip: u4

# === 资源更新 ===
@fnc gpu_update_buffer:: buf: u4, data: const ::sc_gpu_range&
@fnc gpu_append_buffer:: i4, buf: u4, data: const ::sc_gpu_range&
@fnc gpu_update_image:: img: u4, data: const ::sc_gpu_image_data&

# === 资源状态查询（sc_gpu_resource_state） ===
@fnc gpu_query_buffer_state:: i4, buf: u4
@fnc gpu_query_image_state:: i4, img: u4
@fnc gpu_query_shader_state:: i4, shd: u4
@fnc gpu_query_pipeline_state:: i4, pip: u4

# === 帧：pass 与绘制 ===
@fnc gpu_begin_pass:: pass: const ::sc_gpu_pass&
@fnc gpu_apply_viewport:: x: i4, y: i4, width: i4, height: i4, top_left: i4
@fnc gpu_apply_scissor:: x: i4, y: i4, width: i4, height: i4, top_left: i4
@fnc gpu_apply_pipeline:: pip: u4
@fnc gpu_apply_bindings:: bindings: const ::sc_gpu_bindings&
@fnc gpu_apply_uniforms:: stage: i4, slot: i4, data: const &, size: u8
@fnc gpu_draw:: base: i4, count: i4, instances: i4
@fnc gpu_dispatch:: gx: i4, gy: i4, gz: i4
@fnc gpu_end_pass::
@fnc gpu_commit::
