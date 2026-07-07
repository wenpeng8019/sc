
#include "internal.h"

#if defined(WSI_WAYLAND)

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
        g_wsi.wl.compositor =
            wl_registry_bind(registry, name, &wl_compositor_interface,
                             wsi_min(3, version));
    }
    else if (strcmp(interface, "wl_subcompositor") == 0)
    {
        g_wsi.wl.subcompositor =
            wl_registry_bind(registry, name, &wl_subcompositor_interface, 1);
    }
    else if (strcmp(interface, "wl_shm") == 0)
    {
        g_wsi.wl.shm =
            wl_registry_bind(registry, name, &wl_shm_interface, 1);
    }
    else if (strcmp(interface, "wl_output") == 0)
    {
        wayland_AddOutput(name, version);
    }
    else if (strcmp(interface, "wl_seat") == 0)
    {
        if (!g_wsi.wl.seat)
        {
            g_wsi.wl.seat =
                wl_registry_bind(registry, name, &wl_seat_interface,
                                 wsi_min(8, version));
            wayland_AddSeatListener(g_wsi.wl.seat);
        }
    }
    else if (strcmp(interface, "wl_data_device_manager") == 0)
    {
        if (!g_wsi.wl.dataDeviceManager)
        {
            g_wsi.wl.dataDeviceManager =
                wl_registry_bind(registry, name,
                                 &wl_data_device_manager_interface, 1);
        }
    }
    else if (strcmp(interface, "xdg_wm_base") == 0)
    {
        g_wsi.wl.wmBase =
            wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(g_wsi.wl.wmBase, &wmBaseListener, NULL);
    }
    else if (strcmp(interface, "zxdg_decoration_manager_v1") == 0)
    {
        g_wsi.wl.decorationManager =
            wl_registry_bind(registry, name,
                             &zxdg_decoration_manager_v1_interface,
                             1);
    }
    else if (strcmp(interface, "wp_viewporter") == 0)
    {
        g_wsi.wl.viewporter =
            wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
    }
    else if (strcmp(interface, "zwp_relative_pointer_manager_v1") == 0)
    {
        g_wsi.wl.relativePointerManager =
            wl_registry_bind(registry, name,
                             &zwp_relative_pointer_manager_v1_interface,
                             1);
    }
    else if (strcmp(interface, "zwp_pointer_constraints_v1") == 0)
    {
        g_wsi.wl.pointerConstraints =
            wl_registry_bind(registry, name,
                             &zwp_pointer_constraints_v1_interface,
                             1);
    }
    else if (strcmp(interface, "zwp_idle_inhibit_manager_v1") == 0)
    {
        g_wsi.wl.idleInhibitManager =
            wl_registry_bind(registry, name,
                             &zwp_idle_inhibit_manager_v1_interface,
                             1);
    }
    else if (strcmp(interface, "xdg_activation_v1") == 0)
    {
        g_wsi.wl.activationManager =
            wl_registry_bind(registry, name,
                             &xdg_activation_v1_interface,
                             1);
    }
    else if (strcmp(interface, "wp_fractional_scale_manager_v1") == 0)
    {
        g_wsi.wl.fractionalScaleManager =
            wl_registry_bind(registry, name,
                             &wp_fractional_scale_manager_v1_interface,
                             1);
    }
}

