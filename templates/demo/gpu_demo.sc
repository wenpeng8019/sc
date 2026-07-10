# gpu_demo —— gpu(运行环境) + gfx(渲染) 两层演示：执行 scc 编译转义后的
# GPU 代码（Metal / GL 三角形）
#
# 链路（本 demo 的意义即验证此闭环）：
#   gpu_shader/gpu_tri.ss ──scc──▶ gpu_tri.shader.h/.c（资源化：字节数组 +
#                                  反射 JSON 内嵌，每目标三连 [reflect, vs, fs]）
#                                        │
#   wsi 窗口 ─▶ gpu_init（环境：device + surface 交换链）
#              └▶ gfx_init（渲染）─▶ gfx_make_shader(直接吃内嵌资源)
#                          └▶ gfx_make_pipeline ─▶ 帧循环 gfx_draw(3)
#
# 用法（macOS，从仓库根目录运行；gpu/gfx 源码动态编译，平台框架链接由编译器自动注入，零 SCC_LDFLAGS）：
#   ./templates/utils/wsi/build.sh                 # 仅 wsi 需预编（libwsi.a 已入库，改源码后重跑）
#   ./compiler/build/scc templates/demo/gpu_shader/gpu_tri.ss -o templates/demo/gpu_shader/out/gpu_tri
#   ./compiler/build/scc templates/demo/gpu_demo.sc
#   ./templates/demo/gpu_demo                      # 着色器已内嵌，任意目录可运行
#   GPU_BACKEND=gl ./templates/demo/gpu_demo       # 切 OpenGL 后端

inc io.sc
inc ../utils/wsi/wsi.sc
inc gpu.sc
inc gfx.sc

# 着色器资源：scc 默认产物 gpu_tri.shader.h/.c（字节数组 + 反射 JSON 内嵌，
# 零运行时文件路径）；条目布局每目标三连：[reflect, vs, fs]（见 .shader.h enum）。
# sc 侧按同布局定义结构认领 C 符号（TU 各自独立，链接期按符号对接）。
add gpu_shader/out/gpu_tri.shader.c

def shader_blob: {
    entry:  const char&
    stage:  const char&
    target: const char&
    ext:    const char&
    data:   const u1&
    size:   u8
}
@fnc shader_gpu_tri_get:: shader_blob&, i: u8    # 绑 sc_shader_gpu_tri_get（越界返 nil）

