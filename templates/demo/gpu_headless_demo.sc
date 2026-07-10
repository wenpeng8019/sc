# gpu_headless_demo —— 无表面渲染演示：不开窗口，渲染到可导出内存图像
#
# 两种驱动模式（视频流编码 / 嵌入式按需绘制场景）：
#   Mode A「gpu 驱动环」：MEMORY surface（内存交换链）——渲染循环与窗口
#     场景完全同构，commit 后 dequeue 取帧（dma-buf/IOSurface）送编码器
#   Mode B「gfx 驱动/按需」：memimg 绑定 gfx image 作离屏渲染目标，
#     应用按需绘制，export 导出（环由应用自管）
#
# 验证方式：渲染三角形 → map 映射 CPU → 写 PPM 文件肉眼校验。
#   真实链路中 map 换成把 frame.fd / frame.native 交给 v4l2 / VideoToolbox。
#
# 用法（macOS，从仓库根目录运行；默认 Metal，GPU_BACKEND=gl 切 NSGL 无屏；
#      平台框架链接由编译器自动注入，零 SCC_LDFLAGS）：
#   ./compiler/build/scc templates/demo/gpu_headless_demo.sc
#   产出 sc_headless_a.ppm（Mode A，深蓝底）/ sc_headless_b.ppm（Mode B，深绿底），写当前目录

inc io.sc
inc gpu.sc
inc gfx.sc

# 着色器资源：scc 默认产物 gpu_tri.shader.c（与 gpu_demo 共享；每目标三连
# [reflect, vs, fs]：metal20000=0..2、glcore410=3..5，见 .shader.h enum）。
add gpu_shader/out/gpu_tri.shader.c

def shader_blob: {
    entry:  const char&
    stage:  const char&
    target: const char&
    ext:    const char&
    data:   const u1&
    size:   u8
}
@fnc shader_gpu_tri_get:: shader_blob&, i: u8    # 绑 sc_shader_gpu_tri_get

# BGRA 像素块 → PPM（P6，固定 256x256）
@fnc save_ppm: i4, path: const char&, pix: const char&, stride: u4
    var f: & = (::fopen(path, "wb"): &)
    if f == nil
        print "无法写 ", path, "\n"
        return 0
    ::fwrite("P6\n256 256\n255\n", 1, 15, f)
    var row: char& = (::malloc(768): char&)     # 256*3
    var y: i4 = 0
    while y < 256
        var x: i4 = 0
        while x < 256
            var si: i8 = ((y: i8) * (stride: i8)) + ((x: i8) * 4)
            row[((x * 3): i8)]     = pix[si + 2]    # R（BGRA 序）
            row[((x * 3 + 1): i8)] = pix[si + 1]    # G
            row[((x * 3 + 2): i8)] = pix[si]        # B
            x = x + 1
        ::fwrite(row, 1, 768, f)
        y = y + 1
    ::free(row)
    ::fclose(f)
    print "已写 ", path, "\n"
    return 1

