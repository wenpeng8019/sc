# wsi_demo —— wsi 窗口系统集成层演示：一个完整的原生窗口程序
#
# 打开一个原生窗口并进入事件循环，直到用户关闭窗口后退出。
#
# 用法（macOS，需链接 Cocoa 系列框架）：
#   ./templates/utils/wsi/build.sh    # 先编出 libwsi.a
#   SCC_LDFLAGS="-framework Cocoa -framework IOKit -framework CoreFoundation" \
#       ./compiler/build/scc templates/demo/wsi_demo.sc
#   # 其他平台请改用对应系统窗口库的链接选项。

inc io.sc
inc ../utils/wsi/wsi.sc

fnc main: i4
    if wsi_init() == 0
        print "wsi_init 失败\n"
        return 1

    var major: i4 = 0
    var minor: i4 = 0
    var rev:   i4 = 0
    wsi_get_version(&major, &minor, &rev)
    print "wsi 版本: ", major, ".", minor, ".", rev, "\n"

    var win: ::sc_window& = wsi_win_create(640, 480, "sc · wsi demo", nil, nil)
    if win == nil
        print "窗口创建失败\n"
        wsi_terminate()
        return 1

    wsi_win_show(win)
    print "窗口已打开，关闭窗口以退出...\n"

    # 事件循环：阻塞等待事件，直到窗口被请求关闭
    while wsi_win_get_should_close(win) == 0
        wsi_wait_events()

    wsi_win_destroy(win)
    wsi_terminate()
    print "已退出\n"
    return 0
