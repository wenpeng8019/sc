# gpu —— GPU 运行环境（env 层）
#
# 定位（类比 sokol_app 去掉窗口维护的部分）：
#   为渲染层（gfx 模块）搭好"GPU 运行环境"：平台后端选择、
#   surface（呈现目标/交换链，含 MSAA/深度附属目标/vsync/resize）、
#   每帧渲染目标交付。真正的绘制渲染在 utils/gfx（gfx.sc）。
#
# 多后端机制（同 wsi/glfw）：一个 libgpu.a 可同时编入多个后端，运行时
#   按平台默认或 sc_gpu_desc.backend 显式选择：
#     macOS → Metal（默认）/ GL     linux/win → GL（默认）
#
# 依赖：wsi（native_window / native_display 来自 sc_wsi_win_get_native_*）。
#
# 句柄：C 侧为纯 u4 typedef（低 16 位池索引 + 高 16 位代数），绑定声明
#   为 u4；0 = 无效句柄。desc 结构体经指针传递（&）。
#
# 说明：各接口的功能、参数与默认值注释见 gpu.h。
#   帧交付接口（device / frame_acquire / frame_end）是 C 侧 gfx 后端
#   消费的内部契约，一般不需要在 sc 侧直接调用，故不做绑定。

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
