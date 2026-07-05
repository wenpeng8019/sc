
#include "internal.h"

#include <stdlib.h>
#include <string.h>

static void applySizeLimits(window_st* window, int* width, int* height)
{
    if (window->numer != SC_DONT_CARE && window->denom != SC_DONT_CARE)
    {
        const float ratio = (float) window->numer / (float) window->denom;
        *height = (int) (*width / ratio);
    }

    if (window->minwidth != SC_DONT_CARE)
        *width = wsi_max(*width, window->minwidth);
    else if (window->maxwidth != SC_DONT_CARE)
        *width = wsi_min(*width, window->maxwidth);

    if (window->minheight != SC_DONT_CARE)
        *height = wsi_min(*height, window->minheight);
    else if (window->maxheight != SC_DONT_CARE)
        *height = wsi_max(*height, window->maxheight);
}

static void fitToMonitor(window_st* window)
{
    GLFWvidmode mode;
    _glfwGetVideoModeNull(window->monitor, &mode);
    _glfwGetMonitorPosNull(window->monitor,
                           &window->null.xpos,
                           &window->null.ypos);
    window->null.width = mode.width;
    window->null.height = mode.height;
}

static void acquireMonitor(window_st* window)
{
    impl_on_monitor_window(window->monitor, window);
}

static void releaseMonitor(window_st* window)
{
    if (window->monitor->window != window)
        return;

    impl_on_monitor_window(window->monitor, NULL);
}

static int createNativeWindow(window_st* window,
                              const wnd_config_st* wndconfig)
{
    if (window->monitor)
        fitToMonitor(window);
    else
    {
        if (wndconfig->xpos == SC_ANY_POSITION && wndconfig->ypos == SC_ANY_POSITION)
        {
            window->null.xpos = 17;
            window->null.ypos = 17;
        }
        else
        {
            window->null.xpos = wndconfig->xpos;
            window->null.ypos = wndconfig->ypos;
        }

        window->null.width = wndconfig->width;
        window->null.height = wndconfig->height;
    }

    window->null.visible = wndconfig->visible;
    window->null.decorated = wndconfig->decorated;
    window->null.maximized = wndconfig->maximized;
    window->null.floating = wndconfig->floating;
    window->null.transparent = false;
    window->null.opacity = 1.f;

    return true;
}


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

bool _glfwCreateWindowNull(window_st* window,
                               const wnd_config_st* wndconfig)
{
    if (!createNativeWindow(window, wndconfig))
        return false;

    if (wndconfig->mousePassthrough)
        _glfwSetWindowMousePassthroughNull(window, true);

    if (window->monitor)
    {
        _glfwShowWindowNull(window);
        _glfwFocusWindowNull(window);
        acquireMonitor(window);

        if (wndconfig->centerCursor)
            wsi_center_cursor_in_content_area(window);
    }
    else
    {
        if (wndconfig->visible)
        {
            _glfwShowWindowNull(window);
            if (wndconfig->focused)
                _glfwFocusWindowNull(window);
        }
    }

    return true;
}

void _glfwDestroyWindowNull(window_st* window)
{
    if (window->monitor)
        releaseMonitor(window);

    if (g_wsi.null.focusedWindow == window)
        g_wsi.null.focusedWindow = NULL;
}

void _glfwSetWindowTitleNull(window_st* window, const char* title)
{
}

void _glfwSetWindowIconNull(window_st* window, int count, const GLFWimage* images)
{
}

void _glfwSetWindowMonitorNull(window_st* window,
                               monitor_st* monitor,
                               int xpos, int ypos,
                               int width, int height,
                               int refreshRate)
{
    if (window->monitor == monitor)
    {
        if (!monitor)
        {
            _glfwSetWindowPosNull(window, xpos, ypos);
            _glfwSetWindowSizeNull(window, width, height);
        }

        return;
    }

    if (window->monitor)
        releaseMonitor(window);

    impl_on_win_monitor(window, monitor);

    if (window->monitor)
    {
        window->null.visible = true;
        acquireMonitor(window);
        fitToMonitor(window);
    }
    else
    {
        _glfwSetWindowPosNull(window, xpos, ypos);
        _glfwSetWindowSizeNull(window, width, height);
    }
}

