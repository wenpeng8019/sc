
#if defined(GLFW_BUILD_WIN32_MODULE) || \
    defined(GLFW_BUILD_POSIX_MODULE) || \
    defined(GLFW_BUILD_POSIX_POLL)
 #error "You must not define these; define zero or more _GLFW_<platform> macros instead"
#endif

#include "null_platform.h"

#if defined(WSI_WIN32)
 #include "win32_platform.h"
 #define WSI_EXPOSE_NATIVE_WIN32
#else
 #define GLFW_WIN32_WINDOW_STATE
 #define GLFW_WIN32_MONITOR_STATE
 #define GLFW_WIN32_CURSOR_STATE
 #define GLFW_WIN32_LIBRARY_WINDOW_STATE
#endif

#if defined(WSI_COCOA)
 #include "cocoa_platform.h"
 #define WSI_EXPOSE_NATIVE_COCOA
#else
 #define GLFW_COCOA_WINDOW_STATE
 #define GLFW_COCOA_MONITOR_STATE
 #define GLFW_COCOA_CURSOR_STATE
 #define GLFW_COCOA_LIBRARY_WINDOW_STATE
#endif

#if defined(WSI_WAYLAND)
 #include "wl_platform.h"
 #define WSI_EXPOSE_NATIVE_WAYLAND
#else
 #define GLFW_WAYLAND_WINDOW_STATE
 #define GLFW_WAYLAND_MONITOR_STATE
 #define GLFW_WAYLAND_CURSOR_STATE
 #define GLFW_WAYLAND_LIBRARY_WINDOW_STATE
#endif

#if defined(WSI_X11)
 #include "x11_platform.h"
 #define WSI_EXPOSE_NATIVE_X11
#else
 #define GLFW_X11_WINDOW_STATE
 #define GLFW_X11_MONITOR_STATE
 #define GLFW_X11_CURSOR_STATE
 #define GLFW_X11_LIBRARY_WINDOW_STATE
#endif


#define GLFW_PLATFORM_WINDOW_STATE \
        GLFW_WIN32_WINDOW_STATE \
        GLFW_COCOA_WINDOW_STATE \
        GLFW_WAYLAND_WINDOW_STATE \
        GLFW_X11_WINDOW_STATE \
        GLFW_NULL_WINDOW_STATE \

#define GLFW_PLATFORM_MONITOR_STATE \
        GLFW_WIN32_MONITOR_STATE \
        GLFW_COCOA_MONITOR_STATE \
        GLFW_WAYLAND_MONITOR_STATE \
        GLFW_X11_MONITOR_STATE \
        GLFW_NULL_MONITOR_STATE \

#define GLFW_PLATFORM_CURSOR_STATE \
        GLFW_WIN32_CURSOR_STATE \
        GLFW_COCOA_CURSOR_STATE \
        GLFW_WAYLAND_CURSOR_STATE \
        GLFW_X11_CURSOR_STATE \
        GLFW_NULL_CURSOR_STATE \


#define GLFW_PLATFORM_LIBRARY_WINDOW_STATE \
        GLFW_WIN32_LIBRARY_WINDOW_STATE \
        GLFW_COCOA_LIBRARY_WINDOW_STATE \
        GLFW_WAYLAND_LIBRARY_WINDOW_STATE \
        GLFW_X11_LIBRARY_WINDOW_STATE \
        GLFW_NULL_LIBRARY_WINDOW_STATE \


#define GLFW_PLATFORM_CONTEXT_STATE
#define GLFW_PLATFORM_LIBRARY_CONTEXT_STATE

// 线程/TLS/互斥与时钟不再由 wsi 自带的平台文件提供，统一由 sc 的 builtins/platform.h
// 跨平台层承担（编译期 TLS 宏 + 单调时钟）；相应的 posix/macos/win32 thread/time 源已删除。

#if defined(_WIN32)
 #define GLFW_BUILD_WIN32_MODULE
#else
 #define GLFW_BUILD_POSIX_MODULE
#endif

#if defined(WSI_WAYLAND) || defined(WSI_X11)
 #include "posix_poll.h"
 #define GLFW_BUILD_POSIX_POLL
#endif