fnc main: i4
    # ---- wsi 窗口 ----
    if wsi_init() == 0
        print "wsi_init 失败\n"
        return 1
    var win: ::sc_window& = wsi_win_create(640, 480, "sc · gpu demo (三角形)", nil, nil)
    if win == nil
        print "窗口创建失败\n"
        wsi_terminate()
        return 1
    wsi_win_show(win)

    # 帧缓冲像素尺寸由窗口系统给出（Retina/Wayland buffer_scale 等）
    var w: i4 = 0
    var h: i4 = 0
    wsi_win_get_size(win, &w, &h)
    var fbw: i4 = 0
    var fbh: i4 = 0
    wsi_win_get_framebuffer_size(win, &fbw, &fbh)

    # ---- gpu 初始化（默认 surface 的 native handle 来自 wsi） ----
    var gd: ::sc_gpu_desc
    ::memset(&gd, 0, sizeof(::sc_gpu_desc))
    gd.surface.native_window  = wsi_win_get_native_window(win)
    gd.surface.native_display = wsi_win_get_native_display(win)
    gd.surface.width  = fbw
    gd.surface.height = fbh
    # GPU_BACKEND=gl 切 OpenGL；=vulkan 切 Vulkan；=d3d 切 Direct3D 11（默认平台首选）
    var envb: char& = (::getenv("GPU_BACKEND"): char&)
    if envb != nil && envb[0] == (103: char)        # 'g'
        gd.backend = 2                              # SC_GPU_BACKEND_GL
    if envb != nil && envb[0] == (118: char)        # 'v'
        gd.backend = 3                              # SC_GPU_BACKEND_VULKAN
    if envb != nil && envb[0] == (100: char)        # 'd'
        gd.backend = 4                              # SC_GPU_BACKEND_D3D11
    if gpu_init(&gd) == 0
        print "gpu_init 失败\n"
        wsi_terminate()
        return 1
    var bk: i4 = gpu_query_backend()
    print "gpu 后端: ", bk, " (1=Metal 2=GL 3=Vulkan 4=D3D11 5=Null)\n"

    # ---- gfx 渲染层（后端种类跟随 gpu） ----
    var fd: ::sc_gfx_desc
    ::memset(&fd, 0, sizeof(::sc_gfx_desc))
    if gfx_init(&fd) == 0
        print "gfx_init 失败\n"
        gpu_shutdown()
        wsi_terminate()
        return 1

    # ---- 着色器：直接消费内嵌资源表（按后端选目标三连 [reflect, vs, fs]）----
    var base: u8 = 0                             # metal20000
    if bk == 2
        base = 3                                 # glcore410
    if bk == 3
        base = 6                                 # vulkan450（SPIR-V 二进制）
    if bk == 4
        base = 9                                 # d3d50（HLSL 文本）
    var brj: shader_blob& = shader_gpu_tri_get(base)
    var bvs: shader_blob& = shader_gpu_tri_get(base + 1)
    var bfs: shader_blob& = shader_gpu_tri_get(base + 2)
    var rj: const char& = (brj->data: const char&)
    var vs: const u1&   = bvs->data
    var vsn: u8         = bvs->size
    var fs: const u1&   = bfs->data
    var fsn: u8         = bfs->size

    var sd: ::sc_gfx_shader_desc
    ::memset(&sd, 0, sizeof(::sc_gfx_shader_desc))
    sd.vs.code.ptr  = (vs: &)
    sd.vs.code.size = vsn
    sd.vs.entry     = "vs_main"      # Metal 产物入口 = .ss 阶段函数名；GL 恒为 main（后端忽略）
    sd.fs.code.ptr  = (fs: &)
    sd.fs.code.size = fsn
    sd.fs.entry     = "fs_main"
    sd.reflect_json = rj             # 反射清单：绑定信息自动解析
    sd.label        = "tri"
    var shd: u4 = gfx_make_shader(&sd)
    if shd == 0
        print "make_shader 失败\n"
        return 1

    # ---- 管线（无顶点缓冲；格式/深度默认对齐交换链） ----
    var pd: ::sc_gfx_pipeline_desc
    ::memset(&pd, 0, sizeof(::sc_gfx_pipeline_desc))
    pd.shader = shd
    pd.label  = "tri"
    var pip: u4 = gfx_make_pipeline(&pd)
    if pip == 0
        print "make_pipeline 失败\n"
        return 1

    # ---- 帧循环 ----
    var bnd: ::sc_gfx_bindings
    ::memset(&bnd, 0, sizeof(::sc_gfx_bindings))
    print "三角形已上屏，关闭窗口以退出...\n"
    # 帧缓冲像素尺寸由窗口系统给出：Wayland 上 buffer_scale 可能在窗口 show 之后
    # 才由 output enter 事件更新，故每帧重查并按需重建交换链。
    var cur_fbw: i4 = fbw
    var cur_fbh: i4 = fbh
    while wsi_win_get_should_close(win) == 0
        wsi_poll_events()
        var nw: i4 = 0
        var nh: i4 = 0
        wsi_win_get_size(win, &nw, &nh)
        var nfbw: i4 = 0
        var nfbh: i4 = 0
        wsi_win_get_framebuffer_size(win, &nfbw, &nfbh)
        if nfbw != cur_fbw || nfbh != cur_fbh
            w = nw
            h = nh
            cur_fbw = nfbw
            cur_fbh = nfbh
            gpu_resize(nfbw, nfbh)

        var pass: ::sc_gfx_pass           # 全零 = 交换链 pass，默认 clear
        ::memset(&pass, 0, sizeof(::sc_gfx_pass))
        pass.action.colors[0].clear[0] = 0.10
        pass.action.colors[0].clear[1] = 0.10
        pass.action.colors[0].clear[2] = 0.12
        pass.action.colors[0].clear[3] = 1.0
        gfx_begin_pass(&pass)
        gfx_apply_pipeline(pip)
        gfx_apply_bindings(&bnd)
        gfx_draw(0, 3, 1)
        gfx_end_pass()
        gfx_commit()

    # ---- 清理（先 gfx 后 gpu） ----
    gfx_destroy_pipeline(pip)
    gfx_destroy_shader(shd)
    gfx_shutdown()
    gpu_shutdown()
    wsi_win_destroy(win)
    wsi_terminate()
    print "已退出\n"
    return 0
