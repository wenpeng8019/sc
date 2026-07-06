# gpu_demo —— gpu 模块演示：执行 scc 编译转义后的 GPU 代码（Metal / GL 三角形）
#
# 链路（本 demo 的意义即验证此闭环）：
#   gpu_shader/gpu_tri.sg ──scc──▶ vs_main.metal20000.metal / vs_main.glcore410.vert
#                                  + gpu_tri.<tar>.reflect.json
#                                        │
#   wsi 窗口（native_window）──▶ gpu_init ─▶ make_shader(直接吃 scc 产物)
#                                        └▶ make_pipeline ─▶ 帧循环 draw(3)
#
# 用法（macOS，从仓库根目录运行）：
#   ./templates/utils/wsi/build.sh                 # 先编出 libwsi.a
#   ./templates/utils/gpu/build.sh                 # 再编出 libgpu.a（Metal + GL 后端）
#   ./compiler/build/scc templates/demo/gpu_shader/gpu_tri.sg -o templates/demo/gpu_shader/out/gpu_tri
#   SCC_LDFLAGS="-framework Cocoa -framework IOKit -framework CoreFoundation \
#                -framework Metal -framework QuartzCore -framework OpenGL" \
#       ./compiler/build/scc templates/demo/gpu_demo.sc
#   ./templates/demo/gpu_demo                      # 需从仓库根运行（着色器按相对路径加载）
#   GPU_BACKEND=gl ./templates/demo/gpu_demo       # 切 OpenGL 后端

inc io.sc
inc ../utils/wsi/wsi.sc
inc ../utils/gpu/gpu.sc

