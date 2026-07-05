
#include "internal.h"

#if defined(_GLFW_WAYLAND)

#include <errno.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>

#include "wayland-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"
#include "pointer-constraints-unstable-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "xdg-activation-v1-client-protocol.h"
#include "idle-inhibit-unstable-v1-client-protocol.h"

// NOTE: Versions of wayland-scanner prior to 1.17.91 named every global array of
//       wl_interface pointers 'types', making it impossible to combine several unmodified
//       private-code files into a single compilation unit
// HACK: We override this name with a macro for each file, allowing them to coexist

#define types _glfw_wayland_types
#include "wayland-client-protocol-code.h"
#undef types

#define types _glfw_xdg_shell_types
#include "xdg-shell-client-protocol-code.h"
#undef types

#define types _glfw_xdg_decoration_types
#include "xdg-decoration-unstable-v1-client-protocol-code.h"
#undef types

#define types _glfw_viewporter_types
#include "viewporter-client-protocol-code.h"
#undef types

#define types _glfw_relative_pointer_types
#include "relative-pointer-unstable-v1-client-protocol-code.h"
#undef types

#define types _glfw_pointer_constraints_types
#include "pointer-constraints-unstable-v1-client-protocol-code.h"
#undef types

#define types _glfw_fractional_scale_types
#include "fractional-scale-v1-client-protocol-code.h"
#undef types

#define types _glfw_xdg_activation_types
#include "xdg-activation-v1-client-protocol-code.h"
#undef types

#define types _glfw_idle_inhibit_types
#include "idle-inhibit-unstable-v1-client-protocol-code.h"
#undef types

static void wmBaseHandlePing(void* userData,
                             struct xdg_wm_base* wmBase,
                             uint32_t serial)
{
    xdg_wm_base_pong(wmBase, serial);
}

static const struct xdg_wm_base_listener wmBaseListener =
{
    wmBaseHandlePing
};

static void registryHandleGlobal(void* userData,
                                 struct wl_registry* registry,
                                 uint32_t name,
                                 const char* interface,
                                 uint32_t version)
{
    if (strcmp(interface, "wl_compositor") == 0)
    {
        _glfw.wl.compositor =
            wl_registry_bind(registry, name, &wl_compositor_interface,
                             _glfw_min(3, version));
    }
    else if (strcmp(interface, "wl_subcompositor") == 0)
    {
        _glfw.wl.subcompositor =
            wl_registry_bind(registry, name, &wl_subcompositor_interface, 1);
    }
    else if (strcmp(interface, "wl_shm") == 0)
    {
        _glfw.wl.shm =
            wl_registry_bind(registry, name, &wl_shm_interface, 1);
    }
    else if (strcmp(interface, "wl_output") == 0)
    {
        _glfwAddOutputWayland(name, version);
    }
    else if (strcmp(interface, "wl_seat") == 0)
    {
        if (!_glfw.wl.seat)
        {
            _glfw.wl.seat =
                wl_registry_bind(registry, name, &wl_seat_interface,
                                 _glfw_min(8, version));
            _glfwAddSeatListenerWayland(_glfw.wl.seat);
        }
    }
    else if (strcmp(interface, "wl_data_device_manager") == 0)
    {
        if (!_glfw.wl.dataDeviceManager)
        {
            _glfw.wl.dataDeviceManager =
                wl_registry_bind(registry, name,
                                 &wl_data_device_manager_interface, 1);
        }
    }
    else if (strcmp(interface, "xdg_wm_base") == 0)
    {
        _glfw.wl.wmBase =
            wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(_glfw.wl.wmBase, &wmBaseListener, NULL);
    }
    else if (strcmp(interface, "zxdg_decoration_manager_v1") == 0)
    {
        _glfw.wl.decorationManager =
            wl_registry_bind(registry, name,
                             &zxdg_decoration_manager_v1_interface,
                             1);
    }
    else if (strcmp(interface, "wp_viewporter") == 0)
    {
        _glfw.wl.viewporter =
            wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
    }
    else if (strcmp(interface, "zwp_relative_pointer_manager_v1") == 0)
    {
        _glfw.wl.relativePointerManager =
            wl_registry_bind(registry, name,
                             &zwp_relative_pointer_manager_v1_interface,
                             1);
    }
    else if (strcmp(interface, "zwp_pointer_constraints_v1") == 0)
    {
        _glfw.wl.pointerConstraints =
            wl_registry_bind(registry, name,
                             &zwp_pointer_constraints_v1_interface,
                             1);
    }
    else if (strcmp(interface, "zwp_idle_inhibit_manager_v1") == 0)
    {
        _glfw.wl.idleInhibitManager =
            wl_registry_bind(registry, name,
                             &zwp_idle_inhibit_manager_v1_interface,
                             1);
    }
    else if (strcmp(interface, "xdg_activation_v1") == 0)
    {
        _glfw.wl.activationManager =
            wl_registry_bind(registry, name,
                             &xdg_activation_v1_interface,
                             1);
    }
    else if (strcmp(interface, "wp_fractional_scale_manager_v1") == 0)
    {
        _glfw.wl.fractionalScaleManager =
            wl_registry_bind(registry, name,
                             &wp_fractional_scale_manager_v1_interface,
                             1);
    }
}

static void registryHandleGlobalRemove(void* userData,
                                       struct wl_registry* registry,
                                       uint32_t name)
{
    for (int i = 0; i < _glfw.monitorCount; ++i)
    {
        _sc_monitor* monitor = _glfw.monitors[i];
        if (monitor->wl.name == name)
        {
            _glfwInputMonitor(monitor, SC_DISCONNECTED, 0);
            return;
        }
    }
}


static const struct wl_registry_listener registryListener =
{
    registryHandleGlobal,
    registryHandleGlobalRemove
};

void libdecorHandleError(struct libdecor* context,
                         enum libdecor_error error,
                         const char* message)
{
    _glfwInputError(SC_WIN_ERR_PLATFORM_ERROR,
                    "Wayland: libdecor error %u: %s",
                    error, message);
}

static const struct libdecor_interface libdecorInterface =
{
    libdecorHandleError
};

static void libdecorReadyCallback(void* userData,
                                  struct wl_callback* callback,
                                  uint32_t time)
{
    _glfw.wl.libdecor.ready = GLFW_TRUE;
    wl_callback_destroy(callback);
}

static const struct wl_callback_listener libdecorReadyListener =
{
    libdecorReadyCallback
};

