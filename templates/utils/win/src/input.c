
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
void _glfwInputKey(_sc_window* window, int key, int scancode, int action, int mods)
{
    assert(window != NULL);
    assert(key >= 0 || key == SC_KEY_UNKNOWN);
    assert(key <= SC_KEY_LAST);
    assert(action == SC_PRESS || action == SC_RELEASE);
    assert(mods == (mods & SC_MOD_MASK));

    if (key >= 0 && key <= SC_KEY_LAST)
    {
        GLFWbool repeated = GLFW_FALSE;

        if (action == SC_RELEASE && window->keys[key] == SC_RELEASE)
            return;

        if (action == SC_PRESS && window->keys[key] == SC_PRESS)
            repeated = GLFW_TRUE;

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
void _glfwInputChar(_sc_window* window, uint32_t codepoint, int mods, GLFWbool plain)
{
    assert(window != NULL);
    assert(mods == (mods & SC_MOD_MASK));
    assert(plain == GLFW_TRUE || plain == GLFW_FALSE);

    if (codepoint < 32 || (codepoint > 126 && codepoint < 160))
        return;

    if (!window->lockKeyMods)
        mods &= ~(SC_MOD_CAPS_LOCK | SC_MOD_NUM_LOCK);

    if (window->callbacks.charmods)
        window->callbacks.charmods((sc_window*) window, codepoint, mods);

    if (plain)
    {
        if (window->callbacks.character)
            window->callbacks.character((sc_window*) window, codepoint);
    }
}

// Notifies shared code of a scroll event
//
void _glfwInputScroll(_sc_window* window, double xoffset, double yoffset)
{
    assert(window != NULL);
    assert(isfinite(xoffset));
    assert(isfinite(yoffset));

    if (window->callbacks.scroll)
        window->callbacks.scroll((sc_window*) window, xoffset, yoffset);
}

// Notifies shared code of a mouse button click event
//
void _glfwInputMouseClick(_sc_window* window, int button, int action, int mods)
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

    if (window->callbacks.mouseButton)
        window->callbacks.mouseButton((sc_window*) window, button, action, mods);
}

// Notifies shared code of a cursor motion event
// The position is specified in content area relative screen coordinates
//
void _glfwInputCursorPos(_sc_window* window, double xpos, double ypos)
{
    assert(window != NULL);
    assert(isfinite(xpos));
    assert(isfinite(ypos));

    if (window->virtualCursorPosX == xpos && window->virtualCursorPosY == ypos)
        return;

    window->virtualCursorPosX = xpos;
    window->virtualCursorPosY = ypos;

    if (window->callbacks.cursorPos)
        window->callbacks.cursorPos((sc_window*) window, xpos, ypos);
}

// Notifies shared code of a cursor enter/leave event
//
void _glfwInputCursorEnter(_sc_window* window, GLFWbool entered)
{
    assert(window != NULL);
    assert(entered == GLFW_TRUE || entered == GLFW_FALSE);

    if (window->callbacks.cursorEnter)
        window->callbacks.cursorEnter((sc_window*) window, entered);
}

// Notifies shared code of files or directories dropped on a window
//
void _glfwInputDrop(_sc_window* window, int count, const char** paths)
{
    assert(window != NULL);
    assert(count > 0);
    assert(paths != NULL);

    if (window->callbacks.drop)
        window->callbacks.drop((sc_window*) window, count, paths);
}

// Center the cursor in the content area of the specified window
//
void _glfwCenterCursorInContentArea(_sc_window* window)
{
    int width, height;

    _glfw.platform.getWindowSize(window, &width, &height);
    _glfw.platform.setCursorPos(window, width / 2.0, height / 2.0);
}


//////////////////////////////////////////////////////////////////////////
//////                        GLFW public API                       //////
//////////////////////////////////////////////////////////////////////////

GLFWAPI int glfwGetInputMode(sc_window* handle, int mode)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(0);

    _sc_window* window = (_sc_window*) handle;
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

    _glfwInputError(SC_WIN_ERR_INVALID_ENUM, "Invalid input mode 0x%08X", mode);
    return 0;
}

GLFWAPI void glfwSetInputMode(sc_window* handle, int mode, int value)
{
    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;
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
                _glfwInputError(SC_WIN_ERR_INVALID_ENUM,
                                "Invalid cursor mode 0x%08X",
                                value);
                return;
            }

            if (window->cursorMode == value)
                return;

            window->cursorMode = value;

            _glfw.platform.getCursorPos(window,
                                        &window->virtualCursorPosX,
                                        &window->virtualCursorPosY);
            _glfw.platform.setCursorMode(window, value);
            return;
        }

        case SC_STICKY_KEYS:
        {
            value = value ? GLFW_TRUE : GLFW_FALSE;
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
            value = value ? GLFW_TRUE : GLFW_FALSE;
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
            window->lockKeyMods = value ? GLFW_TRUE : GLFW_FALSE;
            return;
        }

        case SC_RAW_MOUSE_MOTION:
        {
            if (!_glfw.platform.rawMouseMotionSupported())
            {
                _glfwInputError(SC_WIN_ERR_PLATFORM_ERROR,
                                "Raw mouse motion is not supported on this system");
                return;
            }

            value = value ? GLFW_TRUE : GLFW_FALSE;
            if (window->rawMouseMotion == value)
                return;

            window->rawMouseMotion = value;
            _glfw.platform.setRawMouseMotion(window, value);
            return;
        }

        case SC_UNLIMITED_MOUSE_BUTTONS:
        {
            window->disableMouseButtonLimit = value ? GLFW_TRUE : GLFW_FALSE;
            return;
        }
    }

    _glfwInputError(SC_WIN_ERR_INVALID_ENUM, "Invalid input mode 0x%08X", mode);
}

