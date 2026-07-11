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
#
# 用法（Linux，Nuklear 后端软件渲染到 wsi 窗口，当前支持 X11）：
#   ./templates/utils/wsi/build.sh
#   ./templates/utils/ui/build.sh     # Linux 目标自动选用 nk_ui.c（SC_UI_NK）
#   SCC_LDFLAGS="-L.../ui -lui -L.../wsi -lwsi -lX11 -lXrandr -lXinerama \
#       -lXcursor -lXi -lXext -lXfixes -lwayland-client -lwayland-cursor \
#       -lxkbcommon -lm -ldl -lpthread -lrt" \
#       ./compiler/build/scc templates/demo/ui_demo.sc
#   # Wayland 呈现待补；Windows/其他平台的 ui 后端尚未实现（null 后端仅维护逻辑树）。

inc io.sc
inc ../utils/wsi/wsi.sc
inc ../utils/ui/ui.sc

# 控件事件回调（event 取值见 ui.h：1=CLICK 2=TOGGLE 3=TEXT 4=SELECT）。
fnc on_ui_event: c: ::sc_ui_control&, event: i4, user: &
    if event == 1
        print "事件: 按钮点击\n"
    if event == 2
        print "事件: 勾选切换\n"
    if event == 3
        print "事件: 文本变化\n"
    if event == 4
        print "事件: 选择变化\n"

fnc main: i4
    if wsi_app_startup() == 0
        print "wsi_app_startup 失败\n"
        return 1

    var win: ::sc_window& = wsi_win_create(480, 360, "sc · ui demo", nil, nil)
    if win == nil
        print "窗口创建失败\n"
        wsi_app_cleanup()
        return 1

    var ui: ::sc_ui_ctx& = ui_create(win)
    if ui == nil
        print "ui_create 失败\n"
        wsi_win_destroy(win)
        wsi_app_cleanup()
        return 1

    # 可选：加载系统字体以显示中文（scc 不内置字库）。传 nil 让后端自动探测
    # 含 CJK 的系统字体；找不到则保持默认 ASCII 字体（中文显示为 ?/方块）。
    if ui_set_font(ui, nil, 16.0f) == 0
        print "未找到系统 CJK 字体，中文将显示为占位符\n"

    # 一列原生控件（左上原点坐标，由 ui 的翻转容器保证）
    var lbl:   ::sc_ui_control& = ui_label_create(ui, 20, 20, 200, 22, "Label 标签")
    var edit:  ::sc_ui_control& = ui_edit_create(ui, 20, 52, 200, 24, "可编辑文本")
    var btn:   ::sc_ui_control& = ui_button_create(ui, 20, 88, 120, 30, "Button")
    var chk:   ::sc_ui_control& = ui_checkbox_create(ui, 20, 128, 200, 22, "Checkbox")
    var radio: ::sc_ui_control& = ui_radiobox_create(ui, 20, 156, 200, 22, "Radiobox")
    var combo: ::sc_ui_control& = ui_combo_create(ui, 20, 190, 200, 26, "")
    var list:  ::sc_ui_control& = ui_list_create(ui, 20, 226, 200, 100, "")

    ui_control_set_checked(chk, 1)

    # 注册控件事件回调
    ui_control_set_callback(btn, on_ui_event, nil)
    ui_control_set_callback(chk, on_ui_event, nil)
    ui_control_set_callback(radio, on_ui_event, nil)
    ui_control_set_callback(edit, on_ui_event, nil)

    wsi_win_show(win)
    print "ui demo 窗口已打开，关闭窗口以退出...\n"

    while wsi_win_get_should_close(win) == 0
        wsi_loop_wait(0)

    ui_destroy(ui)
    wsi_win_destroy(win)
    wsi_app_cleanup()
    print "已退出\n"
    return 0