// Create key code translation tables
//
static void createKeyTables(void)
{
    memset(_glfw.wl.keycodes, -1, sizeof(_glfw.wl.keycodes));
    memset(_glfw.wl.scancodes, -1, sizeof(_glfw.wl.scancodes));

    _glfw.wl.keycodes[KEY_GRAVE]      = SC_KEY_GRAVE_ACCENT;
    _glfw.wl.keycodes[KEY_1]          = SC_KEY_1;
    _glfw.wl.keycodes[KEY_2]          = SC_KEY_2;
    _glfw.wl.keycodes[KEY_3]          = SC_KEY_3;
    _glfw.wl.keycodes[KEY_4]          = SC_KEY_4;
    _glfw.wl.keycodes[KEY_5]          = SC_KEY_5;
    _glfw.wl.keycodes[KEY_6]          = SC_KEY_6;
    _glfw.wl.keycodes[KEY_7]          = SC_KEY_7;
    _glfw.wl.keycodes[KEY_8]          = SC_KEY_8;
    _glfw.wl.keycodes[KEY_9]          = SC_KEY_9;
    _glfw.wl.keycodes[KEY_0]          = SC_KEY_0;
    _glfw.wl.keycodes[KEY_SPACE]      = SC_KEY_SPACE;
    _glfw.wl.keycodes[KEY_MINUS]      = SC_KEY_MINUS;
    _glfw.wl.keycodes[KEY_EQUAL]      = SC_KEY_EQUAL;
    _glfw.wl.keycodes[KEY_Q]          = SC_KEY_Q;
    _glfw.wl.keycodes[KEY_W]          = SC_KEY_W;
    _glfw.wl.keycodes[KEY_E]          = SC_KEY_E;
    _glfw.wl.keycodes[KEY_R]          = SC_KEY_R;
    _glfw.wl.keycodes[KEY_T]          = SC_KEY_T;
    _glfw.wl.keycodes[KEY_Y]          = SC_KEY_Y;
    _glfw.wl.keycodes[KEY_U]          = SC_KEY_U;
    _glfw.wl.keycodes[KEY_I]          = SC_KEY_I;
    _glfw.wl.keycodes[KEY_O]          = SC_KEY_O;
    _glfw.wl.keycodes[KEY_P]          = SC_KEY_P;
    _glfw.wl.keycodes[KEY_LEFTBRACE]  = SC_KEY_LEFT_BRACKET;
    _glfw.wl.keycodes[KEY_RIGHTBRACE] = SC_KEY_RIGHT_BRACKET;
    _glfw.wl.keycodes[KEY_A]          = SC_KEY_A;
    _glfw.wl.keycodes[KEY_S]          = SC_KEY_S;
    _glfw.wl.keycodes[KEY_D]          = SC_KEY_D;
    _glfw.wl.keycodes[KEY_F]          = SC_KEY_F;
    _glfw.wl.keycodes[KEY_G]          = SC_KEY_G;
    _glfw.wl.keycodes[KEY_H]          = SC_KEY_H;
    _glfw.wl.keycodes[KEY_J]          = SC_KEY_J;
    _glfw.wl.keycodes[KEY_K]          = SC_KEY_K;
    _glfw.wl.keycodes[KEY_L]          = SC_KEY_L;
    _glfw.wl.keycodes[KEY_SEMICOLON]  = SC_KEY_SEMICOLON;
    _glfw.wl.keycodes[KEY_APOSTROPHE] = SC_KEY_APOSTROPHE;
    _glfw.wl.keycodes[KEY_Z]          = SC_KEY_Z;
    _glfw.wl.keycodes[KEY_X]          = SC_KEY_X;
    _glfw.wl.keycodes[KEY_C]          = SC_KEY_C;
    _glfw.wl.keycodes[KEY_V]          = SC_KEY_V;
    _glfw.wl.keycodes[KEY_B]          = SC_KEY_B;
    _glfw.wl.keycodes[KEY_N]          = SC_KEY_N;
    _glfw.wl.keycodes[KEY_M]          = SC_KEY_M;
    _glfw.wl.keycodes[KEY_COMMA]      = SC_KEY_COMMA;
    _glfw.wl.keycodes[KEY_DOT]        = SC_KEY_PERIOD;
    _glfw.wl.keycodes[KEY_SLASH]      = SC_KEY_SLASH;
    _glfw.wl.keycodes[KEY_BACKSLASH]  = SC_KEY_BACKSLASH;
    _glfw.wl.keycodes[KEY_ESC]        = SC_KEY_ESCAPE;
    _glfw.wl.keycodes[KEY_TAB]        = SC_KEY_TAB;
    _glfw.wl.keycodes[KEY_LEFTSHIFT]  = SC_KEY_LEFT_SHIFT;
    _glfw.wl.keycodes[KEY_RIGHTSHIFT] = SC_KEY_RIGHT_SHIFT;
    _glfw.wl.keycodes[KEY_LEFTCTRL]   = SC_KEY_LEFT_CONTROL;
    _glfw.wl.keycodes[KEY_RIGHTCTRL]  = SC_KEY_RIGHT_CONTROL;
    _glfw.wl.keycodes[KEY_LEFTALT]    = SC_KEY_LEFT_ALT;
    _glfw.wl.keycodes[KEY_RIGHTALT]   = SC_KEY_RIGHT_ALT;
    _glfw.wl.keycodes[KEY_LEFTMETA]   = SC_KEY_LEFT_SUPER;
    _glfw.wl.keycodes[KEY_RIGHTMETA]  = SC_KEY_RIGHT_SUPER;
    _glfw.wl.keycodes[KEY_COMPOSE]    = SC_KEY_MENU;
    _glfw.wl.keycodes[KEY_NUMLOCK]    = SC_KEY_NUM_LOCK;
    _glfw.wl.keycodes[KEY_CAPSLOCK]   = SC_KEY_CAPS_LOCK;
    _glfw.wl.keycodes[KEY_PRINT]      = SC_KEY_PRINT_SCREEN;
    _glfw.wl.keycodes[KEY_SCROLLLOCK] = SC_KEY_SCROLL_LOCK;
    _glfw.wl.keycodes[KEY_PAUSE]      = SC_KEY_PAUSE;
    _glfw.wl.keycodes[KEY_DELETE]     = SC_KEY_DELETE;
    _glfw.wl.keycodes[KEY_BACKSPACE]  = SC_KEY_BACKSPACE;
    _glfw.wl.keycodes[KEY_ENTER]      = SC_KEY_ENTER;
    _glfw.wl.keycodes[KEY_HOME]       = SC_KEY_HOME;
    _glfw.wl.keycodes[KEY_END]        = SC_KEY_END;
    _glfw.wl.keycodes[KEY_PAGEUP]     = SC_KEY_PAGE_UP;
    _glfw.wl.keycodes[KEY_PAGEDOWN]   = SC_KEY_PAGE_DOWN;
    _glfw.wl.keycodes[KEY_INSERT]     = SC_KEY_INSERT;
    _glfw.wl.keycodes[KEY_LEFT]       = SC_KEY_LEFT;
    _glfw.wl.keycodes[KEY_RIGHT]      = SC_KEY_RIGHT;
    _glfw.wl.keycodes[KEY_DOWN]       = SC_KEY_DOWN;
    _glfw.wl.keycodes[KEY_UP]         = SC_KEY_UP;
    _glfw.wl.keycodes[KEY_F1]         = SC_KEY_F1;
    _glfw.wl.keycodes[KEY_F2]         = SC_KEY_F2;
    _glfw.wl.keycodes[KEY_F3]         = SC_KEY_F3;
    _glfw.wl.keycodes[KEY_F4]         = SC_KEY_F4;
    _glfw.wl.keycodes[KEY_F5]         = SC_KEY_F5;
    _glfw.wl.keycodes[KEY_F6]         = SC_KEY_F6;
    _glfw.wl.keycodes[KEY_F7]         = SC_KEY_F7;
    _glfw.wl.keycodes[KEY_F8]         = SC_KEY_F8;
    _glfw.wl.keycodes[KEY_F9]         = SC_KEY_F9;
    _glfw.wl.keycodes[KEY_F10]        = SC_KEY_F10;
    _glfw.wl.keycodes[KEY_F11]        = SC_KEY_F11;
    _glfw.wl.keycodes[KEY_F12]        = SC_KEY_F12;
    _glfw.wl.keycodes[KEY_F13]        = SC_KEY_F13;
    _glfw.wl.keycodes[KEY_F14]        = SC_KEY_F14;
    _glfw.wl.keycodes[KEY_F15]        = SC_KEY_F15;
    _glfw.wl.keycodes[KEY_F16]        = SC_KEY_F16;
    _glfw.wl.keycodes[KEY_F17]        = SC_KEY_F17;
    _glfw.wl.keycodes[KEY_F18]        = SC_KEY_F18;
    _glfw.wl.keycodes[KEY_F19]        = SC_KEY_F19;
    _glfw.wl.keycodes[KEY_F20]        = SC_KEY_F20;
    _glfw.wl.keycodes[KEY_F21]        = SC_KEY_F21;
    _glfw.wl.keycodes[KEY_F22]        = SC_KEY_F22;
    _glfw.wl.keycodes[KEY_F23]        = SC_KEY_F23;
    _glfw.wl.keycodes[KEY_F24]        = SC_KEY_F24;
    _glfw.wl.keycodes[KEY_KPSLASH]    = SC_KEY_KP_DIVIDE;
    _glfw.wl.keycodes[KEY_KPASTERISK] = SC_KEY_KP_MULTIPLY;
    _glfw.wl.keycodes[KEY_KPMINUS]    = SC_KEY_KP_SUBTRACT;
    _glfw.wl.keycodes[KEY_KPPLUS]     = SC_KEY_KP_ADD;
    _glfw.wl.keycodes[KEY_KP0]        = SC_KEY_KP_0;
    _glfw.wl.keycodes[KEY_KP1]        = SC_KEY_KP_1;
    _glfw.wl.keycodes[KEY_KP2]        = SC_KEY_KP_2;
    _glfw.wl.keycodes[KEY_KP3]        = SC_KEY_KP_3;
    _glfw.wl.keycodes[KEY_KP4]        = SC_KEY_KP_4;
    _glfw.wl.keycodes[KEY_KP5]        = SC_KEY_KP_5;
    _glfw.wl.keycodes[KEY_KP6]        = SC_KEY_KP_6;
    _glfw.wl.keycodes[KEY_KP7]        = SC_KEY_KP_7;
    _glfw.wl.keycodes[KEY_KP8]        = SC_KEY_KP_8;
    _glfw.wl.keycodes[KEY_KP9]        = SC_KEY_KP_9;
    _glfw.wl.keycodes[KEY_KPDOT]      = SC_KEY_KP_DECIMAL;
    _glfw.wl.keycodes[KEY_KPEQUAL]    = SC_KEY_KP_EQUAL;
    _glfw.wl.keycodes[KEY_KPENTER]    = SC_KEY_KP_ENTER;
    _glfw.wl.keycodes[KEY_102ND]      = SC_KEY_WORLD_2;

    for (int scancode = 0;  scancode < 256;  scancode++)
    {
        if (_glfw.wl.keycodes[scancode] > 0)
            _glfw.wl.scancodes[_glfw.wl.keycodes[scancode]] = scancode;
    }
}

