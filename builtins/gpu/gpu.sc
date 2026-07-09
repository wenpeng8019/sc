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
# 无窗口库依赖：native_window/native_display 是平台原生句柄（标准定义
#   在 gpu.h 文件头），窗口库（如 utils/wsi）适配本标准交付句柄。
#
# 句柄：C 侧为纯 u4 typedef（低 16 位池索引 + 高 16 位代数），绑定声明
#   为 u4；0 = 无效句柄。desc 结构体经指针传递（&）。
#
# 说明：各接口的功能、参数与默认值注释见 gpu.h。
#   帧交付接口（device / frame_acquire / frame_end）是 C 侧 gfx 后端
#   消费的内部契约，一般不需要在 sc 侧直接调用，故不做绑定。

inc gpu.h

# 实现：源码动态编译（与 .sc 转义同一条工具链，交叉/远程天然正确）。
# 平台选择全在源文件内：后端宏按平台自推导（src/internal.h），非本平台
# 文件 #ifdef 自守卫空化；.m（ObjC）在 darwin 目标自动 ObjC 模式。
# GLES 形态：目标档 cflags 加 -DSC_GPU_GLES -I builtins/gpu/khr。
add src/gpu.c
add src/null_env.c
add src/gl_env.c
add src/gl_egl.c
add src/gl_ctx.m
add src/metal_env.m
add src/vulkan_env.c

# === 生命周期 ===
@fnc gpu_init:: i4, desc: const ::sc_gpu_desc&
@fnc gpu_shutdown::
@fnc gpu_isvalid:: i4
@fnc gpu_query_backend:: i4
@fnc gpu_resize:: width: i4, height: i4

# === surface（呈现目标；多窗口用 make_current 切换） ===
# kind=MEMORY 时为无表面内存交换链（图像环），渲染后 dequeue 送编码器
@fnc gpu_make_surface:: u4, desc: const ::sc_gpu_surface_desc&
@fnc gpu_destroy_surface:: surf: u4
@fnc gpu_make_current:: surf: u4
@fnc gpu_query_current_surface:: u4
@fnc gpu_surface_resize:: surf: u4, width: i4, height: i4

# === memimg（可导出/可导入内存图像：dma-buf / IOSurface） ===
@fnc gpu_memimg_alloc:: u4, desc: const ::sc_gpu_memimg_desc&
@fnc gpu_memimg_import:: u4, src: const ::sc_gpu_memory_frame&
@fnc gpu_memimg_export:: i4, img: u4, out: ::sc_gpu_memory_frame&, with_fence: i4
@fnc gpu_memimg_map:: &, img: u4, plane: i4, out_stride: u4&
@fnc gpu_memimg_unmap:: img: u4, plane: i4
@fnc gpu_memimg_free:: img: u4

# === MEMORY surface 消费端（编码器侧，可在另一线程） ===
@fnc gpu_memory_dequeue:: i4, surf: u4, out: ::sc_gpu_memory_frame&
@fnc gpu_memory_enqueue:: surf: u4, slot: u4
