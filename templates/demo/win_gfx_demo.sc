# win_gfx_demo —— win + gfx 模块集成演示
#
# 打开一个窗口，创建 OpenGL Context，每帧清屏变色，运行约 5 秒后关闭。
# 演示 win 和 gfx 模块的独立性和组合使用方式。
#
# 用法：scc templates/demo/win_gfx_demo.sc

#define GL_SILENCE_DEPRECATION

inc ../../templates/wsi/win.sc
inc ../../templates/utils/gfx/gfx.sc

inc OpenGL/gl.h
inc math.h

var g_ctx: &    # gfx context handle
var g_w: &      # win handle
var g_frame: i4 # frame counter

fnc my_frame:
    g_frame = g_frame + 1

    # 随时间变换清屏颜色
    var r: f4 = 0.15 + 0.05 * (::sinf((g_frame: f4) * 0.05) + 1.0)
    var g: f4 = 0.20 + 0.05 * (::cosf((g_frame: f4) * 0.04) + 1.0)
    var b: f4 = 0.35

    gfx_make_current(g_ctx)
    ::glClearColor(r, g, b, 1.0)
    ::glClear(::GL_COLOR_BUFFER_BIT)
    gfx_swap(g_ctx)

    # 约 5 秒后退出（~300 帧 @60fps）
    if g_frame >= 300
        ::exit(0)

fnc entry:
    g_frame = 0

    if win_init() != 0
        return

    g_w = win_create("sc win + gfx demo", 800, 600)
    if g_w == ::NULL
        return

    var nwh: & = win_get_nwh(g_w)
    g_ctx = gfx_init(nwh, 800, 600, 3, 3)
    if g_ctx == ::NULL
        win_destroy(g_w)
        return

    # 阻塞式事件循环
    win_run(g_w, (my_frame: &))

    gfx_destroy(g_ctx)
    win_destroy(g_w)
