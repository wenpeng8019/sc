#ifndef SC_UI_H
#define SC_UI_H

#include "../../../builtins/platform.h"
#include "../wsi/wsi.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef UI_SHARED
 #define UI_SHARED 0
#endif
#ifndef UI_EXPORTS
 #define UI_EXPORTS 0
#endif

#define UI_API SC_API(UI)

/* ============================================================
 * ui —— 各平台原生控件/子窗口的跨平台封装层
 * ============================================================
 * 四层架构：
 *   - wsi     ：root window 与事件循环（平台相关）
 *   - layout  ：逻辑视图树 + 布局驱动（平台无关，通过 ui 的 sc_ui_sink 操作 ui）
 *   - ui      ：各平台原生控件/子窗口封装（本模块，平台相关）
 *   - surface ：从 native handle 创建渲染 surface（平台相关）
 *
 * 本模块职责：
 *   ui 只提供各平台的「物理原生组件」封装，两类平级对象：
 *     - sc_ui_window ：原生子窗口（可承载 surface）
 *     - sc_ui_control：原生控件（label/button/...）
 *   两者对外都只需暴露 pos/size/z-order，即可被 layout 驱动位置。
 *   「逻辑视图树 / 布局」不在本模块，而在平台无关的 layout 模块。
 *
 * 与外部驱动器的衔接（★ 接口方向）：
 *   ui 自己定义并实现「操作 ui 对象」的标准接口 sc_ui_sink（set_frame/
 *   set_z）。layout 或其他组件作为驱动器，通过该接口来操作 ui，
 *   而不需了解 ui 内部结构。即：ui 定标准，外部引用。
 *   拿 sink：sc_ui_window_sink() / sc_ui_control_sink()。
 *
 * 与 surface 的衔接：
 *   sc_ui_window 携带 platform + nativeDisplay + nativeWindow 三元组，
 *   供 surface 模块的 sc_surface_create_from_native(...) 消费。
 *   取/挂句柄见「子窗口：原生句柄绑定」区段。
 * ============================================================ */

/* ============================================================
 * 类型与常量
 * ============================================================ */

/* 不透明句柄：具体布局见 src/ui.c。 */
typedef struct sc_ui_ctx sc_ui_ctx;         /* 一个 root window 对应一个 UI 上下文 */
typedef struct sc_ui_window sc_ui_window;   /* 原生子窗口（可承载 surface，与 control 平级） */
typedef struct sc_ui_control sc_ui_control; /* 原生控件（label/button/...） */

/* ============================================================
 * 驱动接口标准（sc_ui_sink）
 * ============================================================
 * ui 定义并实现的「操作 ui 对象」标准接口：外部组件（如 layout，
 * 或任何布局/动画/驱动器）通过它来设置 ui 对象的位置/尺寸/层叠，
 * 而无需了解 ui 内部结构。
 *
 * target 为被操作对象的指针（sc_ui_window* / sc_ui_control*），由 sink
 * 的提供方（见 sc_ui_window_sink / sc_ui_control_sink）约定。
 * 两个回调均可为 NULL（表示不支持对应维度）。
 * ============================================================ */
typedef struct sc_ui_sink
{
    void (*set_frame)(void* target, int x, int y, int width, int height);
    void (*set_z)(void* target, int z);
} sc_ui_sink;

/* 控件种类，用于 sc_ui_control_get_kind 与创建接口的语义区分。 */
enum
{
    SC_UI_LABEL = 1,
    SC_UI_EDIT,
    SC_UI_TEXT,
    SC_UI_BUTTON,
    SC_UI_CHECKBOX,
    SC_UI_RADIOBOX,
    SC_UI_COMBO,
    SC_UI_LIST
};

/* 控件事件类型（传入 sc_ui_control_cb 的 event 参数）。 */
enum
{
    SC_UI_EVENT_CLICK = 1, /* button 被点击 */
    SC_UI_EVENT_TOGGLE,    /* checkbox/radiobox 勾选态变化（新态见 get_checked） */
    SC_UI_EVENT_TEXT,      /* edit 文本变化（新值见 get_text） */
    SC_UI_EVENT_SELECT     /* combo/list 选中项变化（新下标见 get_selected_index） */
};

/* 控件事件回调：control 为触发源，event 为 SC_UI_EVENT_*，user 为注册时透传。
 * 仅在支持交互的后端（如 Linux Nuklear 后端）中触发；原生控件后端
 * （cocoa/win32）当前不派发本回调。 */
typedef void (*sc_ui_control_cb)(sc_ui_control* control, int event, void* user);

/* ============================================================
 * 上下文（ctx）：生命周期与访问器
 * ============================================================ */

/* 在给定 root window 之上创建 UI 上下文；自动建立 root window 并从
 * window 取回原生句柄与初始尺寸。window 为 NULL 时返回 NULL。 */
UI_API sc_ui_ctx* sc_ui_create(sc_window* window);

