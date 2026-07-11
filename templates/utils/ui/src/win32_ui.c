#include "ui_internal.h"

#if defined(SC_UI_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdlib.h>

/* ============================================================
 * ui win32 后端 —— 原生子窗口/控件实现
 * ============================================================
 * 契约见 src/ui_internal.h。约定：
 *   - sc_ui_window.backend  = HWND
 *       · root（无 parent）：直接复用 wsi 的窗口 HWND 作为控件容器，
 *         不新建、也不在销毁时 DestroyWindow（wsi 拥有其生命周期）。
 *       · 子窗口：新建 WS_CHILD 窗口，backend 持有该 HWND；
 *         同时把 nativeWindow=该 HWND、platform=SC_PLATFORM_WIN32，
 *         供 surface 模块消费。
 *   - sc_ui_control.backend = 控件 HWND（BUTTON/EDIT/STATIC/...）
 *   - 几何以窗口客户区左上原点为准（win32 子窗口坐标即左上原点，
 *     与 UI 语义一致，无需翻转）。
 *   - z-order 用窗口属性 SC_Z 暂存，restack 时按 z 升序把子窗口
 *     依次置顶（z 大者最终最靠前 = 最上层）。
 * ============================================================ */

static const WCHAR* SC_UI_CHILD_CLASS = L"SC_UI_ChildWindow";
static const WCHAR* SC_UI_Z_PROP = L"SC_UI_Z";
/* root（wsi 窗口）上暂存的子类信息：原始 WndProc 与所属 ui ctx，
 * 用于在 WM_DPICHANGED 时按新 DPI 重排控件（HiDPI 动态适配）。 */
static const WCHAR* SC_UI_OPROC_PROP = L"SC_UI_OProc";
static const WCHAR* SC_UI_CTX_PROP = L"SC_UI_Ctx";

/* ---- UTF-8 -> UTF-16，调用方负责 free；空串返回可用的空字符串 ---- */
static WCHAR* ui_win32_to_wide(const char* s)
{
    if (!s)
        s = "";

    int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (len <= 0)
        len = 1;

    WCHAR* w = (WCHAR*) malloc((size_t) len * sizeof(WCHAR));
    if (!w)
        return NULL;

    if (MultiByteToWideChar(CP_UTF8, 0, s, -1, w, len) <= 0)
        w[0] = L'\0';
    return w;
}

/* ---- 子窗口类：一次性注册，DefWindowProc + 擦除背景 ---- */
static LRESULT CALLBACK ui_win32_child_proc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProcW(hWnd, msg, wp, lp);
}

static void ui_win32_register_child_class(void)
{
    static int registered = 0;
    if (registered)
        return;
    registered = 1;

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ui_win32_child_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR) IDC_ARROW);
    wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
    wc.lpszClassName = SC_UI_CHILD_CLASS;
    RegisterClassExW(&wc);
}

/* ============================================================
 * HiDPI 缩放
 * ============================================================
 * 应用传入的 x/y/width/height 一律按 96-DPI 逻辑像素解释（与 macOS 的
 * point 语义对齐）。本进程为 Per-Monitor-V2 DPI 感知，客户区坐标是
 * 物理像素，故控件几何与字体都要按控件所在窗口的当前 DPI 做
 * 「逻辑 -> 物理」换算；窗口跨屏移动时（WM_DPICHANGED）动态重算重排。
 * ============================================================ */

/* 取窗口当前 DPI；GetDpiForWindow 需 Win10 1607，动态加载并回退。 */
static UINT ui_win32_dpi(HWND hwnd)
{
    typedef UINT (WINAPI *GetDpiForWindow_t)(HWND);
    static GetDpiForWindow_t p = NULL;
    static int resolved = 0;

    if (!resolved)
    {
        resolved = 1;
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32)
            p = (GetDpiForWindow_t) GetProcAddress(user32, "GetDpiForWindow");
    }

    if (p && hwnd)
    {
        UINT dpi = p(hwnd);
        if (dpi)
            return dpi;
    }

    /* 回退：系统 DC 的逻辑像素密度（不区分显示器）。 */
    HDC dc = GetDC(hwnd);
    UINT dpi = dc ? (UINT) GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc)
        ReleaseDC(hwnd, dc);
    return dpi ? dpi : 96;
}

