# ui —— 各平台原生控件/子窗口的跨平台封装层（依赖 wsi）
#
# 四层架构：
#   - wsi     ：root window 与事件循环
#   - layout  ：逻辑视图树 + 布局驱动（平台无关，通过 sc_ui_sink 操作 ui）
#   - ui      ：各平台原生子窗口/控件封装（本模块）
#   - surface ：独立模块，从 native handle 创建渲染 surface

inc ui.h
add libui.a

# 说明：各接口函数的功能与参数注释见 ui.h（本文件仅按功能分区罗列绑定）。

# === 上下文（ctx）：生命周期与访问器 ===
@fnc ui_create:: ::sc_ui_ctx&, window: ::sc_window&
@fnc ui_destroy:: ctx: ::sc_ui_ctx&
@fnc ui_get_window:: ::sc_window&, ctx: ::sc_ui_ctx&
@fnc ui_get_root_window:: ::sc_ui_window&, ctx: ::sc_ui_ctx&

# 字体（可选）：加载系统字体以支持 CJK；path 传 nil 时后端自动探测。返回 1 成功 / 0 失败。
@fnc ui_set_font:: i4, ctx: ::sc_ui_ctx&, path: const char&, size: f4

# === 子窗口（window）：几何和父子关系 ===
@fnc ui_window_create:: ::sc_ui_window&, ctx: ::sc_ui_ctx&, parent: ::sc_ui_window&, x: i4, y: i4, width: i4, height: i4, flags: i4
@fnc ui_window_destroy:: win: ::sc_ui_window&
@fnc ui_window_first_child:: ::sc_ui_window&, win: ::sc_ui_window&
@fnc ui_window_next_sibling:: ::sc_ui_window&, win: ::sc_ui_window&
@fnc ui_window_get_frame:: win: ::sc_ui_window&, x: i4&, y: i4&, width: i4&, height: i4&
@fnc ui_window_set_frame:: win: ::sc_ui_window&, x: i4, y: i4, width: i4, height: i4
@fnc ui_window_get_z:: i4, win: ::sc_ui_window&
@fnc ui_window_set_z:: win: ::sc_ui_window&, z: i4
@fnc ui_window_get_flags:: i4, win: ::sc_ui_window&
@fnc ui_window_set_flags:: win: ::sc_ui_window&, flags: i4

# === 子窗口：原生句柄绑定（★ surface 绑定入口） ===
@fnc ui_window_get_platform:: i4, win: ::sc_ui_window&
@fnc ui_window_get_native_display:: &, win: ::sc_ui_window&
@fnc ui_window_get_native_window:: &, win: ::sc_ui_window&
@fnc ui_window_set_native_window:: win: ::sc_ui_window&, platform: i4, nativeDisplay: &, nativeWindow: &

# === 控件（control）：创建 ===
@fnc ui_label_create:: ::sc_ui_control&, ctx: ::sc_ui_ctx&, x: i4, y: i4, width: i4, height: i4, text: const char&
@fnc ui_edit_create:: ::sc_ui_control&, ctx: ::sc_ui_ctx&, x: i4, y: i4, width: i4, height: i4, text: const char&
@fnc ui_text_create:: ::sc_ui_control&, ctx: ::sc_ui_ctx&, x: i4, y: i4, width: i4, height: i4, text: const char&
@fnc ui_button_create:: ::sc_ui_control&, ctx: ::sc_ui_ctx&, x: i4, y: i4, width: i4, height: i4, text: const char&
@fnc ui_checkbox_create:: ::sc_ui_control&, ctx: ::sc_ui_ctx&, x: i4, y: i4, width: i4, height: i4, text: const char&
@fnc ui_radiobox_create:: ::sc_ui_control&, ctx: ::sc_ui_ctx&, x: i4, y: i4, width: i4, height: i4, text: const char&
@fnc ui_combo_create:: ::sc_ui_control&, ctx: ::sc_ui_ctx&, x: i4, y: i4, width: i4, height: i4, text: const char&
@fnc ui_list_create:: ::sc_ui_control&, ctx: ::sc_ui_ctx&, x: i4, y: i4, width: i4, height: i4, text: const char&

# === 控件：生命周期与遍历 ===
@fnc ui_control_destroy:: control: ::sc_ui_control&
@fnc ui_control_get_kind:: i4, control: ::sc_ui_control&
@fnc ui_control_get_id:: i4, control: ::sc_ui_control&
@fnc ui_control_get_window:: ::sc_ui_window&, control: ::sc_ui_control&
@fnc ui_first_control:: ::sc_ui_control&, ctx: ::sc_ui_ctx&
@fnc ui_control_next:: ::sc_ui_control&, control: ::sc_ui_control&

# === 控件：几何 ===
@fnc ui_control_get_frame:: control: ::sc_ui_control&, x: i4&, y: i4&, width: i4&, height: i4&
@fnc ui_control_set_frame:: control: ::sc_ui_control&, x: i4, y: i4, width: i4, height: i4
@fnc ui_control_get_z:: i4, control: ::sc_ui_control&
@fnc ui_control_set_z:: control: ::sc_ui_control&, z: i4

# === 控件：文本 ===
@fnc ui_control_get_text:: const char&, control: ::sc_ui_control&
@fnc ui_control_set_text:: control: ::sc_ui_control&, text: const char&

# === 控件：勾选状态（checkbox/radiobox） ===
@fnc ui_control_get_checked:: i4, control: ::sc_ui_control&
@fnc ui_control_set_checked:: control: ::sc_ui_control&, checked: i4

# === 控件：列表项（combo/list） ===
@fnc ui_control_set_items:: i4, control: ::sc_ui_control&, items: const char&&, count: i4
@fnc ui_control_get_item_count:: i4, control: ::sc_ui_control&
@fnc ui_control_get_item:: const char&, control: ::sc_ui_control&, index: i4

@fnc ui_control_get_selected_index:: i4, control: ::sc_ui_control&
@fnc ui_control_set_selected_index:: control: ::sc_ui_control&, index: i4

# === 控件：事件回调 ===
# 回调 typedef ::sc_ui_control_cb 定义于 ui.h，签名 void(*)(sc_ui_control*, int event, void*)。
# 事件常量 SC_UI_EVENT_CLICK/TOGGLE/TEXT/SELECT 见 ui.h；user 数据映射裸指针 &。
@fnc ui_control_set_callback:: control: ::sc_ui_control&, cb: ::sc_ui_control_cb, user: &

# === 驱动 sink 提供者（★ 供 layout 等外部组件操作 ui） ===
@fnc ui_window_sink:: const ::sc_ui_sink&
@fnc ui_control_sink:: const ::sc_ui_sink&
