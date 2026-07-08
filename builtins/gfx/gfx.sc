# gfx —— 渲染层（执行 scc 编译转义后的 GPU 代码）
#
# 定位（对应 sokol_gfx 的渲染部分）：在 gpu 模块（GPU 运行环境）之上
#   做**驱动 GPU 硬件、执行 scc 编译 .ss 产物（MSL/GLSL + 反射清单）**
#   的薄硬件访问层。资源模型：buffer/image/sampler/shader/pipeline
#   五类句柄 + desc 一次性描述。
#
# 与 gpu 的关系（须先 inc gpu.sc 并 gpu_init 成功）：
#   gfx_init 跟随 gpu 的后端种类；交换链 pass 渲染到 gpu 当前 surface；
#   gfx_commit 呈现并收尾帧。
#
# 句柄：C 侧为纯 u4 typedef；0 = 无效。大结构体经指针传递（&）。
# 说明：各接口的功能、参数与默认值注释见 gfx.h。

inc gfx.h

# 实现：源码动态编译（同 gpu.sc：平台自守卫 + .m 自动 ObjC）
add src/gfx.c
add src/gfx_reflect.c
add src/null_gfx.c
add src/gl_gfx.c
add src/metal_gfx.m

# === 生命周期 ===
@fnc gfx_init:: i4, desc: const ::sc_gfx_desc&
@fnc gfx_shutdown::
@fnc gfx_isvalid:: i4
@fnc gfx_finish::

# === 资源创建 / 销毁（句柄 = u4，0 无效） ===
@fnc gfx_make_buffer:: u4, desc: const ::sc_gfx_buffer_desc&
@fnc gfx_make_image:: u4, desc: const ::sc_gfx_image_desc&
@fnc gfx_make_sampler:: u4, desc: const ::sc_gfx_sampler_desc&
@fnc gfx_make_shader:: u4, desc: const ::sc_gfx_shader_desc&
@fnc gfx_make_pipeline:: u4, desc: const ::sc_gfx_pipeline_desc&
@fnc gfx_destroy_buffer:: buf: u4
@fnc gfx_destroy_image:: img: u4
@fnc gfx_destroy_sampler:: smp: u4
@fnc gfx_destroy_shader:: shd: u4
@fnc gfx_destroy_pipeline:: pip: u4

# === 资源更新 ===
@fnc gfx_update_buffer:: buf: u4, data: const ::sc_gfx_range&
@fnc gfx_append_buffer:: i4, buf: u4, data: const ::sc_gfx_range&
@fnc gfx_update_image:: img: u4, data: const ::sc_gfx_image_data&

# === 资源状态查询（sc_gfx_resource_state） ===
@fnc gfx_query_buffer_state:: i4, buf: u4
@fnc gfx_query_image_state:: i4, img: u4
@fnc gfx_query_shader_state:: i4, shd: u4
@fnc gfx_query_pipeline_state:: i4, pip: u4

# === 帧：pass 与绘制 ===
@fnc gfx_begin_pass:: pass: const ::sc_gfx_pass&
@fnc gfx_apply_viewport:: x: i4, y: i4, width: i4, height: i4, top_left: i4
@fnc gfx_apply_scissor:: x: i4, y: i4, width: i4, height: i4, top_left: i4
@fnc gfx_apply_pipeline:: pip: u4
@fnc gfx_apply_bindings:: bindings: const ::sc_gfx_bindings&
@fnc gfx_apply_uniforms:: stage: i4, slot: i4, data: const &, size: u8
@fnc gfx_draw:: base: i4, count: i4, instances: i4
@fnc gfx_dispatch:: gx: i4, gy: i4, gz: i4
@fnc gfx_end_pass::
@fnc gfx_commit::
