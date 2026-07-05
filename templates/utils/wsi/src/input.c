
#include "internal.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Internal key state used for sticky keys
#define _GLFW_STICK 3
#define SC_MOD_MASK (SC_MOD_SHIFT | \
                       SC_MOD_CONTROL | \
                       SC_MOD_ALT | \
                       SC_MOD_SUPER | \
                       SC_MOD_CAPS_LOCK | \
                       SC_MOD_NUM_LOCK)


//////////////////////////////////////////////////////////////////////////
//////                         GLFW event API                       //////
//////////////////////////////////////////////////////////////////////////

// Notifies shared code of a physical key event
//
void impl_on_key(window_st* window, int key, int scancode, int action, int mods)
{
    assert(window != NULL);
    assert(key >= 0 || key == SC_KEY_UNKNOWN);
    assert(key <= SC_KEY_LAST);
    assert(action == SC_PRESS || action == SC_RELEASE);
    assert(mods == (mods & SC_MOD_MASK));

    if (key >= 0 && key <= SC_KEY_LAST)
    {
        bool repeated = false;

        if (action == SC_RELEASE && window->keys[key] == SC_RELEASE)
            return;

        if (action == SC_PRESS && window->keys[key] == SC_PRESS)
            repeated = true;

        if (action == SC_RELEASE && window->stickyKeys)
            window->keys[key] = _GLFW_STICK;
        else
            window->keys[key] = (char) action;

        if (repeated)
            action = SC_REPEAT;
    }

    if (!window->lockKeyMods)
        mods &= ~(SC_MOD_CAPS_LOCK | SC_MOD_NUM_LOCK);

    if (window->callbacks.key)
        window->callbacks.key((sc_window*) window, key, scancode, action, mods);
}

// Notifies shared code of a Unicode codepoint input event
// The 'plain' parameter determines whether to emit a regular character event
//
void impl_on_chr(window_st* window, uint32_t codepoint, int mods, bool plain)
{
    assert(window != NULL);
    assert(mods == (mods & SC_MOD_MASK));
    assert(plain == true || plain == false);

    if (codepoint < 32 || (codepoint > 126 && codepoint < 160))
        return;

    if (!window->lockKeyMods)
        mods &= ~(SC_MOD_CAPS_LOCK | SC_MOD_NUM_LOCK);

    if (window->callbacks.chr_mods)
        window->callbacks.chr_mods((sc_window*) window, codepoint, mods);

    if (plain)
    {
        if (window->callbacks.chr)
            window->callbacks.chr((sc_window*) window, codepoint);
    }
}

// Notifies shared code of a scroll event
//
void impl_on_scroll(window_st* window, double xoffset, double yoffset)
{
    assert(window != NULL);
    assert(isfinite(xoffset));
    assert(isfinite(yoffset));

    if (window->callbacks.scroll)
        window->callbacks.scroll((sc_window*) window, xoffset, yoffset);
}

// Notifies shared code of a mouse button click event
//
void impl_on_mouse_click(window_st* window, int button, int action, int mods)
{
    assert(window != NULL);
    assert(button >= 0);
    assert(action == SC_PRESS || action == SC_RELEASE);
    assert(mods == (mods & SC_MOD_MASK));

    if (button < 0 || (!window->disableMouseButtonLimit && button > SC_MOUSE_BUTTON_LAST))
        return;

    if (!window->lockKeyMods)
        mods &= ~(SC_MOD_CAPS_LOCK | SC_MOD_NUM_LOCK);

    if (button <= SC_MOUSE_BUTTON_LAST)
    {
        if (action == SC_RELEASE && window->stickyMouseButtons)
            window->mouseButtons[button] = _GLFW_STICK;
        else
            window->mouseButtons[button] = (char) action;
    }

    if (window->callbacks.mouse_button)
        window->callbacks.mouse_button((sc_window*) window, button, action, mods);
}

// Notifies shared code of a cursor motion event
// The position is specified in content area relative screen coordinates
//
void impl_on_cursor_pos(window_st* window, double xpos, double ypos)
{
    assert(window != NULL);
    assert(isfinite(xpos));
    assert(isfinite(ypos));

    if (window->virtualCursorPosX == xpos && window->virtualCursorPosY == ypos)
        return;

    window->virtualCursorPosX = xpos;
    window->virtualCursorPosY = ypos;

    if (window->callbacks.cursor_pos)
        window->callbacks.cursor_pos((sc_window*) window, xpos, ypos);
}

