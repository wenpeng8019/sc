
#include "internal.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <float.h>
#include <math.h>

//////////////////////////////////////////////////////////////////////////
//////                         GLFW event API                       //////
//////////////////////////////////////////////////////////////////////////

// Notifies shared code that a window has lost or received input focus
//
void impl_on_win_focus(window_st* window, bool focused)
{
    assert(window != NULL);
    assert(focused == true || focused == false);

    if (window->callbacks.focus)
        window->callbacks.focus((sc_window*) window, focused);

    if (!focused)
    {
        int key, button;

        for (key = 0;  key <= SC_KEY_LAST;  key++)
        {
            if (window->keys[key] == SC_PRESS)
            {
                const int scancode = g_wsi.platform.getKeyScancode(key);
                impl_on_key(window, key, scancode, SC_RELEASE, 0);
            }
        }

        for (button = 0;  button <= SC_MOUSE_BUTTON_LAST;  button++)
        {
            if (window->mouseButtons[button] == SC_PRESS)
                impl_on_mouse_click(window, button, SC_RELEASE, 0);
        }
    }
}

// Notifies shared code that a window has moved
// The position is specified in content area relative screen coordinates
//
void impl_on_win_pos(window_st* window, int x, int y)
{
    assert(window != NULL);

    if (window->callbacks.pos)
        window->callbacks.pos((sc_window*) window, x, y);
}

// Notifies shared code that a window has been resized
// The size is specified in screen coordinates
//
void impl_on_win_size(window_st* window, int width, int height)
{
    assert(window != NULL);
    assert(width >= 0);
    assert(height >= 0);

    if (window->callbacks.size)
        window->callbacks.size((sc_window*) window, width, height);
}

// Notifies shared code that a window has been iconified or restored
//
void impl_on_win_iconify(window_st* window, bool iconified)
{
    assert(window != NULL);
    assert(iconified == true || iconified == false);

    if (window->callbacks.iconify)
        window->callbacks.iconify((sc_window*) window, iconified);
}

// Notifies shared code that a window has been maximized or restored
//
void impl_on_win_maximize(window_st* window, bool maximized)
{
    assert(window != NULL);
    assert(maximized == true || maximized == false);

    if (window->callbacks.maximize)
        window->callbacks.maximize((sc_window*) window, maximized);
}

// Notifies shared code that a window content scale has changed
// The scale is specified as the ratio between the current and default DPI
//
void impl_on_win_content_scale(window_st* window, float xscale, float yscale)
{
    assert(window != NULL);
    assert(xscale > 0.f);
    assert(isfinite(xscale));
    assert(yscale > 0.f);
    assert(isfinite(yscale));

    if (window->callbacks.scale)
        window->callbacks.scale((sc_window*) window, xscale, yscale);
}

// Notifies shared code that the window contents needs updating
//
void impl_on_win_damage(window_st* window)
{
    assert(window != NULL);

    if (window->callbacks.refresh)
        window->callbacks.refresh((sc_window*) window);
}

// Notifies shared code that the user wishes to close a window
//
void impl_on_win_close_req(window_st* window)
{
    assert(window != NULL);

    window->shouldClose = true;

    if (window->callbacks.close)
        window->callbacks.close((sc_window*) window);
}

// Notifies shared code that a window has changed its desired monitor
//
void impl_on_win_monitor(window_st* window, monitor_st* monitor)
{
    assert(window != NULL);
    window->monitor = monitor;
}

//////////////////////////////////////////////////////////////////////////
//////                        GLFW public API                       //////
//////////////////////////////////////////////////////////////////////////

