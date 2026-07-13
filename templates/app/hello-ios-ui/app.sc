# hello-ios-ui —— 在 iOS 模拟器上验证 ui 原生控件后端（UIKit）
#
# 与 hello-android-ui 对称：主窗口就绪后用 ui 模块创建原生控件（UILabel/UIButton/
# UISwitch），挂到 wsi 提供的根 UIView（rootViewController.view）之上。按钮点击、
# 开关切换经 UIKit target-action → uikit_ui.m 回调 → ui_emit_event → 本文件
# on_ui_event，验证控件事件通路。
#
# iOS 无桌面的显式 while 主循环——app 逻辑由四回调交付（after_startup /
# main_window_created / on_frame / before_cleanup），main 仅调 wsi_app_run 进入
# UIKit 循环（不返回）。坐标单位为点（point），非像素。
#
# 用法（M 芯片 Mac + iOS 模拟器；先预编 libui/libwsi 的 ios-sim 变体）：
#   ./templates/app/hello-ios-ui/build.sh

inc io.sc
inc wsi.sc
inc ui.sc

var g_ui: ::sc_ui_ctx& = nil   # UI 上下文（跨回调持有）

# 控件事件回调（event：1=CLICK 2=TOGGLE 3=TEXT 4=SELECT）。
fnc on_ui_event: c: ::sc_ui_control&, event: i4, user: &
    if event == 1
        print "ui-event: 按钮点击（target-action 通路验证成功）\n"
    if event == 2
        var ck: i4 = ui_control_get_checked(c)
        print "ui-event: 开关切换 checked=", ck, "\n".

fnc on_after_startup:
    print "hello-ios-ui: 子系统就绪\n".

fnc on_frame:
    return

fnc on_main_window_created: win: ::sc_window&
    var w: i4 = 0
    var h: i4 = 0
    wsi_win_get_framebuffer_size(win, &w, &h)
    print "hello-ios-ui: 主窗口就绪 · 帧缓冲 ", w, " x ", h, "\n"

    g_ui = ui_create(win)
    if g_ui == nil
        print "hello-ios-ui: ui_create 失败\n".
        return

    # 一组原生控件（iOS 点坐标，绝对定位于根 UIView）
    var lbl: ::sc_ui_control& = ui_label_create(g_ui, 20, 80, 320, 32, "sc · ui on iOS")
    var btn: ::sc_ui_control& = ui_button_create(g_ui, 20, 130, 220, 48, "点我 · Tap me")
    # UIKit 的 UISwitch 无文字标题，另加一枚标签作为说明。
    var cap: ::sc_ui_control& = ui_label_create(g_ui, 90, 200, 240, 32, "开关 Switch")
    var chk: ::sc_ui_control& = ui_checkbox_create(g_ui, 20, 200, 60, 32, "")

    ui_control_set_checked(chk, 1)

    ui_control_set_callback(btn, on_ui_event, nil)
    ui_control_set_callback(chk, on_ui_event, nil)

    print "hello-ios-ui: 已创建原生控件（label/button/switch）\n".

fnc on_before_cleanup:
    if g_ui != nil
        ui_destroy(g_ui)
        g_ui = nil
    print "hello-ios-ui: cleanup\n"

fnc main: i4
    print "hello-ios-ui: 进入 UIKit 主循环（wsi_app_run）\n"
    return wsi_app_run(on_after_startup, on_main_window_created, on_frame, on_before_cleanup)