void _glfwGetWindowPosNull(window_st* window, int* xpos, int* ypos)
{
    if (xpos)
        *xpos = window->null.xpos;
    if (ypos)
        *ypos = window->null.ypos;
}

void _glfwSetWindowPosNull(window_st* window, int xpos, int ypos)
{
    if (window->monitor)
        return;

    if (window->null.xpos != xpos || window->null.ypos != ypos)
    {
        window->null.xpos = xpos;
        window->null.ypos = ypos;
        impl_on_win_pos(window, xpos, ypos);
    }
}

void _glfwGetWindowSizeNull(window_st* window, int* width, int* height)
{
    if (width)
        *width = window->null.width;
    if (height)
        *height = window->null.height;
}

void _glfwSetWindowSizeNull(window_st* window, int width, int height)
{
    if (window->monitor)
        return;

    if (window->null.width != width || window->null.height != height)
    {
        window->null.width = width;
        window->null.height = height;
        impl_on_win_damage(window);
        impl_on_win_size(window, width, height);
    }
}

void _glfwSetWindowSizeLimitsNull(window_st* window,
                                  int minwidth, int minheight,
                                  int maxwidth, int maxheight)
{
    int width = window->null.width;
    int height = window->null.height;
    applySizeLimits(window, &width, &height);
    _glfwSetWindowSizeNull(window, width, height);
}

void _glfwSetWindowAspectRatioNull(window_st* window, int n, int d)
{
    int width = window->null.width;
    int height = window->null.height;
    applySizeLimits(window, &width, &height);
    _glfwSetWindowSizeNull(window, width, height);
}

void _glfwGetWindowFrameSizeNull(window_st* window,
                                 int* left, int* top,
                                 int* right, int* bottom)
{
    if (window->null.decorated && !window->monitor)
    {
        if (left)
            *left = 1;
        if (top)
            *top = 10;
        if (right)
            *right = 1;
        if (bottom)
            *bottom = 1;
    }
    else
    {
        if (left)
            *left = 0;
        if (top)
            *top = 0;
        if (right)
            *right = 0;
        if (bottom)
            *bottom = 0;
    }
}

void _glfwGetWindowContentScaleNull(window_st* window, float* xscale, float* yscale)
{
    if (xscale)
        *xscale = 1.f;
    if (yscale)
        *yscale = 1.f;
}

void _glfwIconifyWindowNull(window_st* window)
{
    if (g_wsi.null.focusedWindow == window)
    {
        g_wsi.null.focusedWindow = NULL;
        impl_on_win_focus(window, false);
    }

    if (!window->null.iconified)
    {
        window->null.iconified = true;
        impl_on_win_iconify(window, true);

        if (window->monitor)
            releaseMonitor(window);
    }
}

void _glfwRestoreWindowNull(window_st* window)
{
    if (window->null.iconified)
    {
        window->null.iconified = false;
        impl_on_win_iconify(window, false);

        if (window->monitor)
            acquireMonitor(window);
    }
    else if (window->null.maximized)
    {
        window->null.maximized = false;
        impl_on_win_maximize(window, false);
    }
}

void _glfwMaximizeWindowNull(window_st* window)
{
    if (!window->null.maximized)
    {
        window->null.maximized = true;
        impl_on_win_maximize(window, true);
    }
}

bool _glfwWindowMaximizedNull(window_st* window)
{
    return window->null.maximized;
}

bool _glfwWindowHoveredNull(window_st* window)
{
    return g_wsi.null.xcursor >= window->null.xpos &&
           g_wsi.null.ycursor >= window->null.ypos &&
           g_wsi.null.xcursor <= window->null.xpos + window->null.width - 1 &&
           g_wsi.null.ycursor <= window->null.ypos + window->null.height - 1;
}

