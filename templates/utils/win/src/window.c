
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
void _glfwInputWindowFocus(_sc_window* window, GLFWbool focused)
{
    assert(window != NULL);
    assert(focused == GLFW_TRUE || focused == GLFW_FALSE);

    if (window->callbacks.focus)
        window->callbacks.focus((sc_window*) window, focused);

    if (!focused)
    {
        int key, button;

        for (key = 0;  key <= SC_KEY_LAST;  key++)
        {
            if (window->keys[key] == SC_PRESS)
            {
                const int scancode = _glfw.platform.getKeyScancode(key);
                _glfwInputKey(window, key, scancode, SC_RELEASE, 0);
            }
        }

        for (button = 0;  button <= SC_MOUSE_BUTTON_LAST;  button++)
        {
            if (window->mouseButtons[button] == SC_PRESS)
                _glfwInputMouseClick(window, button, SC_RELEASE, 0);
        }
    }
}

// Notifies shared code that a window has moved
// The position is specified in content area relative screen coordinates
//
void _glfwInputWindowPos(_sc_window* window, int x, int y)
{
    assert(window != NULL);

    if (window->callbacks.pos)
        window->callbacks.pos((sc_window*) window, x, y);
}

// Notifies shared code that a window has been resized
// The size is specified in screen coordinates
//
void _glfwInputWindowSize(_sc_window* window, int width, int height)
{
    assert(window != NULL);
    assert(width >= 0);
    assert(height >= 0);

    if (window->callbacks.size)
        window->callbacks.size((sc_window*) window, width, height);
}

// Notifies shared code that a window has been iconified or restored
//
void _glfwInputWindowIconify(_sc_window* window, GLFWbool iconified)
{
    assert(window != NULL);
    assert(iconified == GLFW_TRUE || iconified == GLFW_FALSE);

    if (window->callbacks.iconify)
        window->callbacks.iconify((sc_window*) window, iconified);
}

// Notifies shared code that a window has been maximized or restored
//
void _glfwInputWindowMaximize(_sc_window* window, GLFWbool maximized)
{
    assert(window != NULL);
    assert(maximized == GLFW_TRUE || maximized == GLFW_FALSE);

    if (window->callbacks.maximize)
        window->callbacks.maximize((sc_window*) window, maximized);
}

// Notifies shared code that a window content scale has changed
// The scale is specified as the ratio between the current and default DPI
//
void _glfwInputWindowContentScale(_sc_window* window, float xscale, float yscale)
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
void _glfwInputWindowDamage(_sc_window* window)
{
    assert(window != NULL);

    if (window->callbacks.refresh)
        window->callbacks.refresh((sc_window*) window);
}

// Notifies shared code that the user wishes to close a window
//
void _glfwInputWindowCloseRequest(_sc_window* window)
{
    assert(window != NULL);

    window->shouldClose = GLFW_TRUE;

    if (window->callbacks.close)
        window->callbacks.close((sc_window*) window);
}

// Notifies shared code that a window has changed its desired monitor
//
void _glfwInputWindowMonitor(_sc_window* window, _sc_monitor* monitor)
{
    assert(window != NULL);
    window->monitor = monitor;
}

//////////////////////////////////////////////////////////////////////////
//////                        GLFW public API                       //////
//////////////////////////////////////////////////////////////////////////

GLFWAPI sc_window* glfwCreateWindow(int width, int height,
                                     const char* title,
                                     sc_monitor* monitor,
                                     sc_window* share)
{
    _GLFWwndconfig wndconfig;
    _sc_window* window;

    assert(title != NULL);
    assert(width >= 0);
    assert(height >= 0);

    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    if (width <= 0 || height <= 0)
    {
        _glfwInputError(SC_WIN_ERR_INVALID_VALUE,
                        "Invalid window size %ix%i",
                        width, height);

        return NULL;
    }

    wndconfig = _glfw.hints.window;

    wndconfig.width  = width;
    wndconfig.height = height;

    window = _glfw_calloc(1, sizeof(_sc_window));
    window->next = _glfw.windowListHead;
    _glfw.windowListHead = window;

    window->videoMode.width  = width;
    window->videoMode.height = height;

    window->monitor          = (_sc_monitor*) monitor;
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
    window->title       = _glfw_strdup(title);

    if (!_glfw.platform.createWindow(window, &wndconfig))
    {
        glfwDestroyWindow((sc_window*) window);
        return NULL;
    }

    return (sc_window*) window;
}