fnc main: i4
    # ---- gpu headless 初始化（无 native_window，不建默认 surface） ----
    var gd: ::sc_gpu_desc
    ::memset(&gd, 0, sizeof(::sc_gpu_desc))
    # GPU_BACKEND=gl 切 OpenGL 后端；=vulkan 切 Vulkan（默认平台首选：mac=Metal）
    var envb: char& = (::getenv("GPU_BACKEND"): char&)
    if envb != nil && envb[0] == (103: char)        # 'g'
        gd.backend = 2                              # SC_GPU_BACKEND_GL
    if envb != nil && envb[0] == (118: char)        # 'v'
        gd.backend = 3                              # SC_GPU_BACKEND_VULKAN
    if gpu_init(&gd) == 0
        print "gpu_init 失败\n"
        return 1
    var bk: i4 = gpu_query_backend()
    print "gpu 后端: ", bk, " (1=Metal 2=GL 3=Vulkan 4=Null，headless)\n"

    # ---- Mode A：MEMORY surface（内存交换链，gpu 驱动环） ----
    var sdc: ::sc_gpu_surface_desc
    ::memset(&sdc, 0, sizeof(::sc_gpu_surface_desc))
    sdc.kind   = 1                    # SC_GPU_SURFACE_MEMORY
    sdc.width  = 256
    sdc.height = 256
    var surf: u4 = gpu_make_surface(&sdc)
    if surf == 0
        print "MEMORY surface 创建失败\n"
        gpu_shutdown()
        return 1
    gpu_make_current(surf)

    var fdd: ::sc_gfx_desc
    ::memset(&fdd, 0, sizeof(::sc_gfx_desc))
    if gfx_init(&fdd) == 0
        print "gfx_init 失败\n"
        gpu_shutdown()
        return 1

    # ---- 着色器（内嵌资源表，按后端选目标三连） ----
    var base: u8 = 0                             # metal20000
    if bk == 2
        base = 3                                 # glcore410
    if bk == 3
        base = 6                                 # vulkan450（SPIR-V 二进制）
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
        print "make_shader 失败\n"
        return 1

    # 交换链管线（默认对齐当前 MEMORY surface：BGRA8 + DEPTH_STENCIL）
    var pd: ::sc_gfx_pipeline_desc
    ::memset(&pd, 0, sizeof(::sc_gfx_pipeline_desc))
    pd.shader = shd
    var pip: u4 = gfx_make_pipeline(&pd)
    if pip == 0
        print "make_pipeline 失败\n"
        return 1

    var bnd: ::sc_gfx_bindings
    ::memset(&bnd, 0, sizeof(::sc_gfx_bindings))

    # 渲染一帧到内存交换链
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
    gfx_finish()                       # mac 无 fence（sync_fd=-1）→ CPU 同步

    # 消费端：dequeue 取帧（真实链路把 frm.fd/native 交编码器）
    var frm: ::sc_gpu_memory_frame
    ::memset(&frm, 0, sizeof(::sc_gpu_memory_frame))
    if gpu_memory_dequeue(surf, &frm) == 0
        print "dequeue 失败\n"
        return 1
    var slot: u4 = frm.slot
    print "Mode A dequeue: slot=", slot, "\n"
    var stride: u4 = 0
    var pix: char& = (gpu_memimg_map(frm.img, 0, &stride): char&)
    if pix == nil
        print "memimg_map 失败\n"
        return 1
    save_ppm("sc_headless_a.ppm", pix, stride)
    gpu_memimg_unmap(frm.img, 0)
    gpu_memory_enqueue(surf, slot)

    # ---- Mode B：memimg 绑定 gfx image，按需离屏渲染（gfx 驱动） ----
    var mid: ::sc_gpu_memimg_desc
    ::memset(&mid, 0, sizeof(::sc_gpu_memimg_desc))
    mid.width  = 256
    mid.height = 256
    mid.render_target = 1
    var mimg: u4 = gpu_memimg_alloc(&mid)
    if mimg == 0
        print "memimg_alloc 失败\n"
        return 1
    var idc: ::sc_gfx_image_desc
    ::memset(&idc, 0, sizeof(::sc_gfx_image_desc))
    idc.width  = 256
    idc.height = 256
    idc.format = 10                    # SC_GPU_PIXELFORMAT_BGRA8
    idc.render_target = 1
    idc.memimg = mimg
    var tgt: u4 = gfx_make_image(&idc)
    if tgt == 0
        print "memimg 绑定 gfx image 失败\n"
        return 1

    # 离屏管线：无深度附件、BGRA8
    var pd2: ::sc_gfx_pipeline_desc
    ::memset(&pd2, 0, sizeof(::sc_gfx_pipeline_desc))
    pd2.shader = shd
    pd2.depth.format = 1               # SC_GPU_PIXELFORMAT_NONE
    pd2.colors[0].format = 10          # BGRA8
    var pip2: u4 = gfx_make_pipeline(&pd2)

    var pass2: ::sc_gfx_pass
    ::memset(&pass2, 0, sizeof(::sc_gfx_pass))
    pass2.colors[0].image = tgt
    pass2.action.colors[0].clear[0] = 0.04
    pass2.action.colors[0].clear[1] = 0.22
    pass2.action.colors[0].clear[2] = 0.07
    pass2.action.colors[0].clear[3] = 1.0
    gfx_begin_pass(&pass2)
    gfx_apply_pipeline(pip2)
    gfx_apply_bindings(&bnd)
    gfx_draw(0, 3, 1)
    gfx_end_pass()
    gfx_commit()
    gfx_finish()

    var frm2: ::sc_gpu_memory_frame
    ::memset(&frm2, 0, sizeof(::sc_gpu_memory_frame))
    if gpu_memimg_export(mimg, &frm2, 0) == 0
        print "memimg_export 失败\n"
        return 1
    var pix2: char& = (gpu_memimg_map(mimg, 0, &stride): char&)
    if pix2 == nil
        print "memimg_map(B) 失败\n"
        return 1
    save_ppm("sc_headless_b.ppm", pix2, stride)
    gpu_memimg_unmap(mimg, 0)

    # ---- 清理（先 gfx 后 gpu） ----
    gfx_destroy_image(tgt)
    gfx_destroy_pipeline(pip2)
    gfx_destroy_pipeline(pip)
    gfx_destroy_shader(shd)
    gfx_shutdown()
    gpu_memimg_free(mimg)
    gpu_shutdown()
    print "headless 渲染完成\n"
    return 0