// Notifies shared code of a cursor enter/leave event
//
void impl_on_cursor_enter(window_st* window, bool entered)
{
    assert(window != NULL);
    assert(entered == true || entered == false);

    if (window->callbacks.cursor_enter)
        window->callbacks.cursor_enter((sc_window*) window, entered);
}

// Notifies shared code of files or directories dropped on a window
//
void impl_on_drop(window_st* window, int count, const char** paths)
{
    assert(window != NULL);
    assert(count > 0);
    assert(paths != NULL);

    if (window->callbacks.drop)
        window->callbacks.drop((sc_window*) window, count, paths);
}

// Center the cursor in the content area of the specified window
//
void wsi_center_cursor_in_content_area(window_st* window)
{
    int width, height;

    g_wsi.platform.getWindowSize(window, &width, &height);
    g_wsi.platform.setCursorPos(window, width / 2.0, height / 2.0);
}


//////////////////////////////////////////////////////////////////////////
//////                        GLFW public API                       //////
//////////////////////////////////////////////////////////////////////////

WSI_API int sc_wsi_input_get_mode(sc_window* handle, int mode)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return 0;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    switch (mode)
    {
        case SC_CURSOR:
            return window->cursorMode;
        case SC_STICKY_KEYS:
            return window->stickyKeys;
        case SC_STICKY_MOUSE_BUTTONS:
            return window->stickyMouseButtons;
        case SC_LOCK_KEY_MODS:
            return window->lockKeyMods;
        case SC_RAW_MOUSE_MOTION:
            return window->rawMouseMotion;
        case SC_UNLIMITED_MOUSE_BUTTONS:
            return window->disableMouseButtonLimit;
    }

    impl_on_error(SC_WSI_ERR_INVALID_ENUM, "Invalid input mode 0x%08X", mode);
    return 0;
}

WSI_API void sc_wsi_input_set_mode(sc_window* handle, int mode, int value)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    switch (mode)
    {
        case SC_CURSOR:
        {
            if (value != SC_CURSOR_NORMAL &&
                value != SC_CURSOR_HIDDEN &&
                value != SC_CURSOR_DISABLED &&
                value != SC_CURSOR_CAPTURED)
            {
                impl_on_error(SC_WSI_ERR_INVALID_ENUM,
                                "Invalid cursor mode 0x%08X",
                                value);
                return;
            }

            if (window->cursorMode == value)
                return;

            window->cursorMode = value;

            g_wsi.platform.getCursorPos(window,
                                        &window->virtualCursorPosX,
                                        &window->virtualCursorPosY);
            g_wsi.platform.setCursorMode(window, value);
            return;
        }

        case SC_STICKY_KEYS:
        {
            value = value ? true : false;
            if (window->stickyKeys == value)
                return;

            if (!value)
            {
                int i;

                // Release all sticky keys
                for (i = 0;  i <= SC_KEY_LAST;  i++)
                {
                    if (window->keys[i] == _GLFW_STICK)
                        window->keys[i] = SC_RELEASE;
                }
            }

            window->stickyKeys = value;
            return;
        }

        case SC_STICKY_MOUSE_BUTTONS:
        {
            value = value ? true : false;
            if (window->stickyMouseButtons == value)
                return;

            if (!value)
            {
                int i;

                // Release all sticky mouse buttons
                for (i = 0;  i <= SC_MOUSE_BUTTON_LAST;  i++)
                {
                    if (window->mouseButtons[i] == _GLFW_STICK)
                        window->mouseButtons[i] = SC_RELEASE;
                }
            }

            window->stickyMouseButtons = value;
            return;
        }

        case SC_LOCK_KEY_MODS:
        {
            window->lockKeyMods = value ? true : false;
            return;
        }

        case SC_RAW_MOUSE_MOTION:
        {
            if (!g_wsi.platform.rawMouseMotionSupported())
            {
                impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                                "Raw mouse motion is not supported on this system");
                return;
            }

            value = value ? true : false;
            if (window->rawMouseMotion == value)
                return;

            window->rawMouseMotion = value;
            g_wsi.platform.setRawMouseMotion(window, value);
            return;
        }

        case SC_UNLIMITED_MOUSE_BUTTONS:
        {
            window->disableMouseButtonLimit = value ? true : false;
            return;
        }
    }

    impl_on_error(SC_WSI_ERR_INVALID_ENUM, "Invalid input mode 0x%08X", mode);
}

