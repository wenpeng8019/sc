#include "ui_internal.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================
 * ui 实现 —— 接口语义与参数说明见 ui.h（本文件仅做实现与区段划分）。
 * 数据结构与平台后端 hook 契约见 src/ui_internal.h。
 * ============================================================ */

/* ============================================================
 * 内部辅助
 * ============================================================ */

static char* ui_strdup(const char* text)
{
    if (!text)
        return NULL;

    size_t len = strlen(text);
    char* out = (char*) malloc(len + 1);
    if (!out)
        return NULL;

    memcpy(out, text, len + 1);
    return out;
}

static void ui_free_items(sc_ui_control* control)
{
    if (!control || !control->items)
        return;

    for (int i = 0; i < control->itemCount; ++i)
        free(control->items[i]);

    free(control->items);
    control->items = NULL;
    control->itemCount = 0;
    control->selectedIndex = -1;
}

static void ui_remove_control_from_list(sc_ui_ctx* ctx, sc_ui_control* control)
{
    if (!ctx || !control)
        return;

    sc_ui_control* prev = NULL;
    sc_ui_control* it = ctx->controlsHead;

    while (it)
    {
        if (it == control)
        {
            if (prev)
                prev->next = it->next;
            else
                ctx->controlsHead = it->next;

            if (ctx->controlsTail == it)
                ctx->controlsTail = prev;

            return;
        }

        prev = it;
        it = it->next;
    }
}

static void ui_destroy_window_tree(sc_ui_window* win)
{
    if (!win)
        return;

    sc_ui_window* child = win->firstChild;
    while (child)
    {
        sc_ui_window* next = child->nextSibling;
        ui_destroy_window_tree(child);
        child = next;
    }

    ui_backend_window_destroy(win);
    free(win);
}

static sc_ui_control* ui_create_control(sc_ui_ctx* ctx,
                                        int kind,
                                        int x,
                                        int y,
                                        int width,
                                        int height,
                                        const char* text)
{
    if (!ctx)
        return NULL;

    sc_ui_control* control = (sc_ui_control*) calloc(1, sizeof(sc_ui_control));
    if (!control)
        return NULL;

    control->ctx = ctx;
    control->window = ctx->rootWindow;
    control->id = ++ctx->nextControlId;
    control->kind = kind;
    control->x = x;
    control->y = y;
    control->width = width;
    control->height = height;
    control->text = ui_strdup(text ? text : "");
    control->selectedIndex = -1;

    if (!ctx->controlsHead)
        ctx->controlsHead = control;
    else
        ctx->controlsTail->next = control;

    ctx->controlsTail = control;

    ui_backend_control_create(control);
    return control;
}

/* ============================================================
 * 上下文（ctx）：生命周期与访问器
 * ============================================================ */

UI_API sc_ui_ctx* sc_ui_create(sc_window* window)
{
    if (!window)
        return NULL;

    sc_ui_ctx* ctx = (sc_ui_ctx*) calloc(1, sizeof(sc_ui_ctx));
    if (!ctx)
        return NULL;

    sc_ui_window* root = (sc_ui_window*) calloc(1, sizeof(sc_ui_window));
    if (!root)
    {
        free(ctx);
        return NULL;
    }

    ctx->window = window;
    ctx->rootWindow = root;
    ctx->nextControlId = 0;

    root->ctx = ctx;
    root->platform = sc_wsi_get_platform();
    root->nativeDisplay = sc_wsi_win_get_native_display(window);
    root->nativeWindow = sc_wsi_win_get_native_window(window);

    int w = 0;
    int h = 0;
    sc_wsi_win_get_size(window, &w, &h);
    root->width = w;
    root->height = h;

    ui_backend_window_create(root);
    return ctx;
}

UI_API void sc_ui_destroy(sc_ui_ctx* ctx)
{
    if (!ctx)
        return;

    sc_ui_control* c = ctx->controlsHead;
    while (c)
    {
        sc_ui_control* next = c->next;
        ui_backend_control_destroy(c);
        ui_free_items(c);
        free(c->text);
        free(c);
        c = next;
    }

    ui_destroy_window_tree(ctx->rootWindow);
    free(ctx);
}

