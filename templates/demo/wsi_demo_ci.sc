# wsi_demo_ci —— wsi 窗口演示的 CI 无人值守变体
#
# 与 wsi_demo.sc 相同地打开一个原生窗口，但不阻塞在 wait_events 等用户关窗，
# 而是定时泵事件约 12 秒后自动退出——便于 GitHub Actions 之类无人值守环境
# 在窗口显示期间截图，随后进程自行结束（不会挂死）。
#
# 用法（交叉编译到 Windows）：
#   ./templates/utils/wsi/build.sh --cc x86_64-w64-mingw32-gcc \
#       --ar x86_64-w64-mingw32-ar --target x86_64-windows-gnu
#   SCC_LDFLAGS="-lgdi32 -luser32 -lshell32 -limm32 -lole32 -loleaut32 \
#       -lversion -luuid -ldwmapi -static" \
#       ./compiler/build/scc templates/demo/wsi_demo_ci.sc --build \
#       -o wsi_demo_ci.exe --target templates/targets/windows-x64-mingw.target

inc io.sc
inc ../utils/wsi/wsi.sc

fnc main: i4
    if wsi_init() == 0
        print "wsi_init 失败\n"
        return 1

    var win: ::sc_window& = wsi_win_create(640, 480, "sc · wsi CI demo", nil, nil)
    if win == nil
        print "窗口创建失败\n"
        wsi_terminate()
        return 1

    wsi_win_show(win)
    print "WSI_CI_WINDOW_READY\n"

    # 无人值守：定时泵事件（每次至多阻塞 0.1 秒），约 120 次 ≈ 12 秒后自动退出；
    # 期间若用户/CI 主动关窗则提前结束。
    var i: i4 = 0
    while i < 120
        wsi_wait_events_timeout(0.1)
        if wsi_win_get_should_close(win) != 0
            i = 120
        else
            i = i + 1

    wsi_win_destroy(win)
    wsi_terminate()
    print "WSI_CI_DONE\n"
    return 0
