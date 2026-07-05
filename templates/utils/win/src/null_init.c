
#include "internal.h"

#include <stdlib.h>
#include <string.h>


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

GLFWbool _glfwConnectNull(int platformID, _GLFWplatform* platform)
{
    const _GLFWplatform null =
    {
        .platformID = SC_PLATFORM_NULL,
        .init = _glfwInitNull,
        .terminate = _glfwTerminateNull,
        .getCursorPos = _glfwGetCursorPosNull,
        .setCursorPos = _glfwSetCursorPosNull,
        .setCursorMode = _glfwSetCursorModeNull,
        .setRawMouseMotion = _glfwSetRawMouseMotionNull,
        .rawMouseMotionSupported = _glfwRawMouseMotionSupportedNull,
        .createCursor = _glfwCreateCursorNull,
        .createStandardCursor = _glfwCreateStandardCursorNull,
        .destroyCursor = _glfwDestroyCursorNull,
        .setCursor = _glfwSetCursorNull,
        .getScancodeName = _glfwGetScancodeNameNull,
        .getKeyScancode = _glfwGetKeyScancodeNull,
        .setClipboardString = _glfwSetClipboardStringNull,
        .getClipboardString = _glfwGetClipboardStringNull,
        .freeMonitor = _glfwFreeMonitorNull,
        .getMonitorPos = _glfwGetMonitorPosNull,
        .getMonitorContentScale = _glfwGetMonitorContentScaleNull,
        .getMonitorWorkarea = _glfwGetMonitorWorkareaNull,
        .getVideoModes = _glfwGetVideoModesNull,
        .getVideoMode = _glfwGetVideoModeNull,
        .getGammaRamp = _glfwGetGammaRampNull,
        .setGammaRamp = _glfwSetGammaRampNull,
        .createWindow = _glfwCreateWindowNull,
        .destroyWindow = _glfwDestroyWindowNull,
        .setWindowTitle = _glfwSetWindowTitleNull,
        .setWindowIcon = _glfwSetWindowIconNull,
        .getWindowPos = _glfwGetWindowPosNull,
        .setWindowPos = _glfwSetWindowPosNull,
        .getWindowSize = _glfwGetWindowSizeNull,
        .setWindowSize = _glfwSetWindowSizeNull,
        .setWindowSizeLimits = _glfwSetWindowSizeLimitsNull,
        .setWindowAspectRatio = _glfwSetWindowAspectRatioNull,
        .getWindowFrameSize = _glfwGetWindowFrameSizeNull,
        .getWindowContentScale = _glfwGetWindowContentScaleNull,
        .iconifyWindow = _glfwIconifyWindowNull,
        .restoreWindow = _glfwRestoreWindowNull,
        .maximizeWindow = _glfwMaximizeWindowNull,
        .showWindow = _glfwShowWindowNull,
        .hideWindow = _glfwHideWindowNull,
        .requestWindowAttention = _glfwRequestWindowAttentionNull,
        .focusWindow = _glfwFocusWindowNull,
        .setWindowMonitor = _glfwSetWindowMonitorNull,
        .windowFocused = _glfwWindowFocusedNull,
        .windowIconified = _glfwWindowIconifiedNull,
        .windowVisible = _glfwWindowVisibleNull,
        .windowMaximized = _glfwWindowMaximizedNull,
        .windowHovered = _glfwWindowHoveredNull,
        .getWindowOpacity = _glfwGetWindowOpacityNull,
        .setWindowResizable = _glfwSetWindowResizableNull,
        .setWindowDecorated = _glfwSetWindowDecoratedNull,
        .setWindowFloating = _glfwSetWindowFloatingNull,
        .setWindowOpacity = _glfwSetWindowOpacityNull,
        .setWindowMousePassthrough = _glfwSetWindowMousePassthroughNull,
        .pollEvents = _glfwPollEventsNull,
        .waitEvents = _glfwWaitEventsNull,
        .waitEventsTimeout = _glfwWaitEventsTimeoutNull,
        .postEmptyEvent = _glfwPostEmptyEventNull,
    };

    *platform = null;
    return GLFW_TRUE;
}