/* 逻辑像素 -> 物理像素（按 96 基准）。 */
static int ui_win32_scale(int logical, UINT dpi)
{
    return MulDiv(logical, (int) dpi, 96);
}

/* 按 DPI 缓存缩放后的默认 GUI 字体（进程存活期内复用，DPI 档位有限）。 */
static HFONT ui_win32_font_for(UINT dpi)
{
    enum { CACHE_MAX = 8 };
    static UINT cachedDpi[CACHE_MAX];
    static HFONT cachedFont[CACHE_MAX];
    static int cacheCount = 0;

    for (int i = 0; i < cacheCount; ++i)
        if (cachedDpi[i] == dpi)
            return cachedFont[i];

    LOGFONTW lf;
    ZeroMemory(&lf, sizeof(lf));

    HFONT base = (HFONT) GetStockObject(DEFAULT_GUI_FONT);
    HFONT font = NULL;
    if (base && GetObjectW(base, sizeof(lf), &lf) == (int) sizeof(lf))
    {
        /* DEFAULT_GUI_FONT 按 96-DPI 设计，等比缩放其字高。 */
        lf.lfHeight = -MulDiv(abs((int) lf.lfHeight), (int) dpi, 96);
        lf.lfWidth = 0;
        font = CreateFontIndirectW(&lf);
    }
    if (!font)
        font = base;

    if (cacheCount < CACHE_MAX)
    {
        cachedDpi[cacheCount] = dpi;
        cachedFont[cacheCount] = font;
        ++cacheCount;
    }
    return font;
}

/* 给控件应用其所在容器 DPI 对应的缩放字体。 */
static void ui_win32_apply_font(HWND hwnd)
{
    HWND container = GetParent(hwnd);
    UINT dpi = ui_win32_dpi(container ? container : hwnd);
    SendMessageW(hwnd, WM_SETFONT, (WPARAM) ui_win32_font_for(dpi),
                 MAKELPARAM(TRUE, 0));
}


/* ============================================================
 * z-order 与 restack 辅助
 * ============================================================ */

static void ui_win32_set_z(HWND hwnd, int z)
{
    SetPropW(hwnd, SC_UI_Z_PROP, (HANDLE) (INT_PTR) z);
}

static int ui_win32_get_z(HWND hwnd)
{
    return (int) (INT_PTR) GetPropW(hwnd, SC_UI_Z_PROP);
}