WSI_API int sc_wsi_mouse_raw_motion_supported(void)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return false;
    }
    return g_wsi.platform.rawMouseMotionSupported();
}

WSI_API const char* sc_wsi_key_name(int key, int scancode)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }

    if (key != SC_KEY_UNKNOWN)
    {
        if (key < SC_KEY_SPACE || key > SC_KEY_LAST)
        {
            impl_on_error(SC_WSI_ERR_INVALID_ENUM, "Invalid key %i", key);
            return NULL;
        }

        if (key != SC_KEY_KP_EQUAL &&
            (key < SC_KEY_KP_0 || key > SC_KEY_KP_ADD) &&
            (key < SC_KEY_APOSTROPHE || key > SC_KEY_WORLD_2))
        {
            return NULL;
        }

        scancode = g_wsi.platform.getKeyScancode(key);
    }

    return g_wsi.platform.getScancodeName(scancode);
}

WSI_API int sc_wsi_key_scancode(int key)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return 0;
    }

    if (key < SC_KEY_SPACE || key > SC_KEY_LAST)
    {
        impl_on_error(SC_WSI_ERR_INVALID_ENUM, "Invalid key %i", key);
        return -1;
    }

    return g_wsi.platform.getKeyScancode(key);
}

WSI_API int sc_wsi_key(sc_window* handle, int key)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return SC_RELEASE;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    if (key < SC_KEY_SPACE || key > SC_KEY_LAST)
    {
        impl_on_error(SC_WSI_ERR_INVALID_ENUM, "Invalid key %i", key);
        return SC_RELEASE;
    }

    if (window->keys[key] == _GLFW_STICK)
    {
        // Sticky mode: release key now
        window->keys[key] = SC_RELEASE;
        return SC_PRESS;
    }

    return (int) window->keys[key];
}

WSI_API int sc_wsi_mouse_button(sc_window* handle, int button)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return SC_RELEASE;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    if (button < SC_MOUSE_BUTTON_1 || button > SC_MOUSE_BUTTON_LAST)
    {
        impl_on_error(SC_WSI_ERR_INVALID_ENUM, "Invalid mouse button %i", button);
        return SC_RELEASE;
    }

    if (window->mouseButtons[button] == _GLFW_STICK)
    {
        // Sticky mode: release mouse button now
        window->mouseButtons[button] = SC_RELEASE;
        return SC_PRESS;
    }

    return (int) window->mouseButtons[button];
}

WSI_API void sc_wsi_cursor_get_pos(sc_window* handle, double* xpos, double* ypos)
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

    if (window->cursorMode == SC_CURSOR_DISABLED)
    {
        if (xpos)
            *xpos = window->virtualCursorPosX;
        if (ypos)
            *ypos = window->virtualCursorPosY;
    }
    else
        g_wsi.platform.getCursorPos(window, xpos, ypos);
}

WSI_API void sc_wsi_cursor_set_pos(sc_window* handle, double xpos, double ypos)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    if (!isfinite(xpos) || !isfinite(ypos))
    {
        impl_on_error(SC_WSI_ERR_INVALID_VALUE,
                        "Invalid cursor position %f %f",
                        xpos, ypos);
        return;
    }

    if (!g_wsi.platform.windowFocused(window))
        return;

    if (window->cursorMode == SC_CURSOR_DISABLED)
    {
        // Only update the accumulated position if the cursor is disabled
        window->virtualCursorPosX = xpos;
        window->virtualCursorPosY = ypos;
    }
    else
    {
        // Update system cursor position
        g_wsi.platform.setCursorPos(window, xpos, ypos);
    }
}

