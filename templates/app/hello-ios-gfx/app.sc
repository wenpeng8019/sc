# hello-ios-gfx —— iOS(Metal) 三角形：桌面 gpu_demo 的移动等效（path A 回调驱动）
#
# 与桌面 templates/demo/gpu_demo.sc 同一闭环，只是主循环被 UIKit 接管为回调：
#   gpu_shader/gpu_tri.ss ──scc──▶ gpu_tri.shader.h/.c（资源化，每目标三连
#                                  [reflect, vs, fs]；iOS 用 metal20000，base=0）
#   on_main_window_created ─▶ gpu_init（Metal device + CAMetalLayer 交换链，
#                             native_window = wsi 交付的 UIView*）
#                          └▶ gfx_init ─▶ gfx_make_shader ─▶ gfx_make_pipeline
#   on_frame（CADisplayLink 每帧）─▶ gfx_begin_pass/draw(3)/commit
#
# iOS 上系统 app 对象（UIApplication）拥有主循环，桌面的显式 while 不适用：
# 逻辑由四回调交付，main 仅调 wsi_app_run 进入 UIKit 循环（不返回）。
#
# 用法（M 芯片 Mac + iOS 模拟器；scc 的 run 对 iOS 无效，走 simctl 打包部署）：
#   ./templates/app/hello-ios-gfx/build.sh

inc io.sc
inc wsi.sc
inc gpu.sc
inc gfx.sc

# 着色器资源（与桌面 demo 共用同一产物；iOS 只取 metal20000 三连 base=0）。
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
var g_pip:   u4  = 0
var g_shd:   u4  = 0
var g_ready: i4  = 0          # gpu/gfx/管线就绪标志（就绪前 on_frame 空转）
var g_fbw:   i4  = 0          # 当前交换链像素尺寸（旋转/尺寸变时重建）
var g_fbh:   i4  = 0
var g_frames: i4 = 0

fnc on_after_startup:
    print "hello-ios-gfx: 子系统就绪\n"
    ::fflush(nil)

# 主窗口创建：wsi 已自建全屏窗口，交付 UIView* 句柄；在此初始化 gpu/gfx/管线。
fnc on_main_window_created: win: ::sc_window&
    var fbw: i4 = 0
    var fbh: i4 = 0
    wsi_win_get_framebuffer_size(win, &fbw, &fbh)
    print "hello-ios-gfx: 主窗口就绪 · 帧缓冲 ", fbw, " x ", fbh, "\n"
    ::fflush(nil)

    # ---- gpu 初始化（Metal；native_window = UIView*，iOS 无 native_display） ----
    var gd: ::sc_gpu_desc
    ::memset(&gd, 0, sizeof(::sc_gpu_desc))
    gd.surface.native_window  = wsi_win_get_native_window(win)
    gd.surface.native_display = wsi_win_get_native_display(win)
    gd.surface.width  = fbw
    gd.surface.height = fbh
    if gpu_init(&gd) == 0
        print "hello-ios-gfx: gpu_init 失败\n"
        ::fflush(nil)
        return
    var bk: i4 = gpu_query_backend()
    print "hello-ios-gfx: gpu 后端 ", bk, " (1=Metal)\n"

    # ---- gfx 渲染层 ----
    var fd: ::sc_gfx_desc
    ::memset(&fd, 0, sizeof(::sc_gfx_desc))
    if gfx_init(&fd) == 0
        print "hello-ios-gfx: gfx_init 失败\n"
        ::fflush(nil)
        gpu_shutdown()
        return

    # ---- 着色器：metal20000 三连 [reflect, vs, fs]，base=0 ----
    var base: u8 = 0
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
        print "hello-ios-gfx: make_shader 失败\n"
        ::fflush(nil)
        return
    g_shd = shd

    var pd: ::sc_gfx_pipeline_desc
    ::memset(&pd, 0, sizeof(::sc_gfx_pipeline_desc))
    pd.shader = shd
    pd.label  = "tri"
    var pip: u4 = gfx_make_pipeline(&pd)
    if pip == 0
        print "hello-ios-gfx: make_pipeline 失败\n"
        ::fflush(nil)
        return
    g_pip = pip

    g_fbw   = fbw
    g_fbh   = fbh
    g_ready = 1
    print "hello-ios-gfx: 三角形管线就绪\n"
    ::fflush(nil)

# 每帧回调（CADisplayLink 驱动）：清屏 + 画三角形。
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
        print "hello-ios-gfx: 帧 ", g_frames, "\n"
        ::fflush(nil)

fnc on_before_cleanup:
    print "hello-ios-gfx: cleanup\n"
    if g_ready != 0
        gfx_destroy_pipeline(g_pip)
        gfx_destroy_shader(g_shd)
        gfx_shutdown()
        gpu_shutdown()

fnc main: i4
    print "hello-ios-gfx: 进入 UIKit 主循环（wsi_app_run）\n"
    return wsi_app_run(on_after_startup, on_main_window_created, on_frame, on_before_cleanup)