int _glfwInitNull(void)
{
    int scancode;

    memset(_glfw.null.keycodes, -1, sizeof(_glfw.null.keycodes));
    memset(_glfw.null.scancodes, -1, sizeof(_glfw.null.scancodes));

    _glfw.null.keycodes[GLFW_NULL_SC_SPACE]         = SC_KEY_SPACE;
    _glfw.null.keycodes[GLFW_NULL_SC_APOSTROPHE]    = SC_KEY_APOSTROPHE;
    _glfw.null.keycodes[GLFW_NULL_SC_COMMA]         = SC_KEY_COMMA;
    _glfw.null.keycodes[GLFW_NULL_SC_MINUS]         = SC_KEY_MINUS;
    _glfw.null.keycodes[GLFW_NULL_SC_PERIOD]        = SC_KEY_PERIOD;
    _glfw.null.keycodes[GLFW_NULL_SC_SLASH]         = SC_KEY_SLASH;
    _glfw.null.keycodes[GLFW_NULL_SC_0]             = SC_KEY_0;
    _glfw.null.keycodes[GLFW_NULL_SC_1]             = SC_KEY_1;
    _glfw.null.keycodes[GLFW_NULL_SC_2]             = SC_KEY_2;
    _glfw.null.keycodes[GLFW_NULL_SC_3]             = SC_KEY_3;
    _glfw.null.keycodes[GLFW_NULL_SC_4]             = SC_KEY_4;
    _glfw.null.keycodes[GLFW_NULL_SC_5]             = SC_KEY_5;
    _glfw.null.keycodes[GLFW_NULL_SC_6]             = SC_KEY_6;
    _glfw.null.keycodes[GLFW_NULL_SC_7]             = SC_KEY_7;
    _glfw.null.keycodes[GLFW_NULL_SC_8]             = SC_KEY_8;
    _glfw.null.keycodes[GLFW_NULL_SC_9]             = SC_KEY_9;
    _glfw.null.keycodes[GLFW_NULL_SC_SEMICOLON]     = SC_KEY_SEMICOLON;
    _glfw.null.keycodes[GLFW_NULL_SC_EQUAL]         = SC_KEY_EQUAL;
    _glfw.null.keycodes[GLFW_NULL_SC_A]             = SC_KEY_A;
    _glfw.null.keycodes[GLFW_NULL_SC_B]             = SC_KEY_B;
    _glfw.null.keycodes[GLFW_NULL_SC_C]             = SC_KEY_C;
    _glfw.null.keycodes[GLFW_NULL_SC_D]             = SC_KEY_D;
    _glfw.null.keycodes[GLFW_NULL_SC_E]             = SC_KEY_E;
    _glfw.null.keycodes[GLFW_NULL_SC_F]             = SC_KEY_F;
    _glfw.null.keycodes[GLFW_NULL_SC_G]             = SC_KEY_G;
    _glfw.null.keycodes[GLFW_NULL_SC_H]             = SC_KEY_H;
    _glfw.null.keycodes[GLFW_NULL_SC_I]             = SC_KEY_I;
    _glfw.null.keycodes[GLFW_NULL_SC_J]             = SC_KEY_J;
    _glfw.null.keycodes[GLFW_NULL_SC_K]             = SC_KEY_K;
    _glfw.null.keycodes[GLFW_NULL_SC_L]             = SC_KEY_L;
    _glfw.null.keycodes[GLFW_NULL_SC_M]             = SC_KEY_M;
    _glfw.null.keycodes[GLFW_NULL_SC_N]             = SC_KEY_N;
    _glfw.null.keycodes[GLFW_NULL_SC_O]             = SC_KEY_O;
    _glfw.null.keycodes[GLFW_NULL_SC_P]             = SC_KEY_P;
    _glfw.null.keycodes[GLFW_NULL_SC_Q]             = SC_KEY_Q;
    _glfw.null.keycodes[GLFW_NULL_SC_R]             = SC_KEY_R;
    _glfw.null.keycodes[GLFW_NULL_SC_S]             = SC_KEY_S;
    _glfw.null.keycodes[GLFW_NULL_SC_T]             = SC_KEY_T;
    _glfw.null.keycodes[GLFW_NULL_SC_U]             = SC_KEY_U;
    _glfw.null.keycodes[GLFW_NULL_SC_V]             = SC_KEY_V;
    _glfw.null.keycodes[GLFW_NULL_SC_W]             = SC_KEY_W;
    _glfw.null.keycodes[GLFW_NULL_SC_X]             = SC_KEY_X;
    _glfw.null.keycodes[GLFW_NULL_SC_Y]             = SC_KEY_Y;
    _glfw.null.keycodes[GLFW_NULL_SC_Z]             = SC_KEY_Z;
    _glfw.null.keycodes[GLFW_NULL_SC_LEFT_BRACKET]  = SC_KEY_LEFT_BRACKET;
    _glfw.null.keycodes[GLFW_NULL_SC_BACKSLASH]     = SC_KEY_BACKSLASH;
    _glfw.null.keycodes[GLFW_NULL_SC_RIGHT_BRACKET] = SC_KEY_RIGHT_BRACKET;
    _glfw.null.keycodes[GLFW_NULL_SC_GRAVE_ACCENT]  = SC_KEY_GRAVE_ACCENT;
    _glfw.null.keycodes[GLFW_NULL_SC_WORLD_1]       = SC_KEY_WORLD_1;
    _glfw.null.keycodes[GLFW_NULL_SC_WORLD_2]       = SC_KEY_WORLD_2;
    _glfw.null.keycodes[GLFW_NULL_SC_ESCAPE]        = SC_KEY_ESCAPE;
    _glfw.null.keycodes[GLFW_NULL_SC_ENTER]         = SC_KEY_ENTER;
    _glfw.null.keycodes[GLFW_NULL_SC_TAB]           = SC_KEY_TAB;
    _glfw.null.keycodes[GLFW_NULL_SC_BACKSPACE]     = SC_KEY_BACKSPACE;
    _glfw.null.keycodes[GLFW_NULL_SC_INSERT]        = SC_KEY_INSERT;
    _glfw.null.keycodes[GLFW_NULL_SC_DELETE]        = SC_KEY_DELETE;
    _glfw.null.keycodes[GLFW_NULL_SC_RIGHT]         = SC_KEY_RIGHT;
    _glfw.null.keycodes[GLFW_NULL_SC_LEFT]          = SC_KEY_LEFT;
    _glfw.null.keycodes[GLFW_NULL_SC_DOWN]          = SC_KEY_DOWN;
    _glfw.null.keycodes[GLFW_NULL_SC_UP]            = SC_KEY_UP;
    _glfw.null.keycodes[GLFW_NULL_SC_PAGE_UP]       = SC_KEY_PAGE_UP;
    _glfw.null.keycodes[GLFW_NULL_SC_PAGE_DOWN]     = SC_KEY_PAGE_DOWN;
    _glfw.null.keycodes[GLFW_NULL_SC_HOME]          = SC_KEY_HOME;
    _glfw.null.keycodes[GLFW_NULL_SC_END]           = SC_KEY_END;
    _glfw.null.keycodes[GLFW_NULL_SC_CAPS_LOCK]     = SC_KEY_CAPS_LOCK;
    _glfw.null.keycodes[GLFW_NULL_SC_SCROLL_LOCK]   = SC_KEY_SCROLL_LOCK;
    _glfw.null.keycodes[GLFW_NULL_SC_NUM_LOCK]      = SC_KEY_NUM_LOCK;
    _glfw.null.keycodes[GLFW_NULL_SC_PRINT_SCREEN]  = SC_KEY_PRINT_SCREEN;
    _glfw.null.keycodes[GLFW_NULL_SC_PAUSE]         = SC_KEY_PAUSE;
    _glfw.null.keycodes[GLFW_NULL_SC_F1]            = SC_KEY_F1;
    _glfw.null.keycodes[GLFW_NULL_SC_F2]            = SC_KEY_F2;
    _glfw.null.keycodes[GLFW_NULL_SC_F3]            = SC_KEY_F3;
    _glfw.null.keycodes[GLFW_NULL_SC_F4]            = SC_KEY_F4;
    _glfw.null.keycodes[GLFW_NULL_SC_F5]            = SC_KEY_F5;
    _glfw.null.keycodes[GLFW_NULL_SC_F6]            = SC_KEY_F6;
    _glfw.null.keycodes[GLFW_NULL_SC_F7]            = SC_KEY_F7;
    _glfw.null.keycodes[GLFW_NULL_SC_F8]            = SC_KEY_F8;
    _glfw.null.keycodes[GLFW_NULL_SC_F9]            = SC_KEY_F9;
    _glfw.null.keycodes[GLFW_NULL_SC_F10]           = SC_KEY_F10;
    _glfw.null.keycodes[GLFW_NULL_SC_F11]           = SC_KEY_F11;
    _glfw.null.keycodes[GLFW_NULL_SC_F12]           = SC_KEY_F12;
    _glfw.null.keycodes[GLFW_NULL_SC_F13]           = SC_KEY_F13;
    _glfw.null.keycodes[GLFW_NULL_SC_F14]           = SC_KEY_F14;
    _glfw.null.keycodes[GLFW_NULL_SC_F15]           = SC_KEY_F15;
    _glfw.null.keycodes[GLFW_NULL_SC_F16]           = SC_KEY_F16;
    _glfw.null.keycodes[GLFW_NULL_SC_F17]           = SC_KEY_F17;
    _glfw.null.keycodes[GLFW_NULL_SC_F18]           = SC_KEY_F18;
    _glfw.null.keycodes[GLFW_NULL_SC_F19]           = SC_KEY_F19;
    _glfw.null.keycodes[GLFW_NULL_SC_F20]           = SC_KEY_F20;
    _glfw.null.keycodes[GLFW_NULL_SC_F21]           = SC_KEY_F21;
    _glfw.null.keycodes[GLFW_NULL_SC_F22]           = SC_KEY_F22;
    _glfw.null.keycodes[GLFW_NULL_SC_F23]           = SC_KEY_F23;
    _glfw.null.keycodes[GLFW_NULL_SC_F24]           = SC_KEY_F24;
    _glfw.null.keycodes[GLFW_NULL_SC_F25]           = SC_KEY_F25;
    _glfw.null.keycodes[GLFW_NULL_SC_KP_0]          = SC_KEY_KP_0;
    _glfw.null.keycodes[GLFW_NULL_SC_KP_1]          = SC_KEY_KP_1;
    _glfw.null.keycodes[GLFW_NULL_SC_KP_2]          = SC_KEY_KP_2;
    _glfw.null.keycodes[GLFW_NULL_SC_KP_3]          = SC_KEY_KP_3;
    _glfw.null.keycodes[GLFW_NULL_SC_KP_4]          = SC_KEY_KP_4;
    _glfw.null.keycodes[GLFW_NULL_SC_KP_5]          = SC_KEY_KP_5;
    _glfw.null.keycodes[GLFW_NULL_SC_KP_6]          = SC_KEY_KP_6;
    _glfw.null.keycodes[GLFW_NULL_SC_KP_7]          = SC_KEY_KP_7;
    _glfw.null.keycodes[GLFW_NULL_SC_KP_8]          = SC_KEY_KP_8;
    _glfw.null.keycodes[GLFW_NULL_SC_KP_9]          = SC_KEY_KP_9;
    _glfw.null.keycodes[GLFW_NULL_SC_KP_DECIMAL]    = SC_KEY_KP_DECIMAL;
    _glfw.null.keycodes[GLFW_NULL_SC_KP_DIVIDE]     = SC_KEY_KP_DIVIDE;
    _glfw.null.keycodes[GLFW_NULL_SC_KP_MULTIPLY]   = SC_KEY_KP_MULTIPLY;
    _glfw.null.keycodes[GLFW_NULL_SC_KP_SUBTRACT]   = SC_KEY_KP_SUBTRACT;
    _glfw.null.keycodes[GLFW_NULL_SC_KP_ADD]        = SC_KEY_KP_ADD;
    _glfw.null.keycodes[GLFW_NULL_SC_KP_ENTER]      = SC_KEY_KP_ENTER;
    _glfw.null.keycodes[GLFW_NULL_SC_KP_EQUAL]      = SC_KEY_KP_EQUAL;
    _glfw.null.keycodes[GLFW_NULL_SC_LEFT_SHIFT]    = SC_KEY_LEFT_SHIFT;
    _glfw.null.keycodes[GLFW_NULL_SC_LEFT_CONTROL]  = SC_KEY_LEFT_CONTROL;
    _glfw.null.keycodes[GLFW_NULL_SC_LEFT_ALT]      = SC_KEY_LEFT_ALT;
    _glfw.null.keycodes[GLFW_NULL_SC_LEFT_SUPER]    = SC_KEY_LEFT_SUPER;
    _glfw.null.keycodes[GLFW_NULL_SC_RIGHT_SHIFT]   = SC_KEY_RIGHT_SHIFT;
    _glfw.null.keycodes[GLFW_NULL_SC_RIGHT_CONTROL] = SC_KEY_RIGHT_CONTROL;
    _glfw.null.keycodes[GLFW_NULL_SC_RIGHT_ALT]     = SC_KEY_RIGHT_ALT;
    _glfw.null.keycodes[GLFW_NULL_SC_RIGHT_SUPER]   = SC_KEY_RIGHT_SUPER;
    _glfw.null.keycodes[GLFW_NULL_SC_MENU]          = SC_KEY_MENU;

    for (scancode = GLFW_NULL_SC_FIRST;  scancode < GLFW_NULL_SC_LAST;  scancode++)
    {
        if (_glfw.null.keycodes[scancode] > 0)
            _glfw.null.scancodes[_glfw.null.keycodes[scancode]] = scancode;
    }

    _glfwPollMonitorsNull();
    return GLFW_TRUE;
}

void _glfwTerminateNull(void)
{
    free(_glfw.null.clipboardString);
    _glfwTerminateOSMesa();
    _glfwTerminateEGL();
    memset(&_glfw.null, 0, sizeof(_glfw.null));
}

