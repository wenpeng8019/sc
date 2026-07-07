
#include "internal.h"

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
    null_get_video_mode(window->monitor, &mode);
    null_get_monitor_pos(window->monitor,
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

bool null_create_window(window_st* window,
                               const wnd_config_st* wndconfig)
{
    if (!createNativeWindow(window, wndconfig))
        return false;

    if (wndconfig->mousePassthrough)
        null_set_window_mouse_passthrough(window, true);

    if (window->monitor)
    {
        null_show_window(window);
        null_focus_window(window);
        acquireMonitor(window);

        if (wndconfig->centerCursor)
            wsi_center_cursor_in_content_area(window);
    }
    else
    {
        if (wndconfig->visible)
        {
            null_show_window(window);
            if (wndconfig->focused)
                null_focus_window(window);
        }
    }

    return true;
}

void null_destroy_window(window_st* window)
{
    if (window->monitor)
        releaseMonitor(window);

    if (g_wsi.null.focusedWindow == window)
        g_wsi.null.focusedWindow = NULL;
}

void null_set_window_title(window_st* window, const char* title)
{
}

void null_set_window_icon(window_st* window, int count, const GLFWimage* images)
{
}

void null_set_window_monitor(window_st* window,
                               monitor_st* monitor,
                               int xpos, int ypos,
                               int width, int height,
                               int refreshRate)
{
    if (window->monitor == monitor)
    {
        if (!monitor)
        {
            null_set_window_pos(window, xpos, ypos);
            null_set_window_size(window, width, height);
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
        null_set_window_pos(window, xpos, ypos);
        null_set_window_size(window, width, height);
    }
}

void null_get_window_pos(window_st* window, int* xpos, int* ypos)
{
    if (xpos)
        *xpos = window->null.xpos;
    if (ypos)
        *ypos = window->null.ypos;
}

void null_set_window_pos(window_st* window, int xpos, int ypos)
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

void null_get_window_size(window_st* window, int* width, int* height)
{
    if (width)
        *width = window->null.width;
    if (height)
        *height = window->null.height;
}

void null_set_window_size(window_st* window, int width, int height)
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

void null_set_window_size_limits(window_st* window,
                                  int minwidth, int minheight,
                                  int maxwidth, int maxheight)
{
    int width = window->null.width;
    int height = window->null.height;
    applySizeLimits(window, &width, &height);
    null_set_window_size(window, width, height);
}

void null_set_window_aspect_ratio(window_st* window, int n, int d)
{
    int width = window->null.width;
    int height = window->null.height;
    applySizeLimits(window, &width, &height);
    null_set_window_size(window, width, height);
}

void null_get_window_frame_size(window_st* window,
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

void null_get_window_content_scale(window_st* window, float* xscale, float* yscale)
{
    if (xscale)
        *xscale = 1.f;
    if (yscale)
        *yscale = 1.f;
}

void null_iconify_window(window_st* window)
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

void null_restore_window(window_st* window)
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

void null_maximize_window(window_st* window)
{
    if (!window->null.maximized)
    {
        window->null.maximized = true;
        impl_on_win_maximize(window, true);
    }
}

bool null_window_maximized(window_st* window)
{
    return window->null.maximized;
}

bool null_window_hovered(window_st* window)
{
    return g_wsi.null.xcursor >= window->null.xpos &&
           g_wsi.null.ycursor >= window->null.ypos &&
           g_wsi.null.xcursor <= window->null.xpos + window->null.width - 1 &&
           g_wsi.null.ycursor <= window->null.ypos + window->null.height - 1;
}

void null_set_window_resizable(window_st* window, bool enabled)
{
    window->null.resizable = enabled;
}

void null_set_window_decorated(window_st* window, bool enabled)
{
    window->null.decorated = enabled;
}

void null_set_window_floating(window_st* window, bool enabled)
{
    window->null.floating = enabled;
}

void null_set_window_mouse_passthrough(window_st* window, bool enabled)
{
}

float null_get_window_opacity(window_st* window)
{
    return window->null.opacity;
}

void null_set_window_opacity(window_st* window, float opacity)
{
    window->null.opacity = opacity;
}

void null_set_mouse_raw_motion(window_st *window, bool enabled)
{
}

bool null_mouse_raw_motion_supported(void)
{
    return true;
}

void null_show_window(window_st* window)
{
    window->null.visible = true;
}

void null_request_window_attention(window_st* window)
{
}

void null_hide_window(window_st* window)
{
    if (g_wsi.null.focusedWindow == window)
    {
        g_wsi.null.focusedWindow = NULL;
        impl_on_win_focus(window, false);
    }

    window->null.visible = false;
}

void null_focus_window(window_st* window)
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
            null_iconify_window(previous);
    }

    impl_on_win_focus(window, true);
}

bool null_window_focused(window_st* window)
{
    return g_wsi.null.focusedWindow == window;
}

bool null_window_iconified(window_st* window)
{
    return window->null.iconified;
}

bool null_window_visible(window_st* window)
{
    return window->null.visible;
}

void null_poll_events(void)
{
}

void null_wait_events(void)
{
}

void null_wait_eventsTimeout(double timeout)
{
}

void null_post_empty_event(void)
{
}

static void null_get_cursor_pos(window_st* window, double* xpos, double* ypos)
{
    if (xpos)
        *xpos = g_wsi.null.xcursor - window->null.xpos;
    if (ypos)
        *ypos = g_wsi.null.ycursor - window->null.ypos;
}

void null_set_cursor_pos(window_st* window, double x, double y)
{
    g_wsi.null.xcursor = window->null.xpos + (int) x;
    g_wsi.null.ycursor = window->null.ypos + (int) y;
}

void null_set_cursor_mode(window_st* window, int mode)
{
}

bool null_create_cursor(cursor_st* cursor,
                               const GLFWimage* image,
                               int xhot, int yhot)
{
    return true;
}

bool null_create_standard_cursor(cursor_st* cursor, int shape)
{
    return true;
}

void null_destroy_cursor(cursor_st* cursor)
{
}

void null_set_cursor(window_st* window, cursor_st* cursor)
{
}

void null_set_clipboard_string(const char* string)
{
    char* copy = wsi_strdup(string);
    wsi_free(g_wsi.null.clipboardString);
    g_wsi.null.clipboardString = copy;
}

const char* null_get_clipboard_string(void)
{
    return g_wsi.null.clipboardString;
}

const char* null_get_scancode_name(int scancode)
{
    if (scancode < NULL_SC_FIRST || scancode > NULL_SC_LAST)
    {
        impl_on_error(SC_WSI_ERR_INVALID_VALUE, "Invalid scancode %i", scancode);
        return NULL;
    }

    switch (scancode)
    {
        case NULL_SC_APOSTROPHE:
            return "'";
        case NULL_SC_COMMA:
            return ",";
        case NULL_SC_MINUS:
        case NULL_SC_KP_SUBTRACT:
            return "-";
        case NULL_SC_PERIOD:
        case NULL_SC_KP_DECIMAL:
            return ".";
        case NULL_SC_SLASH:
        case NULL_SC_KP_DIVIDE:
            return "/";
        case NULL_SC_SEMICOLON:
            return ";";
        case NULL_SC_EQUAL:
        case NULL_SC_KP_EQUAL:
            return "=";
        case NULL_SC_LEFT_BRACKET:
            return "[";
        case NULL_SC_RIGHT_BRACKET:
            return "]";
        case NULL_SC_KP_MULTIPLY:
            return "*";
        case NULL_SC_KP_ADD:
            return "+";
        case NULL_SC_BACKSLASH:
        case NULL_SC_WORLD_1:
        case NULL_SC_WORLD_2:
            return "\\";
        case NULL_SC_0:
        case NULL_SC_KP_0:
            return "0";
        case NULL_SC_1:
        case NULL_SC_KP_1:
            return "1";
        case NULL_SC_2:
        case NULL_SC_KP_2:
            return "2";
        case NULL_SC_3:
        case NULL_SC_KP_3:
            return "3";
        case NULL_SC_4:
        case NULL_SC_KP_4:
            return "4";
        case NULL_SC_5:
        case NULL_SC_KP_5:
            return "5";
        case NULL_SC_6:
        case NULL_SC_KP_6:
            return "6";
        case NULL_SC_7:
        case NULL_SC_KP_7:
            return "7";
        case NULL_SC_8:
        case NULL_SC_KP_8:
            return "8";
        case NULL_SC_9:
        case NULL_SC_KP_9:
            return "9";
        case NULL_SC_A:
            return "a";
        case NULL_SC_B:
            return "b";
        case NULL_SC_C:
            return "c";
        case NULL_SC_D:
            return "d";
        case NULL_SC_E:
            return "e";
        case NULL_SC_F:
            return "f";
        case NULL_SC_G:
            return "g";
        case NULL_SC_H:
            return "h";
        case NULL_SC_I:
            return "i";
        case NULL_SC_J:
            return "j";
        case NULL_SC_K:
            return "k";
        case NULL_SC_L:
            return "l";
        case NULL_SC_M:
            return "m";
        case NULL_SC_N:
            return "n";
        case NULL_SC_O:
            return "o";
        case NULL_SC_P:
            return "p";
        case NULL_SC_Q:
            return "q";
        case NULL_SC_R:
            return "r";
        case NULL_SC_S:
            return "s";
        case NULL_SC_T:
            return "t";
        case NULL_SC_U:
            return "u";
        case NULL_SC_V:
            return "v";
        case NULL_SC_W:
            return "w";
        case NULL_SC_X:
            return "x";
        case NULL_SC_Y:
            return "y";
        case NULL_SC_Z:
            return "z";
    }

    return NULL;
}

int null_get_key_scancode(int key)
{
    return g_wsi.null.scancodes[key];
}

int null_init(void)
{
    int scancode;

    memset(g_wsi.null.keycodes, -1, sizeof(g_wsi.null.keycodes));
    memset(g_wsi.null.scancodes, -1, sizeof(g_wsi.null.scancodes));

    g_wsi.null.keycodes[NULL_SC_SPACE]         = SC_KEY_SPACE;
    g_wsi.null.keycodes[NULL_SC_APOSTROPHE]    = SC_KEY_APOSTROPHE;
    g_wsi.null.keycodes[NULL_SC_COMMA]         = SC_KEY_COMMA;
    g_wsi.null.keycodes[NULL_SC_MINUS]         = SC_KEY_MINUS;
    g_wsi.null.keycodes[NULL_SC_PERIOD]        = SC_KEY_PERIOD;
    g_wsi.null.keycodes[NULL_SC_SLASH]         = SC_KEY_SLASH;
    g_wsi.null.keycodes[NULL_SC_0]             = SC_KEY_0;
    g_wsi.null.keycodes[NULL_SC_1]             = SC_KEY_1;
    g_wsi.null.keycodes[NULL_SC_2]             = SC_KEY_2;
    g_wsi.null.keycodes[NULL_SC_3]             = SC_KEY_3;
    g_wsi.null.keycodes[NULL_SC_4]             = SC_KEY_4;
    g_wsi.null.keycodes[NULL_SC_5]             = SC_KEY_5;
    g_wsi.null.keycodes[NULL_SC_6]             = SC_KEY_6;
    g_wsi.null.keycodes[NULL_SC_7]             = SC_KEY_7;
    g_wsi.null.keycodes[NULL_SC_8]             = SC_KEY_8;
    g_wsi.null.keycodes[NULL_SC_9]             = SC_KEY_9;
    g_wsi.null.keycodes[NULL_SC_SEMICOLON]     = SC_KEY_SEMICOLON;
    g_wsi.null.keycodes[NULL_SC_EQUAL]         = SC_KEY_EQUAL;
    g_wsi.null.keycodes[NULL_SC_A]             = SC_KEY_A;
    g_wsi.null.keycodes[NULL_SC_B]             = SC_KEY_B;
    g_wsi.null.keycodes[NULL_SC_C]             = SC_KEY_C;
    g_wsi.null.keycodes[NULL_SC_D]             = SC_KEY_D;
    g_wsi.null.keycodes[NULL_SC_E]             = SC_KEY_E;
    g_wsi.null.keycodes[NULL_SC_F]             = SC_KEY_F;
    g_wsi.null.keycodes[NULL_SC_G]             = SC_KEY_G;
    g_wsi.null.keycodes[NULL_SC_H]             = SC_KEY_H;
    g_wsi.null.keycodes[NULL_SC_I]             = SC_KEY_I;
    g_wsi.null.keycodes[NULL_SC_J]             = SC_KEY_J;
    g_wsi.null.keycodes[NULL_SC_K]             = SC_KEY_K;
    g_wsi.null.keycodes[NULL_SC_L]             = SC_KEY_L;
    g_wsi.null.keycodes[NULL_SC_M]             = SC_KEY_M;
    g_wsi.null.keycodes[NULL_SC_N]             = SC_KEY_N;
    g_wsi.null.keycodes[NULL_SC_O]             = SC_KEY_O;
    g_wsi.null.keycodes[NULL_SC_P]             = SC_KEY_P;
    g_wsi.null.keycodes[NULL_SC_Q]             = SC_KEY_Q;
    g_wsi.null.keycodes[NULL_SC_R]             = SC_KEY_R;
    g_wsi.null.keycodes[NULL_SC_S]             = SC_KEY_S;
    g_wsi.null.keycodes[NULL_SC_T]             = SC_KEY_T;
    g_wsi.null.keycodes[NULL_SC_U]             = SC_KEY_U;
    g_wsi.null.keycodes[NULL_SC_V]             = SC_KEY_V;
    g_wsi.null.keycodes[NULL_SC_W]             = SC_KEY_W;
    g_wsi.null.keycodes[NULL_SC_X]             = SC_KEY_X;
    g_wsi.null.keycodes[NULL_SC_Y]             = SC_KEY_Y;
    g_wsi.null.keycodes[NULL_SC_Z]             = SC_KEY_Z;
    g_wsi.null.keycodes[NULL_SC_LEFT_BRACKET]  = SC_KEY_LEFT_BRACKET;
    g_wsi.null.keycodes[NULL_SC_BACKSLASH]     = SC_KEY_BACKSLASH;
    g_wsi.null.keycodes[NULL_SC_RIGHT_BRACKET] = SC_KEY_RIGHT_BRACKET;
    g_wsi.null.keycodes[NULL_SC_GRAVE_ACCENT]  = SC_KEY_GRAVE_ACCENT;
    g_wsi.null.keycodes[NULL_SC_WORLD_1]       = SC_KEY_WORLD_1;
    g_wsi.null.keycodes[NULL_SC_WORLD_2]       = SC_KEY_WORLD_2;
    g_wsi.null.keycodes[NULL_SC_ESCAPE]        = SC_KEY_ESCAPE;
    g_wsi.null.keycodes[NULL_SC_ENTER]         = SC_KEY_ENTER;
    g_wsi.null.keycodes[NULL_SC_TAB]           = SC_KEY_TAB;
    g_wsi.null.keycodes[NULL_SC_BACKSPACE]     = SC_KEY_BACKSPACE;
    g_wsi.null.keycodes[NULL_SC_INSERT]        = SC_KEY_INSERT;
    g_wsi.null.keycodes[NULL_SC_DELETE]        = SC_KEY_DELETE;
    g_wsi.null.keycodes[NULL_SC_RIGHT]         = SC_KEY_RIGHT;
    g_wsi.null.keycodes[NULL_SC_LEFT]          = SC_KEY_LEFT;
    g_wsi.null.keycodes[NULL_SC_DOWN]          = SC_KEY_DOWN;
    g_wsi.null.keycodes[NULL_SC_UP]            = SC_KEY_UP;
    g_wsi.null.keycodes[NULL_SC_PAGE_UP]       = SC_KEY_PAGE_UP;
    g_wsi.null.keycodes[NULL_SC_PAGE_DOWN]     = SC_KEY_PAGE_DOWN;
    g_wsi.null.keycodes[NULL_SC_HOME]          = SC_KEY_HOME;
    g_wsi.null.keycodes[NULL_SC_END]           = SC_KEY_END;
    g_wsi.null.keycodes[NULL_SC_CAPS_LOCK]     = SC_KEY_CAPS_LOCK;
    g_wsi.null.keycodes[NULL_SC_SCROLL_LOCK]   = SC_KEY_SCROLL_LOCK;
    g_wsi.null.keycodes[NULL_SC_NUM_LOCK]      = SC_KEY_NUM_LOCK;
    g_wsi.null.keycodes[NULL_SC_PRINT_SCREEN]  = SC_KEY_PRINT_SCREEN;
    g_wsi.null.keycodes[NULL_SC_PAUSE]         = SC_KEY_PAUSE;
    g_wsi.null.keycodes[NULL_SC_F1]            = SC_KEY_F1;
    g_wsi.null.keycodes[NULL_SC_F2]            = SC_KEY_F2;
    g_wsi.null.keycodes[NULL_SC_F3]            = SC_KEY_F3;
    g_wsi.null.keycodes[NULL_SC_F4]            = SC_KEY_F4;
    g_wsi.null.keycodes[NULL_SC_F5]            = SC_KEY_F5;
    g_wsi.null.keycodes[NULL_SC_F6]            = SC_KEY_F6;
    g_wsi.null.keycodes[NULL_SC_F7]            = SC_KEY_F7;
    g_wsi.null.keycodes[NULL_SC_F8]            = SC_KEY_F8;
    g_wsi.null.keycodes[NULL_SC_F9]            = SC_KEY_F9;
    g_wsi.null.keycodes[NULL_SC_F10]           = SC_KEY_F10;
    g_wsi.null.keycodes[NULL_SC_F11]           = SC_KEY_F11;
    g_wsi.null.keycodes[NULL_SC_F12]           = SC_KEY_F12;
    g_wsi.null.keycodes[NULL_SC_F13]           = SC_KEY_F13;
    g_wsi.null.keycodes[NULL_SC_F14]           = SC_KEY_F14;
    g_wsi.null.keycodes[NULL_SC_F15]           = SC_KEY_F15;
    g_wsi.null.keycodes[NULL_SC_F16]           = SC_KEY_F16;
    g_wsi.null.keycodes[NULL_SC_F17]           = SC_KEY_F17;
    g_wsi.null.keycodes[NULL_SC_F18]           = SC_KEY_F18;
    g_wsi.null.keycodes[NULL_SC_F19]           = SC_KEY_F19;
    g_wsi.null.keycodes[NULL_SC_F20]           = SC_KEY_F20;
    g_wsi.null.keycodes[NULL_SC_F21]           = SC_KEY_F21;
    g_wsi.null.keycodes[NULL_SC_F22]           = SC_KEY_F22;
    g_wsi.null.keycodes[NULL_SC_F23]           = SC_KEY_F23;
    g_wsi.null.keycodes[NULL_SC_F24]           = SC_KEY_F24;
    g_wsi.null.keycodes[NULL_SC_F25]           = SC_KEY_F25;
    g_wsi.null.keycodes[NULL_SC_KP_0]          = SC_KEY_KP_0;
    g_wsi.null.keycodes[NULL_SC_KP_1]          = SC_KEY_KP_1;
    g_wsi.null.keycodes[NULL_SC_KP_2]          = SC_KEY_KP_2;
    g_wsi.null.keycodes[NULL_SC_KP_3]          = SC_KEY_KP_3;
    g_wsi.null.keycodes[NULL_SC_KP_4]          = SC_KEY_KP_4;
    g_wsi.null.keycodes[GLFW_NULL_SC_KP_5]          = SC_KEY_KP_5;
    g_wsi.null.keycodes[GLFW_NULL_SC_KP_6]          = SC_KEY_KP_6;
    g_wsi.null.keycodes[GLFW_NULL_SC_KP_7]          = SC_KEY_KP_7;
    g_wsi.null.keycodes[GLFW_NULL_SC_KP_8]          = SC_KEY_KP_8;
    g_wsi.null.keycodes[GLFW_NULL_SC_KP_9]          = SC_KEY_KP_9;
    g_wsi.null.keycodes[GLFW_NULL_SC_KP_DECIMAL]    = SC_KEY_KP_DECIMAL;
    g_wsi.null.keycodes[GLFW_NULL_SC_KP_DIVIDE]     = SC_KEY_KP_DIVIDE;
    g_wsi.null.keycodes[GLFW_NULL_SC_KP_MULTIPLY]   = SC_KEY_KP_MULTIPLY;
    g_wsi.null.keycodes[GLFW_NULL_SC_KP_SUBTRACT]   = SC_KEY_KP_SUBTRACT;
    g_wsi.null.keycodes[GLFW_NULL_SC_KP_ADD]        = SC_KEY_KP_ADD;
    g_wsi.null.keycodes[GLFW_NULL_SC_KP_ENTER]      = SC_KEY_KP_ENTER;
    g_wsi.null.keycodes[GLFW_NULL_SC_KP_EQUAL]      = SC_KEY_KP_EQUAL;
    g_wsi.null.keycodes[GLFW_NULL_SC_LEFT_SHIFT]    = SC_KEY_LEFT_SHIFT;
    g_wsi.null.keycodes[GLFW_NULL_SC_LEFT_CONTROL]  = SC_KEY_LEFT_CONTROL;
    g_wsi.null.keycodes[GLFW_NULL_SC_LEFT_ALT]      = SC_KEY_LEFT_ALT;
    g_wsi.null.keycodes[GLFW_NULL_SC_LEFT_SUPER]    = SC_KEY_LEFT_SUPER;
    g_wsi.null.keycodes[GLFW_NULL_SC_RIGHT_SHIFT]   = SC_KEY_RIGHT_SHIFT;
    g_wsi.null.keycodes[GLFW_NULL_SC_RIGHT_CONTROL] = SC_KEY_RIGHT_CONTROL;
    g_wsi.null.keycodes[GLFW_NULL_SC_RIGHT_ALT]     = SC_KEY_RIGHT_ALT;
    g_wsi.null.keycodes[GLFW_NULL_SC_RIGHT_SUPER]   = SC_KEY_RIGHT_SUPER;
    g_wsi.null.keycodes[GLFW_NULL_SC_MENU]          = SC_KEY_MENU;

    for (scancode = GLFW_NULL_SC_FIRST;  scancode < GLFW_NULL_SC_LAST;  scancode++)
    {
        if (g_wsi.null.keycodes[scancode] > 0)
            g_wsi.null.scancodes[g_wsi.null.keycodes[scancode]] = scancode;
    }

    null_poll_monitors();
    return true;
}

void null_terminate(void)
{
    wsi_free(g_wsi.null.clipboardString);
    memset(&g_wsi.null, 0, sizeof(g_wsi.null));
}

bool null_connect(int platformID, platform_st* platform)
{
    const platform_st null =
    {
        .platformID = SC_PLATFORM_NULL,
        .init = null_init,
        .terminate = null_terminate,

        .pollEvents                 = null_poll_events,
        .waitEvents                 = null_wait_events,
        .waitEventsTimeout          = null_wait_eventsTimeout,
        .postEmptyEvent             = null_post_empty_event,

        .createWindow               = null_create_window,
        .destroyWindow              = null_destroy_window,
        .setWindowTitle             = null_set_window_title,
        .setWindowIcon              = null_set_window_icon,
        .setWindowMonitor           = null_set_window_monitor,
        .setWindowMousePassthrough  = null_set_window_mouse_passthrough,

        .setWindowDecorated         = null_set_window_decorated,
        .setWindowResizable         = null_set_window_resizable,
        .setWindowFloating          = null_set_window_floating,
        .setWindowOpacity           = null_set_window_opacity,
        .getWindowOpacity           = null_get_window_opacity,

        .getWindowPos               = null_get_window_pos,
        .setWindowPos               = null_set_window_pos,
        .getWindowSize              = null_get_window_size,
        .setWindowSize              = null_set_window_size,
        .getWindowFrameSize         = null_get_window_frame_size,
        .setWindowSizeLimits        = null_set_window_size_limits,
        .getWindowContentScale      = null_get_window_content_scale,
        .setWindowAspectRatio       = null_set_window_aspect_ratio,

        .showWindow                 = null_show_window,
        .hideWindow                 = null_hide_window,
        .maximizeWindow             = null_maximize_window,
        .restoreWindow              = null_restore_window,
        .focusWindow                = null_focus_window,
        .iconifyWindow              = null_iconify_window,
        .requestWindowAttention     = null_request_window_attention,

        .windowVisible              = null_window_visible,
        .windowMaximized            = null_window_maximized,
        .windowFocused              = null_window_focused,
        .windowHovered              = null_window_hovered,
        .windowIconified            = null_window_iconified,

        .setCursor                  = null_set_cursor,
        .createStandardCursor       = null_create_standard_cursor,
        .createCursor               = null_create_cursor,
        .destroyCursor              = null_destroy_cursor,
        .setCursorMode              = null_set_cursor_mode,
        .setCursorPos               = null_set_cursor_pos,
        .getCursorPos               = null_get_cursor_pos,
        .setRawMouseMotion          = null_set_mouse_raw_motion,
        .rawMouseMotionSupported    = null_mouse_raw_motion_supported,

        .getKeyScancode             = null_get_key_scancode,
        .getScancodeName            = null_get_scancode_name,
        .getClipboardString         = null_get_clipboard_string,
        .setClipboardString         = null_set_clipboard_string,

        .freeMonitor                = null_free_monitor,
        .getMonitorPos              = null_get_monitor_pos,
        .getMonitorWorkarea         = null_get_monitor_work_area,
        .getMonitorContentScale     = null_get_monitor_content_scale,
        .getVideoModes              = null_get_video_modes,
        .getVideoMode               = null_get_video_mode,
        .getGammaRamp               = null_get_gamma_ramp,
        .setGammaRamp               = null_set_gamma_ramp,
    };

    *platform = null;
    return true;
}
