# hello-android-vk-memimg —— Android(Vulkan) memimg 无表面渲染验证（回调驱动 · 无 main）
#
# gpu_headless_demo「Mode A」的 Android 等效：先在 on_after_startup 建 MEMORY surface
# （内存交换链）→ 渲染三角形 → dequeue 取帧 → memimg_map 映射 CPU → 读中心/角落
# 像素并 logcat 打印校验。用于验证 Vulkan memimg 回读链路（离屏 VkImage +
# host-visible staging 回读，平台中性）在 Android 上端到端可用。
#
# 自检（on_after_startup）跑完并 gpu_shutdown 后，on_main_window_created 于窗口
# 重建 gpu/gfx，on_frame 每帧画同一三角形——屏幕可见（不再是黑屏）。
#
# 用法（M 芯片 Mac + Android NDK/SDK + 支持 Vulkan 的模拟器或真机）：
#   ANDROID_NDK_HOME=... ANDROID_HOME=... ./templates/app/hello-android-vk-memimg/build.sh

inc io.sc
inc wsi.sc
inc gpu.sc
inc gfx.sc

add ../../demo/gpu_shader/out/gpu_tri.shader.c

def shader_blob: {
    entry:  const char&
    stage:  const char&
    target: const char&
    ext:    const char&
    data:   const u1&
    size:   u8
}
@fnc shader_gpu_tri_get:: shader_blob&, i: u8

# ---- 窗口渲染的跨回调共享状态（模块级全局；memimg 自检不用这些） ----
var g_pip:    u4 = 0
var g_shd:    u4 = 0
var g_ready:  i4 = 0          # 窗口 gpu/gfx/管线就绪标志（就绪前 on_frame 空转）
var g_frames: i4 = 0

# BGRA 像素读取并打印（BGRA8 序：偏移 0=B 1=G 2=R 3=A）
fnc dump_px: pix: const char&, stride: u4, x: i4, y: i4, tag: const char&
    var si: i8 = ((y: i8) * (stride: i8)) + ((x: i8) * 4)
    var b: i4 = (pix[si]: i4)       & 255
    var g: i4 = (pix[si + 1]: i4)   & 255
    var r: i4 = (pix[si + 2]: i4)   & 255
    var a: i4 = (pix[si + 3]: i4)   & 255
    print "  ", tag, " (", x, ",", y, ") BGRA = ", b, " ", g, " ", r, " ", a, "\n"
    ::fflush(nil)