void _glfwSetWindowResizableNull(window_st* window, bool enabled)
{
    window->null.resizable = enabled;
}

void _glfwSetWindowDecoratedNull(window_st* window, bool enabled)
{
    window->null.decorated = enabled;
}

void _glfwSetWindowFloatingNull(window_st* window, bool enabled)
{
    window->null.floating = enabled;
}

void _glfwSetWindowMousePassthroughNull(window_st* window, bool enabled)
{
}

float _glfwGetWindowOpacityNull(window_st* window)
{
    return window->null.opacity;
}

void _glfwSetWindowOpacityNull(window_st* window, float opacity)
{
    window->null.opacity = opacity;
}

void _glfwSetRawMouseMotionNull(window_st *window, bool enabled)
{
}

bool _glfwRawMouseMotionSupportedNull(void)
{
    return true;
}

void _glfwShowWindowNull(window_st* window)
{
    window->null.visible = true;
}

void _glfwRequestWindowAttentionNull(window_st* window)
{
}

void _glfwHideWindowNull(window_st* window)
{
    if (g_wsi.null.focusedWindow == window)
    {
        g_wsi.null.focusedWindow = NULL;
        impl_on_win_focus(window, false);
    }

    window->null.visible = false;
}

void _glfwFocusWindowNull(window_st* window)
{
    window_st* previous;

    if (g_wsi.null.focusedWindow == window)
        return;

    if (!window->null.visible)
        return;

    previous = g_wsi.null.focusedWindow;
    g_wsi.null.focusedWindow = window;

    if (previous)
    {
        impl_on_win_focus(previous, false);
        if (previous->monitor && previous->autoIconify)
            _glfwIconifyWindowNull(previous);
    }

    impl_on_win_focus(window, true);
}

bool _glfwWindowFocusedNull(window_st* window)
{
    return g_wsi.null.focusedWindow == window;
}

bool _glfwWindowIconifiedNull(window_st* window)
{
    return window->null.iconified;
}

bool _glfwWindowVisibleNull(window_st* window)
{
    return window->null.visible;
}

void _glfwPollEventsNull(void)
{
}

void _glfwWaitEventsNull(void)
{
}

void _glfwWaitEventsTimeoutNull(double timeout)
{
}

void _glfwPostEmptyEventNull(void)
{
}

void _glfwGetCursorPosNull(window_st* window, double* xpos, double* ypos)
{
    if (xpos)
        *xpos = g_wsi.null.xcursor - window->null.xpos;
    if (ypos)
        *ypos = g_wsi.null.ycursor - window->null.ypos;
}

void _glfwSetCursorPosNull(window_st* window, double x, double y)
{
    g_wsi.null.xcursor = window->null.xpos + (int) x;
    g_wsi.null.ycursor = window->null.ypos + (int) y;
}

void _glfwSetCursorModeNull(window_st* window, int mode)
{
}

bool _glfwCreateCursorNull(cursor_st* cursor,
                               const GLFWimage* image,
                               int xhot, int yhot)
{
    return true;
}

bool _glfwCreateStandardCursorNull(cursor_st* cursor, int shape)
{
    return true;
}

void _glfwDestroyCursorNull(cursor_st* cursor)
{
}

void _glfwSetCursorNull(window_st* window, cursor_st* cursor)
{
}

void _glfwSetClipboardStringNull(const char* string)
{
    char* copy = wsi_strdup(string);
    wsi_free(g_wsi.null.clipboardString);
    g_wsi.null.clipboardString = copy;
}

const char* _glfwGetClipboardStringNull(void)
{
    return g_wsi.null.clipboardString;
}

