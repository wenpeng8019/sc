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
#if defined(WSI_IOS)
        case SC_PLATFORM_IOS:
            return NULL;
#endif
#if defined(WSI_ANDROID)
        // EGL 用 EGL_DEFAULT_DISPLAY；无独立原生显示句柄
        case SC_PLATFORM_ANDROID:
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
#if defined(WSI_IOS)
        // 返回 UIView*（其 layerClass 为 CAMetalLayer）；gpu 侧取 view.layer 建 Metal surface
        case SC_PLATFORM_IOS:
            return window->ios.view;
#endif
#if defined(WSI_ANDROID)
        // 返回 ANativeWindow*；gpu 侧据此建 EGL/Vulkan surface
        case SC_PLATFORM_ANDROID:
            return window->android.window;
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

#if defined(WSI_ANDROID)
// android 后端的帧驱动入口（android_platform.c 待实现，对照 uikit_run）：
// 由 wsi 的 ANativeActivity_onCreate 在渲染线程经 sc_app_main 调用。
extern int android_run(sc_wsi_app_cb after_startup, sc_wsi_window_cb main_window_created,
                       sc_wsi_app_cb on_frame, sc_wsi_app_cb before_cleanup);
#endif

// 统一的应用入口：表面调用形式三平台一致（四回调），底层各有实现——
//   * iOS      = 自有主循环（uikit_run→UIApplicationMain 阻塞主线程）；
//   * Android  = 事件注册（android_run 注册 ANativeWindow/AChoreographer 后进入渲染线程循环）；
//   * 桌面     = 本函数内的通用托管循环（wsi_app_startup → after_startup → 建默认窗口 →
//               main_window_created → poll_events + on_frame → before_cleanup → wsi_app_cleanup）。
// 四回调语义跨平台一致，故桌面/移动的应用源码除入口外可完全一致。桌面程序也可
// 不用本函数，沿用「显式 while 自管循环」写法（wsi_app_startup → win_create → 自绘 → cleanup）。
WSI_API int sc_wsi_app_run(sc_wsi_app_cb after_startup,
                           sc_wsi_window_cb main_window_created,
                           sc_wsi_app_cb on_frame,
                           sc_wsi_app_cb before_cleanup)
{
#if defined(WSI_IOS)
    return uikit_run(after_startup, main_window_created, on_frame, before_cleanup);
#elif defined(WSI_ANDROID)
    return android_run(after_startup, main_window_created, on_frame, before_cleanup);
#else
    // 桌面平台（Cocoa/Win32/X11/Wayland）通用托管流程：wsi 建一个默认窗口，经
    // main_window_created 交付句柄——应用在回调里自定义窗口（sc_wsi_win_set_title/
    // sc_wsi_win_set_size 等）并初始化 gpu/gfx，随后 wsi 显示窗口并进入帧循环。
    {
        // 默认窗口尺寸/标题；应用可在 main_window_created 内改写。
        const int   default_width  = 1280;
        const int   default_height = 720;
        const char* default_title  = "sc";

        if (!sc_wsi_app_startup())
            return 1;

        // 子系统就绪后、建窗前回调一次。
        if (after_startup)
            after_startup();

        sc_window* win = sc_wsi_win_create(default_width, default_height,
                                           default_title, NULL, NULL);
        if (!win)
        {
            sc_wsi_app_cleanup();
            return 1;
        }

        // 交付主窗口：应用在此自定义窗口、初始化渲染、经 sc_wsi_win_set_callback 挂 delegate。
        if (main_window_created)
            main_window_created(win);

        sc_wsi_win_show(win);

        // 主循环：非阻塞泵事件 + 每帧回调（帧率由 on_frame 内的 present/vsync 决定，
        // 无渲染场景会忙转——托管流程面向 gpu 应用，present 提供节拍）。
        while (!sc_wsi_win_get_should_close(win))
        {
            sc_wsi_loop_poll();
            if (on_frame)
                on_frame();
        }

        if (before_cleanup)
            before_cleanup();

        sc_wsi_win_destroy(win);
        sc_wsi_app_cleanup();
        return 0;
    }
#endif
}

WSI_API sc_window* sc_wsi_app_main_window(void)
{
#if defined(WSI_IOS)
    return (sc_window*) g_wsi.ios.window;
#elif defined(WSI_ANDROID)
    return (sc_window*) g_wsi.android.window;
#else
    return NULL;
#endif
}