UI_API sc_window* sc_ui_get_window(sc_ui_ctx* ctx)
{
    return ctx ? ctx->window : NULL;
}

UI_API sc_ui_window* sc_ui_get_root_window(sc_ui_ctx* ctx)
{
    return ctx ? ctx->rootWindow : NULL;
}

UI_API int sc_ui_set_font(sc_ui_ctx* ctx, const char* path, float size)
{
    if (!ctx)
        return 0;
    return ui_backend_set_font(ctx, path, size);
}

/* ============================================================
 * 子窗口（window）：树结构与几何
 * ============================================================ */

UI_API sc_ui_window* sc_ui_window_create(sc_ui_ctx* ctx,
                                       sc_ui_window* parent,
                                       int x,
                                       int y,
                                       int width,
                                       int height,
                                       int flags)
{
    if (!ctx)
        return NULL;

    if (!parent)
        parent = ctx->rootWindow;

    sc_ui_window* win = (sc_ui_window*) calloc(1, sizeof(sc_ui_window));
    if (!win)
        return NULL;

    win->ctx = ctx;
    win->parent = parent;
    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    win->flags = flags;
    win->platform = parent ? parent->platform : SC_PLATFORM_ANY;

    if (!parent->firstChild)
        parent->firstChild = win;
    else
        parent->lastChild->nextSibling = win;

    parent->lastChild = win;

    ui_backend_window_create(win);
    return win;
}

UI_API void sc_ui_window_destroy(sc_ui_window* win)
{
    if (!win || !win->ctx || win == win->ctx->rootWindow)
        return;

    sc_ui_window* parent = win->parent;
    if (parent)
    {
        sc_ui_window* prev = NULL;
        sc_ui_window* it = parent->firstChild;
        while (it)
        {
            if (it == win)
            {
                if (prev)
                    prev->nextSibling = it->nextSibling;
                else
                    parent->firstChild = it->nextSibling;

                if (parent->lastChild == it)
                    parent->lastChild = prev;

                break;
            }
            prev = it;
            it = it->nextSibling;
        }
    }

    ui_destroy_window_tree(win);
}

UI_API sc_ui_window* sc_ui_window_first_child(sc_ui_window* win)
{
    return win ? win->firstChild : NULL;
}

UI_API sc_ui_window* sc_ui_window_next_sibling(sc_ui_window* win)
{
    return win ? win->nextSibling : NULL;
}

UI_API void sc_ui_window_get_frame(sc_ui_window* win, int* x, int* y, int* width, int* height)
{
    if (!win)
        return;

    if (x) *x = win->x;
    if (y) *y = win->y;
    if (width) *width = win->width;
    if (height) *height = win->height;
}

UI_API void sc_ui_window_set_frame(sc_ui_window* win, int x, int y, int width, int height)
{
    if (!win)
        return;

    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    ui_backend_window_set_frame(win);
}

UI_API int sc_ui_window_get_z(sc_ui_window* win)
{
    return win ? win->z : 0;
}

UI_API void sc_ui_window_set_z(sc_ui_window* win, int z)
{
    if (!win)
        return;

    win->z = z;
    ui_backend_window_set_z(win);
}

UI_API int sc_ui_window_get_flags(sc_ui_window* win)
{
    return win ? win->flags : 0;
}

UI_API void sc_ui_window_set_flags(sc_ui_window* win, int flags)
{
    if (!win)
        return;

    win->flags = flags;
}

/* ============================================================
 * 子窗口：原生句柄绑定（★ surface 绑定入口）
 * ============================================================ */

UI_API int sc_ui_window_get_platform(sc_ui_window* win)
{
    return win ? win->platform : SC_PLATFORM_ANY;
}

UI_API void* sc_ui_window_get_native_display(sc_ui_window* win)
{
    return win ? win->nativeDisplay : NULL;
}

UI_API void* sc_ui_window_get_native_window(sc_ui_window* win)
{
    return win ? win->nativeWindow : NULL;
}