const char* _glfwGetScancodeNameNull(int scancode)
{
    if (scancode < GLFW_NULL_SC_FIRST || scancode > GLFW_NULL_SC_LAST)
    {
        impl_on_error(SC_WSI_ERR_INVALID_VALUE, "Invalid scancode %i", scancode);
        return NULL;
    }

    switch (scancode)
    {
        case GLFW_NULL_SC_APOSTROPHE:
            return "'";
        case GLFW_NULL_SC_COMMA:
            return ",";
        case GLFW_NULL_SC_MINUS:
        case GLFW_NULL_SC_KP_SUBTRACT:
            return "-";
        case GLFW_NULL_SC_PERIOD:
        case GLFW_NULL_SC_KP_DECIMAL:
            return ".";
        case GLFW_NULL_SC_SLASH:
        case GLFW_NULL_SC_KP_DIVIDE:
            return "/";
        case GLFW_NULL_SC_SEMICOLON:
            return ";";
        case GLFW_NULL_SC_EQUAL:
        case GLFW_NULL_SC_KP_EQUAL:
            return "=";
        case GLFW_NULL_SC_LEFT_BRACKET:
            return "[";
        case GLFW_NULL_SC_RIGHT_BRACKET:
            return "]";
        case GLFW_NULL_SC_KP_MULTIPLY:
            return "*";
        case GLFW_NULL_SC_KP_ADD:
            return "+";
        case GLFW_NULL_SC_BACKSLASH:
        case GLFW_NULL_SC_WORLD_1:
        case GLFW_NULL_SC_WORLD_2:
            return "\\";
        case GLFW_NULL_SC_0:
        case GLFW_NULL_SC_KP_0:
            return "0";
        case GLFW_NULL_SC_1:
        case GLFW_NULL_SC_KP_1:
            return "1";
        case GLFW_NULL_SC_2:
        case GLFW_NULL_SC_KP_2:
            return "2";
        case GLFW_NULL_SC_3:
        case GLFW_NULL_SC_KP_3:
            return "3";
        case GLFW_NULL_SC_4:
        case GLFW_NULL_SC_KP_4:
            return "4";
        case GLFW_NULL_SC_5:
        case GLFW_NULL_SC_KP_5:
            return "5";
        case GLFW_NULL_SC_6:
        case GLFW_NULL_SC_KP_6:
            return "6";
        case GLFW_NULL_SC_7:
        case GLFW_NULL_SC_KP_7:
            return "7";
        case GLFW_NULL_SC_8:
        case GLFW_NULL_SC_KP_8:
            return "8";
        case GLFW_NULL_SC_9:
        case GLFW_NULL_SC_KP_9:
            return "9";
        case GLFW_NULL_SC_A:
            return "a";
        case GLFW_NULL_SC_B:
            return "b";
        case GLFW_NULL_SC_C:
            return "c";
        case GLFW_NULL_SC_D:
            return "d";
        case GLFW_NULL_SC_E:
            return "e";
        case GLFW_NULL_SC_F:
            return "f";
        case GLFW_NULL_SC_G:
            return "g";
        case GLFW_NULL_SC_H:
            return "h";
        case GLFW_NULL_SC_I:
            return "i";
        case GLFW_NULL_SC_J:
            return "j";
        case GLFW_NULL_SC_K:
            return "k";
        case GLFW_NULL_SC_L:
            return "l";
        case GLFW_NULL_SC_M:
            return "m";
        case GLFW_NULL_SC_N:
            return "n";
        case GLFW_NULL_SC_O:
            return "o";
        case GLFW_NULL_SC_P:
            return "p";
        case GLFW_NULL_SC_Q:
            return "q";
        case GLFW_NULL_SC_R:
            return "r";
        case GLFW_NULL_SC_S:
            return "s";
        case GLFW_NULL_SC_T:
            return "t";
        case GLFW_NULL_SC_U:
            return "u";
        case GLFW_NULL_SC_V:
            return "v";
        case GLFW_NULL_SC_W:
            return "w";
        case GLFW_NULL_SC_X:
            return "x";
        case GLFW_NULL_SC_Y:
            return "y";
        case GLFW_NULL_SC_Z:
            return "z";
    }

    return NULL;
}

int _glfwGetKeyScancodeNull(int key)
{
    return g_wsi.null.scancodes[key];
}


