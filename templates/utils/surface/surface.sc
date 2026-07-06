# surface —— 原生窗口句柄到渲染 surface 的独立封装层
#
# 目的：给渲染后端一个统一的“native target 包装对象”，解耦 wsi/ui 与渲染实现。
#
# 参数语义：
# - nativeDisplay：平台 display/connection 级对象（如 X11 Display*、wl_display*）。
# - nativeWindow：平台 window/surface 级对象（如 X11 Window、wl_surface*、HWND、NSView*）。
#
# 说明：这两个字段不是 OpenGL 专属命名；是否映射到 EGLDisplay/EGLSurface、
#       GLX Display/Drawable、Vulkan surface 创建参数，由具体渲染后端决定。

inc surface.h
add libsurface.a

@fnc surface_create_from_native:: ::sc_surface&, platform: i4, nativeDisplay: &, nativeWindow: &
@fnc surface_destroy:: surface: ::sc_surface&
@fnc surface_get_platform:: i4, surface: ::sc_surface&
@fnc surface_get_native_display:: &, surface: ::sc_surface&
@fnc surface_get_native_window:: &, surface: ::sc_surface&
