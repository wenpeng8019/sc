#ifndef SC_SURFACE_H
#define SC_SURFACE_H

#include "../../../builtins/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SURFACE_SHARED
 #define SURFACE_SHARED 0
#endif
#ifndef SURFACE_EXPORTS
 #define SURFACE_EXPORTS 0
#endif

#define SURFACE_API SC_API(SURFACE)

enum
{
    /* 自动/未指定平台。通常由上层按当前运行平台决定具体后端。 */
    SC_SURFACE_PLATFORM_ANY = 0,
    /* Windows: Win32 句柄族。 */
    SC_SURFACE_PLATFORM_WIN32 = 1,
    /* macOS: Cocoa 句柄族。 */
    SC_SURFACE_PLATFORM_COCOA = 2,
    /* Linux: Wayland 句柄族。 */
    SC_SURFACE_PLATFORM_WAYLAND = 3,
    /* Linux/Unix: X11 句柄族。 */
    SC_SURFACE_PLATFORM_X11 = 4,
    /* 无窗口/离屏等占位后端。 */
    SC_SURFACE_PLATFORM_NULL = 5
};

typedef struct sc_surface sc_surface;

/* 从原生句柄创建 surface 包装对象。
 * 仅做句柄归档与平台标识，不创建/销毁 native 对象本身。
 *
 * nativeDisplay/nativeWindow 的语义按平台约定：
 * - X11:     nativeDisplay = Display*，nativeWindow = Window
 * - Wayland: nativeDisplay = wl_display*，nativeWindow = wl_surface*
 * - Cocoa:   nativeDisplay 常为 NULL，nativeWindow = NSView* or CALayer*（由上层约定）
 * - Win32:   nativeDisplay 常为 NULL，nativeWindow = HWND（或由上层约定的等价句柄）
 *
 * 注意：它们不是固定绑定 OpenGL 的 glDisplay/glSurface 概念。
 * 对 EGL/GLX/Vulkan/Metal 等后端，上层可把这两个字段映射为各自需要的原生对象。 */
SURFACE_API sc_surface* sc_surface_create_from_native(int platform,
                                                      void* nativeDisplay,
                                                      void* nativeWindow);
/* 销毁 surface 包装对象；不销毁 nativeDisplay/nativeWindow 指向的外部资源。 */
SURFACE_API void sc_surface_destroy(sc_surface* surface);

/* 查询 surface 的平台标签（SC_SURFACE_PLATFORM_*）。 */
SURFACE_API int sc_surface_get_platform(sc_surface* surface);
/* 取回创建时传入的 native display 句柄。 */
SURFACE_API void* sc_surface_get_native_display(sc_surface* surface);
/* 取回创建时传入的 native window/surface 句柄。 */
SURFACE_API void* sc_surface_get_native_window(sc_surface* surface);

#ifdef __cplusplus
}
#endif

#endif
