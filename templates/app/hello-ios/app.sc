# hello-ios —— wsi iOS 后端形态验证（模拟器 · path A 回调驱动）
#
# iOS 上系统 app 对象（UIApplication）拥有主循环，桌面的显式 while 在此不适用：
# app 逻辑改由四个回调交付——after_startup（子系统就绪）/ main_window_created（主窗口
# 创建，wsi 自建交付，恒非 nil）/ on_frame（CADisplayLink 每帧）/ before_cleanup（终止前）。
# main 仅调 wsi_app_run 进入 UIKit 循环（不返回）。
#
# 与桌面对照：hello-mac 的 while 主循环，在 iOS 上整体被系统接管为 on_frame 回调。
# main 也不再调 wsi_app_startup——后端在 UIApplication didFinishLaunching 时自行启动。
#
# 用法（在 M 芯片 Mac 上用 iOS 模拟器运行；scc 的 run 对 iOS 无效，改走
# xcrun simctl 打包 .app 后 boot/install/launch，详见 build.sh）：
#   ./templates/app/hello-ios/build.sh

inc io.sc
inc wsi.sc

var g_frames: i4 = 0          # 帧计数（跨回调共享，模块级全局）

# 子系统就绪：wsi 已启动、建窗前回调一次（真实 app 可在此做与窗口无关的准备）。
fnc on_after_startup:
    print "hello-ios: 子系统就绪\n", .

# 主窗口创建：wsi 已自建全屏窗口（iOS 恒全屏，按 UIScreen.bounds 铺满），经回调交付
# 句柄（恒非 nil），应用不再自己调 wsi_win_create。真实 app 在此初始化 gpu/gfx，并经
# wsi_win_set_callback 注册 touch/suspend/resume/destroy 委托（窗口丢失经 destroy 交付）。
fnc on_main_window_created: win: ::sc_window&
    var w: i4 = 0
    var h: i4 = 0
    wsi_win_get_framebuffer_size(win, &w, &h)
    # iOS 上 stdout 非 tty 时全缓冲，末项 “.” flush 以让打印即时可见（真实 app 宜改用 os_log）。
    print "hello-ios: 主窗口就绪 · 帧缓冲 ", w, " x ", h, "\n", .

# 每帧回调（CADisplayLink 驱动）：真实 app 在此更新并渲染 present。
# 脚手架只做心跳打印（每约 60 帧一次），证明帧循环在跑。
fnc on_frame:
    g_frames = g_frames + 1
    if g_frames % 60 == 0
        print "hello-ios: 帧 ", g_frames, "\n", .

# 终止前回调：释放资源（脚手架无资源，仅示意）。
fnc on_before_cleanup:
    print "hello-ios: cleanup\n"

fnc main: i4
    print "hello-ios: 进入 UIKit 主循环（wsi_app_run）\n"
    # 进入系统主循环，不返回；桌面平台在此处写显式 while，iOS 交回调驱动。
    return wsi_app_run(on_after_startup, on_main_window_created, on_frame, on_before_cleanup)