static void registryHandleGlobalRemove(void* userData,
                                       struct wl_registry* registry,
                                       uint32_t name)
{
    for (int i = 0; i < g_wsi.monitorCount; ++i)
    {
        monitor_st* monitor = g_wsi.monitors[i];
        if (monitor->wl.name == name)
        {
            impl_on_monitor(monitor, SC_DISCONNECTED, 0);
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
    impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
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
    g_wsi.wl.libdecor.ready = true;
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
    memset(g_wsi.wl.keycodes, -1, sizeof(g_wsi.wl.keycodes));
    memset(g_wsi.wl.scancodes, -1, sizeof(g_wsi.wl.scancodes));

    g_wsi.wl.keycodes[KEY_GRAVE]      = SC_KEY_GRAVE_ACCENT;
    g_wsi.wl.keycodes[KEY_1]          = SC_KEY_1;
    g_wsi.wl.keycodes[KEY_2]          = SC_KEY_2;
    g_wsi.wl.keycodes[KEY_3]          = SC_KEY_3;
    g_wsi.wl.keycodes[KEY_4]          = SC_KEY_4;
    g_wsi.wl.keycodes[KEY_5]          = SC_KEY_5;
    g_wsi.wl.keycodes[KEY_6]          = SC_KEY_6;
    g_wsi.wl.keycodes[KEY_7]          = SC_KEY_7;
    g_wsi.wl.keycodes[KEY_8]          = SC_KEY_8;
    g_wsi.wl.keycodes[KEY_9]          = SC_KEY_9;
    g_wsi.wl.keycodes[KEY_0]          = SC_KEY_0;
    g_wsi.wl.keycodes[KEY_SPACE]      = SC_KEY_SPACE;
    g_wsi.wl.keycodes[KEY_MINUS]      = SC_KEY_MINUS;
    g_wsi.wl.keycodes[KEY_EQUAL]      = SC_KEY_EQUAL;
    g_wsi.wl.keycodes[KEY_Q]          = SC_KEY_Q;
    g_wsi.wl.keycodes[KEY_W]          = SC_KEY_W;
    g_wsi.wl.keycodes[KEY_E]          = SC_KEY_E;
    g_wsi.wl.keycodes[KEY_R]          = SC_KEY_R;
    g_wsi.wl.keycodes[KEY_T]          = SC_KEY_T;
    g_wsi.wl.keycodes[KEY_Y]          = SC_KEY_Y;
    g_wsi.wl.keycodes[KEY_U]          = SC_KEY_U;
    g_wsi.wl.keycodes[KEY_I]          = SC_KEY_I;
    g_wsi.wl.keycodes[KEY_O]          = SC_KEY_O;
    g_wsi.wl.keycodes[KEY_P]          = SC_KEY_P;
    g_wsi.wl.keycodes[KEY_LEFTBRACE]  = SC_KEY_LEFT_BRACKET;
    g_wsi.wl.keycodes[KEY_RIGHTBRACE] = SC_KEY_RIGHT_BRACKET;
    g_wsi.wl.keycodes[KEY_A]          = SC_KEY_A;
    g_wsi.wl.keycodes[KEY_S]          = SC_KEY_S;
    g_wsi.wl.keycodes[KEY_D]          = SC_KEY_D;
    g_wsi.wl.keycodes[KEY_F]          = SC_KEY_F;
    g_wsi.wl.keycodes[KEY_G]          = SC_KEY_G;
    g_wsi.wl.keycodes[KEY_H]          = SC_KEY_H;
    g_wsi.wl.keycodes[KEY_J]          = SC_KEY_J;
    g_wsi.wl.keycodes[KEY_K]          = SC_KEY_K;
    g_wsi.wl.keycodes[KEY_L]          = SC_KEY_L;
    g_wsi.wl.keycodes[KEY_SEMICOLON]  = SC_KEY_SEMICOLON;
    g_wsi.wl.keycodes[KEY_APOSTROPHE] = SC_KEY_APOSTROPHE;
    g_wsi.wl.keycodes[KEY_Z]          = SC_KEY_Z;
    g_wsi.wl.keycodes[KEY_X]          = SC_KEY_X;
    g_wsi.wl.keycodes[KEY_C]          = SC_KEY_C;
    g_wsi.wl.keycodes[KEY_V]          = SC_KEY_V;
    g_wsi.wl.keycodes[KEY_B]          = SC_KEY_B;
    g_wsi.wl.keycodes[KEY_N]          = SC_KEY_N;
    g_wsi.wl.keycodes[KEY_M]          = SC_KEY_M;
    g_wsi.wl.keycodes[KEY_COMMA]      = SC_KEY_COMMA;
    g_wsi.wl.keycodes[KEY_DOT]        = SC_KEY_PERIOD;
    g_wsi.wl.keycodes[KEY_SLASH]      = SC_KEY_SLASH;
    g_wsi.wl.keycodes[KEY_BACKSLASH]  = SC_KEY_BACKSLASH;
    g_wsi.wl.keycodes[KEY_ESC]        = SC_KEY_ESCAPE;
    g_wsi.wl.keycodes[KEY_TAB]        = SC_KEY_TAB;
    g_wsi.wl.keycodes[KEY_LEFTSHIFT]  = SC_KEY_LEFT_SHIFT;
    g_wsi.wl.keycodes[KEY_RIGHTSHIFT] = SC_KEY_RIGHT_SHIFT;
    g_wsi.wl.keycodes[KEY_LEFTCTRL]   = SC_KEY_LEFT_CONTROL;
    g_wsi.wl.keycodes[KEY_RIGHTCTRL]  = SC_KEY_RIGHT_CONTROL;
    g_wsi.wl.keycodes[KEY_LEFTALT]    = SC_KEY_LEFT_ALT;
    g_wsi.wl.keycodes[KEY_RIGHTALT]   = SC_KEY_RIGHT_ALT;
    g_wsi.wl.keycodes[KEY_LEFTMETA]   = SC_KEY_LEFT_SUPER;
    g_wsi.wl.keycodes[KEY_RIGHTMETA]  = SC_KEY_RIGHT_SUPER;
    g_wsi.wl.keycodes[KEY_COMPOSE]    = SC_KEY_MENU;
    g_wsi.wl.keycodes[KEY_NUMLOCK]    = SC_KEY_NUM_LOCK;
    g_wsi.wl.keycodes[KEY_CAPSLOCK]   = SC_KEY_CAPS_LOCK;
    g_wsi.wl.keycodes[KEY_PRINT]      = SC_KEY_PRINT_SCREEN;
    g_wsi.wl.keycodes[KEY_SCROLLLOCK] = SC_KEY_SCROLL_LOCK;
    g_wsi.wl.keycodes[KEY_PAUSE]      = SC_KEY_PAUSE;
    g_wsi.wl.keycodes[KEY_DELETE]     = SC_KEY_DELETE;
    g_wsi.wl.keycodes[KEY_BACKSPACE]  = SC_KEY_BACKSPACE;
    g_wsi.wl.keycodes[KEY_ENTER]      = SC_KEY_ENTER;
    g_wsi.wl.keycodes[KEY_HOME]       = SC_KEY_HOME;
    g_wsi.wl.keycodes[KEY_END]        = SC_KEY_END;
    g_wsi.wl.keycodes[KEY_PAGEUP]     = SC_KEY_PAGE_UP;
    g_wsi.wl.keycodes[KEY_PAGEDOWN]   = SC_KEY_PAGE_DOWN;
    g_wsi.wl.keycodes[KEY_INSERT]     = SC_KEY_INSERT;
    g_wsi.wl.keycodes[KEY_LEFT]       = SC_KEY_LEFT;
    g_wsi.wl.keycodes[KEY_RIGHT]      = SC_KEY_RIGHT;
    g_wsi.wl.keycodes[KEY_DOWN]       = SC_KEY_DOWN;
    g_wsi.wl.keycodes[KEY_UP]         = SC_KEY_UP;
    g_wsi.wl.keycodes[KEY_F1]         = SC_KEY_F1;
    g_wsi.wl.keycodes[KEY_F2]         = SC_KEY_F2;
    g_wsi.wl.keycodes[KEY_F3]         = SC_KEY_F3;
    g_wsi.wl.keycodes[KEY_F4]         = SC_KEY_F4;
    g_wsi.wl.keycodes[KEY_F5]         = SC_KEY_F5;
    g_wsi.wl.keycodes[KEY_F6]         = SC_KEY_F6;
    g_wsi.wl.keycodes[KEY_F7]         = SC_KEY_F7;
    g_wsi.wl.keycodes[KEY_F8]         = SC_KEY_F8;
    g_wsi.wl.keycodes[KEY_F9]         = SC_KEY_F9;
    g_wsi.wl.keycodes[KEY_F10]        = SC_KEY_F10;
    g_wsi.wl.keycodes[KEY_F11]        = SC_KEY_F11;
    g_wsi.wl.keycodes[KEY_F12]        = SC_KEY_F12;
    g_wsi.wl.keycodes[KEY_F13]        = SC_KEY_F13;
    g_wsi.wl.keycodes[KEY_F14]        = SC_KEY_F14;
    g_wsi.wl.keycodes[KEY_F15]        = SC_KEY_F15;
    g_wsi.wl.keycodes[KEY_F16]        = SC_KEY_F16;
    g_wsi.wl.keycodes[KEY_F17]        = SC_KEY_F17;
    g_wsi.wl.keycodes[KEY_F18]        = SC_KEY_F18;
    g_wsi.wl.keycodes[KEY_F19]        = SC_KEY_F19;
    g_wsi.wl.keycodes[KEY_F20]        = SC_KEY_F20;
    g_wsi.wl.keycodes[KEY_F21]        = SC_KEY_F21;
    g_wsi.wl.keycodes[KEY_F22]        = SC_KEY_F22;
    g_wsi.wl.keycodes[KEY_F23]        = SC_KEY_F23;
    g_wsi.wl.keycodes[KEY_F24]        = SC_KEY_F24;
    g_wsi.wl.keycodes[KEY_KPSLASH]    = SC_KEY_KP_DIVIDE;
    g_wsi.wl.keycodes[KEY_KPASTERISK] = SC_KEY_KP_MULTIPLY;
    g_wsi.wl.keycodes[KEY_KPMINUS]    = SC_KEY_KP_SUBTRACT;
    g_wsi.wl.keycodes[KEY_KPPLUS]     = SC_KEY_KP_ADD;
    g_wsi.wl.keycodes[KEY_KP0]        = SC_KEY_KP_0;
    g_wsi.wl.keycodes[KEY_KP1]        = SC_KEY_KP_1;
    g_wsi.wl.keycodes[KEY_KP2]        = SC_KEY_KP_2;
    g_wsi.wl.keycodes[KEY_KP3]        = SC_KEY_KP_3;
    g_wsi.wl.keycodes[KEY_KP4]        = SC_KEY_KP_4;
    g_wsi.wl.keycodes[KEY_KP5]        = SC_KEY_KP_5;
    g_wsi.wl.keycodes[KEY_KP6]        = SC_KEY_KP_6;
    g_wsi.wl.keycodes[KEY_KP7]        = SC_KEY_KP_7;
    g_wsi.wl.keycodes[KEY_KP8]        = SC_KEY_KP_8;
    g_wsi.wl.keycodes[KEY_KP9]        = SC_KEY_KP_9;
    g_wsi.wl.keycodes[KEY_KPDOT]      = SC_KEY_KP_DECIMAL;
    g_wsi.wl.keycodes[KEY_KPEQUAL]    = SC_KEY_KP_EQUAL;
    g_wsi.wl.keycodes[KEY_KPENTER]    = SC_KEY_KP_ENTER;
    g_wsi.wl.keycodes[KEY_102ND]      = SC_KEY_WORLD_2;

    for (int scancode = 0;  scancode < 256;  scancode++)
    {
        if (g_wsi.wl.keycodes[scancode] > 0)
            g_wsi.wl.scancodes[g_wsi.wl.keycodes[scancode]] = scancode;
    }
}

static bool loadCursorTheme(void)
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

    g_wsi.wl.cursorTheme = wl_cursor_theme_load(themeName, cursorSize, g_wsi.wl.shm);
    if (!g_wsi.wl.cursorTheme)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to load default cursor theme");
        return false;
    }

    // If this happens to be NULL, we just fallback to the scale=1 version.
    g_wsi.wl.cursorThemeHiDPI =
        wl_cursor_theme_load(themeName, cursorSize * 2, g_wsi.wl.shm);

    g_wsi.wl.cursorSurface = wl_compositor_create_surface(g_wsi.wl.compositor);
    g_wsi.wl.cursorTimerfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    return true;
}


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

