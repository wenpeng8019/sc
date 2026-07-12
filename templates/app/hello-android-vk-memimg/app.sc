# hello-android-vk-memimg —— Android(Vulkan) memimg 无表面渲染验证（回调驱动 · 无 main）
#
# gpu_headless_demo「Mode A」的 Android 等效：不渲染到 NativeActivity 窗口，而是
# 建 MEMORY surface（内存交换链）→ 渲染三角形 → dequeue 取帧 → memimg_map 映射
# CPU → 读中心/角落像素并 logcat 打印校验。用于验证 Vulkan memimg 回读链路
# （离屏 VkImage + host-visible staging 回读，平台中性）在 Android 上端到端可用。
#
# 全部工作在 on_after_startup 完成（一次性自检），随后空转窗口帧。
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

fnc on_main_window_created: win: ::sc_window&
    print "hello-android-vk-memimg: 窗口就绪（本 demo 不渲染窗口，仅 memimg 自检）\n"
    ::fflush(nil)

fnc on_frame:
    return

fnc on_before_cleanup:
    print "hello-android-vk-memimg: cleanup\n"

@fnc app_main: i4
    print "hello-android-vk-memimg: 进入 wsi 事件循环（wsi_app_run）\n"
    return wsi_app_run(on_after_startup, on_main_window_created, on_frame, on_before_cleanup)
