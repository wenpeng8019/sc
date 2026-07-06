# ui_demo —— ui 原生控件层演示（依赖 wsi + ui）
#
# 在一个原生窗口上创建各类原生控件（label/edit/button/checkbox/
# radiobox/combo/list），进入事件循环直到窗口关闭。
#
# 用法（macOS，需链接 Cocoa 系列框架）：
#   ./templates/utils/wsi/build.sh    # 先编出 libwsi.a
#   ./templates/utils/ui/build.sh     # 再编出 libui.a
#   SCC_LDFLAGS="-framework Cocoa -framework IOKit -framework CoreFoundation" \
#       ./compiler/build/scc templates/demo/ui_demo.sc
#   # 其他平台的 ui 后端尚未实现（null 后端仅维护逻辑树，不建原生控件）。

inc io.sc
inc ../utils/wsi/wsi.sc
inc ../utils/ui/ui.sc

fnc main: i4
    if wsi_init() == 0
        print "wsi_init 失败\n"
        return 1

    var win: ::sc_window& = wsi_win_create(480, 360, "sc · ui demo", nil, nil)
    if win == nil
        print "窗口创建失败\n"
        wsi_terminate()
        return 1

    var ui: ::sc_ui_ctx& = ui_create(win)
    if ui == nil
        print "ui_create 失败\n"
        wsi_win_destroy(win)
        wsi_terminate()
        return 1

    # 一列原生控件（左上原点坐标，由 ui 的翻转容器保证）
    var lbl:   ::sc_ui_control& = ui_label_create(ui, 20, 20, 200, 22, "Label 标签")
    var edit:  ::sc_ui_control& = ui_edit_create(ui, 20, 52, 200, 24, "可编辑文本")
    var btn:   ::sc_ui_control& = ui_button_create(ui, 20, 88, 120, 30, "Button")
    var chk:   ::sc_ui_control& = ui_checkbox_create(ui, 20, 128, 200, 22, "Checkbox")
    var radio: ::sc_ui_control& = ui_radiobox_create(ui, 20, 156, 200, 22, "Radiobox")
    var combo: ::sc_ui_control& = ui_combo_create(ui, 20, 190, 200, 26, "")
    var list:  ::sc_ui_control& = ui_list_create(ui, 20, 226, 200, 100, "")

    ui_control_set_checked(chk, 1)

    wsi_win_show(win)
    print "ui demo 窗口已打开，关闭窗口以退出...\n"

    while wsi_win_get_should_close(win) == 0
        wsi_wait_events()

    ui_destroy(ui)
    wsi_win_destroy(win)
    wsi_terminate()
    print "已退出\n"
    return 0