void glfwDefaultWindowHints(void)
{
    _GLFW_REQUIRE_INIT();

    // The default is a focused, visible, resizable window with decorations
    memset(&_glfw.hints.window, 0, sizeof(_glfw.hints.window));
    _glfw.hints.window.resizable    = true;
    _glfw.hints.window.visible      = true;
    _glfw.hints.window.decorated    = true;
    _glfw.hints.window.focused      = true;
    _glfw.hints.window.autoIconify  = true;
    _glfw.hints.window.centerCursor = true;
    _glfw.hints.window.focusOnShow  = true;
    _glfw.hints.window.xpos         = SC_ANY_POSITION;
    _glfw.hints.window.ypos         = SC_ANY_POSITION;
}

GLFWAPI void glfwWindowHint(int hint, int value)
{
    _GLFW_REQUIRE_INIT();

    switch (hint)
    {
        case SC_WIN_RESIZABLE:
            _glfw.hints.window.resizable = value;
            return;
        case SC_WIN_DECORATED:
            _glfw.hints.window.decorated = value;
            return;
        case SC_WIN_FOCUSED:
            _glfw.hints.window.focused = value;
            return;
        case SC_WIN_AUTO_ICONIFY:
            _glfw.hints.window.autoIconify = value;
            return;
        case SC_WIN_FLOATING:
            _glfw.hints.window.floating = value;
            return;
        case SC_WIN_MAXIMIZED:
            _glfw.hints.window.maximized = value;
            return;
        case SC_WIN_VISIBLE:
            _glfw.hints.window.visible = value;
            return;
        case SC_WIN_POSITION_X:
            _glfw.hints.window.xpos = value;
            return;
        case SC_WIN_POSITION_Y:
            _glfw.hints.window.ypos = value;
            return;
        case GLFW_WIN32_KEYBOARD_MENU:
            _glfw.hints.window.win32.keymenu = value;
            return;
        case SC_WIN32_SHOWDEFAULT:
            _glfw.hints.window.win32.showDefault = value;
            return;
        case SC_SCALE_TO_MONITOR:
            _glfw.hints.window.scaleToMonitor = value;
            return;
        case SC_WIN_CENTER_CURSOR:
            _glfw.hints.window.centerCursor = value;
            return;
        case SC_WIN_FOCUS_ON_SHOW:
            _glfw.hints.window.focusOnShow = value;
            return;
        case SC_WIN_MOUSE_PASSTHROUGH:
            _glfw.hints.window.mousePassthrough = value;
            return;
    }

    _glfwInputError(SC_WIN_ERR_INVALID_ENUM, "Invalid window hint 0x%08X", hint);
}

GLFWAPI void glfwWindowHintString(int hint, const char* value)
{
    assert(value != NULL);

    _GLFW_REQUIRE_INIT();

    switch (hint)
    {
        case SC_COCOA_FRAME_NAME:
            strncpy(_glfw.hints.window.ns.frameName, value,
                    sizeof(_glfw.hints.window.ns.frameName) - 1);
            return;
        case SC_X11_CLASS_NAME:
            strncpy(_glfw.hints.window.x11.className, value,
                    sizeof(_glfw.hints.window.x11.className) - 1);
            return;
        case SC_X11_INSTANCE_NAME:
            strncpy(_glfw.hints.window.x11.instanceName, value,
                    sizeof(_glfw.hints.window.x11.instanceName) - 1);
            return;
        case SC_WAYLAND_APP_ID:
            strncpy(_glfw.hints.window.wl.appId, value,
                    sizeof(_glfw.hints.window.wl.appId) - 1);
            return;
    }

    _glfwInputError(SC_WIN_ERR_INVALID_ENUM, "Invalid window hint string 0x%08X", hint);
}