/* 销毁上下文：释放全部控件、整棵子窗口树与上下文自身。 */
UI_API void sc_ui_destroy(sc_ui_ctx* ctx);

/* 返回上下文关联的 root window（wsi 窗口，不转移所有权）。 */
UI_API sc_window* sc_ui_get_window(sc_ui_ctx* ctx);

/* 返回根子窗口（绑定到整个 root window 的顶层 sc_ui_window）。 */
UI_API sc_ui_window* sc_ui_get_root_window(sc_ui_ctx* ctx);

/* ============================================================
 * 字体（可选）
 * ============================================================
 * scc 不内置任何字库（尤其 CJK 字库体积巨大）。默认字体仅含 ASCII，
 * 中文/CJK 会显示为 ?/方块。如需 CJK，调用本接口加载一个系统字体：
 *   - path 指向 TTF/TTC/OTF 文件；size<=0 时用默认字号；
 *   - path 传 NULL 时，后端在常见系统路径中自动探测含 CJK 的字体兜底。
 * 成功返回 1，失败返回 0（保持原字体）。仅在软件渲染后端（Linux
 * Nuklear）生效；原生控件后端（cocoa/win32）由系统字体渲染，返回 0。 */
UI_API int sc_ui_set_font(sc_ui_ctx* ctx, const char* path, float size);

/* ============================================================
 * 子窗口（window）：树结构与几何
 * ============================================================
 * 原生子窗口的嵌套层级。注意：这里的树是「原生窗口层级」，
 * 与布局无关；逻辑视图树/布局由 layout 模块负责驱动。
 * ============================================================ */

/* 在 parent 下创建子窗口（parent 为 NULL 时挂到 root window）。
 * 子窗口继承 parent 的 platform，原生句柄默认为空，需另行绑定。 */
UI_API sc_ui_window* sc_ui_window_create(sc_ui_ctx* ctx,
                                       sc_ui_window* parent,
                                       int x,
                                       int y,
                                       int width,
                                       int height,
                                       int flags);

/* 销毁子窗口及其整棵子树；root window 不可销毁（静默忽略）。 */
UI_API void sc_ui_window_destroy(sc_ui_window* win);

/* 遍历：返回首个子窗口 / 下一个兄弟窗口，无则返回 NULL。 */
UI_API sc_ui_window* sc_ui_window_first_child(sc_ui_window* win);
UI_API sc_ui_window* sc_ui_window_next_sibling(sc_ui_window* win);

/* 几何（相对父窗口坐标）：读取/设置 x/y/width/height。
 * get 的输出指针允许为 NULL（按需取值）。 */
UI_API void sc_ui_window_get_frame(sc_ui_window* win, int* x, int* y, int* width, int* height);
UI_API void sc_ui_window_set_frame(sc_ui_window* win, int x, int y, int width, int height);

/* z-order（层叠顺序，值大者在上）：读取/设置。 */
UI_API int sc_ui_window_get_z(sc_ui_window* win);
UI_API void sc_ui_window_set_z(sc_ui_window* win, int z);

/* 标志位（由调用方约定语义）：读取/设置。 */
UI_API int sc_ui_window_get_flags(sc_ui_window* win);
UI_API void sc_ui_window_set_flags(sc_ui_window* win, int flags);

/* ============================================================
 * 子窗口：原生句柄绑定（★ surface 绑定入口）
 * ============================================================
 * 这组接口是 ui 与 surface 模块的衔接点：
 *   取句柄 → sc_surface_create_from_native(platform, display, window)。
 * ============================================================ */

/* 返回该窗口的平台 ID（SC_PLATFORM_*）。 */
UI_API int sc_ui_window_get_platform(sc_ui_window* win);

/* 返回原生 display / window 句柄（含义随平台而定，未绑定时为 NULL）。 */
UI_API void* sc_ui_window_get_native_display(sc_ui_window* win);
UI_API void* sc_ui_window_get_native_window(sc_ui_window* win);

/* 将一组原生句柄绑定到该窗口（把外部/子窗口的 native handle
 * 挂到其上，随后交给 surface 模块创建渲染 surface）。 */
UI_API void sc_ui_window_set_native_window(sc_ui_window* win,
                                          int platform,
                                          void* nativeDisplay,
                                          void* nativeWindow);

/* ============================================================
 * 控件（control）：创建
 * ============================================================
 * 以下 create 接口形状一致 (ctx, x, y, width, height, text)，
 * 仅控件种类（SC_UI_*）不同。新建控件默认挂在 root space 下，
 * 追加到上下文的控件链表尾部。ctx 为 NULL 时返回 NULL。
 * ============================================================ */