UI_API void sc_ui_window_set_native_window(sc_ui_window* win,
                                          int platform,
                                          void* nativeDisplay,
                                          void* nativeWindow)
{
    if (!win)
        return;

    win->platform = platform;
    win->nativeDisplay = nativeDisplay;
    win->nativeWindow = nativeWindow;
}

/* ============================================================
 * 控件（control）：创建
 * ============================================================ */

UI_API sc_ui_control* sc_ui_label_create(sc_ui_ctx* ctx, int x, int y, int width, int height, const char* text)
{
    return ui_create_control(ctx, SC_UI_LABEL, x, y, width, height, text);
}

UI_API sc_ui_control* sc_ui_edit_create(sc_ui_ctx* ctx, int x, int y, int width, int height, const char* text)
{
    return ui_create_control(ctx, SC_UI_EDIT, x, y, width, height, text);
}

UI_API sc_ui_control* sc_ui_text_create(sc_ui_ctx* ctx, int x, int y, int width, int height, const char* text)
{
    return ui_create_control(ctx, SC_UI_TEXT, x, y, width, height, text);
}

UI_API sc_ui_control* sc_ui_button_create(sc_ui_ctx* ctx, int x, int y, int width, int height, const char* text)
{
    return ui_create_control(ctx, SC_UI_BUTTON, x, y, width, height, text);
}

UI_API sc_ui_control* sc_ui_checkbox_create(sc_ui_ctx* ctx, int x, int y, int width, int height, const char* text)
{
    return ui_create_control(ctx, SC_UI_CHECKBOX, x, y, width, height, text);
}

UI_API sc_ui_control* sc_ui_radiobox_create(sc_ui_ctx* ctx, int x, int y, int width, int height, const char* text)
{
    return ui_create_control(ctx, SC_UI_RADIOBOX, x, y, width, height, text);
}

UI_API sc_ui_control* sc_ui_combo_create(sc_ui_ctx* ctx, int x, int y, int width, int height, const char* text)
{
    return ui_create_control(ctx, SC_UI_COMBO, x, y, width, height, text);
}

UI_API sc_ui_control* sc_ui_list_create(sc_ui_ctx* ctx, int x, int y, int width, int height, const char* text)
{
    return ui_create_control(ctx, SC_UI_LIST, x, y, width, height, text);
}

/* ============================================================
 * 控件：生命周期与遍历
 * ============================================================ */

UI_API void sc_ui_control_destroy(sc_ui_control* control)
{
    if (!control)
        return;

    sc_ui_ctx* ctx = control->ctx;
    ui_remove_control_from_list(ctx, control);
    ui_backend_control_destroy(control);
    ui_free_items(control);
    free(control->text);
    free(control);
}

UI_API int sc_ui_control_get_kind(sc_ui_control* control)
{
    return control ? control->kind : 0;
}

UI_API int sc_ui_control_get_id(sc_ui_control* control)
{
    return control ? control->id : 0;
}

UI_API sc_ui_window* sc_ui_control_get_window(sc_ui_control* control)
{
    return control ? control->window : NULL;
}

UI_API sc_ui_control* sc_ui_first_control(sc_ui_ctx* ctx)
{
    return ctx ? ctx->controlsHead : NULL;
}

UI_API sc_ui_control* sc_ui_control_next(sc_ui_control* control)
{
    return control ? control->next : NULL;
}

/* ============================================================
 * 控件：几何
 * ============================================================ */

UI_API void sc_ui_control_get_frame(sc_ui_control* control, int* x, int* y, int* width, int* height)
{
    if (!control)
        return;

    if (x) *x = control->x;
    if (y) *y = control->y;
    if (width) *width = control->width;
    if (height) *height = control->height;
}

UI_API void sc_ui_control_set_frame(sc_ui_control* control, int x, int y, int width, int height)
{
    if (!control)
        return;

    control->x = x;
    control->y = y;
    control->width = width;
    control->height = height;
    ui_backend_control_set_frame(control);
}

UI_API int sc_ui_control_get_z(sc_ui_control* control)
{
    return control ? control->z : 0;
}

