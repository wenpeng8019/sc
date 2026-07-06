#ifndef SC_UI_INTERNAL_H
#define SC_UI_INTERNAL_H

#include "../ui.h"

/* ============================================================
 * ui 内部头 —— 共享数据结构 + 平台后端 hook 契约
 * ============================================================
 * 参考 wsi(glfw) 的做法：共享逻辑（ui.c）维护对象树/链表并调用一组
 * 平台后端 hook；各平台后端（cocoa_ui.m / null_ui.c）实现这些 hook，
 * 负责真正的原生子视图/控件的创建、销毁与属性下发。
 *
 * 每个对象的 backend 字段存放后端自己的原生对象（如 NSView 或 NSControl），
 * 生命周期由后端在 *_create / *_destroy hook 中管理。
 * ============================================================ */

/* ============================================================
 * 共享数据结构（ui.c 与各后端共用）
 * ============================================================ */

struct sc_ui_window
{
    struct sc_ui_ctx* ctx;
    sc_ui_window* parent;
    sc_ui_window* firstChild;
    sc_ui_window* lastChild;
    sc_ui_window* nextSibling;

    int x;
    int y;
    int width;
    int height;
    int z;
    int flags;

    int platform;
    void* nativeDisplay;
    void* nativeWindow;

    void* backend; /* 后端原生对象（cocoa: 承载子视图的 NSView*） */
};

struct sc_ui_control
{
    struct sc_ui_ctx* ctx;
    sc_ui_window* window;
    sc_ui_control* next;

    int id;
    int kind;
    int x;
    int y;
    int width;
    int height;
    int z;

    char* text;
    int checked;

    char** items;
    int itemCount;
    int selectedIndex;

    void* backend; /* 后端原生对象（cocoa: NSControl 或 NSScrollView） */
};

struct sc_ui_ctx
{
    sc_window* window;
    sc_ui_window* rootWindow;
    sc_ui_control* controlsHead;
    sc_ui_control* controlsTail;
    int nextControlId;
};

/* ============================================================
 * 平台后端 hook 契约
 * ============================================================
 * ui.c 在对象增删改时调用；后端可对无关平台/无效对象直接返回。
 * 所有 hook 均以对象当前字段为准（调用方保证字段已更新）。
 * ============================================================ */

/* 子窗口：创建原生视图并挂到父视图 / 销毁 / 下发几何与层叠。 */
void ui_backend_window_create(sc_ui_window* win);
void ui_backend_window_destroy(sc_ui_window* win);
void ui_backend_window_set_frame(sc_ui_window* win);
void ui_backend_window_set_z(sc_ui_window* win);

/* 控件：创建原生控件并挂到所属子窗口 / 销毁 / 下发各属性。 */
void ui_backend_control_create(sc_ui_control* control);
void ui_backend_control_destroy(sc_ui_control* control);
void ui_backend_control_set_frame(sc_ui_control* control);
void ui_backend_control_set_z(sc_ui_control* control);
void ui_backend_control_set_text(sc_ui_control* control);
void ui_backend_control_set_checked(sc_ui_control* control);
void ui_backend_control_set_items(sc_ui_control* control);
void ui_backend_control_set_selected_index(sc_ui_control* control);

#endif /* SC_UI_INTERNAL_H */