WSI_API sc_window* sc_wsi_win_create(int width, int height,
                                     const char* title,
                                     sc_monitor* monitor,
                                     sc_window* share)
{
    wnd_config_st wndconfig;
    window_st* window;

    assert(title != NULL);
    assert(width >= 0);
    assert(height >= 0);

    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }

    if (width <= 0 || height <= 0)
    {
        impl_on_error(SC_WSI_ERR_INVALID_VALUE,
                        "Invalid window size %ix%i",
                        width, height);

        return NULL;
    }

    wndconfig = g_wsi.hints.window;

    wndconfig.width  = width;
    wndconfig.height = height;

    window = wsi_calloc(1, sizeof(window_st));
    window->next = g_wsi.windowListHead;
    g_wsi.windowListHead = window;

    window->videoMode.width  = width;
    window->videoMode.height = height;

    window->monitor          = (monitor_st*) monitor;
    window->resizable        = wndconfig.resizable;
    window->decorated        = wndconfig.decorated;
    window->autoIconify      = wndconfig.autoIconify;
    window->floating         = wndconfig.floating;
    window->focusOnShow      = wndconfig.focusOnShow;
    window->mousePassthrough = wndconfig.mousePassthrough;
    window->cursorMode       = SC_CURSOR_NORMAL;

    window->minwidth    = SC_DONT_CARE;
    window->minheight   = SC_DONT_CARE;
    window->maxwidth    = SC_DONT_CARE;
    window->maxheight   = SC_DONT_CARE;
    window->numer       = SC_DONT_CARE;
    window->denom       = SC_DONT_CARE;
    window->title       = wsi_strdup(title);

    if (!g_wsi.platform.createWindow(window, &wndconfig))
    {
        sc_wsi_win_destroy((sc_window*) window);
        return NULL;
    }

    return (sc_window*) window;
}

void sc_wsi_default_window_hints(void)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    // The default is a focused, visible, resizable window with decorations
    memset(&g_wsi.hints.window, 0, sizeof(g_wsi.hints.window));
    g_wsi.hints.window.resizable    = true;
    g_wsi.hints.window.visible      = true;
    g_wsi.hints.window.decorated    = true;
    g_wsi.hints.window.focused      = true;
    g_wsi.hints.window.autoIconify  = true;
    g_wsi.hints.window.centerCursor = true;
    g_wsi.hints.window.focusOnShow  = true;
    g_wsi.hints.window.xpos         = SC_ANY_POSITION;
    g_wsi.hints.window.ypos         = SC_ANY_POSITION;
}

WSI_API void sc_wsi_window_hint(int hint, int value)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    switch (hint)
    {
        case SC_WIN_RESIZABLE:
            g_wsi.hints.window.resizable = value;
            return;
        case SC_WIN_DECORATED:
            g_wsi.hints.window.decorated = value;
            return;
        case SC_WIN_FOCUSED:
            g_wsi.hints.window.focused = value;
            return;
        case SC_WIN_AUTO_ICONIFY:
            g_wsi.hints.window.autoIconify = value;
            return;
        case SC_WIN_FLOATING:
            g_wsi.hints.window.floating = value;
            return;
        case SC_WIN_MAXIMIZED:
            g_wsi.hints.window.maximized = value;
            return;
        case SC_WIN_VISIBLE:
            g_wsi.hints.window.visible = value;
            return;
        case SC_WIN_POSITION_X:
            g_wsi.hints.window.xpos = value;
            return;
        case SC_WIN_POSITION_Y:
            g_wsi.hints.window.ypos = value;
            return;
        case GLFW_WIN32_KEYBOARD_MENU:
            g_wsi.hints.window.win32.keymenu = value;
            return;
        case SC_WIN32_SHOWDEFAULT:
            g_wsi.hints.window.win32.showDefault = value;
            return;
        case SC_SCALE_TO_MONITOR:
            g_wsi.hints.window.scaleToMonitor = value;
            return;
        case SC_WIN_CENTER_CURSOR:
            g_wsi.hints.window.centerCursor = value;
            return;
        case SC_WIN_FOCUS_ON_SHOW:
            g_wsi.hints.window.focusOnShow = value;
            return;
        case SC_WIN_MOUSE_PASSTHROUGH:
            g_wsi.hints.window.mousePassthrough = value;
            return;
    }

    impl_on_error(SC_WSI_ERR_INVALID_ENUM, "Invalid window hint 0x%08X", hint);
}