GLFWAPI void glfwDestroyWindow(sc_window* handle)
{
    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;

    // Allow closing of NULL (to match the behavior of free)
    if (window == NULL)
        return;

    // Clear all callbacks to avoid exposing a half torn-down window object
    memset(&window->callbacks, 0, sizeof(window->callbacks));

    _glfw.platform.destroyWindow(window);

    // Unlink window from global linked list
    {
        _sc_window** prev = &_glfw.windowListHead;

        while (*prev != window)
            prev = &((*prev)->next);

        *prev = window->next;
    }

    _glfw_free(window->title);
    _glfw_free(window);
}

GLFWAPI int glfwWindowShouldClose(sc_window* handle)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(0);

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    return window->shouldClose;
}

GLFWAPI void glfwSetWindowShouldClose(sc_window* handle, int value)
{
    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    window->shouldClose = value;
}

GLFWAPI const char* glfwGetWindowTitle(sc_window* handle)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    return window->title;
}

GLFWAPI void glfwSetWindowTitle(sc_window* handle, const char* title)
{
    assert(title != NULL);

    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    char* prev = window->title;
    window->title = _glfw_strdup(title);

    _glfw.platform.setWindowTitle(window, title);
    _glfw_free(prev);
}

GLFWAPI void glfwSetWindowIcon(sc_window* handle,
                               int count, const GLFWimage* images)
{
    int i;

    assert(count >= 0);
    assert(count == 0 || images != NULL);

    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    if (count < 0)
    {
        _glfwInputError(SC_WIN_ERR_INVALID_VALUE, "Invalid image count for window icon");
        return;
    }

    for (i = 0; i < count; i++)
    {
        assert(images[i].pixels != NULL);

        if (images[i].width <= 0 || images[i].height <= 0)
        {
            _glfwInputError(SC_WIN_ERR_INVALID_VALUE,
                            "Invalid image dimensions for window icon");
            return;
        }
    }

    _glfw.platform.setWindowIcon(window, count, images);
}

GLFWAPI void glfwGetWindowPos(sc_window* handle, int* xpos, int* ypos)
{
    if (xpos)
        *xpos = 0;
    if (ypos)
        *ypos = 0;

    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    _glfw.platform.getWindowPos(window, xpos, ypos);
}

GLFWAPI void glfwSetWindowPos(sc_window* handle, int xpos, int ypos)
{
    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    if (window->monitor)
        return;

    _glfw.platform.setWindowPos(window, xpos, ypos);
}

GLFWAPI void glfwGetWindowSize(sc_window* handle, int* width, int* height)
{
    if (width)
        *width = 0;
    if (height)
        *height = 0;

    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    _glfw.platform.getWindowSize(window, width, height);
}

GLFWAPI void glfwSetWindowSize(sc_window* handle, int width, int height)
{
    assert(width >= 0);
    assert(height >= 0);

    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    window->videoMode.width  = width;
    window->videoMode.height = height;

    _glfw.platform.setWindowSize(window, width, height);
}

