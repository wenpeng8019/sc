# hello-android-ui —— 在 Android 上验证 ui 原生控件后端 + wsi JNI 反射代理
#
# view-tree 外壳形态（ScActivity + FrameLayout(SurfaceView + 控件)）：主窗口就绪后用
# ui 模块创建原生控件，经 wsi 的 sc_jni_* 反射代理挂到 ScActivity 的 FrameLayout 根
# （叠在 gpu 渲染的 SurfaceView 之上）。按钮/勾选的事件经 Java listener →
# Bridge.nativeInvoke → android_ui.c 回调 → ui_emit_event → 本文件 on_ui_event，
# 验证 C↔Java 双向 JNI 通路。
#
# 用法：
#   ANDROID_NDK_HOME=... ANDROID_HOME=... ./templates/app/hello-android-ui/build.sh

inc io.sc
inc wsi.sc
inc ui.sc

var g_ui: ::sc_ui_ctx& = nil   # UI 上下文（跨回调持有）

# 控件事件回调（event：1=CLICK 2=TOGGLE 3=TEXT 4=SELECT）。
fnc on_ui_event: c: ::sc_ui_control&, event: i4, user: &
    if event == 1
        print "ui-event: 按钮点击（JNI 通路验证成功）\n"
    if event == 2
        var ck: i4 = ui_control_get_checked(c)
        print "ui-event: 勾选切换 checked=", ck, "\n"
    ::fflush(nil)

fnc on_after_startup:
    print "hello-android-ui: 子系统就绪\n"
    ::fflush(nil)

fnc on_frame:
    return

fnc on_main_window_created: win: ::sc_window&
    var w: i4 = 0
    var h: i4 = 0
    wsi_win_get_framebuffer_size(win, &w, &h)
    print "hello-android-ui: 主窗口就绪 · 帧缓冲 ", w, " x ", h, "\n"

    g_ui = ui_create(win)
    if g_ui == nil
        print "hello-android-ui: ui_create 失败\n"
        ::fflush(nil)
        return

    # 一组原生控件（Android 设备像素坐标，绝对定位于覆盖层 FrameLayout）
    var lbl: ::sc_ui_control& = ui_label_create(g_ui, 60, 120, 900, 90, "sc · ui on Android")
    var btn: ::sc_ui_control& = ui_button_create(g_ui, 60, 240, 500, 150, "点我 · Tap me")
    var chk: ::sc_ui_control& = ui_checkbox_create(g_ui, 60, 430, 700, 100, "开关 Checkbox")

    ui_control_set_checked(chk, 1)

    ui_control_set_callback(btn, on_ui_event, nil)
    ui_control_set_callback(chk, on_ui_event, nil)

    print "hello-android-ui: 已创建原生控件（label/button/checkbox）\n"
    ::fflush(nil)

fnc on_before_cleanup:
    if g_ui != nil
        ui_destroy(g_ui)
        g_ui = nil
    print "hello-android-ui: cleanup\n"
    ::fflush(nil)

@fnc app_main: i4
    print "hello-android-ui: 进入 wsi 事件循环\n"
    ::fflush(nil)
    return wsi_app_run(on_after_startup, on_main_window_created, on_frame, on_before_cleanup)