WSI_API void sc_wsi_window_hint_string(int hint, const char* value)
{
    assert(value != NULL);

    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    switch (hint)
    {
        case SC_COCOA_FRAME_NAME:
            strncpy(g_wsi.hints.window.ns.frameName, value,
                    sizeof(g_wsi.hints.window.ns.frameName) - 1);
            return;
        case SC_X11_CLASS_NAME:
            strncpy(g_wsi.hints.window.x11.className, value,
                    sizeof(g_wsi.hints.window.x11.className) - 1);
            return;
        case SC_X11_INSTANCE_NAME:
            strncpy(g_wsi.hints.window.x11.instanceName, value,
                    sizeof(g_wsi.hints.window.x11.instanceName) - 1);
            return;
        case SC_WAYLAND_APP_ID:
            strncpy(g_wsi.hints.window.wl.appId, value,
                    sizeof(g_wsi.hints.window.wl.appId) - 1);
            return;
    }

    impl_on_error(SC_WSI_ERR_INVALID_ENUM, "Invalid window hint string 0x%08X", hint);
}

WSI_API void sc_wsi_win_destroy(sc_window* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) handle;

    // Allow closing of NULL (to match the behavior of free)
    if (window == NULL)
        return;

    // Clear all callbacks to avoid exposing a half torn-down window object
    memset(&window->callbacks, 0, sizeof(window->callbacks));

    g_wsi.platform.destroyWindow(window);

    // Unlink window from global linked list
    {
        window_st** prev = &g_wsi.windowListHead;

        while (*prev != window)
            prev = &((*prev)->next);

        *prev = window->next;
    }

    wsi_free(window->title);
    wsi_free(window);
}

WSI_API int sc_wsi_win_get_should_close(sc_window* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return 0;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    return window->shouldClose;
}

WSI_API void sc_wsi_win_set_should_close(sc_window* handle, int value)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    window->shouldClose = value;
}

WSI_API const char* sc_wsi_win_get_title(sc_window* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    return window->title;
}

WSI_API void sc_wsi_win_set_title(sc_window* handle, const char* title)
{
    assert(title != NULL);

    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    char* prev = window->title;
    window->title = wsi_strdup(title);

    g_wsi.platform.setWindowTitle(window, title);
    wsi_free(prev);
}

WSI_API void sc_wsi_win_set_icon(sc_window* handle,
                               int count, const GLFWimage* images)
{
    int i;

    assert(count >= 0);
    assert(count == 0 || images != NULL);

    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    if (count < 0)
    {
        impl_on_error(SC_WSI_ERR_INVALID_VALUE, "Invalid image count for window icon");
        return;
    }

    for (i = 0; i < count; i++)
    {
        assert(images[i].pixels != NULL);

        if (images[i].width <= 0 || images[i].height <= 0)
        {
            impl_on_error(SC_WSI_ERR_INVALID_VALUE,
                            "Invalid image dimensions for window icon");
            return;
        }
    }

    g_wsi.platform.setWindowIcon(window, count, images);
}

WSI_API void sc_wsi_win_get_pos(sc_window* handle, int* xpos, int* ypos)
{
    if (xpos)
        *xpos = 0;
    if (ypos)
        *ypos = 0;

    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    g_wsi.platform.getWindowPos(window, xpos, ypos);
}

WSI_API void sc_wsi_win_set_pos(sc_window* handle, int xpos, int ypos)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    if (window->monitor)
        return;

    g_wsi.platform.setWindowPos(window, xpos, ypos);
}

WSI_API void sc_wsi_win_get_size(sc_window* handle, int* width, int* height)
{
    if (width)
        *width = 0;
    if (height)
        *height = 0;

    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    g_wsi.platform.getWindowSize(window, width, height);
}

WSI_API void sc_wsi_win_set_size(sc_window* handle, int width, int height)
{
    assert(width >= 0);
    assert(height >= 0);

    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    window->videoMode.width  = width;
    window->videoMode.height = height;

    g_wsi.platform.setWindowSize(window, width, height);
}