bool wayland_connect(int platformID, platform_st* platform)
{
    const platform_st wayland =
    {
        .platformID = SC_PLATFORM_WAYLAND,
        .init = wayland_init,
        .terminate = wayland_terminate,
        .getCursorPos = wayland_get_cursor_pos,
        .setCursorPos = wayland_set_cursor_pos,
        .setCursorMode = wayland_set_cursorMode,
        .setRawMouseMotion = wayland_set_mouse_raw_motion,
        .rawMouseMotionSupported = wayland_mouse_raw_motion_supported,
        .createCursor = wayland_create_cursor,
        .createStandardCursor = wayland_create_standard_cursor,
        .destroyCursor = wayland_destroy_cursor,
        .setCursor = wayland_set_cursor,
        .getScancodeName = wayland_get_scancode_name,
        .getKeyScancode = wayland_get_key_scancode,
        .setClipboardString = wayland_set_clipboard_string,
        .getClipboardString = wayland_get_clipboard_string,
        .freeMonitor = wsi_free_monitorWayland,
        .getMonitorPos = wayland_get_monitor_pos,
        .getMonitorContentScale = wayland_get_monitor_content_scale,
        .getMonitorWorkarea = wayland_get_monitor_work_area,
        .getVideoModes = wayland_get_video_modes,
        .getVideoMode = wayland_get_video_mode,
        .getGammaRamp = wayland_get_gamma_ramp,
        .setGammaRamp = wayland_set_gamma_ramp,
        .createWindow = wayland_create_window,
        .destroyWindow = wayland_destroy_window,
        .setWindowTitle = wayland_set_window_title,
        .setWindowIcon = wayland_set_window_icon,
        .getWindowPos = wayland_get_window_pos,
        .setWindowPos = wayland_set_window_pos,
        .getWindowSize = wayland_get_window_size,
        .setWindowSize = wayland_set_window_size,
        .setWindowSizeLimits = wayland_set_window_size_limits,
        .setWindowAspectRatio = wayland_set_window_aspect_ratio,
        .getWindowFrameSize = wayland_get_window_frame_size,
        .getWindowContentScale = wayland_get_window_content_scale,
        .iconifyWindow = wayland_iconify_window,
        .restoreWindow = wayland_restore_window,
        .maximizeWindow = wayland_maximize_window,
        .showWindow = wayland_show_window,
        .hideWindow = wayland_hide_window,
        .requestWindowAttention = wayland_request_window_attention,
        .focusWindow = wayland_focus_window,
        .setWindowMonitor = wayland_set_window_monitor,
        .windowFocused = wayland_window_focused,
        .windowIconified = wayland_window_iconified,
        .windowVisible = wayland_window_visible,
        .windowMaximized = wayland_window_maximized,
        .windowHovered = wayland_window_hovered,
        .getWindowOpacity = wayland_get_window_opacity,
        .setWindowResizable = wayland_set_window_resizable,
        .setWindowDecorated = wayland_set_window_decorated,
        .setWindowFloating = wayland_set_window_floating,
        .setWindowOpacity = wayland_set_window_opacity,
        .setWindowMousePassthrough = wayland_set_window_mouse_passthrough,
        .pollEvents = wayland_poll_events,
        .waitEvents = wayland_wait_events,
        .waitEventsTimeout = wayland_wait_eventsTimeout,
        .postEmptyEvent = wayland_post_empty_event
    };

    void* module = impl_platform_load_module("libwayland-client.so.0");
    if (!module)
    {
        if (platformID == SC_PLATFORM_WAYLAND)
        {
            impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                            "Wayland: Failed to load libwayland-client");
        }

        return false;
    }

    PFN_wl_display_connect wl_display_connect = (PFN_wl_display_connect)
        impl_platform_get_module_symbol(module, "wl_display_connect");
    if (!wl_display_connect)
    {
        if (platformID == SC_PLATFORM_WAYLAND)
        {
            impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                            "Wayland: Failed to load libwayland-client entry point");
        }

        impl_platform_unload_module(module);
        return false;
    }

    struct wl_display* display = wl_display_connect(NULL);
    if (!display)
    {
        if (platformID == SC_PLATFORM_WAYLAND)
            impl_on_error(SC_WSI_ERR_PLATFORM_ERROR, "Wayland: Failed to connect to display");

        impl_platform_unload_module(module);
        return false;
    }

    g_wsi.wl.display = display;
    g_wsi.wl.client.handle = module;

    *platform = wayland;
    return true;
}