fnc on_after_startup:
    print "hello-android-vk-memimg: 子系统就绪，开始 Vulkan memimg 自检\n"
    ::fflush(nil)

    # ---- gpu 初始化（Vulkan，无 native_window，不建默认 surface） ----
    var gd: ::sc_gpu_desc
    ::memset(&gd, 0, sizeof(::sc_gpu_desc))
    gd.backend = 3                            # SC_GPU_BACKEND_VULKAN
    if gpu_init(&gd) == 0
        print "memimg 自检: gpu_init 失败\n"
        ::fflush(nil)
        return
    var bk: i4 = gpu_query_backend()
    print "memimg 自检: gpu 后端 ", bk, " (3=Vulkan)\n"
    ::fflush(nil)

    # ---- MEMORY surface（内存交换链，256x256） ----
    var sdc: ::sc_gpu_surface_desc
    ::memset(&sdc, 0, sizeof(::sc_gpu_surface_desc))
    sdc.kind   = 1                            # SC_GPU_SURFACE_MEMORY
    sdc.width  = 256
    sdc.height = 256
    var surf: u4 = gpu_make_surface(&sdc)
    if surf == 0
        print "memimg 自检: MEMORY surface 创建失败\n"
        ::fflush(nil)
        gpu_shutdown()
        return
    gpu_make_current(surf)

    var fdd: ::sc_gfx_desc
    ::memset(&fdd, 0, sizeof(::sc_gfx_desc))
    if gfx_init(&fdd) == 0
        print "memimg 自检: gfx_init 失败\n"
        ::fflush(nil)
        gpu_shutdown()
        return

    # ---- 着色器：vulkan450 三连 [reflect, vs, fs]，base=9（SPIR-V 二进制） ----
    var base: u8 = 9
    var brj: shader_blob& = shader_gpu_tri_get(base)
    var bvs: shader_blob& = shader_gpu_tri_get(base + 1)
    var bfs: shader_blob& = shader_gpu_tri_get(base + 2)
    var sd: ::sc_gfx_shader_desc
    ::memset(&sd, 0, sizeof(::sc_gfx_shader_desc))
    sd.vs.code.ptr  = (bvs->data: &)
    sd.vs.code.size = bvs->size
    sd.vs.entry     = "vs_main"
    sd.fs.code.ptr  = (bfs->data: &)
    sd.fs.code.size = bfs->size
    sd.fs.entry     = "fs_main"
    sd.reflect_json = (brj->data: const char&)
    var shd: u4 = gfx_make_shader(&sd)
    if shd == 0
        print "memimg 自检: make_shader 失败\n"
        ::fflush(nil)
        return

    var pd: ::sc_gfx_pipeline_desc
    ::memset(&pd, 0, sizeof(::sc_gfx_pipeline_desc))
    pd.shader = shd
    var pip: u4 = gfx_make_pipeline(&pd)
    if pip == 0
        print "memimg 自检: make_pipeline 失败\n"
        ::fflush(nil)
        return

    # ---- 渲染一帧到内存交换链（深蓝底：clear BGRA≈76/25/12） ----
    var bnd: ::sc_gfx_bindings
    ::memset(&bnd, 0, sizeof(::sc_gfx_bindings))
    var pass: ::sc_gfx_pass
    ::memset(&pass, 0, sizeof(::sc_gfx_pass))
    pass.action.colors[0].clear[0] = 0.05
    pass.action.colors[0].clear[1] = 0.10
    pass.action.colors[0].clear[2] = 0.30
    pass.action.colors[0].clear[3] = 1.0
    gfx_begin_pass(&pass)
    gfx_apply_pipeline(pip)
    gfx_apply_bindings(&bnd)
    gfx_draw(0, 3, 1)
    gfx_end_pass()
    gfx_commit()
    gfx_finish()

    # ---- 消费端：dequeue → map → 读像素校验 ----
    var frm: ::sc_gpu_memory_frame
    ::memset(&frm, 0, sizeof(::sc_gpu_memory_frame))
    if gpu_memory_dequeue(surf, &frm) == 0
        print "memimg 自检: dequeue 失败\n"
        ::fflush(nil)
        return
    var slot: u4 = frm.slot
    var stride: u4 = 0
    var pix: char& = (gpu_memimg_map(frm.img, 0, &stride): char&)
    if pix == nil
        print "memimg 自检: memimg_map 失败\n"
        ::fflush(nil)
        return
    print "memimg 自检: dequeue slot=", slot, " stride=", stride, "\n"
    print "memimg 自检: 像素采样（中心应为三角形色、角落应为深蓝底 BGRA≈76/25/12）\n"
    ::fflush(nil)
    dump_px(pix, stride, 128, 160, "center")
    dump_px(pix, stride, 5,   5,   "corner-tl")
    dump_px(pix, stride, 250, 250, "corner-br")
    gpu_memimg_unmap(frm.img, 0)
    gpu_memory_enqueue(surf, slot)

    print "memimg 自检: 完成（Vulkan memimg 回读链路端到端 OK）\n"
    ::fflush(nil)

    gfx_destroy_pipeline(pip)
    gfx_destroy_shader(shd)
    gfx_shutdown()
    gpu_shutdown()

