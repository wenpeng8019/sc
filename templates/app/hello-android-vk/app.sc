# hello-android-vk —— Android(Vulkan) 三角形：hello-android-gles 的 Vulkan 等效（回调驱动 · 无 main）
#
# 与 hello-android-gles 同一闭环、同一四回调，只把 gpu 后端从 GLES 换成 Vulkan：
#   gpu_shader/gpu_tri.ss ──scc──▶ gpu_tri.shader.h/.c（资源化，每目标三连
#                                  [reflect, vs, fs]；Vulkan 取 vulkan450 SPIR-V，base=9）
#   on_main_window_created ─▶ gpu_init（gd.backend=3=Vulkan，
#                             native_window = wsi 交付的 ANativeWindow*，
#                             vkCreateAndroidSurfaceKHR 建交换链）
#                          └▶ gfx_init ─▶ gfx_make_shader(SPIR-V) ─▶ gfx_make_pipeline
#   on_frame（AChoreographer vsync 每帧）─▶ gfx_begin_pass/draw(3)/commit
#
# 与 hello-android-gles 对照：仅 gd.backend=3（GLES 走平台默认）、着色器取
# vulkan450 三连（base=9，vs/fs 为 SPIR-V 二进制）两处不同；渲染调用完全一致。
#
# 用法（M 芯片 Mac + Android NDK/SDK + 支持 Vulkan 的模拟器或真机）：
#   ANDROID_NDK_HOME=... ANDROID_HOME=... ./templates/app/hello-android-vk/build.sh

inc io.sc
inc wsi.sc
inc gpu.sc
inc gfx.sc

# 着色器资源（与桌面 demo 共用同一产物；Vulkan 只取 vulkan450 三连 base=9）。
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
    print "hello-android-vk: 子系统就绪\n"
    ::fflush(nil)

# 主窗口创建：wsi 已自建全屏窗口，交付 ANativeWindow* 句柄；在此初始化 gpu/gfx/管线。
fnc on_main_window_created: win: ::sc_window&
    var fbw: i4 = 0
    var fbh: i4 = 0
    wsi_win_get_framebuffer_size(win, &fbw, &fbh)
    print "hello-android-vk: 主窗口就绪 · 帧缓冲 ", fbw, " x ", fbh, "\n"
    ::fflush(nil)

    # ---- gpu 初始化（Vulkan；native_window = ANativeWindow* → vkCreateAndroidSurfaceKHR） ----
    var gd: ::sc_gpu_desc
    ::memset(&gd, 0, sizeof(::sc_gpu_desc))
    gd.backend = 3                            # SC_GPU_BACKEND_VULKAN
    gd.surface.native_window  = wsi_win_get_native_window(win)
    gd.surface.native_display = wsi_win_get_native_display(win)
    gd.surface.width  = fbw
    gd.surface.height = fbh
    if gpu_init(&gd) == 0
        print "hello-android-vk: gpu_init 失败\n"
        ::fflush(nil)
        return
    var bk: i4 = gpu_query_backend()
    print "hello-android-vk: gpu 后端 ", bk, " (3=Vulkan)\n"

    # ---- gfx 渲染层 ----
    var fd: ::sc_gfx_desc
    ::memset(&fd, 0, sizeof(::sc_gfx_desc))
    if gfx_init(&fd) == 0
        print "hello-android-vk: gfx_init 失败\n"
        ::fflush(nil)
        gpu_shutdown()
        return

    # ---- 着色器：vulkan450 三连 [reflect, vs, fs]，base=9（vs/fs 为 SPIR-V 二进制） ----
    var base: u8 = 9
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
        print "hello-android-vk: make_shader 失败\n"
        ::fflush(nil)
        return
    g_shd = shd

    var pd: ::sc_gfx_pipeline_desc
    ::memset(&pd, 0, sizeof(::sc_gfx_pipeline_desc))
    pd.shader = shd
    pd.label  = "tri"
    var pip: u4 = gfx_make_pipeline(&pd)
    if pip == 0
        print "hello-android-vk: make_pipeline 失败\n"
        ::fflush(nil)
        return
    g_pip = pip

    g_fbw   = fbw
    g_fbh   = fbh
    g_ready = 1
    print "hello-android-vk: 三角形管线就绪\n"
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
        print "hello-android-vk: 帧 ", g_frames, "\n"
        ::fflush(nil)

fnc on_before_cleanup:
    print "hello-android-vk: cleanup\n"
    if g_ready != 0
        gfx_destroy_pipeline(g_pip)
        gfx_destroy_shader(g_shd)
        gfx_shutdown()
        gpu_shutdown()

# Android 逻辑入口（无 main）：编译为 C 符号 sc_app_main，由 wsi 后端的
# ANativeActivity_onCreate 在渲染线程上调用。@ 导出令其成为外部链接符号
#（wsi 后端 extern 引用）；进入 wsi 循环，不返回。
@fnc app_main: i4
    print "hello-android-vk: 进入 wsi 事件循环（wsi_app_run）\n"
    return wsi_app_run(on_after_startup, on_main_window_created, on_frame, on_before_cleanup)
