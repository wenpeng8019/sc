
#include "internal.h"

#include <stdlib.h>
#include <string.h>


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

bool _glfwConnectNull(int platformID, platform_st* platform)
{
    const platform_st null =
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
        .freeMonitor = wsi_free_monitorNull,
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
    return true;
}

int _glfwInitNull(void)
{
    int scancode;

    memset(g_wsi.null.keycodes, -1, sizeof(g_wsi.null.keycodes));
    memset(g_wsi.null.scancodes, -1, sizeof(g_wsi.null.scancodes));

    g_wsi.null.keycodes[GLFW_NULL_SC_SPACE]         = SC_KEY_SPACE;
    g_wsi.null.keycodes[GLFW_NULL_SC_APOSTROPHE]    = SC_KEY_APOSTROPHE;
    g_wsi.null.keycodes[GLFW_NULL_SC_COMMA]         = SC_KEY_COMMA;
    g_wsi.null.keycodes[GLFW_NULL_SC_MINUS]         = SC_KEY_MINUS;
    g_wsi.null.keycodes[GLFW_NULL_SC_PERIOD]        = SC_KEY_PERIOD;
    g_wsi.null.keycodes[GLFW_NULL_SC_SLASH]         = SC_KEY_SLASH;
    g_wsi.null.keycodes[GLFW_NULL_SC_0]             = SC_KEY_0;
    g_wsi.null.keycodes[GLFW_NULL_SC_1]             = SC_KEY_1;
    g_wsi.null.keycodes[GLFW_NULL_SC_2]             = SC_KEY_2;
    g_wsi.null.keycodes[GLFW_NULL_SC_3]             = SC_KEY_3;
    g_wsi.null.keycodes[GLFW_NULL_SC_4]             = SC_KEY_4;
    g_wsi.null.keycodes[GLFW_NULL_SC_5]             = SC_KEY_5;
    g_wsi.null.keycodes[GLFW_NULL_SC_6]             = SC_KEY_6;
    g_wsi.null.keycodes[GLFW_NULL_SC_7]             = SC_KEY_7;
    g_wsi.null.keycodes[GLFW_NULL_SC_8]             = SC_KEY_8;
    g_wsi.null.keycodes[GLFW_NULL_SC_9]             = SC_KEY_9;
    g_wsi.null.keycodes[GLFW_NULL_SC_SEMICOLON]     = SC_KEY_SEMICOLON;
    g_wsi.null.keycodes[GLFW_NULL_SC_EQUAL]         = SC_KEY_EQUAL;
    g_wsi.null.keycodes[GLFW_NULL_SC_A]             = SC_KEY_A;
    g_wsi.null.keycodes[GLFW_NULL_SC_B]             = SC_KEY_B;
    g_wsi.null.keycodes[GLFW_NULL_SC_C]             = SC_KEY_C;
    g_wsi.null.keycodes[GLFW_NULL_SC_D]             = SC_KEY_D;
    g_wsi.null.keycodes[GLFW_NULL_SC_E]             = SC_KEY_E;
    g_wsi.null.keycodes[GLFW_NULL_SC_F]             = SC_KEY_F;
    g_wsi.null.keycodes[GLFW_NULL_SC_G]             = SC_KEY_G;
    g_wsi.null.keycodes[GLFW_NULL_SC_H]             = SC_KEY_H;
    g_wsi.null.keycodes[GLFW_NULL_SC_I]             = SC_KEY_I;
    g_wsi.null.keycodes[GLFW_NULL_SC_J]             = SC_KEY_J;
    g_wsi.null.keycodes[GLFW_NULL_SC_K]             = SC_KEY_K;
    g_wsi.null.keycodes[GLFW_NULL_SC_L]             = SC_KEY_L;
    g_wsi.null.keycodes[GLFW_NULL_SC_M]             = SC_KEY_M;
    g_wsi.null.keycodes[GLFW_NULL_SC_N]             = SC_KEY_N;
    g_wsi.null.keycodes[GLFW_NULL_SC_O]             = SC_KEY_O;
    g_wsi.null.keycodes[GLFW_NULL_SC_P]             = SC_KEY_P;
    g_wsi.null.keycodes[GLFW_NULL_SC_Q]             = SC_KEY_Q;
    g_wsi.null.keycodes[GLFW_NULL_SC_R]             = SC_KEY_R;
    g_wsi.null.keycodes[GLFW_NULL_SC_S]             = SC_KEY_S;
    g_wsi.null.keycodes[GLFW_NULL_SC_T]             = SC_KEY_T;
    g_wsi.null.keycodes[GLFW_NULL_SC_U]             = SC_KEY_U;
    g_wsi.null.keycodes[GLFW_NULL_SC_V]             = SC_KEY_V;
    g_wsi.null.keycodes[GLFW_NULL_SC_W]             = SC_KEY_W;
    g_wsi.null.keycodes[GLFW_NULL_SC_X]             = SC_KEY_X;
    g_wsi.null.keycodes[GLFW_NULL_SC_Y]             = SC_KEY_Y;
    g_wsi.null.keycodes[GLFW_NULL_SC_Z]             = SC_KEY_Z;
    g_wsi.null.keycodes[GLFW_NULL_SC_LEFT_BRACKET]  = SC_KEY_LEFT_BRACKET;
    g_wsi.null.keycodes[GLFW_NULL_SC_BACKSLASH]     = SC_KEY_BACKSLASH;
    g_wsi.null.keycodes[GLFW_NULL_SC_RIGHT_BRACKET] = SC_KEY_RIGHT_BRACKET;
    g_wsi.null.keycodes[GLFW_NULL_SC_GRAVE_ACCENT]  = SC_KEY_GRAVE_ACCENT;
    g_wsi.null.keycodes[GLFW_NULL_SC_WORLD_1]       = SC_KEY_WORLD_1;
    g_wsi.null.keycodes[GLFW_NULL_SC_WORLD_2]       = SC_KEY_WORLD_2;
    g_wsi.null.keycodes[GLFW_NULL_SC_ESCAPE]        = SC_KEY_ESCAPE;
    g_wsi.null.keycodes[GLFW_NULL_SC_ENTER]         = SC_KEY_ENTER;
    g_wsi.null.keycodes[GLFW_NULL_SC_TAB]           = SC_KEY_TAB;
    g_wsi.null.keycodes[GLFW_NULL_SC_BACKSPACE]     = SC_KEY_BACKSPACE;
    g_wsi.null.keycodes[GLFW_NULL_SC_INSERT]        = SC_KEY_INSERT;
    g_wsi.null.keycodes[GLFW_NULL_SC_DELETE]        = SC_KEY_DELETE;
    g_wsi.null.keycodes[GLFW_NULL_SC_RIGHT]         = SC_KEY_RIGHT;
    g_wsi.null.keycodes[GLFW_NULL_SC_LEFT]          = SC_KEY_LEFT;
    g_wsi.null.keycodes[GLFW_NULL_SC_DOWN]          = SC_KEY_DOWN;
    g_wsi.null.keycodes[GLFW_NULL_SC_UP]            = SC_KEY_UP;
    g_wsi.null.keycodes[GLFW_NULL_SC_PAGE_UP]       = SC_KEY_PAGE_UP;
    g_wsi.null.keycodes[GLFW_NULL_SC_PAGE_DOWN]     = SC_KEY_PAGE_DOWN;
    g_wsi.null.keycodes[GLFW_NULL_SC_HOME]          = SC_KEY_HOME;
    g_wsi.null.keycodes[GLFW_NULL_SC_END]           = SC_KEY_END;
    g_wsi.null.keycodes[GLFW_NULL_SC_CAPS_LOCK]     = SC_KEY_CAPS_LOCK;
    g_wsi.null.keycodes[GLFW_NULL_SC_SCROLL_LOCK]   = SC_KEY_SCROLL_LOCK;
    g_wsi.null.keycodes[GLFW_NULL_SC_NUM_LOCK]      = SC_KEY_NUM_LOCK;
    g_wsi.null.keycodes[GLFW_NULL_SC_PRINT_SCREEN]  = SC_KEY_PRINT_SCREEN;
    g_wsi.null.keycodes[GLFW_NULL_SC_PAUSE]         = SC_KEY_PAUSE;
    g_wsi.null.keycodes[GLFW_NULL_SC_F1]            = SC_KEY_F1;
    g_wsi.null.keycodes[GLFW_NULL_SC_F2]            = SC_KEY_F2;
    g_wsi.null.keycodes[GLFW_NULL_SC_F3]            = SC_KEY_F3;
    g_wsi.null.keycodes[GLFW_NULL_SC_F4]            = SC_KEY_F4;
    g_wsi.null.keycodes[GLFW_NULL_SC_F5]            = SC_KEY_F5;
    g_wsi.null.keycodes[GLFW_NULL_SC_F6]            = SC_KEY_F6;
    g_wsi.null.keycodes[GLFW_NULL_SC_F7]            = SC_KEY_F7;
    g_wsi.null.keycodes[GLFW_NULL_SC_F8]            = SC_KEY_F8;
    g_wsi.null.keycodes[GLFW_NULL_SC_F9]            = SC_KEY_F9;
    g_wsi.null.keycodes[GLFW_NULL_SC_F10]           = SC_KEY_F10;
    g_wsi.null.keycodes[GLFW_NULL_SC_F11]           = SC_KEY_F11;
    g_wsi.null.keycodes[GLFW_NULL_SC_F12]           = SC_KEY_F12;
    g_wsi.null.keycodes[GLFW_NULL_SC_F13]           = SC_KEY_F13;
    g_wsi.null.keycodes[GLFW_NULL_SC_F14]           = SC_KEY_F14;
    g_wsi.null.keycodes[GLFW_NULL_SC_F15]           = SC_KEY_F15;
    g_wsi.null.keycodes[GLFW_NULL_SC_F16]           = SC_KEY_F16;
    g_wsi.null.keycodes[GLFW_NULL_SC_F17]           = SC_KEY_F17;
    g_wsi.null.keycodes[GLFW_NULL_SC_F18]           = SC_KEY_F18;
    g_wsi.null.keycodes[GLFW_NULL_SC_F19]           = SC_KEY_F19;
    g_wsi.null.keycodes[GLFW_NULL_SC_F20]           = SC_KEY_F20;
    g_wsi.null.keycodes[GLFW_NULL_SC_F21]           = SC_KEY_F21;
    g_wsi.null.keycodes[GLFW_NULL_SC_F22]           = SC_KEY_F22;
    g_wsi.null.keycodes[GLFW_NULL_SC_F23]           = SC_KEY_F23;
    g_wsi.null.keycodes[GLFW_NULL_SC_F24]           = SC_KEY_F24;
    g_wsi.null.keycodes[GLFW_NULL_SC_F25]           = SC_KEY_F25;
    g_wsi.null.keycodes[GLFW_NULL_SC_KP_0]          = SC_KEY_KP_0;
    g_wsi.null.keycodes[GLFW_NULL_SC_KP_1]          = SC_KEY_KP_1;
    g_wsi.null.keycodes[GLFW_NULL_SC_KP_2]          = SC_KEY_KP_2;
    g_wsi.null.keycodes[GLFW_NULL_SC_KP_3]          = SC_KEY_KP_3;
    g_wsi.null.keycodes[GLFW_NULL_SC_KP_4]          = SC_KEY_KP_4;
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

    _glfwPollMonitorsNull();
    return true;
}

void _glfwTerminateNull(void)
{
    wsi_free(g_wsi.null.clipboardString);
    memset(&g_wsi.null, 0, sizeof(g_wsi.null));
}