UI_API void sc_ui_control_set_z(sc_ui_control* control, int z)
{
    if (!control)
        return;

    control->z = z;
    ui_backend_control_set_z(control);
}

/* ============================================================
 * 控件：文本
 * ============================================================ */

UI_API const char* sc_ui_control_get_text(sc_ui_control* control)
{
    return control ? control->text : NULL;
}

UI_API void sc_ui_control_set_text(sc_ui_control* control, const char* text)
{
    if (!control)
        return;

    free(control->text);
    control->text = ui_strdup(text ? text : "");
    ui_backend_control_set_text(control);
}

/* ============================================================
 * 控件：勾选状态（checkbox/radiobox）
 * ============================================================ */

UI_API int sc_ui_control_get_checked(sc_ui_control* control)
{
    return control ? control->checked : 0;
}

UI_API void sc_ui_control_set_checked(sc_ui_control* control, int checked)
{
    if (!control)
        return;

    control->checked = checked ? 1 : 0;
    ui_backend_control_set_checked(control);
}

/* ============================================================
 * 控件：列表项（combo/list）
 * ============================================================ */

UI_API int sc_ui_control_set_items(sc_ui_control* control, const char** items, int count)
{
    if (!control || count < 0)
        return 0;

    ui_free_items(control);
    if (count == 0)
    {
        ui_backend_control_set_items(control);
        return 1;
    }

    control->items = (char**) calloc((size_t) count, sizeof(char*));
    if (!control->items)
        return 0;

    for (int i = 0; i < count; ++i)
    {
        control->items[i] = ui_strdup(items[i] ? items[i] : "");
        if (!control->items[i])
        {
            control->itemCount = i;
            ui_free_items(control);
            return 0;
        }
    }

    control->itemCount = count;
    control->selectedIndex = count > 0 ? 0 : -1;
    ui_backend_control_set_items(control);
    return 1;
}

UI_API int sc_ui_control_get_item_count(sc_ui_control* control)
{
    return control ? control->itemCount : 0;
}

UI_API const char* sc_ui_control_get_item(sc_ui_control* control, int index)
{
    if (!control || index < 0 || index >= control->itemCount)
        return NULL;

    return control->items[index];
}

UI_API int sc_ui_control_get_selected_index(sc_ui_control* control)
{
    return control ? control->selectedIndex : -1;
}

UI_API void sc_ui_control_set_selected_index(sc_ui_control* control, int index)
{
    if (!control)
        return;

    if (index < -1 || index >= control->itemCount)
        return;

    control->selectedIndex = index;
    ui_backend_control_set_selected_index(control);
}

/* ============================================================
 * 控件：事件回调
 * ============================================================ */

UI_API void sc_ui_control_set_callback(sc_ui_control* control, sc_ui_control_cb cb, void* user)
{
    if (!control)
        return;

    control->onEvent = cb;
    control->onEventUser = user;
}

/* ============================================================
 * 驱动 sink 实现（sc_ui_sink）
 * ============================================================ */

static void ui_window_sink_set_frame(void* target, int x, int y, int width, int height)
{
    sc_ui_window_set_frame((sc_ui_window*) target, x, y, width, height);
}

static void ui_window_sink_set_z(void* target, int z)
{
    sc_ui_window_set_z((sc_ui_window*) target, z);
}

static void ui_control_sink_set_frame(void* target, int x, int y, int width, int height)
{
    sc_ui_control_set_frame((sc_ui_control*) target, x, y, width, height);
}

static void ui_control_sink_set_z(void* target, int z)
{
    sc_ui_control_set_z((sc_ui_control*) target, z);
}

static const sc_ui_sink g_ui_window_sink = {
    ui_window_sink_set_frame,
    ui_window_sink_set_z
};

static const sc_ui_sink g_ui_control_sink = {
    ui_control_sink_set_frame,
    ui_control_sink_set_z
};

UI_API const sc_ui_sink* sc_ui_window_sink(void)
{
    return &g_ui_window_sink;
}

UI_API const sc_ui_sink* sc_ui_control_sink(void)
{
    return &g_ui_control_sink;
}
