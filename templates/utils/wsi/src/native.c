#include "internal.h"

#include <stdint.h>

WSI_API int sc_wsi_get_platform(void)
{
    if (!g_wsi.initialized)
    {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return SC_PLATFORM_ANY;
    }

    return g_wsi.platform.platformID;
}

WSI_API void* sc_wsi_win_get_native_display(sc_window* handle)
{
    if (!g_wsi.initialized)
    {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }

    window_st* window = (window_st*) handle;
    if (!window)
    {
        impl_on_error(SC_WSI_ERR_INVALID_VALUE, "Invalid window handle");
        return NULL;
    }

    switch (g_wsi.platform.platformID)
    {
#if defined(WSI_X11)
        case SC_PLATFORM_X11:
            return g_wsi.x11.display;
#endif
#if defined(WSI_WAYLAND)
        case SC_PLATFORM_WAYLAND:
            return g_wsi.wl.display;
#endif
#if defined(WSI_WIN32)
        case SC_PLATFORM_WIN32:
            return NULL;
#endif
#if defined(WSI_COCOA)
        case SC_PLATFORM_COCOA:
            return NULL;
#endif
        default:
            impl_on_error(SC_WSI_ERR_PLATFORM_UNAVAILABLE,
                          "No native display for active platform");
            return NULL;
    }
}

WSI_API void* sc_wsi_win_get_native_window(sc_window* handle)
{
    if (!g_wsi.initialized)
    {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }

    window_st* window = (window_st*) handle;
    if (!window)
    {
        impl_on_error(SC_WSI_ERR_INVALID_VALUE, "Invalid window handle");
        return NULL;
    }

    switch (g_wsi.platform.platformID)
    {
#if defined(WSI_WIN32)
        case SC_PLATFORM_WIN32:
            return window->win32.handle;
#endif
#if defined(WSI_COCOA)
        case SC_PLATFORM_COCOA:
            return window->ns.view;
#endif
#if defined(WSI_X11)
        case SC_PLATFORM_X11:
            return (void*) (uintptr_t) window->x11.handle;
#endif
#if defined(WSI_WAYLAND)
        case SC_PLATFORM_WAYLAND:
            return window->wl.surface;
#endif
        default:
            impl_on_error(SC_WSI_ERR_PLATFORM_UNAVAILABLE,
                          "No native window for active platform");
            return NULL;
    }
}