int wayland_init(void)
{
    // These must be set before any failure checks
    g_wsi.wl.keyRepeatTimerfd = -1;
    g_wsi.wl.cursorTimerfd = -1;

    g_wsi.wl.tag = sc_wsi_get_version_string();

    g_wsi.wl.client.display_flush = (PFN_wl_display_flush)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_display_flush");
    g_wsi.wl.client.display_cancel_read = (PFN_wl_display_cancel_read)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_display_cancel_read");
    g_wsi.wl.client.display_dispatch_pending = (PFN_wl_display_dispatch_pending)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_display_dispatch_pending");
    g_wsi.wl.client.display_read_events = (PFN_wl_display_read_events)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_display_read_events");
    g_wsi.wl.client.display_disconnect = (PFN_wl_display_disconnect)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_display_disconnect");
    g_wsi.wl.client.display_roundtrip = (PFN_wl_display_roundtrip)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_display_roundtrip");
    g_wsi.wl.client.display_get_fd = (PFN_wl_display_get_fd)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_display_get_fd");
    g_wsi.wl.client.display_prepare_read = (PFN_wl_display_prepare_read)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_display_prepare_read");
    g_wsi.wl.client.display_create_queue = (PFN_wl_display_create_queue)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_display_create_queue");
    g_wsi.wl.client.display_prepare_read_queue = (PFN_wl_display_prepare_read_queue)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_display_prepare_read_queue");
    g_wsi.wl.client.display_dispatch_queue_pending = (PFN_wl_display_dispatch_queue_pending)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_display_dispatch_queue_pending");
    g_wsi.wl.client.event_queue_destroy = (PFN_wl_event_queue_destroy)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_event_queue_destroy");
    g_wsi.wl.client.proxy_marshal = (PFN_wl_proxy_marshal)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_proxy_marshal");
    g_wsi.wl.client.proxy_add_listener = (PFN_wl_proxy_add_listener)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_proxy_add_listener");
    g_wsi.wl.client.proxy_destroy = (PFN_wl_proxy_destroy)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_proxy_destroy");
    g_wsi.wl.client.proxy_marshal_constructor = (PFN_wl_proxy_marshal_constructor)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_proxy_marshal_constructor");
    g_wsi.wl.client.proxy_marshal_constructor_versioned = (PFN_wl_proxy_marshal_constructor_versioned)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_proxy_marshal_constructor_versioned");
    g_wsi.wl.client.proxy_get_user_data = (PFN_wl_proxy_get_user_data)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_proxy_get_user_data");
    g_wsi.wl.client.proxy_set_user_data = (PFN_wl_proxy_set_user_data)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_proxy_set_user_data");
    g_wsi.wl.client.proxy_get_tag = (PFN_wl_proxy_get_tag)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_proxy_get_tag");
    g_wsi.wl.client.proxy_set_tag = (PFN_wl_proxy_set_tag)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_proxy_set_tag");
    g_wsi.wl.client.proxy_get_version = (PFN_wl_proxy_get_version)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_proxy_get_version");
    g_wsi.wl.client.proxy_marshal_flags = (PFN_wl_proxy_marshal_flags)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_proxy_marshal_flags");
    g_wsi.wl.client.proxy_create_wrapper = (PFN_wl_proxy_create_wrapper)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_proxy_create_wrapper");
    g_wsi.wl.client.proxy_wrapper_destroy = (PFN_wl_proxy_wrapper_destroy)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_proxy_wrapper_destroy");
    g_wsi.wl.client.proxy_set_queue = (PFN_wl_proxy_set_queue)
        impl_platform_get_module_symbol(g_wsi.wl.client.handle, "wl_proxy_set_queue");

    if (!g_wsi.wl.client.display_flush ||
        !g_wsi.wl.client.display_cancel_read ||
        !g_wsi.wl.client.display_dispatch_pending ||
        !g_wsi.wl.client.display_read_events ||
        !g_wsi.wl.client.display_disconnect ||
        !g_wsi.wl.client.display_roundtrip ||
        !g_wsi.wl.client.display_get_fd ||
        !g_wsi.wl.client.display_prepare_read ||
        !g_wsi.wl.client.display_create_queue ||
        !g_wsi.wl.client.display_prepare_read_queue ||
        !g_wsi.wl.client.display_dispatch_queue_pending ||
        !g_wsi.wl.client.event_queue_destroy ||
        !g_wsi.wl.client.proxy_marshal ||
        !g_wsi.wl.client.proxy_add_listener ||
        !g_wsi.wl.client.proxy_destroy ||
        !g_wsi.wl.client.proxy_marshal_constructor ||
        !g_wsi.wl.client.proxy_marshal_constructor_versioned ||
        !g_wsi.wl.client.proxy_get_user_data ||
        !g_wsi.wl.client.proxy_set_user_data ||
        !g_wsi.wl.client.proxy_get_tag ||
        !g_wsi.wl.client.proxy_set_tag ||
        !g_wsi.wl.client.proxy_create_wrapper ||
        !g_wsi.wl.client.proxy_wrapper_destroy ||
        !g_wsi.wl.client.proxy_set_queue)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to load libwayland-client entry point");
        return false;
    }

    g_wsi.wl.cursor.handle = impl_platform_load_module("libwayland-cursor.so.0");
    if (!g_wsi.wl.cursor.handle)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to load libwayland-cursor");
        return false;
    }

    g_wsi.wl.cursor.theme_load = (PFN_wl_cursor_theme_load)
        impl_platform_get_module_symbol(g_wsi.wl.cursor.handle, "wl_cursor_theme_load");
    g_wsi.wl.cursor.theme_destroy = (PFN_wl_cursor_theme_destroy)
        impl_platform_get_module_symbol(g_wsi.wl.cursor.handle, "wl_cursor_theme_destroy");
    g_wsi.wl.cursor.theme_get_cursor = (PFN_wl_cursor_theme_get_cursor)
        impl_platform_get_module_symbol(g_wsi.wl.cursor.handle, "wl_cursor_theme_get_cursor");
    g_wsi.wl.cursor.image_get_buffer = (PFN_wl_cursor_image_get_buffer)
        impl_platform_get_module_symbol(g_wsi.wl.cursor.handle, "wl_cursor_image_get_buffer");

    g_wsi.wl.xkb.handle = impl_platform_load_module("libxkbcommon.so.0");
    if (!g_wsi.wl.xkb.handle)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to load libxkbcommon");
        return false;
    }

    g_wsi.wl.xkb.context_new = (PFN_xkb_context_new)
        impl_platform_get_module_symbol(g_wsi.wl.xkb.handle, "xkb_context_new");
    g_wsi.wl.xkb.context_unref = (PFN_xkb_context_unref)
        impl_platform_get_module_symbol(g_wsi.wl.xkb.handle, "xkb_context_unref");
    g_wsi.wl.xkb.keymap_new_from_string = (PFN_xkb_keymap_new_from_string)
        impl_platform_get_module_symbol(g_wsi.wl.xkb.handle, "xkb_keymap_new_from_string");
    g_wsi.wl.xkb.keymap_unref = (PFN_xkb_keymap_unref)
        impl_platform_get_module_symbol(g_wsi.wl.xkb.handle, "xkb_keymap_unref");
    g_wsi.wl.xkb.keymap_mod_get_index = (PFN_xkb_keymap_mod_get_index)
        impl_platform_get_module_symbol(g_wsi.wl.xkb.handle, "xkb_keymap_mod_get_index");
    g_wsi.wl.xkb.keymap_key_repeats = (PFN_xkb_keymap_key_repeats)
        impl_platform_get_module_symbol(g_wsi.wl.xkb.handle, "xkb_keymap_key_repeats");
    g_wsi.wl.xkb.keymap_key_get_syms_by_level = (PFN_xkb_keymap_key_get_syms_by_level)
        impl_platform_get_module_symbol(g_wsi.wl.xkb.handle, "xkb_keymap_key_get_syms_by_level");
    g_wsi.wl.xkb.state_new = (PFN_xkb_state_new)
        impl_platform_get_module_symbol(g_wsi.wl.xkb.handle, "xkb_state_new");
    g_wsi.wl.xkb.state_unref = (PFN_xkb_state_unref)
        impl_platform_get_module_symbol(g_wsi.wl.xkb.handle, "xkb_state_unref");
    g_wsi.wl.xkb.state_key_get_syms = (PFN_xkb_state_key_get_syms)
        impl_platform_get_module_symbol(g_wsi.wl.xkb.handle, "xkb_state_key_get_syms");
    g_wsi.wl.xkb.state_update_mask = (PFN_xkb_state_update_mask)
        impl_platform_get_module_symbol(g_wsi.wl.xkb.handle, "xkb_state_update_mask");
    g_wsi.wl.xkb.state_key_get_layout = (PFN_xkb_state_key_get_layout)
        impl_platform_get_module_symbol(g_wsi.wl.xkb.handle, "xkb_state_key_get_layout");
    g_wsi.wl.xkb.state_mod_index_is_active = (PFN_xkb_state_mod_index_is_active)
        impl_platform_get_module_symbol(g_wsi.wl.xkb.handle, "xkb_state_mod_index_is_active");
    g_wsi.wl.xkb.compose_table_new_from_locale = (PFN_xkb_compose_table_new_from_locale)
        impl_platform_get_module_symbol(g_wsi.wl.xkb.handle, "xkb_compose_table_new_from_locale");
    g_wsi.wl.xkb.compose_table_unref = (PFN_xkb_compose_table_unref)
        impl_platform_get_module_symbol(g_wsi.wl.xkb.handle, "xkb_compose_table_unref");
    g_wsi.wl.xkb.compose_state_new = (PFN_xkb_compose_state_new)
        impl_platform_get_module_symbol(g_wsi.wl.xkb.handle, "xkb_compose_state_new");
    g_wsi.wl.xkb.compose_state_unref = (PFN_xkb_compose_state_unref)
        impl_platform_get_module_symbol(g_wsi.wl.xkb.handle, "xkb_compose_state_unref");
    g_wsi.wl.xkb.compose_state_feed = (PFN_xkb_compose_state_feed)
        impl_platform_get_module_symbol(g_wsi.wl.xkb.handle, "xkb_compose_state_feed");
    g_wsi.wl.xkb.compose_state_get_status = (PFN_xkb_compose_state_get_status)
        impl_platform_get_module_symbol(g_wsi.wl.xkb.handle, "xkb_compose_state_get_status");
    g_wsi.wl.xkb.compose_state_get_one_sym = (PFN_xkb_compose_state_get_one_sym)
        impl_platform_get_module_symbol(g_wsi.wl.xkb.handle, "xkb_compose_state_get_one_sym");
    g_wsi.wl.xkb.keysym_to_utf32 = (PFN_xkb_keysym_to_utf32)
        impl_platform_get_module_symbol(g_wsi.wl.xkb.handle, "xkb_keysym_to_utf32");
    g_wsi.wl.xkb.keysym_to_utf8 = (PFN_xkb_keysym_to_utf8)
        impl_platform_get_module_symbol(g_wsi.wl.xkb.handle, "xkb_keysym_to_utf8");

    if (!g_wsi.wl.xkb.context_new ||
        !g_wsi.wl.xkb.context_unref ||
        !g_wsi.wl.xkb.keymap_new_from_string ||
        !g_wsi.wl.xkb.keymap_unref ||
        !g_wsi.wl.xkb.keymap_mod_get_index ||
        !g_wsi.wl.xkb.keymap_key_repeats ||
        !g_wsi.wl.xkb.keymap_key_get_syms_by_level ||
        !g_wsi.wl.xkb.state_new ||
        !g_wsi.wl.xkb.state_unref ||
        !g_wsi.wl.xkb.state_key_get_syms ||
        !g_wsi.wl.xkb.state_update_mask ||
        !g_wsi.wl.xkb.state_key_get_layout ||
        !g_wsi.wl.xkb.state_mod_index_is_active ||
        !g_wsi.wl.xkb.compose_table_new_from_locale ||
        !g_wsi.wl.xkb.compose_table_unref ||
        !g_wsi.wl.xkb.compose_state_new ||
        !g_wsi.wl.xkb.compose_state_unref ||
        !g_wsi.wl.xkb.compose_state_feed ||
        !g_wsi.wl.xkb.compose_state_get_status ||
        !g_wsi.wl.xkb.compose_state_get_one_sym)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to load all entry points from libxkbcommon");
        return false;
    }

    if (g_wsi.hints.init.wl.libdecorMode == SC_WAYLAND_PREFER_LIBDECOR)
        g_wsi.wl.libdecor.handle = impl_platform_load_module("libdecor-0.so.0");

    if (g_wsi.wl.libdecor.handle)
    {
        g_wsi.wl.libdecor.libdecor_new_ = (PFN_libdecor_new)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_new");
        g_wsi.wl.libdecor.libdecor_unref_ = (PFN_libdecor_unref)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_unref");
        g_wsi.wl.libdecor.libdecor_get_fd_ = (PFN_libdecor_get_fd)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_get_fd");
        g_wsi.wl.libdecor.libdecor_dispatch_ = (PFN_libdecor_dispatch)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_dispatch");
        g_wsi.wl.libdecor.libdecor_decorate_ = (PFN_libdecor_decorate)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_decorate");
        g_wsi.wl.libdecor.libdecor_frame_unref_ = (PFN_libdecor_frame_unref)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_frame_unref");
        g_wsi.wl.libdecor.libdecor_frame_set_app_id_ = (PFN_libdecor_frame_set_app_id)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_frame_set_app_id");
        g_wsi.wl.libdecor.libdecor_frame_set_title_ = (PFN_libdecor_frame_set_title)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_frame_set_title");
        g_wsi.wl.libdecor.libdecor_frame_set_minimized_ = (PFN_libdecor_frame_set_minimized)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_frame_set_minimized");
        g_wsi.wl.libdecor.libdecor_frame_set_fullscreen_ = (PFN_libdecor_frame_set_fullscreen)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_frame_set_fullscreen");
        g_wsi.wl.libdecor.libdecor_frame_unset_fullscreen_ = (PFN_libdecor_frame_unset_fullscreen)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_frame_unset_fullscreen");
        g_wsi.wl.libdecor.libdecor_frame_map_ = (PFN_libdecor_frame_map)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_frame_map");
        g_wsi.wl.libdecor.libdecor_frame_commit_ = (PFN_libdecor_frame_commit)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_frame_commit");
        g_wsi.wl.libdecor.libdecor_frame_set_min_content_size_ = (PFN_libdecor_frame_set_min_content_size)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_frame_set_min_content_size");
        g_wsi.wl.libdecor.libdecor_frame_set_max_content_size_ = (PFN_libdecor_frame_set_max_content_size)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_frame_set_max_content_size");
        g_wsi.wl.libdecor.libdecor_frame_set_maximized_ = (PFN_libdecor_frame_set_maximized)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_frame_set_maximized");
        g_wsi.wl.libdecor.libdecor_frame_unset_maximized_ = (PFN_libdecor_frame_unset_maximized)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_frame_unset_maximized");
        g_wsi.wl.libdecor.libdecor_frame_set_capabilities_ = (PFN_libdecor_frame_set_capabilities)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_frame_set_capabilities");
        g_wsi.wl.libdecor.libdecor_frame_unset_capabilities_ = (PFN_libdecor_frame_unset_capabilities)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_frame_unset_capabilities");
        g_wsi.wl.libdecor.libdecor_frame_set_visibility_ = (PFN_libdecor_frame_set_visibility)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_frame_set_visibility");
        g_wsi.wl.libdecor.libdecor_frame_is_visible_ = (PFN_libdecor_frame_is_visible)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_frame_is_visible");
        g_wsi.wl.libdecor.libdecor_frame_get_xdg_toplevel_ = (PFN_libdecor_frame_get_xdg_toplevel)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_frame_get_xdg_toplevel");
        g_wsi.wl.libdecor.libdecor_configuration_get_content_size_ = (PFN_libdecor_configuration_get_content_size)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_configuration_get_content_size");
        g_wsi.wl.libdecor.libdecor_configuration_get_window_state_ = (PFN_libdecor_configuration_get_window_state)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_configuration_get_window_state");
        g_wsi.wl.libdecor.libdecor_state_new_ = (PFN_libdecor_state_new)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_state_new");
        g_wsi.wl.libdecor.libdecor_state_free_ = (PFN_libdecor_state_free)
            impl_platform_get_module_symbol(g_wsi.wl.libdecor.handle, "libdecor_state_free");

        if (!g_wsi.wl.libdecor.libdecor_new_ ||
            !g_wsi.wl.libdecor.libdecor_unref_ ||
            !g_wsi.wl.libdecor.libdecor_get_fd_ ||
            !g_wsi.wl.libdecor.libdecor_dispatch_ ||
            !g_wsi.wl.libdecor.libdecor_decorate_ ||
            !g_wsi.wl.libdecor.libdecor_frame_unref_ ||
            !g_wsi.wl.libdecor.libdecor_frame_set_app_id_ ||
            !g_wsi.wl.libdecor.libdecor_frame_set_title_ ||
            !g_wsi.wl.libdecor.libdecor_frame_set_minimized_ ||
            !g_wsi.wl.libdecor.libdecor_frame_set_fullscreen_ ||
            !g_wsi.wl.libdecor.libdecor_frame_unset_fullscreen_ ||
            !g_wsi.wl.libdecor.libdecor_frame_map_ ||
            !g_wsi.wl.libdecor.libdecor_frame_commit_ ||
            !g_wsi.wl.libdecor.libdecor_frame_set_min_content_size_ ||
            !g_wsi.wl.libdecor.libdecor_frame_set_max_content_size_ ||
            !g_wsi.wl.libdecor.libdecor_frame_set_maximized_ ||
            !g_wsi.wl.libdecor.libdecor_frame_unset_maximized_ ||
            !g_wsi.wl.libdecor.libdecor_frame_set_capabilities_ ||
            !g_wsi.wl.libdecor.libdecor_frame_unset_capabilities_ ||
            !g_wsi.wl.libdecor.libdecor_frame_set_visibility_ ||
            !g_wsi.wl.libdecor.libdecor_frame_is_visible_ ||
            !g_wsi.wl.libdecor.libdecor_frame_get_xdg_toplevel_ ||
            !g_wsi.wl.libdecor.libdecor_configuration_get_content_size_ ||
            !g_wsi.wl.libdecor.libdecor_configuration_get_window_state_ ||
            !g_wsi.wl.libdecor.libdecor_state_new_ ||
            !g_wsi.wl.libdecor.libdecor_state_free_)
        {
            impl_platform_unload_module(g_wsi.wl.libdecor.handle);
            memset(&g_wsi.wl.libdecor, 0, sizeof(g_wsi.wl.libdecor));
        }
    }

    g_wsi.wl.registry = wl_display_get_registry(g_wsi.wl.display);
    wl_registry_add_listener(g_wsi.wl.registry, &registryListener, NULL);

    createKeyTables();

    g_wsi.wl.keyRepeatTimerfd =
        timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (g_wsi.wl.keyRepeatTimerfd == -1)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to create timerfd: %s",
                        strerror(errno));
        return false;
    }

    g_wsi.wl.xkb.context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!g_wsi.wl.xkb.context)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to initialize xkb context");
        return false;
    }

    // Sync so we got all registry objects
    wl_display_roundtrip(g_wsi.wl.display);

    // Sync so we got all initial output events
    wl_display_roundtrip(g_wsi.wl.display);

    if (g_wsi.wl.libdecor.handle)
    {
        g_wsi.wl.libdecor.context = libdecor_new(g_wsi.wl.display, &libdecorInterface);
        if (g_wsi.wl.libdecor.context)
        {
            // Perform an initial dispatch and flush to get the init started
            libdecor_dispatch(g_wsi.wl.libdecor.context, 0);

            // Create sync point to "know" when libdecor is ready for use
            struct wl_callback* callback = wl_display_sync(g_wsi.wl.display);
            wl_callback_add_listener(callback, &libdecorReadyListener, NULL);
        }
    }

    if (!g_wsi.wl.wmBase)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to find xdg-shell in your compositor");
        return false;
    }

    if (!g_wsi.wl.shm)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to find wl_shm in your compositor");
        return false;
    }

    if (!loadCursorTheme())
        return false;

    if (g_wsi.wl.seat && g_wsi.wl.dataDeviceManager)
    {
        g_wsi.wl.dataDevice =
            wl_data_device_manager_get_data_device(g_wsi.wl.dataDeviceManager,
                                                   g_wsi.wl.seat);
        wayland_AddDataDeviceListener(g_wsi.wl.dataDevice);
    }

    return true;
}