static GLFWbool loadCursorTheme(void)
{
    int cursorSize = 16;

    const char* sizeString = getenv("XCURSOR_SIZE");
    if (sizeString)
    {
        errno = 0;
        const long cursorSizeLong = strtol(sizeString, NULL, 10);
        if (errno == 0 && cursorSizeLong > 0 && cursorSizeLong < INT_MAX)
            cursorSize = (int) cursorSizeLong;
    }

    const char* themeName = getenv("XCURSOR_THEME");

    _glfw.wl.cursorTheme = wl_cursor_theme_load(themeName, cursorSize, _glfw.wl.shm);
    if (!_glfw.wl.cursorTheme)
    {
        _glfwInputError(SC_WIN_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to load default cursor theme");
        return GLFW_FALSE;
    }

    // If this happens to be NULL, we just fallback to the scale=1 version.
    _glfw.wl.cursorThemeHiDPI =
        wl_cursor_theme_load(themeName, cursorSize * 2, _glfw.wl.shm);

    _glfw.wl.cursorSurface = wl_compositor_create_surface(_glfw.wl.compositor);
    _glfw.wl.cursorTimerfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    return GLFW_TRUE;
}


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

GLFWbool _glfwConnectWayland(int platformID, _GLFWplatform* platform)
{
    const _GLFWplatform wayland =
    {
        .platformID = SC_PLATFORM_WAYLAND,
        .init = _glfwInitWayland,
        .terminate = _glfwTerminateWayland,
        .getCursorPos = _glfwGetCursorPosWayland,
        .setCursorPos = _glfwSetCursorPosWayland,
        .setCursorMode = _glfwSetCursorModeWayland,
        .setRawMouseMotion = _glfwSetRawMouseMotionWayland,
        .rawMouseMotionSupported = _glfwRawMouseMotionSupportedWayland,
        .createCursor = _glfwCreateCursorWayland,
        .createStandardCursor = _glfwCreateStandardCursorWayland,
        .destroyCursor = _glfwDestroyCursorWayland,
        .setCursor = _glfwSetCursorWayland,
        .getScancodeName = _glfwGetScancodeNameWayland,
        .getKeyScancode = _glfwGetKeyScancodeWayland,
        .setClipboardString = _glfwSetClipboardStringWayland,
        .getClipboardString = _glfwGetClipboardStringWayland,
        .freeMonitor = _glfwFreeMonitorWayland,
        .getMonitorPos = _glfwGetMonitorPosWayland,
        .getMonitorContentScale = _glfwGetMonitorContentScaleWayland,
        .getMonitorWorkarea = _glfwGetMonitorWorkareaWayland,
        .getVideoModes = _glfwGetVideoModesWayland,
        .getVideoMode = _glfwGetVideoModeWayland,
        .getGammaRamp = _glfwGetGammaRampWayland,
        .setGammaRamp = _glfwSetGammaRampWayland,
        .createWindow = _glfwCreateWindowWayland,
        .destroyWindow = _glfwDestroyWindowWayland,
        .setWindowTitle = _glfwSetWindowTitleWayland,
        .setWindowIcon = _glfwSetWindowIconWayland,
        .getWindowPos = _glfwGetWindowPosWayland,
        .setWindowPos = _glfwSetWindowPosWayland,
        .getWindowSize = _glfwGetWindowSizeWayland,
        .setWindowSize = _glfwSetWindowSizeWayland,
        .setWindowSizeLimits = _glfwSetWindowSizeLimitsWayland,
        .setWindowAspectRatio = _glfwSetWindowAspectRatioWayland,
        .getFramebufferSize = _glfwGetFramebufferSizeWayland,
        .getWindowFrameSize = _glfwGetWindowFrameSizeWayland,
        .getWindowContentScale = _glfwGetWindowContentScaleWayland,
        .iconifyWindow = _glfwIconifyWindowWayland,
        .restoreWindow = _glfwRestoreWindowWayland,
        .maximizeWindow = _glfwMaximizeWindowWayland,
        .showWindow = _glfwShowWindowWayland,
        .hideWindow = _glfwHideWindowWayland,
        .requestWindowAttention = _glfwRequestWindowAttentionWayland,
        .focusWindow = _glfwFocusWindowWayland,
        .setWindowMonitor = _glfwSetWindowMonitorWayland,
        .windowFocused = _glfwWindowFocusedWayland,
        .windowIconified = _glfwWindowIconifiedWayland,
        .windowVisible = _glfwWindowVisibleWayland,
        .windowMaximized = _glfwWindowMaximizedWayland,
        .windowHovered = _glfwWindowHoveredWayland,
        .framebufferTransparent = _glfwFramebufferTransparentWayland,
        .getWindowOpacity = _glfwGetWindowOpacityWayland,
        .setWindowResizable = _glfwSetWindowResizableWayland,
        .setWindowDecorated = _glfwSetWindowDecoratedWayland,
        .setWindowFloating = _glfwSetWindowFloatingWayland,
        .setWindowOpacity = _glfwSetWindowOpacityWayland,
        .setWindowMousePassthrough = _glfwSetWindowMousePassthroughWayland,
        .pollEvents = _glfwPollEventsWayland,
        .waitEvents = _glfwWaitEventsWayland,
        .waitEventsTimeout = _glfwWaitEventsTimeoutWayland,
        .postEmptyEvent = _glfwPostEmptyEventWayland,
        .getEGLPlatform = _glfwGetEGLPlatformWayland,
        .getEGLNativeDisplay = _glfwGetEGLNativeDisplayWayland,
        .getEGLNativeWindow = _glfwGetEGLNativeWindowWayland,
        .getRequiredInstanceExtensions = _glfwGetRequiredInstanceExtensionsWayland,
        .getPhysicalDevicePresentationSupport = _glfwGetPhysicalDevicePresentationSupportWayland,
        .createWindowSurface = _glfwCreateWindowSurfaceWayland
    };

    void* module = _glfwPlatformLoadModule("libwayland-client.so.0");
    if (!module)
    {
        if (platformID == SC_PLATFORM_WAYLAND)
        {
            _glfwInputError(SC_WIN_ERR_PLATFORM_ERROR,
                            "Wayland: Failed to load libwayland-client");
        }

        return GLFW_FALSE;
    }

    PFN_wl_display_connect wl_display_connect = (PFN_wl_display_connect)
        _glfwPlatformGetModuleSymbol(module, "wl_display_connect");
    if (!wl_display_connect)
    {
        if (platformID == SC_PLATFORM_WAYLAND)
        {
            _glfwInputError(SC_WIN_ERR_PLATFORM_ERROR,
                            "Wayland: Failed to load libwayland-client entry point");
        }

        _glfwPlatformFreeModule(module);
        return GLFW_FALSE;
    }

    struct wl_display* display = wl_display_connect(NULL);
    if (!display)
    {
        if (platformID == SC_PLATFORM_WAYLAND)
            _glfwInputError(SC_WIN_ERR_PLATFORM_ERROR, "Wayland: Failed to connect to display");

        _glfwPlatformFreeModule(module);
        return GLFW_FALSE;
    }

    _glfw.wl.display = display;
    _glfw.wl.client.handle = module;

    *platform = wayland;
    return GLFW_TRUE;
}

int _glfwInitWayland(void)
{
    // These must be set before any failure checks
    _glfw.wl.keyRepeatTimerfd = -1;
    _glfw.wl.cursorTimerfd = -1;

    _glfw.wl.tag = sc_win_get_version_string();

    _glfw.wl.client.display_flush = (PFN_wl_display_flush)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_display_flush");
    _glfw.wl.client.display_cancel_read = (PFN_wl_display_cancel_read)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_display_cancel_read");
    _glfw.wl.client.display_dispatch_pending = (PFN_wl_display_dispatch_pending)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_display_dispatch_pending");
    _glfw.wl.client.display_read_events = (PFN_wl_display_read_events)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_display_read_events");
    _glfw.wl.client.display_disconnect = (PFN_wl_display_disconnect)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_display_disconnect");
    _glfw.wl.client.display_roundtrip = (PFN_wl_display_roundtrip)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_display_roundtrip");
    _glfw.wl.client.display_get_fd = (PFN_wl_display_get_fd)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_display_get_fd");
    _glfw.wl.client.display_prepare_read = (PFN_wl_display_prepare_read)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_display_prepare_read");
    _glfw.wl.client.display_create_queue = (PFN_wl_display_create_queue)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_display_create_queue");
    _glfw.wl.client.display_prepare_read_queue = (PFN_wl_display_prepare_read_queue)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_display_prepare_read_queue");
    _glfw.wl.client.display_dispatch_queue_pending = (PFN_wl_display_dispatch_queue_pending)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_display_dispatch_queue_pending");
    _glfw.wl.client.event_queue_destroy = (PFN_wl_event_queue_destroy)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_event_queue_destroy");
    _glfw.wl.client.proxy_marshal = (PFN_wl_proxy_marshal)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_proxy_marshal");
    _glfw.wl.client.proxy_add_listener = (PFN_wl_proxy_add_listener)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_proxy_add_listener");
    _glfw.wl.client.proxy_destroy = (PFN_wl_proxy_destroy)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_proxy_destroy");
    _glfw.wl.client.proxy_marshal_constructor = (PFN_wl_proxy_marshal_constructor)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_proxy_marshal_constructor");
    _glfw.wl.client.proxy_marshal_constructor_versioned = (PFN_wl_proxy_marshal_constructor_versioned)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_proxy_marshal_constructor_versioned");
    _glfw.wl.client.proxy_get_user_data = (PFN_wl_proxy_get_user_data)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_proxy_get_user_data");
    _glfw.wl.client.proxy_set_user_data = (PFN_wl_proxy_set_user_data)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_proxy_set_user_data");
    _glfw.wl.client.proxy_get_tag = (PFN_wl_proxy_get_tag)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_proxy_get_tag");
    _glfw.wl.client.proxy_set_tag = (PFN_wl_proxy_set_tag)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_proxy_set_tag");
    _glfw.wl.client.proxy_get_version = (PFN_wl_proxy_get_version)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_proxy_get_version");
    _glfw.wl.client.proxy_marshal_flags = (PFN_wl_proxy_marshal_flags)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_proxy_marshal_flags");
    _glfw.wl.client.proxy_create_wrapper = (PFN_wl_proxy_create_wrapper)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_proxy_create_wrapper");
    _glfw.wl.client.proxy_wrapper_destroy = (PFN_wl_proxy_wrapper_destroy)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_proxy_wrapper_destroy");
    _glfw.wl.client.proxy_set_queue = (PFN_wl_proxy_set_queue)
        _glfwPlatformGetModuleSymbol(_glfw.wl.client.handle, "wl_proxy_set_queue");

    if (!_glfw.wl.client.display_flush ||
        !_glfw.wl.client.display_cancel_read ||
        !_glfw.wl.client.display_dispatch_pending ||
        !_glfw.wl.client.display_read_events ||
        !_glfw.wl.client.display_disconnect ||
        !_glfw.wl.client.display_roundtrip ||
        !_glfw.wl.client.display_get_fd ||
        !_glfw.wl.client.display_prepare_read ||
        !_glfw.wl.client.display_create_queue ||
        !_glfw.wl.client.display_prepare_read_queue ||
        !_glfw.wl.client.display_dispatch_queue_pending ||
        !_glfw.wl.client.event_queue_destroy ||
        !_glfw.wl.client.proxy_marshal ||
        !_glfw.wl.client.proxy_add_listener ||
        !_glfw.wl.client.proxy_destroy ||
        !_glfw.wl.client.proxy_marshal_constructor ||
        !_glfw.wl.client.proxy_marshal_constructor_versioned ||
        !_glfw.wl.client.proxy_get_user_data ||
        !_glfw.wl.client.proxy_set_user_data ||
        !_glfw.wl.client.proxy_get_tag ||
        !_glfw.wl.client.proxy_set_tag ||
        !_glfw.wl.client.proxy_create_wrapper ||
        !_glfw.wl.client.proxy_wrapper_destroy ||
        !_glfw.wl.client.proxy_set_queue)
    {
        _glfwInputError(SC_WIN_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to load libwayland-client entry point");
        return GLFW_FALSE;
    }

    _glfw.wl.cursor.handle = _glfwPlatformLoadModule("libwayland-cursor.so.0");
    if (!_glfw.wl.cursor.handle)
    {
        _glfwInputError(SC_WIN_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to load libwayland-cursor");
        return GLFW_FALSE;
    }

    _glfw.wl.cursor.theme_load = (PFN_wl_cursor_theme_load)
        _glfwPlatformGetModuleSymbol(_glfw.wl.cursor.handle, "wl_cursor_theme_load");
    _glfw.wl.cursor.theme_destroy = (PFN_wl_cursor_theme_destroy)
        _glfwPlatformGetModuleSymbol(_glfw.wl.cursor.handle, "wl_cursor_theme_destroy");
    _glfw.wl.cursor.theme_get_cursor = (PFN_wl_cursor_theme_get_cursor)
        _glfwPlatformGetModuleSymbol(_glfw.wl.cursor.handle, "wl_cursor_theme_get_cursor");
    _glfw.wl.cursor.image_get_buffer = (PFN_wl_cursor_image_get_buffer)
        _glfwPlatformGetModuleSymbol(_glfw.wl.cursor.handle, "wl_cursor_image_get_buffer");

    _glfw.wl.egl.handle = _glfwPlatformLoadModule("libwayland-egl.so.1");
    if (!_glfw.wl.egl.handle)
    {
        _glfwInputError(SC_WIN_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to load libwayland-egl");
        return GLFW_FALSE;
    }

    _glfw.wl.egl.window_create = (PFN_wl_egl_window_create)
        _glfwPlatformGetModuleSymbol(_glfw.wl.egl.handle, "wl_egl_window_create");
    _glfw.wl.egl.window_destroy = (PFN_wl_egl_window_destroy)
        _glfwPlatformGetModuleSymbol(_glfw.wl.egl.handle, "wl_egl_window_destroy");
    _glfw.wl.egl.window_resize = (PFN_wl_egl_window_resize)
        _glfwPlatformGetModuleSymbol(_glfw.wl.egl.handle, "wl_egl_window_resize");

    _glfw.wl.xkb.handle = _glfwPlatformLoadModule("libxkbcommon.so.0");
    if (!_glfw.wl.xkb.handle)
    {
        _glfwInputError(SC_WIN_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to load libxkbcommon");
        return GLFW_FALSE;
    }

    _glfw.wl.xkb.context_new = (PFN_xkb_context_new)
        _glfwPlatformGetModuleSymbol(_glfw.wl.xkb.handle, "xkb_context_new");
    _glfw.wl.xkb.context_unref = (PFN_xkb_context_unref)
        _glfwPlatformGetModuleSymbol(_glfw.wl.xkb.handle, "xkb_context_unref");
    _glfw.wl.xkb.keymap_new_from_string = (PFN_xkb_keymap_new_from_string)
        _glfwPlatformGetModuleSymbol(_glfw.wl.xkb.handle, "xkb_keymap_new_from_string");
    _glfw.wl.xkb.keymap_unref = (PFN_xkb_keymap_unref)
        _glfwPlatformGetModuleSymbol(_glfw.wl.xkb.handle, "xkb_keymap_unref");
    _glfw.wl.xkb.keymap_mod_get_index = (PFN_xkb_keymap_mod_get_index)
        _glfwPlatformGetModuleSymbol(_glfw.wl.xkb.handle, "xkb_keymap_mod_get_index");
    _glfw.wl.xkb.keymap_key_repeats = (PFN_xkb_keymap_key_repeats)
        _glfwPlatformGetModuleSymbol(_glfw.wl.xkb.handle, "xkb_keymap_key_repeats");
    _glfw.wl.xkb.keymap_key_get_syms_by_level = (PFN_xkb_keymap_key_get_syms_by_level)
        _glfwPlatformGetModuleSymbol(_glfw.wl.xkb.handle, "xkb_keymap_key_get_syms_by_level");
    _glfw.wl.xkb.state_new = (PFN_xkb_state_new)
        _glfwPlatformGetModuleSymbol(_glfw.wl.xkb.handle, "xkb_state_new");
    _glfw.wl.xkb.state_unref = (PFN_xkb_state_unref)
        _glfwPlatformGetModuleSymbol(_glfw.wl.xkb.handle, "xkb_state_unref");
    _glfw.wl.xkb.state_key_get_syms = (PFN_xkb_state_key_get_syms)
        _glfwPlatformGetModuleSymbol(_glfw.wl.xkb.handle, "xkb_state_key_get_syms");
    _glfw.wl.xkb.state_update_mask = (PFN_xkb_state_update_mask)
        _glfwPlatformGetModuleSymbol(_glfw.wl.xkb.handle, "xkb_state_update_mask");
    _glfw.wl.xkb.state_key_get_layout = (PFN_xkb_state_key_get_layout)
        _glfwPlatformGetModuleSymbol(_glfw.wl.xkb.handle, "xkb_state_key_get_layout");
    _glfw.wl.xkb.state_mod_index_is_active = (PFN_xkb_state_mod_index_is_active)
        _glfwPlatformGetModuleSymbol(_glfw.wl.xkb.handle, "xkb_state_mod_index_is_active");
    _glfw.wl.xkb.compose_table_new_from_locale = (PFN_xkb_compose_table_new_from_locale)
        _glfwPlatformGetModuleSymbol(_glfw.wl.xkb.handle, "xkb_compose_table_new_from_locale");
    _glfw.wl.xkb.compose_table_unref = (PFN_xkb_compose_table_unref)
        _glfwPlatformGetModuleSymbol(_glfw.wl.xkb.handle, "xkb_compose_table_unref");
    _glfw.wl.xkb.compose_state_new = (PFN_xkb_compose_state_new)
        _glfwPlatformGetModuleSymbol(_glfw.wl.xkb.handle, "xkb_compose_state_new");
    _glfw.wl.xkb.compose_state_unref = (PFN_xkb_compose_state_unref)
        _glfwPlatformGetModuleSymbol(_glfw.wl.xkb.handle, "xkb_compose_state_unref");
    _glfw.wl.xkb.compose_state_feed = (PFN_xkb_compose_state_feed)
        _glfwPlatformGetModuleSymbol(_glfw.wl.xkb.handle, "xkb_compose_state_feed");
    _glfw.wl.xkb.compose_state_get_status = (PFN_xkb_compose_state_get_status)
        _glfwPlatformGetModuleSymbol(_glfw.wl.xkb.handle, "xkb_compose_state_get_status");
    _glfw.wl.xkb.compose_state_get_one_sym = (PFN_xkb_compose_state_get_one_sym)
        _glfwPlatformGetModuleSymbol(_glfw.wl.xkb.handle, "xkb_compose_state_get_one_sym");
    _glfw.wl.xkb.keysym_to_utf32 = (PFN_xkb_keysym_to_utf32)
        _glfwPlatformGetModuleSymbol(_glfw.wl.xkb.handle, "xkb_keysym_to_utf32");
    _glfw.wl.xkb.keysym_to_utf8 = (PFN_xkb_keysym_to_utf8)
        _glfwPlatformGetModuleSymbol(_glfw.wl.xkb.handle, "xkb_keysym_to_utf8");

    if (!_glfw.wl.xkb.context_new ||
        !_glfw.wl.xkb.context_unref ||
        !_glfw.wl.xkb.keymap_new_from_string ||
        !_glfw.wl.xkb.keymap_unref ||
        !_glfw.wl.xkb.keymap_mod_get_index ||
        !_glfw.wl.xkb.keymap_key_repeats ||
        !_glfw.wl.xkb.keymap_key_get_syms_by_level ||
        !_glfw.wl.xkb.state_new ||
        !_glfw.wl.xkb.state_unref ||
        !_glfw.wl.xkb.state_key_get_syms ||
        !_glfw.wl.xkb.state_update_mask ||
        !_glfw.wl.xkb.state_key_get_layout ||
        !_glfw.wl.xkb.state_mod_index_is_active ||
        !_glfw.wl.xkb.compose_table_new_from_locale ||
        !_glfw.wl.xkb.compose_table_unref ||
        !_glfw.wl.xkb.compose_state_new ||
        !_glfw.wl.xkb.compose_state_unref ||
        !_glfw.wl.xkb.compose_state_feed ||
        !_glfw.wl.xkb.compose_state_get_status ||
        !_glfw.wl.xkb.compose_state_get_one_sym)
    {
        _glfwInputError(SC_WIN_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to load all entry points from libxkbcommon");
        return GLFW_FALSE;
    }

    if (_glfw.hints.init.wl.libdecorMode == SC_WAYLAND_PREFER_LIBDECOR)
        _glfw.wl.libdecor.handle = _glfwPlatformLoadModule("libdecor-0.so.0");

    if (_glfw.wl.libdecor.handle)
    {
        _glfw.wl.libdecor.libdecor_new_ = (PFN_libdecor_new)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_new");
        _glfw.wl.libdecor.libdecor_unref_ = (PFN_libdecor_unref)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_unref");
        _glfw.wl.libdecor.libdecor_get_fd_ = (PFN_libdecor_get_fd)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_get_fd");
        _glfw.wl.libdecor.libdecor_dispatch_ = (PFN_libdecor_dispatch)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_dispatch");
        _glfw.wl.libdecor.libdecor_decorate_ = (PFN_libdecor_decorate)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_decorate");
        _glfw.wl.libdecor.libdecor_frame_unref_ = (PFN_libdecor_frame_unref)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_frame_unref");
        _glfw.wl.libdecor.libdecor_frame_set_app_id_ = (PFN_libdecor_frame_set_app_id)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_frame_set_app_id");
        _glfw.wl.libdecor.libdecor_frame_set_title_ = (PFN_libdecor_frame_set_title)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_frame_set_title");
        _glfw.wl.libdecor.libdecor_frame_set_minimized_ = (PFN_libdecor_frame_set_minimized)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_frame_set_minimized");
        _glfw.wl.libdecor.libdecor_frame_set_fullscreen_ = (PFN_libdecor_frame_set_fullscreen)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_frame_set_fullscreen");
        _glfw.wl.libdecor.libdecor_frame_unset_fullscreen_ = (PFN_libdecor_frame_unset_fullscreen)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_frame_unset_fullscreen");
        _glfw.wl.libdecor.libdecor_frame_map_ = (PFN_libdecor_frame_map)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_frame_map");
        _glfw.wl.libdecor.libdecor_frame_commit_ = (PFN_libdecor_frame_commit)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_frame_commit");
        _glfw.wl.libdecor.libdecor_frame_set_min_content_size_ = (PFN_libdecor_frame_set_min_content_size)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_frame_set_min_content_size");
        _glfw.wl.libdecor.libdecor_frame_set_max_content_size_ = (PFN_libdecor_frame_set_max_content_size)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_frame_set_max_content_size");
        _glfw.wl.libdecor.libdecor_frame_set_maximized_ = (PFN_libdecor_frame_set_maximized)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_frame_set_maximized");
        _glfw.wl.libdecor.libdecor_frame_unset_maximized_ = (PFN_libdecor_frame_unset_maximized)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_frame_unset_maximized");
        _glfw.wl.libdecor.libdecor_frame_set_capabilities_ = (PFN_libdecor_frame_set_capabilities)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_frame_set_capabilities");
        _glfw.wl.libdecor.libdecor_frame_unset_capabilities_ = (PFN_libdecor_frame_unset_capabilities)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_frame_unset_capabilities");
        _glfw.wl.libdecor.libdecor_frame_set_visibility_ = (PFN_libdecor_frame_set_visibility)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_frame_set_visibility");
        _glfw.wl.libdecor.libdecor_frame_is_visible_ = (PFN_libdecor_frame_is_visible)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_frame_is_visible");
        _glfw.wl.libdecor.libdecor_frame_get_xdg_toplevel_ = (PFN_libdecor_frame_get_xdg_toplevel)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_frame_get_xdg_toplevel");
        _glfw.wl.libdecor.libdecor_configuration_get_content_size_ = (PFN_libdecor_configuration_get_content_size)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_configuration_get_content_size");
        _glfw.wl.libdecor.libdecor_configuration_get_window_state_ = (PFN_libdecor_configuration_get_window_state)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_configuration_get_window_state");
        _glfw.wl.libdecor.libdecor_state_new_ = (PFN_libdecor_state_new)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_state_new");
        _glfw.wl.libdecor.libdecor_state_free_ = (PFN_libdecor_state_free)
            _glfwPlatformGetModuleSymbol(_glfw.wl.libdecor.handle, "libdecor_state_free");

        if (!_glfw.wl.libdecor.libdecor_new_ ||
            !_glfw.wl.libdecor.libdecor_unref_ ||
            !_glfw.wl.libdecor.libdecor_get_fd_ ||
            !_glfw.wl.libdecor.libdecor_dispatch_ ||
            !_glfw.wl.libdecor.libdecor_decorate_ ||
            !_glfw.wl.libdecor.libdecor_frame_unref_ ||
            !_glfw.wl.libdecor.libdecor_frame_set_app_id_ ||
            !_glfw.wl.libdecor.libdecor_frame_set_title_ ||
            !_glfw.wl.libdecor.libdecor_frame_set_minimized_ ||
            !_glfw.wl.libdecor.libdecor_frame_set_fullscreen_ ||
            !_glfw.wl.libdecor.libdecor_frame_unset_fullscreen_ ||
            !_glfw.wl.libdecor.libdecor_frame_map_ ||
            !_glfw.wl.libdecor.libdecor_frame_commit_ ||
            !_glfw.wl.libdecor.libdecor_frame_set_min_content_size_ ||
            !_glfw.wl.libdecor.libdecor_frame_set_max_content_size_ ||
            !_glfw.wl.libdecor.libdecor_frame_set_maximized_ ||
            !_glfw.wl.libdecor.libdecor_frame_unset_maximized_ ||
            !_glfw.wl.libdecor.libdecor_frame_set_capabilities_ ||
            !_glfw.wl.libdecor.libdecor_frame_unset_capabilities_ ||
            !_glfw.wl.libdecor.libdecor_frame_set_visibility_ ||
            !_glfw.wl.libdecor.libdecor_frame_is_visible_ ||
            !_glfw.wl.libdecor.libdecor_frame_get_xdg_toplevel_ ||
            !_glfw.wl.libdecor.libdecor_configuration_get_content_size_ ||
            !_glfw.wl.libdecor.libdecor_configuration_get_window_state_ ||
            !_glfw.wl.libdecor.libdecor_state_new_ ||
            !_glfw.wl.libdecor.libdecor_state_free_)
        {
            _glfwPlatformFreeModule(_glfw.wl.libdecor.handle);
            memset(&_glfw.wl.libdecor, 0, sizeof(_glfw.wl.libdecor));
        }
    }

    _glfw.wl.registry = wl_display_get_registry(_glfw.wl.display);
    wl_registry_add_listener(_glfw.wl.registry, &registryListener, NULL);

    createKeyTables();

    _glfw.wl.keyRepeatTimerfd =
        timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (_glfw.wl.keyRepeatTimerfd == -1)
    {
        _glfwInputError(SC_WIN_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to create timerfd: %s",
                        strerror(errno));
        return GLFW_FALSE;
    }

    _glfw.wl.xkb.context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!_glfw.wl.xkb.context)
    {
        _glfwInputError(SC_WIN_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to initialize xkb context");
        return GLFW_FALSE;
    }

    // Sync so we got all registry objects
    wl_display_roundtrip(_glfw.wl.display);

    // Sync so we got all initial output events
    wl_display_roundtrip(_glfw.wl.display);

    if (_glfw.wl.libdecor.handle)
    {
        _glfw.wl.libdecor.context = libdecor_new(_glfw.wl.display, &libdecorInterface);
        if (_glfw.wl.libdecor.context)
        {
            // Perform an initial dispatch and flush to get the init started
            libdecor_dispatch(_glfw.wl.libdecor.context, 0);

            // Create sync point to "know" when libdecor is ready for use
            struct wl_callback* callback = wl_display_sync(_glfw.wl.display);
            wl_callback_add_listener(callback, &libdecorReadyListener, NULL);
        }
    }

    if (!_glfw.wl.wmBase)
    {
        _glfwInputError(SC_WIN_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to find xdg-shell in your compositor");
        return GLFW_FALSE;
    }

    if (!_glfw.wl.shm)
    {
        _glfwInputError(SC_WIN_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to find wl_shm in your compositor");
        return GLFW_FALSE;
    }

    if (!loadCursorTheme())
        return GLFW_FALSE;

    if (_glfw.wl.seat && _glfw.wl.dataDeviceManager)
    {
        _glfw.wl.dataDevice =
            wl_data_device_manager_get_data_device(_glfw.wl.dataDeviceManager,
                                                   _glfw.wl.seat);
        _glfwAddDataDeviceListenerWayland(_glfw.wl.dataDevice);
    }

    return GLFW_TRUE;
}

void _glfwTerminateWayland(void)
{
    _glfwTerminateEGL();
    _glfwTerminateOSMesa();

    if (_glfw.wl.libdecor.context)
    {
        // Allow libdecor to finish receiving all its requested globals
        // and ensure the associated sync callback object is destroyed
        while (!_glfw.wl.libdecor.ready)
            _glfwWaitEventsWayland();

        libdecor_unref(_glfw.wl.libdecor.context);
    }

    if (_glfw.wl.xkb.composeState)
        xkb_compose_state_unref(_glfw.wl.xkb.composeState);
    if (_glfw.wl.xkb.keymap)
        xkb_keymap_unref(_glfw.wl.xkb.keymap);
    if (_glfw.wl.xkb.state)
        xkb_state_unref(_glfw.wl.xkb.state);
    if (_glfw.wl.xkb.context)
        xkb_context_unref(_glfw.wl.xkb.context);

    if (_glfw.wl.cursorTheme)
        wl_cursor_theme_destroy(_glfw.wl.cursorTheme);
    if (_glfw.wl.cursorThemeHiDPI)
        wl_cursor_theme_destroy(_glfw.wl.cursorThemeHiDPI);

    for (unsigned int i = 0; i < _glfw.wl.offerCount; i++)
        wl_data_offer_destroy(_glfw.wl.offers[i].offer);

    _glfw_free(_glfw.wl.offers);

    if (_glfw.wl.cursorSurface)
        wl_surface_destroy(_glfw.wl.cursorSurface);
    if (_glfw.wl.subcompositor)
        wl_subcompositor_destroy(_glfw.wl.subcompositor);
    if (_glfw.wl.compositor)
        wl_compositor_destroy(_glfw.wl.compositor);
    if (_glfw.wl.shm)
        wl_shm_destroy(_glfw.wl.shm);
    if (_glfw.wl.viewporter)
        wp_viewporter_destroy(_glfw.wl.viewporter);
    if (_glfw.wl.decorationManager)
        zxdg_decoration_manager_v1_destroy(_glfw.wl.decorationManager);
    if (_glfw.wl.wmBase)
        xdg_wm_base_destroy(_glfw.wl.wmBase);
    if (_glfw.wl.selectionOffer)
        wl_data_offer_destroy(_glfw.wl.selectionOffer);
    if (_glfw.wl.dragOffer)
        wl_data_offer_destroy(_glfw.wl.dragOffer);
    if (_glfw.wl.selectionSource)
        wl_data_source_destroy(_glfw.wl.selectionSource);
    if (_glfw.wl.dataDevice)
        wl_data_device_destroy(_glfw.wl.dataDevice);
    if (_glfw.wl.dataDeviceManager)
        wl_data_device_manager_destroy(_glfw.wl.dataDeviceManager);
    if (_glfw.wl.pointer)
        wl_pointer_destroy(_glfw.wl.pointer);
    if (_glfw.wl.keyboard)
        wl_keyboard_destroy(_glfw.wl.keyboard);
    if (_glfw.wl.seat)
        wl_seat_destroy(_glfw.wl.seat);
    if (_glfw.wl.relativePointerManager)
        zwp_relative_pointer_manager_v1_destroy(_glfw.wl.relativePointerManager);
    if (_glfw.wl.pointerConstraints)
        zwp_pointer_constraints_v1_destroy(_glfw.wl.pointerConstraints);
    if (_glfw.wl.idleInhibitManager)
        zwp_idle_inhibit_manager_v1_destroy(_glfw.wl.idleInhibitManager);
    if (_glfw.wl.activationManager)
        xdg_activation_v1_destroy(_glfw.wl.activationManager);
    if (_glfw.wl.fractionalScaleManager)
        wp_fractional_scale_manager_v1_destroy(_glfw.wl.fractionalScaleManager);
    if (_glfw.wl.registry)
        wl_registry_destroy(_glfw.wl.registry);
    if (_glfw.wl.display)
    {
        wl_display_flush(_glfw.wl.display);
        wl_display_disconnect(_glfw.wl.display);
    }

    if (_glfw.wl.keyRepeatTimerfd >= 0)
        close(_glfw.wl.keyRepeatTimerfd);
    if (_glfw.wl.cursorTimerfd >= 0)
        close(_glfw.wl.cursorTimerfd);

    // Free modules only after all Wayland termination functions are called

    _glfwPlatformFreeModule(_glfw.egl.handle);
    _glfwPlatformFreeModule(_glfw.wl.libdecor.handle);
    _glfwPlatformFreeModule(_glfw.wl.egl.handle);
    _glfwPlatformFreeModule(_glfw.wl.xkb.handle);
    _glfwPlatformFreeModule(_glfw.wl.cursor.handle);
    _glfwPlatformFreeModule(_glfw.wl.client.handle);

    _glfw_free(_glfw.wl.clipboardString);

    memset(&_glfw.wl, 0, sizeof(_glfw.wl));
}

#endif // _GLFW_WAYLAND

