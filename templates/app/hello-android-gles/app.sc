# hello-android-gles —— Android(GLES) 三角形：桌面 gpu_demo 的移动等效（回调驱动 · 无 main）
#
# 与 hello-ios-gfx 同一闭环，只把驱动入口从 iOS 的 main/UIKit 换成 Android 的
# app_main/NativeActivity（详见 hello-android/app.sc 的入口模型说明）：
#   gpu_shader/gpu_tri.ss ──scc──▶ gpu_tri.shader.h/.c（资源化，每目标三连
#                                  [reflect, vs, fs]；Android 用 gles300，base=6）
#   on_main_window_created ─▶ gpu_init（GLES + EGL 窗口交换链，
#                             native_window = wsi 交付的 ANativeWindow*，
#                             native_display = NULL = EGL_DEFAULT_DISPLAY）
#                          └▶ gfx_init ─▶ gfx_make_shader ─▶ gfx_make_pipeline
#   on_frame（AChoreographer vsync 每帧）─▶ gfx_begin_pass/draw(3)/commit
#
# 与 hello-ios-gfx 对照：四回调与渲染调用完全一致；仅入口 iOS=main（UIApplicationMain）
# / Android=@fnc app_main（ANativeActivity_onCreate 经 wsi 渲染线程调用），
# 且着色器取 gles300 三连（base=6）而非 metal20000（base=0）。
#
# 用法（M 芯片 Mac + Android NDK/SDK + 模拟器或真机）：
#   ANDROID_NDK_HOME=... ANDROID_HOME=... ./templates/app/hello-android-gles/build.sh

inc io.sc
inc wsi.sc
inc gpu.sc
inc gfx.sc

# 着色器资源（与桌面 demo 共用同一产物；Android 只取 gles300 三连 base=6）。
add ../../demo/gpu_shader/out/gpu_tri.shader.c

def shader_blob: {
    entry:  const char&
    stage:  const char&
    target: const char&
    ext:    const char&
    data:   const u1&
    size:   u8
}
@fnc shader_gpu_tri_get:: shader_blob&, i: u8    # 绑 sc_shader_gpu_tri_get

# ---- 跨回调共享状态（模块级全局） ----
var g_pip:    u4  = 0
var g_shd:    u4  = 0
var g_ready:  i4  = 0          # gpu/gfx/管线就绪标志（就绪前 on_frame 空转）
var g_fbw:    i4  = 0          # 当前交换链像素尺寸
var g_fbh:    i4  = 0
var g_frames: i4  = 0

fnc on_after_startup:
    print "hello-android-gles: 子系统就绪\n"
    ::fflush(nil)

# 主窗口创建：wsi 已自建全屏窗口，交付 ANativeWindow* 句柄；在此初始化 gpu/gfx/管线。
fnc on_main_window_created: win: ::sc_window&
    var fbw: i4 = 0
    var fbh: i4 = 0
    wsi_win_get_framebuffer_size(win, &fbw, &fbh)
    print "hello-android-gles: 主窗口就绪 · 帧缓冲 ", fbw, " x ", fbh, "\n"
    ::fflush(nil)

    # ---- gpu 初始化（GLES；native_window = ANativeWindow*，native_display = NULL） ----
    var gd: ::sc_gpu_desc
    ::memset(&gd, 0, sizeof(::sc_gpu_desc))
    gd.surface.native_window  = wsi_win_get_native_window(win)
    gd.surface.native_display = wsi_win_get_native_display(win)
    gd.surface.width  = fbw
    gd.surface.height = fbh
    if gpu_init(&gd) == 0
        print "hello-android-gles: gpu_init 失败\n"
        ::fflush(nil)
        return
    var bk: i4 = gpu_query_backend()
    print "hello-android-gles: gpu 后端 ", bk, " (2=GL)\n"

    # ---- gfx 渲染层 ----
    var fd: ::sc_gfx_desc
    ::memset(&fd, 0, sizeof(::sc_gfx_desc))
    if gfx_init(&fd) == 0
        print "hello-android-gles: gfx_init 失败\n"
        ::fflush(nil)
        gpu_shutdown()
        return

    # ---- 着色器：gles300 三连 [reflect, vs, fs]，base=6 ----
    var base: u8 = 6
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
    sd.vs.entry     = "vs_main"
    sd.fs.code.ptr  = (fs: &)
    sd.fs.code.size = fsn
    sd.fs.entry     = "fs_main"
    sd.reflect_json = rj
    sd.label        = "tri"
    var shd: u4 = gfx_make_shader(&sd)
    if shd == 0
        print "hello-android-gles: make_shader 失败\n"
        ::fflush(nil)
        return
    g_shd = shd

    var pd: ::sc_gfx_pipeline_desc
    ::memset(&pd, 0, sizeof(::sc_gfx_pipeline_desc))
    pd.shader = shd
    pd.label  = "tri"
    var pip: u4 = gfx_make_pipeline(&pd)
    if pip == 0
        print "hello-android-gles: make_pipeline 失败\n"
        ::fflush(nil)
        return
    g_pip = pip

    g_fbw   = fbw
    g_fbh   = fbh
    g_ready = 1
    print "hello-android-gles: 三角形管线就绪\n"
    ::fflush(nil)

# 每帧回调（AChoreographer vsync 驱动）：清屏 + 画三角形。
fnc on_frame:
    if g_ready == 0
        return
    g_frames = g_frames + 1

    var pass: ::sc_gfx_pass           # 全零 = 交换链 pass，默认 clear
    ::memset(&pass, 0, sizeof(::sc_gfx_pass))
    pass.action.colors[0].clear[0] = 0.10
    pass.action.colors[0].clear[1] = 0.10
    pass.action.colors[0].clear[2] = 0.12
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
        print "hello-android-gles: 帧 ", g_frames, "\n"
        ::fflush(nil)

fnc on_before_cleanup:
    print "hello-android-gles: cleanup\n"
    if g_ready != 0
        gfx_destroy_pipeline(g_pip)
        gfx_destroy_shader(g_shd)
        gfx_shutdown()
        gpu_shutdown()

# Android 逻辑入口（无 main）：编译为 C 符号 sc_app_main，由 wsi 后端的
# ANativeActivity_onCreate 在渲染线程上调用。@ 导出令其成为外部链接符号
#（wsi 后端 extern 引用）；进入 wsi 循环，不返回。
@fnc app_main: i4
    print "hello-android-gles: 进入 wsi 事件循环（wsi_app_run）\n"
    return wsi_app_run(on_after_startup, on_main_window_created, on_frame, on_before_cleanup)