GLFWAPI void glfwSetWindowSizeLimits(sc_window* handle,
                                     int minwidth, int minheight,
                                     int maxwidth, int maxheight)
{
    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    if (minwidth != SC_DONT_CARE && minheight != SC_DONT_CARE)
    {
        if (minwidth < 0 || minheight < 0)
        {
            _glfwInputError(SC_WIN_ERR_INVALID_VALUE,
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
            _glfwInputError(SC_WIN_ERR_INVALID_VALUE,
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

    _glfw.platform.setWindowSizeLimits(window,
                                       minwidth, minheight,
                                       maxwidth, maxheight);
}

GLFWAPI void glfwSetWindowAspectRatio(sc_window* handle, int numer, int denom)
{
    assert(numer != 0);
    assert(denom != 0);

    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    if (numer != SC_DONT_CARE && denom != SC_DONT_CARE)
    {
        if (numer <= 0 || denom <= 0)
        {
            _glfwInputError(SC_WIN_ERR_INVALID_VALUE,
                            "Invalid window aspect ratio %i:%i",
                            numer, denom);
            return;
        }
    }

    window->numer = numer;
    window->denom = denom;

    if (window->monitor || !window->resizable)
        return;

    _glfw.platform.setWindowAspectRatio(window, numer, denom);
}

GLFWAPI void glfwGetWindowFrameSize(sc_window* handle,
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

    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    _glfw.platform.getWindowFrameSize(window, left, top, right, bottom);
}

GLFWAPI void glfwGetWindowContentScale(sc_window* handle,
                                       float* xscale, float* yscale)
{
    if (xscale)
        *xscale = 0.f;
    if (yscale)
        *yscale = 0.f;

    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    _glfw.platform.getWindowContentScale(window, xscale, yscale);
}

GLFWAPI float glfwGetWindowOpacity(sc_window* handle)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(0.f);

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    return _glfw.platform.getWindowOpacity(window);
}

GLFWAPI void glfwSetWindowOpacity(sc_window* handle, float opacity)
{
    assert(isfinite(opacity));
    assert(opacity >= 0.f);
    assert(opacity <= 1.f);

    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    if (!isfinite(opacity) || opacity < 0.f || opacity > 1.f)
    {
        _glfwInputError(SC_WIN_ERR_INVALID_VALUE, "Invalid window opacity %f", opacity);
        return;
    }

    _glfw.platform.setWindowOpacity(window, opacity);
}

GLFWAPI void glfwIconifyWindow(sc_window* handle)
{
    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    _glfw.platform.iconifyWindow(window);
}

GLFWAPI void glfwRestoreWindow(sc_window* handle)
{
    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    _glfw.platform.restoreWindow(window);
}

GLFWAPI void glfwMaximizeWindow(sc_window* handle)
{
    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    if (window->monitor)
        return;

    _glfw.platform.maximizeWindow(window);
}

GLFWAPI void glfwShowWindow(sc_window* handle)
{
    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    if (window->monitor)
        return;

    _glfw.platform.showWindow(window);

    if (window->focusOnShow)
        _glfw.platform.focusWindow(window);
}

GLFWAPI void glfwRequestWindowAttention(sc_window* handle)
{
    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    _glfw.platform.requestWindowAttention(window);
}

GLFWAPI void glfwHideWindow(sc_window* handle)
{
    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    if (window->monitor)
        return;

    _glfw.platform.hideWindow(window);
}

GLFWAPI void glfwFocusWindow(sc_window* handle)
{
    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    _glfw.platform.focusWindow(window);
}

GLFWAPI int glfwGetWindowAttrib(sc_window* handle, int attrib)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(0);

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    switch (attrib)
    {
        case SC_WIN_FOCUSED:
            return _glfw.platform.windowFocused(window);
        case SC_WIN_ICONIFIED:
            return _glfw.platform.windowIconified(window);
        case SC_WIN_VISIBLE:
            return _glfw.platform.windowVisible(window);
        case SC_WIN_MAXIMIZED:
            return _glfw.platform.windowMaximized(window);
        case SC_WIN_HOVERED:
            return _glfw.platform.windowHovered(window);
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

    _glfwInputError(SC_WIN_ERR_INVALID_ENUM, "Invalid window attribute 0x%08X", attrib);
    return 0;
}

GLFWAPI void glfwSetWindowAttrib(sc_window* handle, int attrib, int value)
{
    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    value = value ? GLFW_TRUE : GLFW_FALSE;

    switch (attrib)
    {
        case SC_WIN_AUTO_ICONIFY:
            window->autoIconify = value;
            return;

        case SC_WIN_RESIZABLE:
            window->resizable = value;
            if (!window->monitor)
                _glfw.platform.setWindowResizable(window, value);
            return;

        case SC_WIN_DECORATED:
            window->decorated = value;
            if (!window->monitor)
                _glfw.platform.setWindowDecorated(window, value);
            return;

        case SC_WIN_FLOATING:
            window->floating = value;
            if (!window->monitor)
                _glfw.platform.setWindowFloating(window, value);
            return;

        case SC_WIN_FOCUS_ON_SHOW:
            window->focusOnShow = value;
            return;

        case SC_WIN_MOUSE_PASSTHROUGH:
            window->mousePassthrough = value;
            _glfw.platform.setWindowMousePassthrough(window, value);
            return;
    }

    _glfwInputError(SC_WIN_ERR_INVALID_ENUM, "Invalid window attribute 0x%08X", attrib);
}

GLFWAPI sc_monitor* glfwGetWindowMonitor(sc_window* handle)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    return (sc_monitor*) window->monitor;
}

GLFWAPI void glfwSetWindowMonitor(sc_window* wh,
                                  sc_monitor* mh,
                                  int xpos, int ypos,
                                  int width, int height,
                                  int refreshRate)
{
    assert(width >= 0);
    assert(height >= 0);

    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) wh;
    _sc_monitor* monitor = (_sc_monitor*) mh;
    assert(window != NULL);

    if (width <= 0 || height <= 0)
    {
        _glfwInputError(SC_WIN_ERR_INVALID_VALUE,
                        "Invalid window size %ix%i",
                        width, height);
        return;
    }

    if (refreshRate < 0 && refreshRate != SC_DONT_CARE)
    {
        _glfwInputError(SC_WIN_ERR_INVALID_VALUE,
                        "Invalid refresh rate %i",
                        refreshRate);
        return;
    }

    window->videoMode.width       = width;
    window->videoMode.height      = height;
    window->videoMode.refreshRate = refreshRate;

    _glfw.platform.setWindowMonitor(window, monitor,
                                    xpos, ypos, width, height,
                                    refreshRate);
}

GLFWAPI void glfwSetWindowUserPointer(sc_window* handle, void* pointer)
{
    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    window->userPointer = pointer;
}

GLFWAPI void* glfwGetWindowUserPointer(sc_window* handle)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    return window->userPointer;
}