/* 按 z 升序依次置顶直接子窗口：最后置顶者（z 最大）最靠前。 */
static void ui_win32_restack(HWND parent)
{
    if (!parent)
        return;

    enum { MAX_CHILDREN = 512 };
    HWND items[MAX_CHILDREN];
    int zs[MAX_CHILDREN];
    int n = 0;

    HWND child = GetWindow(parent, GW_CHILD);
    while (child && n < MAX_CHILDREN)
    {
        items[n] = child;
        zs[n] = ui_win32_get_z(child);
        ++n;
        child = GetWindow(child, GW_HWNDNEXT);
    }

    /* 稳定插入排序：按 z 升序。 */
    for (int i = 1; i < n; ++i)
    {
        HWND hw = items[i];
        int zv = zs[i];
        int j = i - 1;
        while (j >= 0 && zs[j] > zv)
        {
            items[j + 1] = items[j];
            zs[j + 1] = zs[j];
            --j;
        }
        items[j + 1] = hw;
        zs[j + 1] = zv;
    }

    /* 升序逐个置顶：z 最大者最后置顶 => 最靠前。 */
    for (int i = 0; i < n; ++i)
    {
        SetWindowPos(items[i], HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

/* ============================================================
 * HiDPI 动态重排：跨屏移动导致 DPI 变化时，按新 DPI 重设所有
 * 控件与子窗口的几何并刷新字体。
 * ============================================================ */

/* 递归重设子窗口树几何（几何本身由 set_frame 按当前 DPI 换算）。 */
static void ui_win32_relayout_windows(sc_ui_window* win)
{
    if (!win)
        return;

    for (sc_ui_window* c = win->firstChild; c; c = c->nextSibling)
    {
        ui_backend_window_set_frame(c);
        ui_win32_relayout_windows(c);
    }
}

/* 按当前 DPI 重排全部控件（几何 + 字体）与子窗口。 */
static void ui_win32_relayout(sc_ui_ctx* ctx)
{
    if (!ctx)
        return;

    for (sc_ui_control* c = ctx->controlsHead; c; c = c->next)
    {
        ui_backend_control_set_frame(c);
        if (c->backend)
            ui_win32_apply_font((HWND) c->backend);
    }

    ui_win32_relayout_windows(ctx->rootWindow);
}

/* root（wsi 窗口）子类过程：擦背景 + 拦截 WM_DPICHANGED/WM_SIZE 做动态适配，
 * 其余转发。
 *
 * 背景说明：wsi 的 WndProc 对 WM_ERASEBKGND 直接返回 TRUE（为渲染器避免闪烁，
 * 从不擦背景）。UI 复用该窗口作控件容器时，DPI 变化/尺寸变化后客户区不会被
 * 重绘，会露出旧内容的拉伸拷贝（子控件因自绘而正常）。故此处：
 *   1) 自行用系统对话框底色擦背景（仅作用于被子类化的 UI root）；
 *   2) DPI 变化重排后、尺寸变化后使整个客户区失效重绘。 */
static LRESULT CALLBACK ui_win32_root_proc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    WNDPROC orig = (WNDPROC) GetPropW(hWnd, SC_UI_OPROC_PROP);

    switch (msg)
    {
        case WM_ERASEBKGND:
        {
            HDC hdc = (HDC) wp;
            RECT rc;
            GetClientRect(hWnd, &rc);
            FillRect(hdc, &rc, (HBRUSH) (COLOR_BTNFACE + 1));
            return 1;
        }

        case WM_DPICHANGED:
        {
            /* 先让 wsi 原始过程按系统建议矩形缩放窗口本身，
             * 再据新 DPI 重排子控件——即窗口跨屏移动后的自动适配。 */
            LRESULT r = orig ? CallWindowProcW(orig, hWnd, msg, wp, lp)
                             : DefWindowProcW(hWnd, msg, wp, lp);

            sc_ui_ctx* ctx = (sc_ui_ctx*) GetPropW(hWnd, SC_UI_CTX_PROP);
            if (ctx)
                ui_win32_relayout(ctx);

            /* 缩放后客户区尺寸已变，强制擦背景重绘，避免旧位图拉伸残留。 */
            InvalidateRect(hWnd, NULL, TRUE);
            return r;
        }

        case WM_SIZE:
        {
            LRESULT r = orig ? CallWindowProcW(orig, hWnd, msg, wp, lp)
                             : DefWindowProcW(hWnd, msg, wp, lp);
            InvalidateRect(hWnd, NULL, TRUE);
            return r;
        }

        default:
            break;
    }

    if (orig)
        return CallWindowProcW(orig, hWnd, msg, wp, lp);
    return DefWindowProcW(hWnd, msg, wp, lp);
}

/* ============================================================
 * 子窗口 hook
 * ============================================================ */

void ui_backend_window_create(sc_ui_window* win)
{
    if (!win)
        return;

    /* 父 HWND：非 root 取 parent 容器；root 取 wsi 窗口句柄。 */
    HWND parent = win->parent ? (HWND) win->parent->backend
                              : (HWND) win->nativeWindow;
    if (!parent)
        return;

    if (!win->parent)
    {
        /* root：复用 wsi 窗口作为控件容器，不新建也不接管其生命周期。 */
        win->backend = parent;

        /* 安装 DPI 子类过程 + 记录 ctx：窗口跨屏移动时据新 DPI 重排控件。 */
        if (win->ctx && !GetPropW(parent, SC_UI_OPROC_PROP))
        {
            WNDPROC orig = (WNDPROC) GetWindowLongPtrW(parent, GWLP_WNDPROC);
            SetPropW(parent, SC_UI_OPROC_PROP, (HANDLE) orig);
            SetPropW(parent, SC_UI_CTX_PROP, (HANDLE) win->ctx);
            SetWindowLongPtrW(parent, GWLP_WNDPROC, (LONG_PTR) ui_win32_root_proc);
        }
        return;
    }

    ui_win32_register_child_class();

    UINT dpi = ui_win32_dpi(parent);
    HWND view = CreateWindowExW(
        0, SC_UI_CHILD_CLASS, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        ui_win32_scale(win->x, dpi), ui_win32_scale(win->y, dpi),
        ui_win32_scale(win->width, dpi), ui_win32_scale(win->height, dpi),
        parent, NULL, GetModuleHandleW(NULL), NULL);
    if (!view)
        return;

    win->backend = view;

    /* 子窗口自身即可作为 surface backing。 */
    win->nativeWindow = view;
    win->platform = SC_PLATFORM_WIN32;

    ui_win32_set_z(view, win->z);
    ui_win32_restack(parent);
}

void ui_backend_window_destroy(sc_ui_window* win)
{
    if (!win)
        return;

    HWND view = (HWND) win->backend;
    /* root 的 backend 是 wsi 窗口，不销毁。 */
    if (view && win->parent)
    {
        RemovePropW(view, SC_UI_Z_PROP);
        DestroyWindow(view);
    }
    else if (view && !win->parent)
    {
        /* 卸载 DPI 子类过程，恢复 wsi 原始 WndProc（wsi 仍持有该窗口）。 */
        WNDPROC orig = (WNDPROC) GetPropW(view, SC_UI_OPROC_PROP);
        if (orig)
            SetWindowLongPtrW(view, GWLP_WNDPROC, (LONG_PTR) orig);
        RemovePropW(view, SC_UI_OPROC_PROP);
        RemovePropW(view, SC_UI_CTX_PROP);
    }
    win->backend = NULL;
}

void ui_backend_window_set_frame(sc_ui_window* win)
{
    if (!win)
        return;

    HWND view = (HWND) win->backend;
    /* root 跟随 wsi 窗口，不由 ui 移动。 */
    if (view && win->parent)
    {
        UINT dpi = ui_win32_dpi(GetParent(view));
        MoveWindow(view,
                   ui_win32_scale(win->x, dpi), ui_win32_scale(win->y, dpi),
                   ui_win32_scale(win->width, dpi), ui_win32_scale(win->height, dpi),
                   TRUE);
    }
}

void ui_backend_window_set_z(sc_ui_window* win)
{
    if (!win || !win->parent)
        return;

    HWND view = (HWND) win->backend;
    if (!view)
        return;

    ui_win32_set_z(view, win->z);
    ui_win32_restack(GetParent(view));
}

/* ============================================================
 * 控件 hook
 * ============================================================ */

void ui_backend_control_create(sc_ui_control* control)
{
    if (!control || !control->window)
        return;

    HWND parent = (HWND) control->window->backend;
    if (!parent)
        return;

    const WCHAR* cls = NULL;
    DWORD style = WS_CHILD | WS_VISIBLE;

    switch (control->kind)
    {
        case SC_UI_LABEL:
            cls = L"STATIC";
            style |= SS_LEFT;
            break;
        case SC_UI_EDIT:
            cls = L"EDIT";
            style |= WS_BORDER | ES_AUTOHSCROLL;
            break;
        case SC_UI_TEXT:
            cls = L"EDIT";
            style |= WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN;
            break;
        case SC_UI_BUTTON:
            cls = L"BUTTON";
            style |= BS_PUSHBUTTON | WS_TABSTOP;
            break;
        case SC_UI_CHECKBOX:
            cls = L"BUTTON";
            style |= BS_AUTOCHECKBOX | WS_TABSTOP;
            break;
        case SC_UI_RADIOBOX:
            cls = L"BUTTON";
            style |= BS_AUTORADIOBUTTON | WS_TABSTOP;
            break;
        case SC_UI_COMBO:
            cls = L"COMBOBOX";
            style |= CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP;
            break;
        case SC_UI_LIST:
            cls = L"LISTBOX";
            style |= WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS;
            break;
        default:
            return;
    }

    WCHAR* wtext = ui_win32_to_wide(control->text);

    UINT dpi = ui_win32_dpi(parent);
    HWND hwnd = CreateWindowExW(
        0, cls, (wtext ? wtext : L""), style,
        ui_win32_scale(control->x, dpi), ui_win32_scale(control->y, dpi),
        ui_win32_scale(control->width, dpi), ui_win32_scale(control->height, dpi),
        parent, NULL, GetModuleHandleW(NULL), NULL);

    free(wtext);

    if (!hwnd)
        return;

    control->backend = hwnd;
    ui_win32_apply_font(hwnd);

    /* 初始属性下发。 */
    if (control->kind == SC_UI_CHECKBOX || control->kind == SC_UI_RADIOBOX)
    {
        SendMessageW(hwnd, BM_SETCHECK,
                     (WPARAM) (control->checked ? BST_CHECKED : BST_UNCHECKED), 0);
    }

    if (control->kind == SC_UI_COMBO || control->kind == SC_UI_LIST)
    {
        ui_backend_control_set_items(control);
        ui_backend_control_set_selected_index(control);
    }

    ui_win32_set_z(hwnd, control->z);
    ui_win32_restack(parent);
}

void ui_backend_control_destroy(sc_ui_control* control)
{
    if (!control)
        return;

    HWND hwnd = (HWND) control->backend;
    if (hwnd)
    {
        RemovePropW(hwnd, SC_UI_Z_PROP);
        DestroyWindow(hwnd);
        control->backend = NULL;
    }
}

void ui_backend_control_set_frame(sc_ui_control* control)
{
    if (!control)
        return;

    HWND hwnd = (HWND) control->backend;
    if (hwnd)
    {
        UINT dpi = ui_win32_dpi(GetParent(hwnd));
        MoveWindow(hwnd,
                   ui_win32_scale(control->x, dpi), ui_win32_scale(control->y, dpi),
                   ui_win32_scale(control->width, dpi), ui_win32_scale(control->height, dpi),
                   TRUE);
    }
}

void ui_backend_control_set_z(sc_ui_control* control)
{
    if (!control)
        return;

    HWND hwnd = (HWND) control->backend;
    if (!hwnd)
        return;

    ui_win32_set_z(hwnd, control->z);
    ui_win32_restack(GetParent(hwnd));
}

void ui_backend_control_set_text(sc_ui_control* control)
{
    if (!control || !control->backend)
        return;

    switch (control->kind)
    {
        case SC_UI_LABEL:
        case SC_UI_EDIT:
        case SC_UI_TEXT:
        case SC_UI_BUTTON:
        case SC_UI_CHECKBOX:
        case SC_UI_RADIOBOX:
        {
            WCHAR* w = ui_win32_to_wide(control->text);
            if (w)
            {
                SetWindowTextW((HWND) control->backend, w);
                free(w);
            }
            break;
        }
        default:
            break;
    }
}

void ui_backend_control_set_checked(sc_ui_control* control)
{
    if (!control || !control->backend)
        return;

    if (control->kind == SC_UI_CHECKBOX || control->kind == SC_UI_RADIOBOX)
    {
        SendMessageW((HWND) control->backend, BM_SETCHECK,
                     (WPARAM) (control->checked ? BST_CHECKED : BST_UNCHECKED), 0);
    }
}

void ui_backend_control_set_items(sc_ui_control* control)
{
    if (!control || !control->backend)
        return;

    HWND hwnd = (HWND) control->backend;
    UINT resetMsg;
    UINT addMsg;

    if (control->kind == SC_UI_COMBO)
    {
        resetMsg = CB_RESETCONTENT;
        addMsg = CB_ADDSTRING;
    }
    else if (control->kind == SC_UI_LIST)
    {
        resetMsg = LB_RESETCONTENT;
        addMsg = LB_ADDSTRING;
    }
    else
    {
        return;
    }

    SendMessageW(hwnd, resetMsg, 0, 0);
    for (int i = 0; i < control->itemCount; ++i)
    {
        WCHAR* w = ui_win32_to_wide(control->items ? control->items[i] : "");
        if (w)
        {
            SendMessageW(hwnd, addMsg, 0, (LPARAM) w);
            free(w);
        }
    }

    ui_backend_control_set_selected_index(control);
}

void ui_backend_control_set_selected_index(sc_ui_control* control)
{
    if (!control || !control->backend)
        return;

    HWND hwnd = (HWND) control->backend;
    int idx = control->selectedIndex;
    if (idx < 0 || idx >= control->itemCount)
        idx = -1;

    if (control->kind == SC_UI_COMBO)
        SendMessageW(hwnd, CB_SETCURSEL, (WPARAM) idx, 0);
    else if (control->kind == SC_UI_LIST)
        SendMessageW(hwnd, LB_SETCURSEL, (WPARAM) idx, 0);
}

int ui_backend_set_font(sc_ui_ctx* ctx, const char* path, float size)
{
    /* Win32 后端使用系统原生控件，已支持 CJK，无需自行加载字体。 */
    (void) ctx; (void) path; (void) size;
    return 0;
}

#endif /* SC_UI_WIN32 */