# 整文件读入 malloc 缓冲（尾部补 NUL，文本产物可当 C 串用）。失败返回 nil。
@fnc load_file: &, path: const char&, out_size: u8&
    var f: & = (::fopen(path, "rb"): &)
    if f == nil
        print "无法打开 ", path, "\n"
        return nil
    ::fseek(f, 0, 2)                        # SEEK_END
    var n: i8 = (::ftell(f): i8)
    ::fseek(f, 0, 0)                        # SEEK_SET
    var buf: char& = (::malloc(((n + 1): u8)): char&)
    if (::fread(buf, 1, (n: u8), f): i8) != n
        print "读取失败 ", path, "\n"
        ::fclose(f)
        ::free(buf)
        return nil
    buf[n] = (0: char)
    ::fclose(f)
    out_size[0] = (n: u8)
    return buf

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

    # 帧缓冲像素尺寸 = 逻辑尺寸 × 内容缩放（Retina）
    var w: i4 = 0
    var h: i4 = 0
    wsi_win_get_size(win, &w, &h)
    var sx: f4 = 1.0
    var sy: f4 = 1.0
    wsi_win_get_content_scale(win, &sx, &sy)
    var fbw: i4 = (((w: f4) * sx): i4)
    var fbh: i4 = (((h: f4) * sy): i4)

    # ---- gpu 初始化（默认 surface 的 native handle 来自 wsi） ----
    var gd: ::sc_gpu_desc
    ::memset(&gd, 0, sizeof(::sc_gpu_desc))
    gd.surface.native_window  = wsi_win_get_native_window(win)
    gd.surface.native_display = wsi_win_get_native_display(win)
    gd.surface.width  = fbw
    gd.surface.height = fbh
    # GPU_BACKEND=gl 切 OpenGL 后端（默认平台首选：mac=Metal）
    var envb: char& = (::getenv("GPU_BACKEND"): char&)
    if envb != nil && envb[0] == (103: char)        # 'g'
        gd.backend = 2                              # SC_GPU_BACKEND_GL
    if gpu_init(&gd) == 0
        print "gpu_init 失败\n"
        wsi_terminate()
        return 1
    var bk: i4 = gpu_query_backend()
    print "gpu 后端: ", bk, " (1=Metal 2=GL 3=Null)\n"

    # ---- 着色器：直接消费 scc 编译 .sg 的产物（按后端选目标产物） ----
    var vsn: u8 = 0
    var fsn: u8 = 0
    var rjn: u8 = 0
    var vs: char& = nil
    var fs: char& = nil
    var rj: char& = nil
    if bk == 2
        vs = (load_file("templates/demo/gpu_shader/out/vs_main.glcore410.vert", &vsn): char&)
        fs = (load_file("templates/demo/gpu_shader/out/fs_main.glcore410.frag", &fsn): char&)
        rj = (load_file("templates/demo/gpu_shader/out/gpu_tri.glcore410.reflect.json", &rjn): char&)
    else
        vs = (load_file("templates/demo/gpu_shader/out/vs_main.metal20000.metal", &vsn): char&)
        fs = (load_file("templates/demo/gpu_shader/out/fs_main.metal20000.metal", &fsn): char&)
        rj = (load_file("templates/demo/gpu_shader/out/gpu_tri.metal20000.reflect.json", &rjn): char&)
    if vs == nil || fs == nil || rj == nil
        print "着色器产物缺失：先运行 scc 编译 gpu_shader/gpu_tri.sg（见文件头用法）\n"
        gpu_shutdown()
        wsi_terminate()
        return 1

    var sd: ::sc_gpu_shader_desc
    ::memset(&sd, 0, sizeof(::sc_gpu_shader_desc))
    sd.vs.code.ptr  = (vs: &)
    sd.vs.code.size = vsn
    sd.vs.entry     = "vs_main"      # Metal 产物入口 = .sg 阶段函数名；GL 恒为 main（后端忽略）
    sd.fs.code.ptr  = (fs: &)
    sd.fs.code.size = fsn
    sd.fs.entry     = "fs_main"
    sd.reflect_json = rj             # 反射清单：绑定信息自动解析
    sd.label        = "tri"
    var shd: u4 = gpu_make_shader(&sd)
    if shd == 0
        print "make_shader 失败\n"
        return 1

    # ---- 管线（无顶点缓冲；格式/深度默认对齐交换链） ----
    var pd: ::sc_gpu_pipeline_desc
    ::memset(&pd, 0, sizeof(::sc_gpu_pipeline_desc))
    pd.shader = shd
    pd.label  = "tri"
    var pip: u4 = gpu_make_pipeline(&pd)
    if pip == 0
        print "make_pipeline 失败\n"
        return 1

    # ---- 帧循环 ----
    var bnd: ::sc_gpu_bindings
    ::memset(&bnd, 0, sizeof(::sc_gpu_bindings))
    print "三角形已上屏，关闭窗口以退出...\n"
    while wsi_win_get_should_close(win) == 0
        wsi_poll_events()
        var nw: i4 = 0
        var nh: i4 = 0
        wsi_win_get_size(win, &nw, &nh)
        if nw != w || nh != h
            w = nw
            h = nh
            gpu_resize((((w: f4) * sx): i4), (((h: f4) * sy): i4))

        var pass: ::sc_gpu_pass           # 全零 = 交换链 pass，默认 clear
        ::memset(&pass, 0, sizeof(::sc_gpu_pass))
        pass.action.colors[0].clear[0] = 0.10
        pass.action.colors[0].clear[1] = 0.10
        pass.action.colors[0].clear[2] = 0.12
        pass.action.colors[0].clear[3] = 1.0
        gpu_begin_pass(&pass)
        gpu_apply_pipeline(pip)
        gpu_apply_bindings(&bnd)
        gpu_draw(0, 3, 1)
        gpu_end_pass()
        gpu_commit()

    # ---- 清理 ----
    gpu_destroy_pipeline(pip)
    gpu_destroy_shader(shd)
    gpu_shutdown()
    ::free(vs)
    ::free(fs)
    ::free(rj)
    wsi_win_destroy(win)
    wsi_terminate()
    print "已退出\n"
    return 0
