# hello-android —— wsi Android 后端形态设计（NativeActivity · 回调驱动 · 无 main）
#
# Android 的入口/循环模型与桌面、iOS 都不同，本文件呈现「实际使用形态」：
#
#   * 无 main：APK 由框架自带的 Java 类 android.app.NativeActivity 拉起，经 JNI
#     调用打进 .so 的 ANativeActivity_onCreate（由 wsi 后端提供，非本文件）。
#     故 app 编译为「共享库 libhello.so」而非可执行文件（详见 build.sh）。
#   * 进程级 init 归 Application：wsi 提供 app 无关的 Application 子类
#     com.sc.wsi.ScApplication（templates/.scenv/modules/wsi/java），其 onCreate（每进程一次、
#     早于任何 Activity）经自定义 JNI（wsi/src/android_jni.c）触发 sc_wsi_app_startup（tier A）。
#     本文件的回调是「窗口/帧级」（tier B/C），挂在 NativeActivity/ANativeWindow 上——
#     Application 无窗口、不做渲染，二者并存（详见 AndroidManifest.xml / README）。
#   * 循环由框架 Looper 推事件；惯例另起「渲染线程」跑帧循环。wsi 在该线程上
#     回调本文件的 app_main（编译为 C 符号 sc_app_main）。
#
# 与 iOS 对照（表面调用形式完全一致，只入口包装略异）：
#   iOS:     main     → wsi_app_run(after_startup, main_window_created, on_frame, before_cleanup)   # 自有主循环实现
#   Android: app_main → wsi_app_run(after_startup, main_window_created, on_frame, before_cleanup)   # 事件注册实现
# 统一的 wsi_app_run 底下两种后端：iOS = UIApplicationMain 阻塞主线程（loop）；
# Android = 注册 ANativeWindow/AChoreographer 后进渲染线程循环（event）。四回调
# 与 hello-ios 完全一致——「同一份 app 逻辑，仅换驱动入口」。
#
# 状态：可用。wsi 的 Android C 后端（android_platform.c：ANativeActivity_onCreate
# + 渲染线程 + ALooper + ANativeWindow/AInputQueue/AChoreographer）已实现并跑通。
#
# 用法：
#   ANDROID_NDK_HOME=... ANDROID_HOME=... ./templates/app/hello-android/build.sh

inc io.sc
inc wsi.sc

var g_frames: i4 = 0          # 帧计数（跨回调共享，模块级全局）

# 子系统就绪：wsi 已启动、建窗前回调一次（真实 app 可在此做与窗口无关的准备）。
fnc on_after_startup:
    print "hello-android: 子系统就绪\n"
    ::fflush(nil)

# 主窗口创建：wsi 已自建全屏窗口（Android 恒全屏，窗口取自框架交付的 ANativeWindow），
# 经回调交付句柄（恒非 nil），应用不再自己调 wsi_win_create。真实 app 在此初始化 gpu/gfx
#（EGL/Vulkan surface 取自 ANativeWindow），并经 wsi_win_set_callback 注册 touch/suspend/
# resume/destroy 委托。窗口丢失（后台/旋转销毁 ANativeWindow）经 destroy 委托交付，
# 重建时经 main_window_created 再次交付新窗口。
fnc on_main_window_created: win: ::sc_window&
    var w: i4 = 0
    var h: i4 = 0
    wsi_win_get_framebuffer_size(win, &w, &h)
    print "hello-android: 主窗口就绪 · 帧缓冲 ", w, " x ", h, "\n"
    # Android 上 stdout 默认不进 logcat（需 setprop log.redirect-stdio true，见 README）；
    # 真实 app 宜改用 __android_log_print。此处 flush 保持与 hello-ios 一致的形态。
    ::fflush(nil)

# 每帧回调（AChoreographer vsync 驱动）：真实 app 在此更新并渲染 present。
# 脚手架只做心跳打印（每约 60 帧一次），证明帧循环在跑。
fnc on_frame:
    g_frames = g_frames + 1
    if g_frames % 60 == 0
        print "hello-android: 帧 ", g_frames, "\n"
        ::fflush(nil)

# 终止前回调（Activity 销毁 / 进程退出）：释放资源（脚手架无资源，仅示意）。
fnc on_before_cleanup:
    print "hello-android: cleanup\n"

# Android 逻辑入口（无 main）：编译为 C 符号 sc_app_main，由 wsi 后端的
# ANativeActivity_onCreate 在渲染线程上按固定符号调用。表面调用形式与 iOS 的
# main 一致（均为 wsi_app_run(4 回调)）；只是 iOS 入口名 main（scc 锤定模块 init/drop），
# Android 入口名 app_main（无 main，由框架经 wsi 回调）。进入 wsi 循环，不返回。
# @ 导出：令其成为外部链接的 C 符号 sc_app_main（wsi 后端 extern 引用，跨 TU 可见）；
# 无 @ 则默认 static（内部链接），dlopen 时 wsi 找不到该符号会 UnsatisfiedLinkError。
@fnc app_main: i4
    print "hello-android: 进入 wsi 事件循环（wsi_app_run）\n"
    return wsi_app_run(on_after_startup, on_main_window_created, on_frame, on_before_cleanup)