WSI_API void sc_wsi_win_set_size_limits(sc_window* handle,
                                     int minwidth, int minheight,
                                     int maxwidth, int maxheight)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    if (minwidth != SC_DONT_CARE && minheight != SC_DONT_CARE)
    {
        if (minwidth < 0 || minheight < 0)
        {
            impl_on_error(SC_WSI_ERR_INVALID_VALUE,
                            "Invalid window minimum size %ix%i",
                            minwidth, minheight);
            return;
        }
    }

    if (maxwidth != SC_DONT_CARE && maxheight != SC_DONT_CARE)
    {
        if (maxwidth < 0 || maxheight < 0 ||
            maxwidth < minwidth || maxheight < minheight)
        {
            impl_on_error(SC_WSI_ERR_INVALID_VALUE,
                            "Invalid window maximum size %ix%i",
                            maxwidth, maxheight);
            return;
        }
    }

    window->minwidth  = minwidth;
    window->minheight = minheight;
    window->maxwidth  = maxwidth;
    window->maxheight = maxheight;

    if (window->monitor || !window->resizable)
        return;

    g_wsi.platform.setWindowSizeLimits(window,
                                       minwidth, minheight,
                                       maxwidth, maxheight);
}

WSI_API void sc_wsi_win_set_size_aspect_ratio(sc_window* handle, int numer, int denom)
{
    assert(numer != 0);
    assert(denom != 0);

    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    if (numer != SC_DONT_CARE && denom != SC_DONT_CARE)
    {
        if (numer <= 0 || denom <= 0)
        {
            impl_on_error(SC_WSI_ERR_INVALID_VALUE,
                            "Invalid window aspect ratio %i:%i",
                            numer, denom);
            return;
        }
    }

    window->numer = numer;
    window->denom = denom;

    if (window->monitor || !window->resizable)
        return;

    g_wsi.platform.setWindowAspectRatio(window, numer, denom);
}

WSI_API void sc_wsi_win_get_frame_size(sc_window* handle,
                                    int* left, int* top,
                                    int* right, int* bottom)
{
    if (left)
        *left = 0;
    if (top)
        *top = 0;
    if (right)
        *right = 0;
    if (bottom)
        *bottom = 0;

    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    g_wsi.platform.getWindowFrameSize(window, left, top, right, bottom);
}

WSI_API void sc_wsi_win_get_content_scale(sc_window* handle,
                                       float* xscale, float* yscale)
{
    if (xscale)
        *xscale = 0.f;
    if (yscale)
        *yscale = 0.f;

    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    g_wsi.platform.getWindowContentScale(window, xscale, yscale);
}

WSI_API float sc_wsi_win_get_opacity(sc_window* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return 0.f;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    return g_wsi.platform.getWindowOpacity(window);
}

WSI_API void sc_wsi_win_set_opacity(sc_window* handle, float opacity)
{
    assert(isfinite(opacity));
    assert(opacity >= 0.f);
    assert(opacity <= 1.f);

    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    if (!isfinite(opacity) || opacity < 0.f || opacity > 1.f)
    {
        impl_on_error(SC_WSI_ERR_INVALID_VALUE, "Invalid window opacity %f", opacity);
        return;
    }

    g_wsi.platform.setWindowOpacity(window, opacity);
}

WSI_API void sc_wsi_win_iconify(sc_window* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    g_wsi.platform.iconifyWindow(window);
}

WSI_API void sc_wsi_win_restore(sc_window* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    g_wsi.platform.restoreWindow(window);
}

WSI_API void sc_wsi_win_maximize(sc_window* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    if (window->monitor)
        return;

    g_wsi.platform.maximizeWindow(window);
}

WSI_API void sc_wsi_win_show(sc_window* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    if (window->monitor)
        return;

    g_wsi.platform.showWindow(window);

    if (window->focusOnShow)
        g_wsi.platform.focusWindow(window);
}

WSI_API void sc_wsi_win_request_attention(sc_window* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    g_wsi.platform.requestWindowAttention(window);
}

WSI_API void sc_wsi_win_hide(sc_window* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    if (window->monitor)
        return;

    g_wsi.platform.hideWindow(window);
}

WSI_API void sc_wsi_win_focus(sc_window* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    g_wsi.platform.focusWindow(window);
}