void wayland_terminate(void)
{
    if (g_wsi.wl.libdecor.context)
    {
        // Allow libdecor to finish receiving all its requested globals
        // and ensure the associated sync callback object is destroyed
        while (!g_wsi.wl.libdecor.ready)
            wayland_wait_events();

        libdecor_unref(g_wsi.wl.libdecor.context);
    }

    if (g_wsi.wl.xkb.composeState)
        xkb_compose_state_unref(g_wsi.wl.xkb.composeState);
    if (g_wsi.wl.xkb.keymap)
        xkb_keymap_unref(g_wsi.wl.xkb.keymap);
    if (g_wsi.wl.xkb.state)
        xkb_state_unref(g_wsi.wl.xkb.state);
    if (g_wsi.wl.xkb.context)
        xkb_context_unref(g_wsi.wl.xkb.context);

    if (g_wsi.wl.cursorTheme)
        wl_cursor_theme_destroy(g_wsi.wl.cursorTheme);
    if (g_wsi.wl.cursorThemeHiDPI)
        wl_cursor_theme_destroy(g_wsi.wl.cursorThemeHiDPI);

    for (unsigned int i = 0; i < g_wsi.wl.offerCount; i++)
        wl_data_offer_destroy(g_wsi.wl.offers[i].offer);

    wsi_free(g_wsi.wl.offers);

    if (g_wsi.wl.cursorSurface)
        wl_surface_destroy(g_wsi.wl.cursorSurface);
    if (g_wsi.wl.subcompositor)
        wl_subcompositor_destroy(g_wsi.wl.subcompositor);
    if (g_wsi.wl.compositor)
        wl_compositor_destroy(g_wsi.wl.compositor);
    if (g_wsi.wl.shm)
        wl_shm_destroy(g_wsi.wl.shm);
    if (g_wsi.wl.viewporter)
        wp_viewporter_destroy(g_wsi.wl.viewporter);
    if (g_wsi.wl.decorationManager)
        zxdg_decoration_manager_v1_destroy(g_wsi.wl.decorationManager);
    if (g_wsi.wl.wmBase)
        xdg_wm_base_destroy(g_wsi.wl.wmBase);
    if (g_wsi.wl.selectionOffer)
        wl_data_offer_destroy(g_wsi.wl.selectionOffer);
    if (g_wsi.wl.dragOffer)
        wl_data_offer_destroy(g_wsi.wl.dragOffer);
    if (g_wsi.wl.selectionSource)
        wl_data_source_destroy(g_wsi.wl.selectionSource);
    if (g_wsi.wl.dataDevice)
        wl_data_device_destroy(g_wsi.wl.dataDevice);
    if (g_wsi.wl.dataDeviceManager)
        wl_data_device_manager_destroy(g_wsi.wl.dataDeviceManager);
    if (g_wsi.wl.pointer)
        wl_pointer_destroy(g_wsi.wl.pointer);
    if (g_wsi.wl.keyboard)
        wl_keyboard_destroy(g_wsi.wl.keyboard);
    if (g_wsi.wl.seat)
        wl_seat_destroy(g_wsi.wl.seat);
    if (g_wsi.wl.relativePointerManager)
        zwp_relative_pointer_manager_v1_destroy(g_wsi.wl.relativePointerManager);
    if (g_wsi.wl.pointerConstraints)
        zwp_pointer_constraints_v1_destroy(g_wsi.wl.pointerConstraints);
    if (g_wsi.wl.idleInhibitManager)
        zwp_idle_inhibit_manager_v1_destroy(g_wsi.wl.idleInhibitManager);
    if (g_wsi.wl.activationManager)
        xdg_activation_v1_destroy(g_wsi.wl.activationManager);
    if (g_wsi.wl.fractionalScaleManager)
        wp_fractional_scale_manager_v1_destroy(g_wsi.wl.fractionalScaleManager);
    if (g_wsi.wl.registry)
        wl_registry_destroy(g_wsi.wl.registry);
    if (g_wsi.wl.display)
    {
        wl_display_flush(g_wsi.wl.display);
        wl_display_disconnect(g_wsi.wl.display);
    }

    if (g_wsi.wl.keyRepeatTimerfd >= 0)
        close(g_wsi.wl.keyRepeatTimerfd);
    if (g_wsi.wl.cursorTimerfd >= 0)
        close(g_wsi.wl.cursorTimerfd);

    // Free modules only after all Wayland termination functions are called

    impl_platform_unload_module(g_wsi.wl.libdecor.handle);
    impl_platform_unload_module(g_wsi.wl.xkb.handle);
    impl_platform_unload_module(g_wsi.wl.cursor.handle);
    impl_platform_unload_module(g_wsi.wl.client.handle);

    wsi_free(g_wsi.wl.clipboardString);

    memset(&g_wsi.wl, 0, sizeof(g_wsi.wl));
}

#endif // WSI_WAYLAND