GLFWAPI sc_win_pos_cb glfwSetWindowPosCallback(sc_window* handle,
                                                  sc_win_pos_cb cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    _GLFW_SWAP(sc_win_pos_cb, window->callbacks.pos, cbfun);
    return cbfun;
}

GLFWAPI sc_win_size_cb glfwSetWindowSizeCallback(sc_window* handle,
                                                    sc_win_size_cb cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    _GLFW_SWAP(sc_win_size_cb, window->callbacks.size, cbfun);
    return cbfun;
}

GLFWAPI sc_win_close_cb glfwSetWindowCloseCallback(sc_window* handle,
                                                      sc_win_close_cb cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    _GLFW_SWAP(sc_win_close_cb, window->callbacks.close, cbfun);
    return cbfun;
}

GLFWAPI sc_win_refresh_cb glfwSetWindowRefreshCallback(sc_window* handle,
                                                          sc_win_refresh_cb cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    _GLFW_SWAP(sc_win_refresh_cb, window->callbacks.refresh, cbfun);
    return cbfun;
}

GLFWAPI sc_win_focus_cb glfwSetWindowFocusCallback(sc_window* handle,
                                                      sc_win_focus_cb cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    _GLFW_SWAP(sc_win_focus_cb, window->callbacks.focus, cbfun);
    return cbfun;
}

GLFWAPI sc_win_iconify_cb glfwSetWindowIconifyCallback(sc_window* handle,
                                                          sc_win_iconify_cb cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    _GLFW_SWAP(sc_win_iconify_cb, window->callbacks.iconify, cbfun);
    return cbfun;
}

GLFWAPI sc_win_maximize_cb glfwSetWindowMaximizeCallback(sc_window* handle,
                                                            sc_win_maximize_cb cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    _GLFW_SWAP(sc_win_maximize_cb, window->callbacks.maximize, cbfun);
    return cbfun;
}

GLFWAPI sc_win_content_scale_cb glfwSetWindowContentScaleCallback(sc_window* handle,
                                                                    sc_win_content_scale_cb cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    _GLFW_SWAP(sc_win_content_scale_cb, window->callbacks.scale, cbfun);
    return cbfun;
}

GLFWAPI void glfwPollEvents(void)
{
    _GLFW_REQUIRE_INIT();
    _glfw.platform.pollEvents();
}

GLFWAPI void glfwWaitEvents(void)
{
    _GLFW_REQUIRE_INIT();
    _glfw.platform.waitEvents();
}

GLFWAPI void glfwWaitEventsTimeout(double timeout)
{
    _GLFW_REQUIRE_INIT();
    assert(isfinite(timeout));
    assert(timeout >= 0.0);

    if (!isfinite(timeout) || timeout < 0.0)
    {
        _glfwInputError(SC_WIN_ERR_INVALID_VALUE, "Invalid time %f", timeout);
        return;
    }

    _glfw.platform.waitEventsTimeout(timeout);
}

GLFWAPI void glfwPostEmptyEvent(void)
{
    _GLFW_REQUIRE_INIT();
    _glfw.platform.postEmptyEvent();
}