WSI_API int sc_wsi_win_get_attrib(sc_window* handle, int attrib)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return 0;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    switch (attrib)
    {
        case SC_WIN_FOCUSED:
            return g_wsi.platform.windowFocused(window);
        case SC_WIN_ICONIFIED:
            return g_wsi.platform.windowIconified(window);
        case SC_WIN_VISIBLE:
            return g_wsi.platform.windowVisible(window);
        case SC_WIN_MAXIMIZED:
            return g_wsi.platform.windowMaximized(window);
        case SC_WIN_HOVERED:
            return g_wsi.platform.windowHovered(window);
        case SC_WIN_FOCUS_ON_SHOW:
            return window->focusOnShow;
        case SC_WIN_MOUSE_PASSTHROUGH:
            return window->mousePassthrough;
        case SC_WIN_RESIZABLE:
            return window->resizable;
        case SC_WIN_DECORATED:
            return window->decorated;
        case SC_WIN_FLOATING:
            return window->floating;
        case SC_WIN_AUTO_ICONIFY:
            return window->autoIconify;
    }

    impl_on_error(SC_WSI_ERR_INVALID_ENUM, "Invalid window attribute 0x%08X", attrib);
    return 0;
}

WSI_API void sc_wsi_win_set_attrib(sc_window* handle, int attrib, int value)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    value = value ? true : false;

    switch (attrib)
    {
        case SC_WIN_AUTO_ICONIFY:
            window->autoIconify = value;
            return;

        case SC_WIN_RESIZABLE:
            window->resizable = value;
            if (!window->monitor)
                g_wsi.platform.setWindowResizable(window, value);
            return;

        case SC_WIN_DECORATED:
            window->decorated = value;
            if (!window->monitor)
                g_wsi.platform.setWindowDecorated(window, value);
            return;

        case SC_WIN_FLOATING:
            window->floating = value;
            if (!window->monitor)
                g_wsi.platform.setWindowFloating(window, value);
            return;

        case SC_WIN_FOCUS_ON_SHOW:
            window->focusOnShow = value;
            return;

        case SC_WIN_MOUSE_PASSTHROUGH:
            window->mousePassthrough = value;
            g_wsi.platform.setWindowMousePassthrough(window, value);
            return;
    }

    impl_on_error(SC_WSI_ERR_INVALID_ENUM, "Invalid window attribute 0x%08X", attrib);
}

WSI_API sc_monitor* sc_wsi_win_get_monitor(sc_window* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    return (sc_monitor*) window->monitor;
}

WSI_API void sc_wsi_win_set_monitor(sc_window* wh,
                                  sc_monitor* mh,
                                  int xpos, int ypos,
                                  int width, int height,
                                  int refreshRate)
{
    assert(width >= 0);
    assert(height >= 0);

    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) wh;
    monitor_st* monitor = (monitor_st*) mh;
    assert(window != NULL);

    if (width <= 0 || height <= 0)
    {
        impl_on_error(SC_WSI_ERR_INVALID_VALUE,
                        "Invalid window size %ix%i",
                        width, height);
        return;
    }

    if (refreshRate < 0 && refreshRate != SC_DONT_CARE)
    {
        impl_on_error(SC_WSI_ERR_INVALID_VALUE,
                        "Invalid refresh rate %i",
                        refreshRate);
        return;
    }

    window->videoMode.width       = width;
    window->videoMode.height      = height;
    window->videoMode.refreshRate = refreshRate;

    g_wsi.platform.setWindowMonitor(window, monitor,
                                    xpos, ypos, width, height,
                                    refreshRate);
}

WSI_API void sc_wsi_win_set_user_data(sc_window* handle, void* pointer)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    window->userPointer = pointer;
}

WSI_API void* sc_wsi_win_get_user_data(sc_window* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    return window->userPointer;
}

WSI_API bool sc_wsi_win_set_callback(sc_window* handle, sc_wsi_win_cb cb) {

    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return false;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    memcpy(&window->callbacks, &cb, sizeof(cb));
    return true;
}


WSI_API void sc_wsi_poll_events(void)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }
    g_wsi.platform.pollEvents();
}

WSI_API void sc_wsi_wait_events(void)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }
    g_wsi.platform.waitEvents();
}

WSI_API void sc_wsi_wait_events_timeout(double timeout)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }
    assert(isfinite(timeout));
    assert(timeout >= 0.0);

    if (!isfinite(timeout) || timeout < 0.0)
    {
        impl_on_error(SC_WSI_ERR_INVALID_VALUE, "Invalid time %f", timeout);
        return;
    }

    g_wsi.platform.waitEventsTimeout(timeout);
}

WSI_API void sc_wsi_post_empty_event(void)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }
    g_wsi.platform.postEmptyEvent();
}

