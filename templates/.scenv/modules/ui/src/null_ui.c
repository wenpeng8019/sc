#include "ui_internal.h"

#if !defined(SC_UI_COCOA) && !defined(SC_UI_WIN32) && !defined(SC_UI_NK)

/* ============================================================
 * ui null 后端 —— 非本机图形平台的空实现
 * ============================================================
 * 保持逻辑层（ui.c）可在任意平台链接通过：所有 hook 为 no-op，
 * 对象树/控件链表仍在共享层正常维护，只是不创建原生视图/控件。
 * 待某平台后端实现后（如 win32_ui.c / x11_ui.c），替换本文件即可。
 * ============================================================ */

void ui_backend_window_create(sc_ui_window* win) { (void) win; }
void ui_backend_window_destroy(sc_ui_window* win) { (void) win; }
void ui_backend_window_set_frame(sc_ui_window* win) { (void) win; }
void ui_backend_window_set_z(sc_ui_window* win) { (void) win; }

void ui_backend_control_create(sc_ui_control* control) { (void) control; }
void ui_backend_control_destroy(sc_ui_control* control) { (void) control; }
void ui_backend_control_set_frame(sc_ui_control* control) { (void) control; }
void ui_backend_control_set_z(sc_ui_control* control) { (void) control; }
void ui_backend_control_set_text(sc_ui_control* control) { (void) control; }
void ui_backend_control_set_checked(sc_ui_control* control) { (void) control; }
void ui_backend_control_set_items(sc_ui_control* control) { (void) control; }
void ui_backend_control_set_selected_index(sc_ui_control* control) { (void) control; }

int ui_backend_set_font(sc_ui_ctx* ctx, const char* path, float size)
{
    (void) ctx; (void) path; (void) size;
    return 0;
}

#endif /* !SC_UI_COCOA && !SC_UI_WIN32 && !SC_UI_NK */
