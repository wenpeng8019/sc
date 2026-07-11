# hello-linux —— sc 桌面 app 脚手架（Linux / X11 · Wayland）
#
# 最小的原生窗口程序：打开窗口、跑主循环、响应输入、干净退出。
# 拷走本目录改名，即可作为你自己 Linux app 的起点。
#
# 桌面平台的循环由「程序自己」掌控（显式 while）；iOS/Android 的循环由系统
# 框架驱动，形态不同——见 templates/app/README.md。
#
# 用法：
#   ./templates/app/hello-linux/build.sh      # 构建并运行（X11/Wayland 运行时选择）

inc io.sc
inc ../../utils/wsi/wsi.sc

fnc main: i4
    # ---- 初始化窗口系统 ----
    if wsi_app_startup() == 0
        print "wsi_app_startup 失败\n"
        return 1

    var major: i4 = 0
    var minor: i4 = 0
    var rev:   i4 = 0
    wsi_get_version(&major, &minor, &rev)
    print "wsi ", major, ".", minor, ".", rev, " · 桌面 app 脚手架 (Linux)\n"

    # ---- 创建并显示窗口 ----
    var win: ::sc_window& = wsi_win_create(960, 540, "sc · hello-linux", nil, nil)
    if win == nil
        print "窗口创建失败\n"
        wsi_app_cleanup()
        return 1
    wsi_win_show(win)
    print "窗口已打开 · 按 ESC 或点关闭按钮退出\n"

    # ---- 主循环（桌面：程序掌控节奏的显式 while）----
    var frames: u8 = 0
    var last:   f8 = wsi_get_time()
    while wsi_win_get_should_close(win) == 0
        # 泵事件并让出约 1/60 秒（无渲染时避免忙转空耗 CPU）。
        # 真实 app 在此处「更新 + 渲染」，并改用 wsi_loop_poll() 不阻塞。
        wsi_loop_wait(0.016)

        # 输入轮询（sc FFI 不导出结构体回调，改用状态查询）：ESC 请求关闭。
        # 键码见 wsi.h：ESCAPE=256，按下状态 SC_PRESS=1。
        if wsi_key(win, 256) == 1
            wsi_win_set_should_close(win, 1)

        # 每秒报一次帧数（脚手架自检，真实 app 可删）。
        frames = frames + 1
        var now: f8 = wsi_get_time()
        if now - last >= 1.0
            print "帧率 ~", (frames: i4), " fps\n"
            frames = 0
            last = now

    # ---- 清理 ----
    wsi_win_destroy(win)
    wsi_app_cleanup()
    print "已退出\n"
    return 0