WSI_API sc_cursor* sc_wsi_cursor_create(const GLFWimage* image, int xhot, int yhot)
{
    cursor_st* cursor;

    assert(image != NULL);
    assert(image->pixels != NULL);

    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }

    if (image->width <= 0 || image->height <= 0)
    {
        impl_on_error(SC_WSI_ERR_INVALID_VALUE, "Invalid image dimensions for cursor");
        return NULL;
    }

    cursor = wsi_calloc(1, sizeof(cursor_st));
    cursor->next = g_wsi.cursorListHead;
    g_wsi.cursorListHead = cursor;

    if (!g_wsi.platform.createCursor(cursor, image, xhot, yhot))
    {
        sc_wsi_cursor_destroy((sc_cursor*) cursor);
        return NULL;
    }

    return (sc_cursor*) cursor;
}

WSI_API sc_cursor* sc_wsi_cursor_create_standard(int shape)
{
    cursor_st* cursor;

    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }

    if (shape != SC_ARROW_CURSOR &&
        shape != SC_IBEAM_CURSOR &&
        shape != SC_CROSSHAIR_CURSOR &&
        shape != SC_POINTING_HAND_CURSOR &&
        shape != SC_RESIZE_EW_CURSOR &&
        shape != SC_RESIZE_NS_CURSOR &&
        shape != SC_RESIZE_NWSE_CURSOR &&
        shape != SC_RESIZE_NESW_CURSOR &&
        shape != SC_RESIZE_ALL_CURSOR &&
        shape != SC_NOT_ALLOWED_CURSOR)
    {
        impl_on_error(SC_WSI_ERR_INVALID_ENUM, "Invalid standard cursor 0x%08X", shape);
        return NULL;
    }

    cursor = wsi_calloc(1, sizeof(cursor_st));
    cursor->next = g_wsi.cursorListHead;
    g_wsi.cursorListHead = cursor;

    if (!g_wsi.platform.createStandardCursor(cursor, shape))
    {
        sc_wsi_cursor_destroy((sc_cursor*) cursor);
        return NULL;
    }

    return (sc_cursor*) cursor;
}

WSI_API void sc_wsi_cursor_destroy(sc_cursor* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    cursor_st* cursor = (cursor_st*) handle;

    if (cursor == NULL)
        return;

    // Make sure the cursor is not being used by any window
    {
        window_st* window;

        for (window = g_wsi.windowListHead;  window;  window = window->next)
        {
            if (window->cursor == cursor)
                sc_wsi_cursor_set((sc_window*) window, NULL);
        }
    }

    g_wsi.platform.destroyCursor(cursor);

    // Unlink cursor from global linked list
    {
        cursor_st** prev = &g_wsi.cursorListHead;

        while (*prev != cursor)
            prev = &((*prev)->next);

        *prev = cursor->next;
    }

    wsi_free(cursor);
}

WSI_API void sc_wsi_cursor_set(sc_window* windowHandle, sc_cursor* cursorHandle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    window_st* window = (window_st*) windowHandle;
    cursor_st* cursor = (cursor_st*) cursorHandle;
    assert(window != NULL);

    window->cursor = cursor;

    g_wsi.platform.setCursor(window, cursor);
}

WSI_API void glfwSetClipboardString(sc_window* handle, const char* string)
{
    assert(string != NULL);

    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }
    g_wsi.platform.setClipboardString(string);
}

WSI_API const char* glfwGetClipboardString(sc_window* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }
    return g_wsi.platform.getClipboardString();
}

WSI_API double sc_wsi_get_time(void)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return 0.0;
    }
    return (double) (wsi_clock_ns() - g_wsi.timer.offset) / 1e9;
}

WSI_API void sc_wsi_set_time(double time)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    if (!isfinite(time) || time < 0.0 || time > 18446744073.0)
    {
        impl_on_error(SC_WSI_ERR_INVALID_VALUE, "Invalid time %f", time);
        return;
    }

    g_wsi.timer.offset = wsi_clock_ns() - (uint64_t) (time * 1e9);
}

WSI_API uint64_t sc_wsi_timer_value(void)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return 0;
    }
    return wsi_clock_ns();
}

WSI_API uint64_t sc_wsi_timer_frequency(void)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return 0;
    }
    return 1000000000ULL;
}