# 主窗口创建：memimg 自检已在 on_after_startup 完成并 gpu_shutdown，此处于
# NativeActivity 窗口重建 gpu/gfx/管线（Vulkan，native_window = wsi 交付的
# ANativeWindow*），供 on_frame 每帧渲染——屏幕可见三角形（不再是黑屏）。
fnc on_main_window_created: win: ::sc_window&
    var fbw: i4 = 0
    var fbh: i4 = 0
    wsi_win_get_framebuffer_size(win, &fbw, &fbh)
    print "hello-android-vk-memimg: 窗口就绪 · 帧缓冲 ", fbw, " x ", fbh, "（自检完毕，转窗口三角形）\n"
    ::fflush(nil)

    # ---- gpu 初始化（Vulkan；native_window = ANativeWindow*，native_display = NULL） ----
    var gd: ::sc_gpu_desc
    ::memset(&gd, 0, sizeof(::sc_gpu_desc))
    gd.backend = 3                            # SC_GPU_BACKEND_VULKAN
    gd.surface.native_window  = wsi_win_get_native_window(win)
    gd.surface.native_display = wsi_win_get_native_display(win)
    gd.surface.width  = fbw
    gd.surface.height = fbh
    if gpu_init(&gd) == 0
        print "hello-android-vk-memimg: 窗口 gpu_init 失败\n"
        ::fflush(nil)
        return

    var fd: ::sc_gfx_desc
    ::memset(&fd, 0, sizeof(::sc_gfx_desc))
    if gfx_init(&fd) == 0
        print "hello-android-vk-memimg: 窗口 gfx_init 失败\n"
        ::fflush(nil)
        gpu_shutdown()
        return

    # ---- 着色器：vulkan450 三连 [reflect, vs, fs]，base=9（SPIR-V） ----
    var base: u8 = 9
    var brj: shader_blob& = shader_gpu_tri_get(base)
    var bvs: shader_blob& = shader_gpu_tri_get(base + 1)
    var bfs: shader_blob& = shader_gpu_tri_get(base + 2)
    var sd: ::sc_gfx_shader_desc
    ::memset(&sd, 0, sizeof(::sc_gfx_shader_desc))
    sd.vs.code.ptr  = (bvs->data: &)
    sd.vs.code.size = bvs->size
    sd.vs.entry     = "vs_main"
    sd.fs.code.ptr  = (bfs->data: &)
    sd.fs.code.size = bfs->size
    sd.fs.entry     = "fs_main"
    sd.reflect_json = (brj->data: const char&)
    var shd: u4 = gfx_make_shader(&sd)
    if shd == 0
        print "hello-android-vk-memimg: 窗口 make_shader 失败\n"
        ::fflush(nil)
        return
    g_shd = shd

    var pd: ::sc_gfx_pipeline_desc
    ::memset(&pd, 0, sizeof(::sc_gfx_pipeline_desc))
    pd.shader = shd
    var pip: u4 = gfx_make_pipeline(&pd)
    if pip == 0
        print "hello-android-vk-memimg: 窗口 make_pipeline 失败\n"
        ::fflush(nil)
        return
    g_pip   = pip
    g_ready = 1
    print "hello-android-vk-memimg: 窗口三角形管线就绪\n"
    ::fflush(nil)

# 每帧回调（AChoreographer vsync 驱动）：清屏（深蓝底）+ 画三角形。
fnc on_frame:
    if g_ready == 0
        return
    g_frames = g_frames + 1

    var pass: ::sc_gfx_pass           # 全零 = 交换链 pass
    ::memset(&pass, 0, sizeof(::sc_gfx_pass))
    pass.action.colors[0].clear[0] = 0.05
    pass.action.colors[0].clear[1] = 0.10
    pass.action.colors[0].clear[2] = 0.30
    pass.action.colors[0].clear[3] = 1.0
    gfx_begin_pass(&pass)
    gfx_apply_pipeline(g_pip)
    var bnd: ::sc_gfx_bindings
    ::memset(&bnd, 0, sizeof(::sc_gfx_bindings))
    gfx_apply_bindings(&bnd)
    gfx_draw(0, 3, 1)
    gfx_end_pass()
    gfx_commit()

    if g_frames % 60 == 0
        print "hello-android-vk-memimg: 窗口帧 ", g_frames, "\n"
        ::fflush(nil)

fnc on_before_cleanup:
    print "hello-android-vk-memimg: cleanup\n"
    if g_ready != 0
        gfx_destroy_pipeline(g_pip)
        gfx_destroy_shader(g_shd)
        gfx_shutdown()
        gpu_shutdown()

@fnc app_main: i4
    print "hello-android-vk-memimg: 进入 wsi 事件循环（wsi_app_run）\n"
    return wsi_app_run(on_after_startup, on_main_window_created, on_frame, on_before_cleanup)