GLFWAPI int glfwRawMouseMotionSupported(void)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(GLFW_FALSE);
    return _glfw.platform.rawMouseMotionSupported();
}

GLFWAPI const char* glfwGetKeyName(int key, int scancode)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    if (key != SC_KEY_UNKNOWN)
    {
        if (key < SC_KEY_SPACE || key > SC_KEY_LAST)
        {
            _glfwInputError(SC_WIN_ERR_INVALID_ENUM, "Invalid key %i", key);
            return NULL;
        }

        if (key != SC_KEY_KP_EQUAL &&
            (key < SC_KEY_KP_0 || key > SC_KEY_KP_ADD) &&
            (key < SC_KEY_APOSTROPHE || key > SC_KEY_WORLD_2))
        {
            return NULL;
        }

        scancode = _glfw.platform.getKeyScancode(key);
    }

    return _glfw.platform.getScancodeName(scancode);
}

GLFWAPI int glfwGetKeyScancode(int key)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(0);

    if (key < SC_KEY_SPACE || key > SC_KEY_LAST)
    {
        _glfwInputError(SC_WIN_ERR_INVALID_ENUM, "Invalid key %i", key);
        return -1;
    }

    return _glfw.platform.getKeyScancode(key);
}

GLFWAPI int glfwGetKey(sc_window* handle, int key)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(SC_RELEASE);

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    if (key < SC_KEY_SPACE || key > SC_KEY_LAST)
    {
        _glfwInputError(SC_WIN_ERR_INVALID_ENUM, "Invalid key %i", key);
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

GLFWAPI int glfwGetMouseButton(sc_window* handle, int button)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(SC_RELEASE);

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    if (button < SC_MOUSE_BUTTON_1 || button > SC_MOUSE_BUTTON_LAST)
    {
        _glfwInputError(SC_WIN_ERR_INVALID_ENUM, "Invalid mouse button %i", button);
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

GLFWAPI void glfwGetCursorPos(sc_window* handle, double* xpos, double* ypos)
{
    if (xpos)
        *xpos = 0;
    if (ypos)
        *ypos = 0;

    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    if (window->cursorMode == SC_CURSOR_DISABLED)
    {
        if (xpos)
            *xpos = window->virtualCursorPosX;
        if (ypos)
            *ypos = window->virtualCursorPosY;
    }
    else
        _glfw.platform.getCursorPos(window, xpos, ypos);
}

GLFWAPI void glfwSetCursorPos(sc_window* handle, double xpos, double ypos)
{
    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    if (!isfinite(xpos) || !isfinite(ypos))
    {
        _glfwInputError(SC_WIN_ERR_INVALID_VALUE,
                        "Invalid cursor position %f %f",
                        xpos, ypos);
        return;
    }

    if (!_glfw.platform.windowFocused(window))
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
        _glfw.platform.setCursorPos(window, xpos, ypos);
    }
}

GLFWAPI sc_cursor* glfwCreateCursor(const GLFWimage* image, int xhot, int yhot)
{
    _sc_cursor* cursor;

    assert(image != NULL);
    assert(image->pixels != NULL);

    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    if (image->width <= 0 || image->height <= 0)
    {
        _glfwInputError(SC_WIN_ERR_INVALID_VALUE, "Invalid image dimensions for cursor");
        return NULL;
    }

    cursor = _glfw_calloc(1, sizeof(_sc_cursor));
    cursor->next = _glfw.cursorListHead;
    _glfw.cursorListHead = cursor;

    if (!_glfw.platform.createCursor(cursor, image, xhot, yhot))
    {
        glfwDestroyCursor((sc_cursor*) cursor);
        return NULL;
    }

    return (sc_cursor*) cursor;
}

GLFWAPI sc_cursor* glfwCreateStandardCursor(int shape)
{
    _sc_cursor* cursor;

    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    if (shape != SC_ARROW_CURSOR &&
        shape != SC_IBEAM_CURSOR &&
        shape != SC_CROSSHAIR_CURSOR &&
        shape != SC_POINTING_HAND_CURSOR &&
        shape != SC_RESIZE_EW_CURSOR &&
        shape != SC_RESIZE_NS_CURSOR &&
        shape != SC_RESIZE_NWSE_CURSOR &&
        shape != SC_RESIZE_NESW_CURSOR &&
        shape != SC_RESIZE_ALL_CURSOR &&
        shape != GLFW_NOT_ALLOWED_CURSOR)
    {
        _glfwInputError(SC_WIN_ERR_INVALID_ENUM, "Invalid standard cursor 0x%08X", shape);
        return NULL;
    }

    cursor = _glfw_calloc(1, sizeof(_sc_cursor));
    cursor->next = _glfw.cursorListHead;
    _glfw.cursorListHead = cursor;

    if (!_glfw.platform.createStandardCursor(cursor, shape))
    {
        glfwDestroyCursor((sc_cursor*) cursor);
        return NULL;
    }

    return (sc_cursor*) cursor;
}

GLFWAPI void glfwDestroyCursor(sc_cursor* handle)
{
    _GLFW_REQUIRE_INIT();

    _sc_cursor* cursor = (_sc_cursor*) handle;

    if (cursor == NULL)
        return;

    // Make sure the cursor is not being used by any window
    {
        _sc_window* window;

        for (window = _glfw.windowListHead;  window;  window = window->next)
        {
            if (window->cursor == cursor)
                glfwSetCursor((sc_window*) window, NULL);
        }
    }

    _glfw.platform.destroyCursor(cursor);

    // Unlink cursor from global linked list
    {
        _sc_cursor** prev = &_glfw.cursorListHead;

        while (*prev != cursor)
            prev = &((*prev)->next);

        *prev = cursor->next;
    }

    _glfw_free(cursor);
}

GLFWAPI void glfwSetCursor(sc_window* windowHandle, sc_cursor* cursorHandle)
{
    _GLFW_REQUIRE_INIT();

    _sc_window* window = (_sc_window*) windowHandle;
    _sc_cursor* cursor = (_sc_cursor*) cursorHandle;
    assert(window != NULL);

    window->cursor = cursor;

    _glfw.platform.setCursor(window, cursor);
}

GLFWAPI sc_key_cb glfwSetKeyCallback(sc_window* handle, sc_key_cb cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    _GLFW_SWAP(sc_key_cb, window->callbacks.key, cbfun);
    return cbfun;
}

GLFWAPI sc_char_cb glfwSetCharCallback(sc_window* handle, sc_char_cb cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    _GLFW_SWAP(sc_char_cb, window->callbacks.character, cbfun);
    return cbfun;
}

GLFWAPI sc_char_mods_cb glfwSetCharModsCallback(sc_window* handle, sc_char_mods_cb cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    _GLFW_SWAP(sc_char_mods_cb, window->callbacks.charmods, cbfun);
    return cbfun;
}

GLFWAPI sc_win_mouse_button_cb glfwSetMouseButtonCallback(sc_window* handle,
                                                      sc_win_mouse_button_cb cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    _GLFW_SWAP(sc_win_mouse_button_cb, window->callbacks.mouseButton, cbfun);
    return cbfun;
}

GLFWAPI sc_cursor_pos_cb glfwSetCursorPosCallback(sc_window* handle,
                                                  sc_cursor_pos_cb cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    _GLFW_SWAP(sc_cursor_pos_cb, window->callbacks.cursorPos, cbfun);
    return cbfun;
}

GLFWAPI sc_cursor_enter_cb glfwSetCursorEnterCallback(sc_window* handle,
                                                      sc_cursor_enter_cb cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    _GLFW_SWAP(sc_cursor_enter_cb, window->callbacks.cursorEnter, cbfun);
    return cbfun;
}

GLFWAPI sc_scroll_cb glfwSetScrollCallback(sc_window* handle,
                                            sc_scroll_cb cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    _GLFW_SWAP(sc_scroll_cb, window->callbacks.scroll, cbfun);
    return cbfun;
}

GLFWAPI sc_drop_cb glfwSetDropCallback(sc_window* handle, sc_drop_cb cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _sc_window* window = (_sc_window*) handle;
    assert(window != NULL);

    _GLFW_SWAP(sc_drop_cb, window->callbacks.drop, cbfun);
    return cbfun;
}

GLFWAPI void glfwSetClipboardString(sc_window* handle, const char* string)
{
    assert(string != NULL);

    _GLFW_REQUIRE_INIT();
    _glfw.platform.setClipboardString(string);
}

GLFWAPI const char* glfwGetClipboardString(sc_window* handle)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);
    return _glfw.platform.getClipboardString();
}

GLFWAPI double glfwGetTime(void)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(0.0);
    return (double) (_glfwPlatformGetTimerValue() - _glfw.timer.offset) /
        _glfwPlatformGetTimerFrequency();
}

GLFWAPI void glfwSetTime(double time)
{
    _GLFW_REQUIRE_INIT();

    if (!isfinite(time) || time < 0.0 || time > 18446744073.0)
    {
        _glfwInputError(SC_WIN_ERR_INVALID_VALUE, "Invalid time %f", time);
        return;
    }

    _glfw.timer.offset = _glfwPlatformGetTimerValue() -
        (uint64_t) (time * _glfwPlatformGetTimerFrequency());
}

GLFWAPI uint64_t glfwGetTimerValue(void)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(0);
    return _glfwPlatformGetTimerValue();
}

GLFWAPI uint64_t glfwGetTimerFrequency(void)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(0);
    return _glfwPlatformGetTimerFrequency();
}