UI_API sc_ui_control* sc_ui_label_create(sc_ui_ctx* ctx, int x, int y, int width, int height, const char* text);
UI_API sc_ui_control* sc_ui_edit_create(sc_ui_ctx* ctx, int x, int y, int width, int height, const char* text);
UI_API sc_ui_control* sc_ui_text_create(sc_ui_ctx* ctx, int x, int y, int width, int height, const char* text);
UI_API sc_ui_control* sc_ui_button_create(sc_ui_ctx* ctx, int x, int y, int width, int height, const char* text);
UI_API sc_ui_control* sc_ui_checkbox_create(sc_ui_ctx* ctx, int x, int y, int width, int height, const char* text);
UI_API sc_ui_control* sc_ui_radiobox_create(sc_ui_ctx* ctx, int x, int y, int width, int height, const char* text);
UI_API sc_ui_control* sc_ui_combo_create(sc_ui_ctx* ctx, int x, int y, int width, int height, const char* text);
UI_API sc_ui_control* sc_ui_list_create(sc_ui_ctx* ctx, int x, int y, int width, int height, const char* text);

/* ============================================================
 * 控件：生命周期与遍历
 * ============================================================ */

/* 销毁控件：从上下文链表摘除并释放其文本/列表项/自身。 */
UI_API void sc_ui_control_destroy(sc_ui_control* control);

/* 属性访问：种类（SC_UI_*）、自增 ID、所属子窗口。 */
UI_API int sc_ui_control_get_kind(sc_ui_control* control);
UI_API int sc_ui_control_get_id(sc_ui_control* control);
UI_API sc_ui_window* sc_ui_control_get_window(sc_ui_control* control);

/* 按创建顺序遍历上下文内全部控件：首个 / 下一个，无则 NULL。 */
UI_API sc_ui_control* sc_ui_first_control(sc_ui_ctx* ctx);
UI_API sc_ui_control* sc_ui_control_next(sc_ui_control* control);

/* ============================================================
 * 控件：几何
 * ============================================================ */

/* 读取/设置控件矩形。get 的输出指针允许为 NULL。 */
UI_API void sc_ui_control_get_frame(sc_ui_control* control, int* x, int* y, int* width, int* height);
UI_API void sc_ui_control_set_frame(sc_ui_control* control, int x, int y, int width, int height);

/* z-order（层叠顺序，值大者在上）：读取/设置。 */
UI_API int sc_ui_control_get_z(sc_ui_control* control);
UI_API void sc_ui_control_set_z(sc_ui_control* control, int z);

/* ============================================================
 * 控件：文本
 * ============================================================ */

/* 读取/设置控件文本（内部持有副本；set 传 NULL 视为空串）。 */
UI_API const char* sc_ui_control_get_text(sc_ui_control* control);
UI_API void sc_ui_control_set_text(sc_ui_control* control, const char* text);

/* ============================================================
 * 控件：勾选状态（checkbox/radiobox）
 * ============================================================ */

/* 读取/设置勾选态（set 归一化为 0/1）。 */
UI_API int sc_ui_control_get_checked(sc_ui_control* control);
UI_API void sc_ui_control_set_checked(sc_ui_control* control, int checked);

/* ============================================================
 * 控件：列表项（combo/list）
 * ============================================================ */

/* 用 items[0..count) 覆盖设置全部列表项（内部深拷贝）。
 * 成功返回 1，失败返回 0；count==0 清空。设置后选中项归 0。 */
UI_API int sc_ui_control_set_items(sc_ui_control* control, const char** items, int count);

/* 列表项数量 / 按下标取项（越界返回 NULL）。 */
UI_API int sc_ui_control_get_item_count(sc_ui_control* control);
UI_API const char* sc_ui_control_get_item(sc_ui_control* control, int index);

/* 当前选中项下标：读取（空为 -1）/ 设置（-1 或越界则忽略）。 */
UI_API int sc_ui_control_get_selected_index(sc_ui_control* control);
UI_API void sc_ui_control_set_selected_index(sc_ui_control* control, int index);

/* ============================================================
 * 控件：事件回调
 * ============================================================
 * 注册控件交互回调（click/toggle/text/select）。cb 传 NULL 清除。
 * 回调在事件处理期间（wsi_wait_events/poll_events 内）被后端调用。
 * ============================================================ */
UI_API void sc_ui_control_set_callback(sc_ui_control* control, sc_ui_control_cb cb, void* user);

/* ============================================================
 * 驱动 sink 提供者（★ 供 layout 等外部组件操作 ui）
 * ============================================================
 * 返回操作对应 ui 对象的 sc_ui_sink（set_frame/set_z）。外部驱动器
 * 拿到 sink + 对象指针即可操作其 pos/size/z-order，例如交给布局器：
 *   layout_bind(node, sc_ui_control_sink(), control);
 * 返回的指针指向静态对象，长期有效，不需释放。
 * ============================================================ */

/* 操作 sc_ui_window 的 sink。 */
UI_API const sc_ui_sink* sc_ui_window_sink(void);

/* 操作 sc_ui_control 的 sink。 */
UI_API const sc_ui_sink* sc_ui_control_sink(void);

#ifdef __cplusplus
}
#endif

#endif
