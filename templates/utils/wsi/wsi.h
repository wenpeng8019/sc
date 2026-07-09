
#ifndef wsi_h_
#define wsi_h_

#include "../../../builtins/platform.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*************************************************************************
 * 编译器与平台相关的预处理
 *************************************************************************/

/* 如果我们在 Windows 上，我们希望使用一个统一的宏定义。
 */
#if !defined(_WIN32) && (defined(__WIN32__) || defined(WIN32) || defined(__MINGW32__))
 #define _WIN32
#endif /* _WIN32 */

#ifndef WSI_SHARED
 #define WSI_SHARED 0
#endif
#ifndef WSI_EXPORTS
 #define WSI_EXPORTS 0
#endif

/* WSI_API 用于声明需要从 DLL / 共享库 / 动态库中导出的公共 API 函数。 */
#define WSI_API SC_API(WSI)

/*************************************************************************
 * GLFW API 标记
 *************************************************************************/

/*! @name GLFW 版本宏
 *  @{ */
/*! @brief GLFW 头文件的主版本号。
 *
 *  GLFW 头文件的主版本号。  This is incremented when the
 *  API is changed in non-compatible ways.
 *  @ingroup init
 */
#define WSI_VERSION_MAJOR          3
/*! @brief GLFW 头文件的次版本号。
 *
 *  GLFW 头文件的次版本号。  This is incremented when
 *  features are added to the API but it remains backward-compatible.
 *  @ingroup init
 */
#define WSI_VERSION_MINOR          5
/*! @brief GLFW 头文件的修订号。
 *
 *  GLFW 头文件的修订号。  This is incremented when a bug fix
 *  release is made that does not contain any API changes.
 *  @ingroup init
 */
#define WSI_VERSION_REVISION       0
/*! @} */

/*! @name 按键与按钮动作
 *  @{ */
/*! @brief The key or mouse button was released.
 *
 *  The key or mouse button was released.
 *
 *  @ingroup input
 */
#define SC_RELEASE                0
/*! @brief The key or mouse button was pressed.
 *
 *  The key or mouse button was pressed.
 *
 *  @ingroup input
 */
#define SC_PRESS                  1
/*! @brief The key was held down until it repeated.
 *
 *  The key was held down until it repeated.
 *
 *  @ingroup input
 */
#define SC_REPEAT                 2
/*! @} */


/*! @ingroup input
 */
#define SC_KEY_UNKNOWN            -1

/*! @} */

/*! @defgroup keys Keyboard key tokens
 *  @brief 键盘按键标记。
 *
 *  See [key input](@ref input_key) for how these are used.
 *
 *  These key codes are inspired by the _USB HID Usage Tables v1.12_ (p. 53-60),
 *  but re-arranged to map to 7-bit ASCII for printable keys (function keys are
 *  put in the 256+ range).
 *
 *  The naming of the key codes follow these rules:
 *   - The US keyboard layout is used
 *   - Names of printable alphanumeric characters are used (e.g. "A", "R",
 *     "3", etc.)
 *   - For non-alphanumeric characters, Unicode:ish names are used (e.g.
 *     "COMMA", "LEFT_SQUARE_BRACKET", etc.). Note that some names do not
 *     correspond to the Unicode standard (usually for brevity)
 *   - Keys that lack a clear US mapping are named "WORLD_x"
 *   - For non-printable keys, custom names are used (e.g. "F4",
 *     "BACKSPACE", etc.)
 *
 *  @ingroup input
 *  @{
 */

/* Printable keys */
#define SC_KEY_SPACE              32
#define SC_KEY_APOSTROPHE         39  /* ' */
#define SC_KEY_COMMA              44  /* , */
#define SC_KEY_MINUS              45  /* - */
#define SC_KEY_PERIOD             46  /* . */
#define SC_KEY_SLASH              47  /* / */
#define SC_KEY_0                  48
#define SC_KEY_1                  49
#define SC_KEY_2                  50
#define SC_KEY_3                  51
#define SC_KEY_4                  52
#define SC_KEY_5                  53
#define SC_KEY_6                  54
#define SC_KEY_7                  55
#define SC_KEY_8                  56
#define SC_KEY_9                  57
#define SC_KEY_SEMICOLON          59  /* ; */
#define SC_KEY_EQUAL              61  /* = */
#define SC_KEY_A                  65
#define SC_KEY_B                  66
#define SC_KEY_C                  67
#define SC_KEY_D                  68
#define SC_KEY_E                  69
#define SC_KEY_F                  70
#define SC_KEY_G                  71
#define SC_KEY_H                  72
#define SC_KEY_I                  73
#define SC_KEY_J                  74
#define SC_KEY_K                  75
#define SC_KEY_L                  76
#define SC_KEY_M                  77
#define SC_KEY_N                  78
#define SC_KEY_O                  79
#define SC_KEY_P                  80
#define SC_KEY_Q                  81
#define SC_KEY_R                  82
#define SC_KEY_S                  83
#define SC_KEY_T                  84
#define SC_KEY_U                  85
#define SC_KEY_V                  86
#define SC_KEY_W                  87
#define SC_KEY_X                  88
#define SC_KEY_Y                  89
#define SC_KEY_Z                  90
#define SC_KEY_LEFT_BRACKET       91  /* [ */
#define SC_KEY_BACKSLASH          92  /* \ */
#define SC_KEY_RIGHT_BRACKET      93  /* ] */
#define SC_KEY_GRAVE_ACCENT       96  /* ` */
#define SC_KEY_WORLD_1            161 /* non-US #1 */
#define SC_KEY_WORLD_2            162 /* non-US #2 */

/* Function keys */
#define SC_KEY_ESCAPE             256
#define SC_KEY_ENTER              257
#define SC_KEY_TAB                258
#define SC_KEY_BACKSPACE          259
#define SC_KEY_INSERT             260
#define SC_KEY_DELETE             261
#define SC_KEY_RIGHT              262
#define SC_KEY_LEFT               263
#define SC_KEY_DOWN               264
#define SC_KEY_UP                 265
#define SC_KEY_PAGE_UP            266
#define SC_KEY_PAGE_DOWN          267
#define SC_KEY_HOME               268
#define SC_KEY_END                269
#define SC_KEY_CAPS_LOCK          280
#define SC_KEY_SCROLL_LOCK        281
#define SC_KEY_NUM_LOCK           282
#define SC_KEY_PRINT_SCREEN       283
#define SC_KEY_PAUSE              284
#define SC_KEY_F1                 290
#define SC_KEY_F2                 291
#define SC_KEY_F3                 292
#define SC_KEY_F4                 293
#define SC_KEY_F5                 294
#define SC_KEY_F6                 295
#define SC_KEY_F7                 296
#define SC_KEY_F8                 297
#define SC_KEY_F9                 298
#define SC_KEY_F10                299
#define SC_KEY_F11                300
#define SC_KEY_F12                301
#define SC_KEY_F13                302
#define SC_KEY_F14                303
#define SC_KEY_F15                304
#define SC_KEY_F16                305
#define SC_KEY_F17                306
#define SC_KEY_F18                307
#define SC_KEY_F19                308
#define SC_KEY_F20                309
#define SC_KEY_F21                310
#define SC_KEY_F22                311
#define SC_KEY_F23                312
#define SC_KEY_F24                313
#define SC_KEY_F25                314
#define SC_KEY_KP_0               320
#define SC_KEY_KP_1               321
#define SC_KEY_KP_2               322
#define SC_KEY_KP_3               323
#define SC_KEY_KP_4               324
#define SC_KEY_KP_5               325
#define SC_KEY_KP_6               326
#define SC_KEY_KP_7               327
#define SC_KEY_KP_8               328
#define SC_KEY_KP_9               329
#define SC_KEY_KP_DECIMAL         330
#define SC_KEY_KP_DIVIDE          331
#define SC_KEY_KP_MULTIPLY        332
#define SC_KEY_KP_SUBTRACT        333
#define SC_KEY_KP_ADD             334
#define SC_KEY_KP_ENTER           335
#define SC_KEY_KP_EQUAL           336
#define SC_KEY_LEFT_SHIFT         340
#define SC_KEY_LEFT_CONTROL       341
#define SC_KEY_LEFT_ALT           342
#define SC_KEY_LEFT_SUPER         343
#define SC_KEY_RIGHT_SHIFT        344
#define SC_KEY_RIGHT_CONTROL      345
#define SC_KEY_RIGHT_ALT          346
#define SC_KEY_RIGHT_SUPER        347
#define SC_KEY_MENU               348

#define SC_KEY_LAST               SC_KEY_MENU

/*! @} */

/*! @defgroup mods 修饰键标志
 *  @brief 修饰键标志.
 *
 *  See [key input](@ref input_key) for how these are used.
 *
 *  @ingroup input
 *  @{ */

/*! @brief If this bit is set one or more Shift keys were held down.
 *
 *  If this bit is set one or more Shift keys were held down.
 */
#define SC_MOD_SHIFT           0x0001
/*! @brief If this bit is set one or more Control keys were held down.
 *
 *  If this bit is set one or more Control keys were held down.
 */
#define SC_MOD_CONTROL         0x0002
/*! @brief If this bit is set one or more Alt keys were held down.
 *
 *  If this bit is set one or more Alt keys were held down.
 */
#define SC_MOD_ALT             0x0004
/*! @brief If this bit is set one or more Super keys were held down.
 *
 *  If this bit is set one or more Super keys were held down.
 */
#define SC_MOD_SUPER           0x0008
/*! @brief If this bit is set the Caps Lock key is enabled.
 *
 *  If this bit is set the Caps Lock key is enabled and the @ref
 *  SC_LOCK_KEY_MODS input mode is set.
 */
#define SC_MOD_CAPS_LOCK       0x0010
/*! @brief If this bit is set the Num Lock key is enabled.
 *
 *  If this bit is set the Num Lock key is enabled and the @ref
 *  SC_LOCK_KEY_MODS input mode is set.
 */
#define SC_MOD_NUM_LOCK        0x0020

/*! @} */

/*! @defgroup buttons 鼠标按钮
 *  @brief Mouse button IDs.
 *
 *  See [mouse button input](@ref input_mouse_button) for how these are used.
 *
 *  @ingroup input
 *  @{ */
#define SC_MOUSE_BUTTON_1         0
#define SC_MOUSE_BUTTON_2         1
#define SC_MOUSE_BUTTON_3         2
#define SC_MOUSE_BUTTON_4         3
#define SC_MOUSE_BUTTON_5         4
#define SC_MOUSE_BUTTON_6         5
#define SC_MOUSE_BUTTON_7         6
#define SC_MOUSE_BUTTON_8         7
#define SC_MOUSE_BUTTON_LAST      SC_MOUSE_BUTTON_8
#define SC_MOUSE_BUTTON_LEFT      SC_MOUSE_BUTTON_1
#define SC_MOUSE_BUTTON_RIGHT     SC_MOUSE_BUTTON_2
#define SC_MOUSE_BUTTON_MIDDLE    SC_MOUSE_BUTTON_3
/*! @} */

//-------------------------------------------------------------------------

/*! @defgroup errors 错误码
 *  @brief 错误码.
 *
 *  See [error handling](@ref error_handling) for how these are used.
 *
 *  @ingroup init
 *  @{ */
/*! @brief 没有发生错误。
 *
 *  没有发生错误。
 *
 *  @analysis Yay.
 */
#define SC_WSI_ERR_NONE               0
/*! @brief GLFW has not been initialized.
 *
 *  This occurs if a GLFW function was called that must not be called unless the
 *  library is [initialized](@ref intro_init).
 *
 *  @analysis Application programmer error.  Initialize GLFW before calling any
 *  function that requires initialization.
 */
#define SC_WSI_ERR_NOT_INITIALIZED        0x00010001
/*! @brief No context is current for this thread.
 *
 *  This occurs if a GLFW function was called that needs and operates on the
 *  current OpenGL or OpenGL ES context but no context is current on the calling
 *  thread.  One such function is @ref glfwSwapInterval.
 *
 *  @analysis Application programmer error.  Ensure a context is current before
 *  calling functions that require a current context.
 */
#define SC_WSI_ERR_NO_CURRENT_CONTEXT     0x00010002
/*! @brief One of the arguments to the function was an invalid enum value.
 *
 *  One of the arguments to the function was an invalid enum value, for example
 *  requesting @ref GLFW_RED_BITS with @ref sc_wsi_win_get_attrib.
 *
 *  @analysis Application programmer error.  Fix the offending call.
 */
#define SC_WSI_ERR_INVALID_ENUM           0x00010003
/*! @brief One of the arguments to the function was an invalid value.
 *
 *  One of the arguments to the function was an invalid value, for example
 *  requesting a non-existent OpenGL or OpenGL ES version like 2.7.
 *
 *  Requesting a valid but unavailable OpenGL or OpenGL ES version will instead
 *  result in a @ref SC_WSI_ERR_VERSION_UNAVAILABLE error.
 *
 *  @analysis Application programmer error.  Fix the offending call.
 */
#define SC_WSI_ERR_INVALID_VALUE          0x00010004
/*! @brief A memory allocation failed.
 *
 *  A memory allocation failed.
 *
 *  @analysis A bug in GLFW or the underlying operating system.  Report the bug
 *  to our [issue tracker](https://github.com/glfw/glfw/issues).
 */
#define SC_WSI_ERR_OUT_OF_MEMORY          0x00010005
/*! @brief GLFW could not find support for the requested API on the system.
 *
 *  GLFW could not find support for the requested API on the system.
 *
 *  @analysis The installed graphics driver does not support the requested
 *  API, or does not support it via the chosen context creation API.
 *  Below are a few examples.
 *
 *  @par
 *  Some pre-installed Windows graphics drivers do not support OpenGL.  AMD only
 *  supports OpenGL ES via EGL, while Nvidia and Intel only support it via
 *  a WGL or GLX extension.  macOS does not provide OpenGL ES at all.  The Mesa
 *  EGL, OpenGL and OpenGL ES libraries do not interface with the Nvidia binary
 *  driver.  Older graphics drivers do not support Vulkan.
 */
#define SC_WSI_ERR_API_UNAVAILABLE        0x00010006
/*! @brief The requested OpenGL or OpenGL ES version is not available.
 *
 *  The requested OpenGL or OpenGL ES version (including any requested context
 *  or framebuffer hints) is not available on this machine.
 *
 *  @analysis The machine does not support your requirements.  If your
 *  application is sufficiently flexible, downgrade your requirements and try
 *  again.  Otherwise, inform the user that their machine does not match your
 *  requirements.
 *
 *  @par
 *  Future invalid OpenGL and OpenGL ES versions, for example OpenGL 4.8 if 5.0
 *  comes out before the 4.x series gets that far, also fail with this error and
 *  not @ref SC_WSI_ERR_INVALID_VALUE, because GLFW cannot know what future versions
 *  will exist.
 */
#define SC_WSI_ERR_VERSION_UNAVAILABLE    0x00010007
/*! @brief A platform-specific error occurred that does not match any of the
 *  more specific categories.
 *
 *  A platform-specific error occurred that does not match any of the more
 *  specific categories.
 *
 *  @analysis A bug or configuration error in GLFW, the underlying operating
 *  system or its drivers, or a lack of required resources.  Report the issue to
 *  our [issue tracker](https://github.com/glfw/glfw/issues).
 */
#define SC_WSI_ERR_PLATFORM_ERROR         0x00010008
/*! @brief The requested format is not supported or available.
 *
 *  If emitted during window creation, the requested pixel format is not
 *  supported.
 *
 *  If emitted when querying the clipboard, the contents of the clipboard could
 *  not be converted to the requested format.
 *
 *  @analysis If emitted during window creation, one or more
 *  [hard constraints](@ref window_hints_hard) did not match any of the
 *  available pixel formats.  If your application is sufficiently flexible,
 *  downgrade your requirements and try again.  Otherwise, inform the user that
 *  their machine does not match your requirements.
 *
 *  @par
 *  If emitted when querying the clipboard, ignore the error or report it to
 *  the user, as appropriate.
 */
#define SC_WSI_ERR_FORMAT_UNAVAILABLE     0x00010009
/*! @brief The specified window does not have an OpenGL or OpenGL ES context.
 *
 *  A window that does not have an OpenGL or OpenGL ES context was passed to
 *  a function that requires it to have one.
 *
 *  @analysis Application programmer error.  Fix the offending call.
 */
#define SC_WSI_ERR_NO_WINDOW_CONTEXT      0x0001000A
/*! @brief The specified cursor shape is not available.
 *
 *  The specified standard cursor shape is not available, either because the
 *  current platform cursor theme does not provide it or because it is not
 *  available on the platform.
 *
 *  @analysis Platform or system settings limitation.  Pick another
 *  [standard cursor shape](@ref shapes) or create a
 *  [custom cursor](@ref cursor_custom).
 */
#define SC_WSI_ERR_CURSOR_UNAVAILABLE     0x0001000B
/*! @brief The requested feature is not provided by the platform.
 *
 *  The requested feature is not provided by the platform, so GLFW is unable to
 *  implement it.  The documentation for each function notes if it could emit
 *  this error.
 *
 *  @analysis Platform or platform version limitation.  The error can be ignored
 *  unless the feature is critical to the application.
 *
 *  @par
 *  A function call that emits this error has no effect other than the error and
 *  updating any existing out parameters.
 */
#define SC_WSI_ERR_FEATURE_UNAVAILABLE    0x0001000C
/*! @brief The requested feature is not implemented for the platform.
 *
 *  The requested feature has not yet been implemented in GLFW for this platform.
 *
 *  @analysis An incomplete implementation of GLFW for this platform, hopefully
 *  fixed in a future release.  The error can be ignored unless the feature is
 *  critical to the application.
 *
 *  @par
 *  A function call that emits this error has no effect other than the error and
 *  updating any existing out parameters.
 */
#define SC_WSI_ERR_FEATURE_UNIMPLEMENTED  0x0001000D
/*! @brief Platform unavailable or no matching platform was found.
 *
 *  If emitted during initialization, no matching platform was found.  If the @ref
 *  SC_PLATFORM init hint was set to `SC_PLATFORM_ANY`, GLFW could not detect any of
 *  the platforms supported by this library binary, except for the Null platform.  If the
 *  init hint was set to a specific platform, it is either not supported by this library
 *  binary or GLFW was not able to detect it.
 *
 *  If emitted by a native access function, GLFW was initialized for a different platform
 *  than the function is for.
 *
 *  @analysis Failure to detect any platform usually only happens on non-macOS Unix
 *  systems, either when no window system is running or the program was run from
 *  a terminal that does not have the necessary environment variables.  Fall back to
 *  a different platform if possible or notify the user that no usable platform was
 *  detected.
 *
 *  Failure to detect a specific platform may have the same cause as above or be because
 *  support for that platform was not compiled in.  Call @ref sc_wsi_platform_supported to
 *  check whether a specific platform is supported by a library binary.
 */
#define SC_WSI_ERR_PLATFORM_UNAVAILABLE   0x0001000E
/*! @} */

//-------------------------------------------------------------------------

/*! @addtogroup window
 *  @{ */
/*! @brief Input focus window hint and attribute
 *
 *  Input focus [window hint](@ref GLFW_FOCUSED_hint) or
 *  [window attribute](@ref GLFW_FOCUSED_attrib).
 */
#define SC_WIN_FOCUSED                0x00020001
/*! @brief Window iconification window attribute
 *
 *  Window iconification [window attribute](@ref GLFW_ICONIFIED_attrib).
 */
#define SC_WIN_ICONIFIED              0x00020002
/*! @brief Window resize-ability window hint and attribute
 *
 *  Window resize-ability [window hint](@ref GLFW_RESIZABLE_hint) and
 *  [window attribute](@ref GLFW_RESIZABLE_attrib).
 */
#define SC_WIN_RESIZABLE              0x00020003
/*! @brief Window visibility window hint and attribute
 *
 *  Window visibility [window hint](@ref GLFW_VISIBLE_hint) and
 *  [window attribute](@ref GLFW_VISIBLE_attrib).
 */
#define SC_WIN_VISIBLE                0x00020004
/*! @brief Window decoration window hint and attribute
 *
 *  Window decoration [window hint](@ref GLFW_DECORATED_hint) and
 *  [window attribute](@ref GLFW_DECORATED_attrib).
 */
#define SC_WIN_DECORATED              0x00020005
/*! @brief Window auto-iconification window hint and attribute
 *
 *  Window auto-iconification [window hint](@ref GLFW_AUTO_ICONIFY_hint) and
 *  [window attribute](@ref GLFW_AUTO_ICONIFY_attrib).
 */
#define SC_WIN_AUTO_ICONIFY           0x00020006
/*! @brief Window decoration window hint and attribute
 *
 *  Window decoration [window hint](@ref GLFW_FLOATING_hint) and
 *  [window attribute](@ref GLFW_FLOATING_attrib).
 */
#define SC_WIN_FLOATING               0x00020007
/*! @brief Window maximization window hint and attribute
 *
 *  Window maximization [window hint](@ref GLFW_MAXIMIZED_hint) and
 *  [window attribute](@ref GLFW_MAXIMIZED_attrib).
 */
#define SC_WIN_MAXIMIZED              0x00020008
/*! @brief Cursor centering window hint
 *
 *  Cursor centering [window hint](@ref GLFW_CENTER_CURSOR_hint).
 */
#define SC_WIN_CENTER_CURSOR          0x00020009
/*! @brief Mouse cursor hover window attribute.
 *
 *  Mouse cursor hover [window attribute](@ref GLFW_HOVERED_attrib).
 */
#define SC_WIN_HOVERED                0x0002000B
/*! @brief Input focus on calling show window hint and attribute
 *
 *  Input focus [window hint](@ref GLFW_FOCUS_ON_SHOW_hint) or
 *  [window attribute](@ref GLFW_FOCUS_ON_SHOW_attrib).
 */
#define SC_WIN_FOCUS_ON_SHOW          0x0002000C

/*! @brief Mouse input transparency window hint and attribute
 *
 *  Mouse input transparency [window hint](@ref SC_MOUSE_PASSTHROUGH_hint) or
 *  [window attribute](@ref SC_MOUSE_PASSTHROUGH_attrib).
 */
#define SC_WIN_MOUSE_PASSTHROUGH      0x0002000D

/*! @brief Initial position x-coordinate window hint.
 *
 *  Initial position x-coordinate [window hint](@ref SC_WIN_POSITION_X).
 */
#define SC_WIN_POSITION_X             0x0002000E

/*! @brief Initial position y-coordinate window hint.
 *
 *  Initial position y-coordinate [window hint](@ref SC_WIN_POSITION_Y).
 */
#define SC_WIN_POSITION_Y             0x0002000F

//-------------------------------------------------------------------------

/*! @brief Window content area scaling window
 *  [window hint](@ref SC_SCALE_TO_MONITOR).
 */
#define SC_SCALE_TO_MONITOR       0x0002200C
/*! @brief macOS specific
 *  [window hint](@ref GLFW_COCOA_FRAME_NAME_hint).
 */
#define SC_COCOA_FRAME_NAME         0x00023002
/*! @brief X11 specific
 *  [window hint](@ref GLFW_X11_CLASS_NAME_hint).
 */
#define SC_X11_CLASS_NAME         0x00024001
/*! @brief X11 specific
 *  [window hint](@ref GLFW_X11_CLASS_NAME_hint).
 */
#define SC_X11_INSTANCE_NAME      0x00024002
#define GLFW_WIN32_KEYBOARD_MENU    0x00025001
/*! @brief Win32 specific [window hint](@ref GLFW_WIN32_SHOWDEFAULT_hint).
 */
#define SC_WIN32_SHOWDEFAULT      0x00025002
/*! @brief Wayland specific
 *  [window hint](@ref GLFW_WAYLAND_APP_ID_hint).
 *  
 *  Allows specification of the Wayland app_id.
 */
#define SC_WAYLAND_APP_ID         0x00026001
/*! @} */


//-------------------------------------------------------------------------

#define SC_CURSOR                  0x00033001
#define SC_STICKY_KEYS             0x00033002
#define SC_STICKY_MOUSE_BUTTONS    0x00033003
#define SC_LOCK_KEY_MODS           0x00033004
#define SC_RAW_MOUSE_MOTION        0x00033005
#define SC_UNLIMITED_MOUSE_BUTTONS 0x00033006

#define SC_CURSOR_NORMAL          0x00034001
#define SC_CURSOR_HIDDEN          0x00034002
#define SC_CURSOR_DISABLED        0x00034003
#define SC_CURSOR_CAPTURED        0x00034004

#define SC_WAYLAND_PREFER_LIBDECOR    0x00038001
#define SC_WAYLAND_DISABLE_LIBDECOR   0x00038002

#define SC_ANY_POSITION           0x80000000

//-------------------------------------------------------------------------

/*! @defgroup shapes 标准光标形状
 *  @brief 标准系统光标形状。
 *
 *  These are the [standard cursor shapes](@ref cursor_standard) that can be
 *  requested from the platform (window system).
 *
 *  @ingroup input
 *  @{ */

/*! @brief The regular arrow cursor shape.
 *
 *  The regular arrow cursor shape.
 */
#define SC_ARROW_CURSOR           0x00036001
/*! @brief The text input I-beam cursor shape.
 *
 *  The text input I-beam cursor shape.
 */
#define SC_IBEAM_CURSOR           0x00036002
/*! @brief The crosshair cursor shape.
 *
 *  The crosshair cursor shape.
 */
#define SC_CROSSHAIR_CURSOR       0x00036003
/*! @brief The pointing hand cursor shape.
 *
 *  The pointing hand cursor shape.
 */
#define SC_POINTING_HAND_CURSOR   0x00036004
/*! @brief The horizontal resize/move arrow shape.
 *
 *  The horizontal resize/move arrow shape.  This is usually a horizontal
 *  double-headed arrow.
 */
#define SC_RESIZE_EW_CURSOR       0x00036005
/*! @brief The vertical resize/move arrow shape.
 *
 *  The vertical resize/move shape.  This is usually a vertical double-headed
 *  arrow.
 */
#define SC_RESIZE_NS_CURSOR       0x00036006
/*! @brief The top-left to bottom-right diagonal resize/move arrow shape.
 *
 *  The top-left to bottom-right diagonal resize/move shape.  This is usually
 *  a diagonal double-headed arrow.
 *
 *  @note __macOS:__ This shape is provided by a private system API and may fail
 *  with @ref SC_WSI_ERR_CURSOR_UNAVAILABLE in the future.
 *
 *  @note __Wayland:__ This shape is provided by a newer standard not supported by
 *  all cursor themes.
 *
 *  @note __X11:__ This shape is provided by a newer standard not supported by all
 *  cursor themes.
 */
#define SC_RESIZE_NWSE_CURSOR     0x00036007
/*! @brief The top-right to bottom-left diagonal resize/move arrow shape.
 *
 *  The top-right to bottom-left diagonal resize/move shape.  This is usually
 *  a diagonal double-headed arrow.
 *
 *  @note __macOS:__ This shape is provided by a private system API and may fail
 *  with @ref SC_WSI_ERR_CURSOR_UNAVAILABLE in the future.
 *
 *  @note __Wayland:__ This shape is provided by a newer standard not supported by
 *  all cursor themes.
 *
 *  @note __X11:__ This shape is provided by a newer standard not supported by all
 *  cursor themes.
 */
#define SC_RESIZE_NESW_CURSOR     0x00036008
/*! @brief The omni-directional resize/move cursor shape.
 *
 *  The omni-directional resize cursor/move shape.  This is usually either
 *  a combined horizontal and vertical double-headed arrow or a grabbing hand.
 */
#define SC_RESIZE_ALL_CURSOR      0x00036009
/*! @brief The operation-not-allowed shape.
 *
 *  The operation-not-allowed shape.  This is usually a circle with a diagonal
 *  line through it.
 *
 *  @note __Wayland:__ This shape is provided by a newer standard not supported by
 *  all cursor themes.
 *
 *  @note __X11:__ This shape is provided by a newer standard not supported by all
 *  cursor themes.
 */
#define SC_NOT_ALLOWED_CURSOR     0x0003600A
/*! @} */

//-------------------------------------------------------------------------

#define SC_CONNECTED              0x00040001
#define SC_DISCONNECTED           0x00040002

//-------------------------------------------------------------------------

/*! @addtogroup init
 *  @{ */
/*! @brief ANGLE rendering backend init hint.
 *
 *  ANGLE rendering backend [init hint](@ref GLFW_ANGLE_PLATFORM_TYPE_hint).
 */
#define SC_ANGLE_PLATFORM_TYPE    0x00050002
/*! @brief Platform selection init hint.
 *
 *  Platform selection [init hint](@ref SC_PLATFORM).
 */
#define SC_PLATFORM               0x00050003
/*! @brief macOS specific init hint.
 *
 *  macOS specific [init hint](@ref GLFW_COCOA_CHDIR_RESOURCES_hint).
 */
#define SC_COCOA_CHDIR_RESOURCES  0x00051001
/*! @brief macOS specific init hint.
 *
 *  macOS specific [init hint](@ref GLFW_COCOA_MENUBAR_hint).
 */
#define SC_COCOA_MENUBAR          0x00051002
/*! @brief Wayland specific init hint.
 *
 *  Wayland specific [init hint](@ref GLFW_WAYLAND_LIBDECOR_hint).
 */
#define SC_WAYLAND_LIBDECOR       0x00053001
/*! @} */

/*! @addtogroup init
 *  @{ */
/*! @brief Hint value that enables automatic platform selection.
 *
 *  Hint value for @ref SC_PLATFORM that enables automatic platform selection.
 */
#define SC_PLATFORM_ANY           0x00060000
#define SC_PLATFORM_WIN32         0x00060001
#define SC_PLATFORM_COCOA         0x00060002
#define SC_PLATFORM_WAYLAND       0x00060003
#define SC_PLATFORM_X11           0x00060004
#define SC_PLATFORM_NULL          0x00060005
/*! @} */

/* Reserved platform define for external Emscripten ports: 0x00060006
 * See https://github.com/pongasoft/emscripten-glfw
 */

#define SC_DONT_CARE              -1


/*************************************************************************
 * GLFW API types
 *************************************************************************/

/*! @brief 错误回调函数指针类型。
 *
 *  这是错误回调的函数指针类型。错误回调函数具有以下签名：
 *  @code
 *  void callback_name(int error_code, const char* description)
 *  @endcode
 *
 *  @param[in] error_code [错误码](@ref errors)。未来版本可能会添加更多错误码。
 *  @param[in] description 描述错误的 UTF-8 编码字符串。
 *
 *  @pointer_lifetime 错误描述字符串在回调函数返回之前保持有效。
 *
 *  @ingroup init
 */
typedef void (* sc_error_cb)(int error_code, const char* description);

/*************************************************************************
 * GLFW API types
 *************************************************************************/

/*! @brief 不透明监视器对象。
 *
 *  不透明监视器对象。
 *
 *  @ingroup monitor
 */
typedef struct sc_monitor sc_monitor;

/*! @brief 监视器配置回调函数指针类型。
 *
 *  This is the function pointer type for monitor configuration callbacks.
 *  A monitor callback function has the following signature:
 *  @code
 *  void function_name(sc_monitor* monitor, int event)
 *  @endcode
 *
 *  @param[in] monitor 被连接或断开的监视器。
 *  @param[in] event `SC_CONNECTED` 或 `SC_DISCONNECTED` 之一。  Future
 *  releases may add more events.
 *
 *  @ingroup monitor
 */
typedef void (* sc_monitor_cb)(sc_monitor* monitor, int event);


/*! @brief 不透明窗口对象。
 *
 *  不透明窗口对象。
 *
 *  @ingroup window
 */
typedef struct sc_window sc_window;

/*! @brief 不透明光标对象。
 *
 *  不透明光标对象。
 *
 *  @ingroup input
 */
typedef struct sc_cursor sc_cursor;

/*************************************************************************
 * GLFW API types
 *************************************************************************/

/*! @brief 窗口关闭回调函数指针类型。
 *
 *  This is the function pointer type for window close callbacks.
 *  当用户尝试关闭窗口时被调用，例如点击标题栏的关闭按钮。
 *
 *  The close flag is set before this callback is called, but you can modify it
 *  at any time with @ref sc_wsi_win_set_should_close.
 *
 *  The close callback is not triggered by @ref sc_wsi_win_destroy.
 *
 *  @param[in] window 用户尝试关闭的窗口。
 *
 *  @remark __macOS:__ Selecting Quit from the application menu will trigger the
 *  close callback for all windows.
 *
 *  @ingroup window
 */
typedef void (* sc_win_close_cb)(sc_window* window);

/*! @brief 窗口位置回调函数指针类型。
 *
 *  这是窗口位置回调的函数指针类型。which is
 *  called when the window is resized.  The callback is provided with the size,
 *  in screen coordinates, of the content area of the window.
 *
 *  @param[in] window 被移动的窗口。
 *  @param[in] xpos 窗口内容区域左上角新的 x 坐标（屏幕坐标）。
 *  @param[in] ypos 窗口内容区域左上角新的 y 坐标（屏幕坐标）。
 *
 *  @ingroup window
 */
typedef void (* sc_win_pos_cb)(sc_window* window, int xpos, int ypos);

/*! @brief 窗口大小回调函数指针类型。
 *
*  This is the function pointer type for window size callbacks.  which is
 *  called when the window is resized.  The callback is provided with the size,
 *  in screen coordinates, of the content area of the window.
 *
 *  @param[in] window 被调整大小的窗口。
 *  @param[in] width The new width, in screen coordinates, of the window.
 *  @param[in] height The new height, in screen coordinates, of the window.
 *
 *  @ingroup window
 */
typedef void (* sc_win_size_cb)(sc_window* window, int width, int height);

/*! @brief The function pointer type for window content refresh callbacks.
 *
 *  This is the function pointer type for window content refresh callbacks.
 *  当窗口内容区域需要重绘时被调用，例如窗口从其他窗口遮挡中暴露出来时。
 *
 *  On compositing window systems such as Aero, Compiz, Aqua or Wayland, where
 *  the window contents are saved off-screen, this callback may be called only
 *  very infrequently or never at all.
 *
 *  @param[in] window 内容需要刷新的窗口。
 *
 *  @ingroup window
 */
typedef void (* sc_win_refresh_cb)(sc_window* window);

/*! @brief 窗口焦点回调函数指针类型。
 *
 *  This is the function pointer type for window focus callbacks. 当窗口获得或失去输入焦点时被调用。
 *
 *  After the focus callback is called for a window that lost input focus,
 *  synthetic key and mouse button release events will be generated for all such
 *  that had been pressed.  For more information, see @ref sc_wsi_win_set_key_callback
 *  and @ref sc_wsi_win_set_mouse_button_callback.
 *
 *  @param[in] window 获得或失去输入焦点的窗口。
 *  @param[in] focused 如果窗口获得输入焦点则为 `true`，如果失去则为 `false`。
 *
 *  @ingroup window
 */
typedef void (* sc_win_focus_cb)(sc_window* window, int focused);

/*! @brief 窗口最小化回调函数指针类型。
 *
 *  This is the function pointer type for window iconify callbacks.  which
 *  is called when the window is iconified or restored.
 *
 *  @param[in] window 被最小化或恢复的窗口。
 *  @param[in] iconified 如果窗口被最小化则为 `true`，如果恢复则为 `false`。
 *
 *  @remark __Wayland:__ This callback will not be called.  The Wayland protocol
 *  provides no way to be notified of when a window is iconified, and no way to
 *  check whether a window is currently iconified.
 *
 *  @ingroup window
 */
typedef void (* sc_win_iconify_cb)(sc_window* window, int iconified);

/*! @brief 窗口最大化回调函数指针类型。
 *
*  This is the function pointer type for window maximize callbacks. which
 *  is called when the window is iconified or restored.
 *
 *  @param[in] window 被最大化或恢复的窗口。
 *  @param[in] maximized 如果窗口被最大化则为 `true`，如果恢复则为 `false`。
 *
 *  @remark __Wayland:__ This callback will not be called.  The Wayland protocol
 *  provides no way to be notified of when a window is iconified, and no way to
 *  check whether a window is currently iconified.
 *
 *  @ingroup window
 */
typedef void (* sc_win_maximize_cb)(sc_window* window, int maximized);

/*! @brief 窗口内容缩放回调函数指针类型。
 *
 *  This is the function pointer type for window content scale callbacks.
 *  当窗口内容缩放比例改变时被调用。
 *
 *  @param[in] window The window whose content scale changed.
 *  @param[in] xscale The new x-axis content scale of the window.
 *  @param[in] yscale The new y-axis content scale of the window.
 *
 *  @ingroup window
 */
typedef void (* sc_win_content_scale_cb)(sc_window* window, float xscale, float yscale);

/*! @brief 鼠标按钮回调函数指针类型。
 *
 *  This is the function pointer type for mouse button callback functions.
 *  当鼠标按钮被按下或释放时被调用。
 *
 *  When a window loses input focus, it will generate synthetic mouse button
 *  release events for all pressed mouse buttons with associated button tokens.
 *  You can tell these events from user-generated events by the fact that the
 *  synthetic ones are generated after the focus loss event has been processed,
 *  i.e. after the [window focus callback](@ref sc_wsi_win_set_focus_callback) has
 *  been called.
 *
 *  The reported `button` value can be higher than `SC_MOUSE_BUTTON_LAST` if
 *  the button does not have an associated [button token](@ref buttons) and the
 *  @ref SC_UNLIMITED_MOUSE_BUTTONS input mode is set.
 *
 *  @param[in] window 接收事件的窗口。
 *  @param[in] button 被按下或释放的 [鼠标按钮](@ref buttons)。
 *  @param[in] action One of `SC_PRESS` or `SC_RELEASE`.  Future releases
 *  may add more actions.
 *  @param[in] mods 描述按下哪些 [修饰键](@ref mods) 的位字段。
 *
 *  @ingroup input
 */
typedef void (* sc_win_mouse_button_cb)(sc_window* window, int button, int action, int mods);

/*! @brief 光标位置回调函数指针类型。
 *
 *  This is the function pointer type for cursor position callbacks. 当光标移动时被调用。
 *  The callback is provided with the
 *  position, in screen coordinates, relative to the upper-left corner of the
 *  content area of the window.
 *
 *  @param[in] window 接收事件的窗口。
 *  @param[in] xpos The new cursor x-coordinate, relative to the left edge of
 *  the content area.
 *  @param[in] ypos The new cursor y-coordinate, relative to the top edge of the
 *  content area.
 *
 *  @ingroup input
 */
typedef void (* sc_cursor_pos_cb)(sc_window* window, double xpos, double ypos);

/*! @brief 光标进入/离开回调函数指针类型。
 *
 *  This is the function pointer type for cursor enter/leave callbacks.
 *  which is called when the cursor enters or leaves the content area of
 *  the window.
 *
 *  @param[in] window 接收事件的窗口。
 *  @param[in] entered 如果光标进入窗口内容区域则为 `true`，如果离开则为 `false`。
 *
 *  @ingroup input
 */
typedef void (* sc_cursor_enter_cb)(sc_window* window, int entered);

/*! @brief 滚轮回调函数指针类型。
 *
 *  This is the function pointer type for scroll callbacks.
 *  当使用滚动设备（如鼠标滚轮或触控板滚动区域）时被调用。
 *
 *  The scroll callback receives all scrolling input, like that from a mouse
 *  wheel or a touchpad scrolling area.
 *
 *  @param[in] window 接收事件的窗口。
 *  @param[in] xoffset 沿 x 轴的滚动偏移。
 *  @param[in] yoffset 沿 y 轴的滚动偏移。
 *
 *  @ingroup input
 */
typedef void (* sc_scroll_cb)(sc_window* window, double xoffset, double yoffset);

/*! @brief 键盘按键回调函数指针类型。
 *
 *  This is the function pointer type for keyboard key callbacks.
 *  当按键被按下、重复或释放时被调用。
 *
 *  按键函数处理物理按键，使用与布局无关的 [按键标记](@ref keys)，以标准美式键盘布局命名。  如需输入文本，请使用 [字符回调](@ref sc_wsi_win_set_char_callback) 代替。
 *
 *  当窗口失去输入焦点时，会为所有按下的关联按键生成合成释放事件。  可以通过合成事件在焦点丢失事件处理之后生成这一事实，将这些事件与用户生成的事件区分开来， i.e. after the
 *  [window focus callback](@ref sc_wsi_win_set_focus_callback) has been called.
 *
 *  按键的扫描码是平台相关的，有时甚至与特定机器相关。  扫描码旨在允许用户绑定没有 GLFW 按键标记的按键。  此类按键的 `key` 被设为 `SC_KEY_UNKNOWN`，其状态不会被保存，因此无法通过 @ref sc_wsi_key 查询。
 *
 *  Sometimes GLFW needs to generate synthetic key events, in which case the
 *  scancode may be zero.
 *
 *  @param[in] window 接收事件的窗口。
 *  @param[in] key 被按下或释放的 [键盘按键](@ref keys)。
 *  @param[in] scancode The platform-specific scancode of the key.
 *  @param[in] action `SC_PRESS`、`SC_RELEASE` 或 `SC_REPEAT`。  Future
 *  releases may add more actions.
 *  @param[in] mods 描述按下哪些 [修饰键](@ref mods) 的位字段。
 *
 *  @ingroup input
 */
typedef void (* sc_key_cb)(sc_window* window, int key, int scancode, int action, int mods);

/*! @brief Unicode 字符回调函数指针类型。
 *
 *  This is the function pointer type for Unicode character callbacks.
 *  当输入 Unicode 字符时被调用。
 *
 *  字符回调用于 Unicode 文本输入。  As it deals with
 *  characters, it is keyboard layout dependent, whereas the
 *  [key callback](@ref sc_wsi_win_set_key_callback) is not.  Characters do not map 1:1
 *  to physical keys, as a key may produce zero, one or more characters.  If you
 *  want to know whether a specific physical key was pressed or released, see
 *  the key callback instead.
 *
 *  The character callback behaves as system text input normally does and will
 *  not be called if modifier keys are held down that would prevent normal text
 *  input on that platform, for example a Super (Command) key on macOS or Alt key
 *  on Windows.
 *
 *  @param[in] window 接收事件的窗口。
 *  @param[in] codepoint 字符的 Unicode 码点。
 *
 *  @ingroup input
 */
typedef void (* sc_char_cb)(sc_window* window, unsigned int codepoint);

/*! @brief The function pointer type for Unicode character with modifiers
 *  callbacks.
 *
 *  This is the function pointer type for Unicode character with modifiers
 *  callbacks.   which is called when a Unicode character is input regardless of what
 *  modifier keys are used.
 *
 *  The character with modifiers callback is intended for implementing custom
 *  Unicode character input.  For regular Unicode text input, see the
 *  [character callback](@ref sc_wsi_win_set_char_callback).  Like the character
 *  callback, the character with modifiers callback deals with characters and is
 *  keyboard layout dependent.  Characters do not map 1:1 to physical keys, as
 *  a key may produce zero, one or more characters.  If you want to know whether
 *  a specific physical key was pressed or released, see the
 *  [key callback](@ref sc_wsi_win_set_key_callback) instead.
 *
 *  @param[in] window 接收事件的窗口。
 *  @param[in] codepoint 字符的 Unicode 码点。
 *  @param[in] mods 描述按下哪些 [修饰键](@ref mods) 的位字段。
 *
 *  @deprecated Scheduled for removal in version 4.0.
 *
 *  @ingroup input
 */
typedef void (* sc_char_mods_cb)(sc_window* window, unsigned int codepoint, int mods);

/*! @brief 文件拖放回调函数指针类型。
 *
 *  This is the function pointer type for path drop callbacks.
 *  当一个或多个拖放路径被放置到窗口上时被调用。
 *
 *  Because the path array and its strings may have been generated specifically
 *  for that event, they are not guaranteed to be valid after the callback has
 *  returned.  If you wish to use them after the callback returns, you need to
 *  make a deep copy.
 *
 *  @param[in] window 接收事件的窗口。
 *  @param[in] path_count The number of dropped paths.
 *  @param[in] paths UTF-8 编码的文件和/或目录路径名。
 *
 *  @pointer_lifetime The path array and its strings are valid until the
 *  callback function returns.
 *
 *  @ingroup input
 */
typedef void (* sc_drop_cb)(sc_window* window, int path_count, const char* paths[]);

typedef struct sc_wsi_win_cb {
    sc_win_close_cb         close;
    sc_win_maximize_cb      maximize;

    sc_win_pos_cb           pos;
    sc_win_size_cb          size;

    sc_win_focus_cb         focus;
    sc_win_iconify_cb       iconify;
    sc_win_refresh_cb       refresh;
    sc_win_content_scale_cb scale;

    sc_key_cb               key;
    sc_char_cb              chr;
    sc_char_mods_cb         chr_mods;

    sc_cursor_pos_cb        cursor_pos;
    sc_cursor_enter_cb      cursor_enter;
    sc_win_mouse_button_cb  mouse_button;
    sc_drop_cb              drop;
    sc_scroll_cb            scroll;
} sc_wsi_win_cb;

WSI_API bool sc_wsi_win_set_callback(sc_window* window, sc_wsi_win_cb cb);

/*************************************************************************
 *
 *************************************************************************/

/*! @brief 视频模式类型。
 *
 *  描述单个视频模式。
 *
 *  @ingroup monitor
 */
typedef struct GLFWvidmode
{
    /*! 视频模式的宽度（屏幕坐标）。
     */
    int width;
    /*! 视频模式的高度（屏幕坐标）。
     */
    int height;
    /*! 视频模式的刷新率（Hz）。
     */
    int refreshRate;
} GLFWvidmode;

/*! @brief 伽马斜坡。
 *
 *  描述监视器的伽马斜坡。
 *
 *  @ingroup monitor
 */
typedef struct GLFWgammaramp
{
    /*! An array of value describing the response of the red channel.
     */
    unsigned short* red;
    /*! An array of value describing the response of the green channel.
     */
    unsigned short* green;
    /*! An array of value describing the response of the blue channel.
     */
    unsigned short* blue;
    /*! The number of elements in each array.
     */
    unsigned int size;
} GLFWgammaramp;

/*! @brief 图像数据。
 *
 *  描述单个二维图像。  See the documentation for each related
 *  function what the expected pixel format is.
 *
 *  @ingroup window
 */
typedef struct GLFWimage
{
    /*! 图像宽度（像素）。
     */
    int width;
    /*! 图像高度（像素）。
     */
    int height;
    /*! 图像像素数据，从左到右、从上到下排列。
     */
    unsigned char* pixels;
} GLFWimage;

/*************************************************************************
 * GLFW API functions
 *************************************************************************/

/*! @brief 获取 GLFW 库的版本。
 *
 *  此函数获取 GLFW 库的主版本号、次版本号和修订号。  It is intended for when you are using GLFW as a shared library and
 *  want to ensure that you are using the minimum required version.
 *
 *  Any or all of the version arguments may be `NULL`.
 *
 *  @param[out] major Where to store the major version number, or `NULL`.
 *  @param[out] minor Where to store the minor version number, or `NULL`.
 *  @param[out] rev Where to store the revision number, or `NULL`.
 *
 *  @errors None.
 *
 *  @remark 此函数可在 @ref sc_wsi_init 之前调用。
 *
 *  @thread_safety 此函数可以从任何线程调用。
 *
 *  @ingroup init
 */
WSI_API void sc_wsi_get_version(int* major, int* minor, int* rev);

/*! @brief Returns a string describing the compile-time configuration.
 *
 *  This function returns the compile-time generated
 *  [version string](@ref intro_version_string) of the GLFW library binary.  It describes
 *  the version, platforms, compiler and any platform or operating system specific
 *  compile-time options.  It should not be confused with the OpenGL or OpenGL ES version
 *  string, queried with `glGetString`.
 *
 *  __Do not use the version string__ to parse the GLFW library version.  The
 *  @ref sc_wsi_get_version function provides the version of the running library
 *  binary in numerical format.
 *
 *  __Do not use the version string__ to parse what platforms are supported.  The @ref
 *  sc_wsi_platform_supported function lets you query platform support.
 *
 *  @return The ASCII encoded GLFW version string.
 *
 *  @errors None.
 *
 *  @remark 此函数可在 @ref sc_wsi_init 之前调用。
 *
 *  @pointer_lifetime The returned string is static and compile-time generated.
 *
 *  @thread_safety 此函数可以从任何线程调用。
 *
 *  @ingroup init
 */
WSI_API const char* sc_wsi_get_version_string(void);

/*! @brief 初始化 GLFW 库。
 *
 *  此函数初始化 GLFW 库。  Before most GLFW functions can
 *  be used, GLFW must be initialized, and before an application terminates GLFW
 *  should be terminated in order to free any resources allocated during or
 *  after initialization.
 *
 *  如果此函数失败，它会在返回前调用 @ref sc_wsi_terminate。  If it
 *  succeeds, you should call @ref sc_wsi_terminate before the application exits.
 *
 *  Additional calls to this function after successful initialization but before
 *  termination will return `true` immediately.
 *
 *  The @ref SC_PLATFORM init hint controls which platforms are considered during
 *  initialization.  This also depends on which platforms the library was compiled to
 *  support.
 *
 *  @return 成功返回 `true`，如果发生 [错误](@ref error_handling) 则返回 `false`。
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_PLATFORM_UNAVAILABLE and @ref
 *  SC_WSI_ERR_PLATFORM_ERROR.
 *
 *  @remark __macOS:__ This function will change the current directory of the
 *  application to the `Contents/Resources` subdirectory of the application's
 *  bundle, if present.  This can be disabled with the @ref
 *  SC_COCOA_CHDIR_RESOURCES init hint.
 *
 *  @remark __macOS:__ This function will create the main menu and dock icon for the
 *  application.  If GLFW finds a `MainMenu.nib` it is loaded and assumed to
 *  contain a menu bar.  Otherwise a minimal menu bar is created manually with
 *  common commands like Hide, Quit and About.  The About entry opens a minimal
 *  about dialog with information from the application's bundle.  The menu bar
 *  and dock icon can be disabled entirely with the @ref SC_COCOA_MENUBAR init
 *  hint.
 *
 *  @remark __Wayland, X11:__ If the library was compiled with support for both
 *  Wayland and X11, and the @ref SC_PLATFORM init hint is set to
 *  `SC_PLATFORM_ANY`, the `XDG_SESSION_TYPE` environment variable affects
 *  which platform is picked.  If the environment variable is not set, or is set
 *  to something other than `wayland` or `x11`, the regular detection mechanism
 *  will be used instead.
 *
 *  @remark __X11:__ This function will set the `LC_CTYPE` category of the
 *  application locale according to the current environment if that category is
 *  still "C".  This is because the "C" locale breaks Unicode text input.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup init
 */
WSI_API int sc_wsi_init(void);

/*! @brief 终止 GLFW 库。
 *
 *  此函数销毁所有剩余窗口和光标，恢复任何修改过的伽马斜坡，并释放所有其他分配的资源。  Once this
 *  function is called, you must again call @ref sc_wsi_init successfully before
 *  you will be able to use most GLFW functions.
 *
 *  如果 GLFW 已成功初始化，应在应用程序退出前调用此函数。  If initialization fails, there is no need to
 *  call this function, as it is called by @ref sc_wsi_init before it returns
 *  failure.
 *
 *  This function has no effect if GLFW is not initialized.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_PLATFORM_ERROR.
 *
 *  @remark 此函数可在 @ref sc_wsi_init 之前调用。
 *
 *  @warning The contexts of any remaining windows must not be current on any
 *  other thread when this function is called.
 *
 *  @reentrancy 此函数禁止在回调中调用。
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup init
 */
WSI_API void sc_wsi_terminate(void);

/*! @brief 设置指定的初始化提示值。
 *
 *  此函数为 GLFW 的下一次初始化设置提示。
 *
 *  The values you set hints to are never reset by GLFW, but they only take
 *  effect during initialization.  Once GLFW has been initialized, any values
 *  you set will be ignored until the library is terminated and initialized
 *  again.
 *
 *  某些提示是平台相关的。  These may be set on any platform but they
 *  will only affect their specific platform.  Other platforms will ignore them.
 *  Setting these hints requires no platform specific headers or functions.
 *
 *  @param[in] hint 要设置的 [初始化提示](@ref init_hints)。
 *  @param[in] value 初始化提示的新值。
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_INVALID_ENUM and @ref
 *  SC_WSI_ERR_INVALID_VALUE.
 *
 *  @remarks 此函数可在 @ref sc_wsi_init 之前调用。
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup init
 */
WSI_API void sc_wsi_init_hint(int hint, int value);

/*! @brief Returns and clears the last error for the calling thread.
 *
 *  This function returns and clears the [error code](@ref errors) of the last
 *  error that occurred on the calling thread, and optionally a UTF-8 encoded
 *  human-readable description of it.  If no error has occurred since the last
 *  call, it returns @ref SC_WSI_ERR_NONE (zero) and the description pointer is
 *  set to `NULL`.
 *
 *  @param[in] description Where to store the error description pointer, or `NULL`.
 *  @return The last error code for the calling thread, or @ref SC_WSI_ERR_NONE
 *  (zero).
 *
 *  @errors None.
 *
 *  @pointer_lifetime The returned string is allocated and freed by GLFW.  You
 *  should not free it yourself.  It is guaranteed to be valid only until the
 *  next error occurs or the library is terminated.
 *
 *  @remark 此函数可在 @ref sc_wsi_init 之前调用。
 *
 *  @thread_safety 此函数可以从任何线程调用。
 *
 *  @ingroup init
 */
WSI_API int sc_wsi_get_error(const char** description);

/*! @brief 设置错误回调。
 *
 *  This function sets the error callback, which is called with an error code
 *  and a human-readable description each time a GLFW error occurs.
 *
 *  The error code is set before the callback is called.  Calling @ref
 *  sc_wsi_get_error from the error callback will return the same value as the error
 *  code argument.
 *
 *  The error callback is called on the thread where the error occurred.  If you
 *  are using GLFW from multiple threads, your error callback needs to be
 *  written accordingly.
 *
 *  Because the description string may have been generated specifically for that
 *  error, it is not guaranteed to be valid after the callback has returned.  If
 *  you wish to use it after the callback returns, you need to make a copy.
 *
 *  Once set, the error callback remains set even after the library has been
 *  terminated.
 *
 *  @param[in] callback 新的回调函数，或 `NULL` 移除当前设置的回调。
 *  @return 之前设置的回调，如果没有设置则返回 `NULL`。
 *
 *  @callback_signature
 *  @code
 *  void callback_name(int error_code, const char* description)
 *  @endcode
 *  For more information about the callback parameters, see the
 *  [callback pointer type](@ref sc_error_cb).
 *
 *  @errors None.
 *
 *  @remark 此函数可在 @ref sc_wsi_init 之前调用。
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup init
 */
WSI_API sc_error_cb sc_wsi_set_error_callback(sc_error_cb callback);

/*************************************************************************
 * GLFW API functions
 *************************************************************************/

/*! @brief 处理所有待处理事件。
 *
 *  此函数仅处理已在事件队列中的事件并立即返回。处理事件将导致与这些事件关联的窗口和输入回调被调用。
 *
 *  On some platforms, a window move, resize or menu operation will cause event
 *  processing to block.  This is due to how event processing is designed on
 *  those platforms.  You can use the
 *  [window refresh callback](@ref window_refresh) to redraw the contents of
 *  your window when necessary during such operations.
 *
 *  Do not assume that callbacks you set will _only_ be called in response to
 *  event processing functions like this one.  While it is necessary to poll for
 *  events, window systems that require GLFW to register callbacks of its own
 *  can pass events to GLFW in response to many window system function calls.
 *  GLFW will pass those events on to the application callbacks before
 *  returning.
 *
 *  接收手柄输入不需要事件处理。  手柄状态在调用手柄输入或游戏手柄输入函数时被轮询。
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @reentrancy 此函数禁止在回调中调用。
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup window
 */
WSI_API void sc_wsi_poll_events(void);

/*! @brief Waits until events are queued and processes them.
 *
 *  此函数使调用线程休眠，直到事件队列中至少有一个事件可用。一旦有一个或多个事件可用，其行为与 @ref sc_wsi_poll_events 完全相同。  Processing events
 *  will cause the window and input callbacks associated with those events to be
 *  called.
 *
 *  Since not all events are associated with callbacks, this function may return
 *  without a callback having been called even if you are monitoring all
 *  callbacks.
 *
 *  On some platforms, a window move, resize or menu operation will cause event
 *  processing to block.  This is due to how event processing is designed on
 *  those platforms.  You can use the
 *  [window refresh callback](@ref window_refresh) to redraw the contents of
 *  your window when necessary during such operations.
 *
 *  Do not assume that callbacks you set will _only_ be called in response to
 *  event processing functions like this one.  While it is necessary to poll for
 *  events, window systems that require GLFW to register callbacks of its own
 *  can pass events to GLFW in response to many window system function calls.
 *  GLFW will pass those events on to the application callbacks before
 *  returning.
 *
 *  接收手柄输入不需要事件处理。  手柄状态在调用手柄输入或游戏手柄输入函数时被轮询。
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @reentrancy 此函数禁止在回调中调用。
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup window
 */
WSI_API void sc_wsi_wait_events(void);

/*! @brief Waits with timeout until events are queued and processes them.
 *
 *  This function puts the calling thread to sleep until at least one event is
 *  available in the event queue, or until the specified timeout is reached.  If
 *  one or more events are available, it behaves exactly like @ref
 *  sc_wsi_poll_events, i.e. the events in the queue are processed and the function
 *  then returns immediately.  Processing events will cause the window and input
 *  callbacks associated with those events to be called.
 *
 *  The timeout value must be a positive finite number.
 *
 *  Since not all events are associated with callbacks, this function may return
 *  without a callback having been called even if you are monitoring all
 *  callbacks.
 *
 *  On some platforms, a window move, resize or menu operation will cause event
 *  processing to block.  This is due to how event processing is designed on
 *  those platforms.  You can use the
 *  [window refresh callback](@ref window_refresh) to redraw the contents of
 *  your window when necessary during such operations.
 *
 *  Do not assume that callbacks you set will _only_ be called in response to
 *  event processing functions like this one.  While it is necessary to poll for
 *  events, window systems that require GLFW to register callbacks of its own
 *  can pass events to GLFW in response to many window system function calls.
 *  GLFW will pass those events on to the application callbacks before
 *  returning.
 *
 *  接收手柄输入不需要事件处理。  手柄状态在调用手柄输入或游戏手柄输入函数时被轮询。
 *
 *  @param[in] timeout The maximum amount of time, in seconds, to wait.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED、@ref SC_WSI_ERR_INVALID_VALUE 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @reentrancy 此函数禁止在回调中调用。
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *  @ingroup window
 */
WSI_API void sc_wsi_wait_events_timeout(double timeout);

/*! @brief 向事件队列发送空事件。
 *
 *  此函数从当前线程向事件队列发送一个空事件，导致 @ref sc_wsi_wait_events 或 @ref sc_wsi_wait_events_timeout 返回。
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @thread_safety 此函数可以从任何线程调用。
 *
 *  @ingroup window
 */
WSI_API void sc_wsi_post_empty_event(void);

/*************************************************************************
 * GLFW API functions
 *************************************************************************/

/*! @brief 返回当前连接的监视器列表。
 *
 *  此函数返回所有当前连接的监视器的句柄数组。  The primary monitor is always first in the returned array.  If no
 *  monitors were found, this function returns `NULL`.
 *
 *  @param[out] count Where to store the number of monitors in the returned
 *  array.  This is set to zero if an error occurred.
 *  @return An array of monitor handles, or `NULL` if no monitors were found or
 *  if an [error](@ref error_handling) occurred.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED。
 *
 *  @pointer_lifetime The returned array is allocated and freed by GLFW.  You
 *  should not free it yourself.  It is guaranteed to be valid only until the
 *  monitor configuration changes or the library is terminated.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup monitor
 */
WSI_API sc_monitor** sc_wsi_get_monitors(int* count);

/*! @brief 返回主监视器。
 *
 *  此函数返回主监视器。  This is usually the monitor
 *  where elements like the task bar or global menu bar are located.
 *
 *  @return The primary monitor, or `NULL` if no monitors were found or if an
 *  [error](@ref error_handling) occurred.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED。
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @remark The primary monitor is always first in the array returned by @ref
 *  sc_wsi_get_monitors.
 *
 *  @ingroup monitor
 */
WSI_API sc_monitor* sc_wsi_get_primary_monitor(void);

/*! @brief Returns the position of the monitor's viewport on the virtual screen.
 *
 *  此函数返回指定监视器左上角的位置（屏幕坐标）。
 *
 *  位置参数中任何一个或全部都可以是 `NULL`。  If an error occurs, all
 *  non-`NULL` position arguments will be set to zero.
 *
 *  @param[in] monitor 要查询的监视器。
 *  @param[out] xpos Where to store the monitor x-coordinate, or `NULL`.
 *  @param[out] ypos Where to store the monitor y-coordinate, or `NULL`.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup monitor
 */
WSI_API void sc_wsi_monitor_get_pos(sc_monitor* monitor, int* xpos, int* ypos);

/*! @brief Retrieves the work area of the monitor.
 *
 *  This function returns the position, in screen coordinates, of the upper-left
 *  corner of the work area of the specified monitor along with the work area
 *  size in screen coordinates. The work area is defined as the area of the
 *  monitor not occluded by the window system task bar where present. If no
 *  task bar exists then the work area is the monitor resolution in screen
 *  coordinates.
 *
 *  Any or all of the position and size arguments may be `NULL`.  If an error
 *  occurs, all non-`NULL` position and size arguments will be set to zero.
 *
 *  @param[in] monitor 要查询的监视器。
 *  @param[out] xpos Where to store the monitor x-coordinate, or `NULL`.
 *  @param[out] ypos Where to store the monitor y-coordinate, or `NULL`.
 *  @param[out] width Where to store the monitor width, or `NULL`.
 *  @param[out] height Where to store the monitor height, or `NULL`.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup monitor
 */
WSI_API void sc_wsi_monitor_get_work_area(sc_monitor* monitor, int* xpos, int* ypos, int* width, int* height);

/*! @brief 返回监视器的物理尺寸。
 *
 *  此函数返回指定监视器显示区域的尺寸（毫米）。
 *
 *  Some platforms do not provide accurate monitor size information, either
 *  because the monitor [EDID][] data is incorrect or because the driver does
 *  not report it accurately.
 *
 *  [EDID]: https://en.wikipedia.org/wiki/Extended_display_identification_data
 *
 *  Any or all of the size arguments may be `NULL`.  If an error occurs, all
 *  non-`NULL` size arguments will be set to zero.
 *
 *  @param[in] monitor 要查询的监视器。
 *  @param[out] widthMM Where to store the width, in millimetres, of the
 *  monitor's display area, or `NULL`.
 *  @param[out] heightMM Where to store the height, in millimetres, of the
 *  monitor's display area, or `NULL`.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED。
 *
 *  @remark __Win32:__ On Windows 8 and earlier the physical size is calculated from
 *  the current resolution and system DPI instead of querying the monitor EDID data.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup monitor
 */
WSI_API void sc_wsi_monitor_get_physical_size(sc_monitor* monitor, int* widthMM, int* heightMM);

/*! @brief Retrieves the content scale for the specified monitor.
 *
 *  This function retrieves the content scale for the specified monitor.  The
 *  content scale is the ratio between the current DPI and the platform's
 *  default DPI.  这对文本和 UI 元素尤其重要。  If
 *  the pixel dimensions of your UI scaled by this look appropriate on your
 *  machine then it should appear at a reasonable size on other machines
 *  regardless of their DPI and scaling settings.  This relies on the system DPI
 *  and scaling settings being somewhat correct.
 *
 *  The content scale may depend on both the monitor resolution and pixel
 *  density and on user settings.  It may be very different from the raw DPI
 *  calculated from the physical size and current resolution.
 *
 *  @param[in] monitor 要查询的监视器。
 *  @param[out] xscale 存储 x 轴内容缩放比例的位置，或 `NULL`。
 *  @param[out] yscale 存储 y 轴内容缩放比例的位置，或 `NULL`。
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @remark __Wayland:__ Fractional scaling information is not yet available for
 *  monitors, so this function only returns integer content scales.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup monitor
 */
WSI_API void sc_wsi_monitor_get_content_scale(sc_monitor* monitor, float* xscale, float* yscale);

/*! @brief 返回指定监视器的名称。
 *
 *  This function returns a human-readable name, encoded as UTF-8, of the
 *  specified monitor.  The name typically reflects the make and model of the
 *  monitor and is not guaranteed to be unique among the connected monitors.
 *
 *  @param[in] monitor 要查询的监视器。
 *  @return The UTF-8 encoded name of the monitor, or `NULL` if an
 *  [error](@ref error_handling) occurred.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED。
 *
 *  @pointer_lifetime The returned string is allocated and freed by GLFW.  You
 *  should not free it yourself.  It is valid until the specified monitor is
 *  disconnected or the library is terminated.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup monitor
 */
WSI_API const char* sc_wsi_monitor_get_name(sc_monitor* monitor);

/*! @brief 返回/设置指定监视器的用户指针。
 *
 *  This function returns/sets the user-defined pointer of the specified monitor.  The
 *  current value is retained until the monitor is disconnected.  The initial
 *  value is `NULL`.
 *
 *  This function may be called from the monitor callback, even for a monitor
 *  that is being disconnected.
 *
 *  @param[in] monitor The monitor whose pointer to set.
 *  @param[in] pointer The new value.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED。
 *
 *  @thread_safety 此函数可以从任何线程调用。  Access is not
 *  synchronized.
 *
 *  @ingroup monitor
 */
WSI_API void* sc_wsi_monitor_get_user_data(sc_monitor* monitor);
WSI_API void sc_wsi_monitor_set_user_data(sc_monitor* monitor, void* pointer);

/*! @brief 设置监视器配置回调。
 *
 *  This function sets the monitor configuration callback, or removes the
 *  currently set callback.  This is called when a monitor is connected to or
 *  disconnected from the system.
 *
 *  @param[in] callback 新的回调函数，或 `NULL` 移除当前设置的回调。
 *  @return The previously set callback, or `NULL` if no callback was set or the
 *  library had not been [initialized](@ref intro_init).
 *
 *  @callback_signature
 *  @code
 *  void function_name(sc_monitor* monitor, int event)
 *  @endcode
 *  For more information about the callback parameters, see the
 *  [function pointer type](@ref sc_monitor_cb).
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED。
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup monitor
 */
WSI_API sc_monitor_cb sc_wsi_monitor_set_callback(sc_monitor_cb callback);

/*! @brief 返回指定监视器的可用视频模式。
 *
 *  此函数返回指定监视器支持的所有视频模式数组。  The returned array is sorted in ascending order, first by color
 *  bit depth (the sum of all channel depths), then by resolution area (the
 *  product of width and height), then resolution width and finally by refresh
 *  rate.
 *
 *  @param[in] monitor 要查询的监视器。
 *  @param[out] count Where to store the number of video modes in the returned
 *  array.  This is set to zero if an error occurred.
 *  @return An array of video modes, or `NULL` if an
 *  [error](@ref error_handling) occurred.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @pointer_lifetime The returned array is allocated and freed by GLFW.  You
 *  should not free it yourself.  It is valid until the specified monitor is
 *  disconnected, this function is called again for that monitor or the library
 *  is terminated.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup monitor
 */
WSI_API const GLFWvidmode* sc_wsi_monitor_get_video_modes(sc_monitor* monitor, int* count);

/*! @brief 返回指定监视器的当前模式。
 *
 *  此函数返回指定监视器的当前视频模式。  If
 *  you have created a full screen window for that monitor, the return value
 *  will depend on whether that window is iconified.
 *
 *  @param[in] monitor 要查询的监视器。
 *  @return The current mode of the monitor, or `NULL` if an
 *  [error](@ref error_handling) occurred.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @pointer_lifetime The returned array is allocated and freed by GLFW.  You
 *  should not free it yourself.  It is valid until the specified monitor is
 *  disconnected or the library is terminated.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup monitor
 */
WSI_API const GLFWvidmode* sc_wsi_monitor_get_video_mode(sc_monitor* monitor);

/*! @brief 生成伽马斜坡并设置到指定监视器。
 *
 *  This function generates an appropriately sized gamma ramp from the specified
 *  exponent and then calls @ref sc_wsi_monitor_set_gamma_ramp with it.  The value must be
 *  a finite number greater than zero.
 *
 *  The software controlled gamma ramp is applied _in addition_ to the hardware
 *  gamma correction, which today is usually an approximation of sRGB gamma.
 *  This means that setting a perfectly linear ramp, or gamma 1.0, will produce
 *  the default (usually sRGB-like) behavior.
 *
 *  For gamma correct rendering with OpenGL or OpenGL ES, see the @ref
 *  GLFW_SRGB_CAPABLE hint.
 *
 *  @param[in] monitor The monitor whose gamma ramp to set.
 *  @param[in] gamma The desired exponent.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED, @ref SC_WSI_ERR_INVALID_VALUE,
 *  @ref SC_WSI_ERR_PLATFORM_ERROR and @ref SC_WSI_ERR_FEATURE_UNAVAILABLE (see remarks).
 *
 *  @remark __Wayland:__ Monitor gamma is a privileged protocol, so this function
 *  cannot be implemented and emits @ref SC_WSI_ERR_FEATURE_UNAVAILABLE.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup monitor
 */
WSI_API void sc_wsi_monitor_get_gamma(sc_monitor* monitor, float gamma);

/*! @brief 返回/设置指定监视器当前的伽马斜坡。
 *
 *  此函数返回/设置指定监视器的当前伽马斜坡。The
 *  original gamma ramp for that monitor is saved by GLFW the first time this
 *  function is called and is restored by @ref sc_wsi_terminate.
 *
 *  The software controlled gamma ramp is applied _in addition_ to the hardware
 *  gamma correction, which today is usually an approximation of sRGB gamma.
 *  This means that setting a perfectly linear ramp, or gamma 1.0, will produce
 *  the default (usually sRGB-like) behavior.
 *
 *  For gamma correct rendering with OpenGL or OpenGL ES, see the @ref
 *  GLFW_SRGB_CAPABLE hint.
 *
 *  @param[in] monitor 要查询的监视器。
 *  @return The current gamma ramp, or `NULL` if an
 *  [error](@ref error_handling) occurred.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED, @ref SC_WSI_ERR_PLATFORM_ERROR
 *  and @ref SC_WSI_ERR_FEATURE_UNAVAILABLE (see remarks).
 *
 *  @remark __Wayland:__ Monitor gamma is a privileged protocol, so this function
 *  cannot be implemented and emits @ref SC_WSI_ERR_FEATURE_UNAVAILABLE while
 *  returning `NULL`.
 *
 *  @pointer_lifetime The returned structure and its arrays are allocated and
 *  freed by GLFW.  You should not free them yourself.  They are valid until the
 *  specified monitor is disconnected, this function is called again for that
 *  monitor or the library is terminated.
 *
 *  @remark The size of the specified gamma ramp should match the size of the
 *  current ramp for that monitor.
 *
 *  @remark __Win32:__ The gamma ramp size must be 256.
 *
 *  @remark __Wayland:__ Monitor gamma is a privileged protocol, so this function
 *  cannot be implemented and emits @ref SC_WSI_ERR_FEATURE_UNAVAILABLE.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup monitor
 */
WSI_API const GLFWgammaramp* sc_wsi_monitor_get_gamma_ramp(sc_monitor* monitor);
WSI_API void sc_wsi_monitor_set_gamma_ramp(sc_monitor* monitor, const GLFWgammaramp* ramp);

/*************************************************************************
 * GLFW API functions
 *************************************************************************/

/*! @brief 重置所有窗口提示为默认值。
 *
 *  此函数将所有窗口提示重置为 [默认值](@ref window_hints_values)。
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED。
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup window
 */
WSI_API void sc_wsi_default_window_hints(void);

/*! @brief 设置指定的窗口提示值。
 *
 *  此函数为下一次调用 @ref sc_wsi_win_create 设置提示。  The
 *  hints, once set, retain their values until changed by a call to this
 *  function or @ref sc_wsi_default_window_hints, or until the library is terminated.
 *
 *  Only integer value hints can be set with this function.  String value hints
 *  are set with @ref sc_wsi_window_hint_string.
 *
 *  This function does not check whether the specified hint values are valid.
 *  If you set hints to invalid values this will instead be reported by the next
 *  call to @ref sc_wsi_win_create.
 *
 *  某些提示是平台相关的。  These may be set on any platform but they
 *  will only affect their specific platform.  Other platforms will ignore them.
 *  Setting these hints requires no platform specific headers or functions.
 *
 *  @param[in] hint The [window hint](@ref window_hints) to set.
 *  @param[in] value The new value of the window hint.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_INVALID_ENUM。
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup window
 */
WSI_API void sc_wsi_window_hint(int hint, int value);

/*! @brief 设置指定的窗口提示值。
 *
 *  此函数为下一次调用 @ref sc_wsi_win_create 设置提示。  The
 *  hints, once set, retain their values until changed by a call to this
 *  function or @ref sc_wsi_default_window_hints, or until the library is terminated.
 *
 *  Only string type hints can be set with this function.  Integer value hints
 *  are set with @ref sc_wsi_window_hint.
 *
 *  This function does not check whether the specified hint values are valid.
 *  If you set hints to invalid values this will instead be reported by the next
 *  call to @ref sc_wsi_win_create.
 *
 *  某些提示是平台相关的。  These may be set on any platform but they
 *  will only affect their specific platform.  Other platforms will ignore them.
 *  Setting these hints requires no platform specific headers or functions.
 *
 *  @param[in] hint The [window hint](@ref window_hints) to set.
 *  @param[in] value The new value of the window hint.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_INVALID_ENUM。
 *
 *  @pointer_lifetime The specified string is copied before this function
 *  returns.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup window
 */
WSI_API void sc_wsi_window_hint_string(int hint, const char* value);

//-----------------------------------------------------------------------------

/*! @brief 创建窗口及其关联上下文。
 *
 *  This function creates a window and its associated OpenGL or OpenGL ES
 *  context.  Most of the options controlling how the window and its context
 *  should be created are specified with [window hints](@ref window_hints).
 *
 *  成功创建不会改变当前上下文。  Before you
 *  can use the newly created context, you need to
 *  [make it current](@ref context_current).  For information about the `share`
 *  parameter, see @ref context_sharing.
 *
 *  The created window, framebuffer and context may differ from what you
 *  requested, as not all parameters and hints are
 *  [hard constraints](@ref window_hints_hard).  This includes the size of the
 *  window, especially for full screen windows.  To query the actual attributes
 *  of the created window, framebuffer and context, see @ref
 *  sc_wsi_win_get_attrib, @ref sc_wsi_win_get_size and @ref glfwGetFramebufferSize.
 *
 *  To create a full screen window, you need to specify the monitor the window
 *  will cover.  If no monitor is specified, the window will be windowed mode.
 *  Unless you have a way for the user to choose a specific monitor, it is
 *  recommended that you pick the primary monitor.  For more information on how
 *  to query connected monitors, see @ref monitor_monitors.
 *
 *  For full screen windows, the specified size becomes the resolution of the
 *  window's _desired video mode_.  As long as a full screen window is not
 *  iconified, the supported video mode most closely matching the desired video
 *  mode is set for the specified monitor.  For more information about full
 *  screen windows, including the creation of so called _windowed full screen_
 *  or _borderless full screen_ windows, see @ref window_windowed_full_screen.
 *
 *  Once you have created the window, you can switch it between windowed and
 *  full screen mode with @ref sc_wsi_win_set_monitor.  This will not affect its
 *  OpenGL or OpenGL ES context.
 *
 *  By default, newly created windows use the placement recommended by the
 *  window system.  To create the window at a specific position, set the @ref
 *  SC_WIN_POSITION_X and @ref SC_WIN_POSITION_Y window hints before creation.  To
 *  restore the default behavior, set either or both hints back to
 *  `SC_ANY_POSITION`.
 *
 *  As long as at least one full screen window is not iconified, the screensaver
 *  is prohibited from starting.
 *
 *  Window systems put limits on window sizes.  Very large or very small window
 *  dimensions may be overridden by the window system on creation.  Check the
 *  actual [size](@ref window_size) after creation.
 *
 *  The [swap interval](@ref buffer_swap) is not set during window creation and
 *  the initial value may vary depending on driver settings and defaults.
 *
 *  @param[in] width The desired width, in screen coordinates, of the window.
 *  This must be greater than zero.
 *  @param[in] height The desired height, in screen coordinates, of the window.
 *  This must be greater than zero.
 *  @param[in] title The initial, UTF-8 encoded window title.
 *  @param[in] monitor 全屏模式使用的监视器，或 `NULL` 表示窗口模式。
 *  @param[in] share 要共享资源的窗口上下文，或 `NULL` 不共享资源。
 *  @return 创建的窗口句柄，如果发生 [错误](@ref error_handling) 则返回 `NULL`。
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED, @ref
 *  SC_WSI_ERR_INVALID_ENUM, @ref SC_WSI_ERR_INVALID_VALUE, @ref SC_WSI_ERR_API_UNAVAILABLE, @ref
 *  SC_WSI_ERR_VERSION_UNAVAILABLE, @ref SC_WSI_ERR_FORMAT_UNAVAILABLE, @ref
 *  SC_WSI_ERR_NO_WINDOW_CONTEXT and @ref SC_WSI_ERR_PLATFORM_ERROR.
 *
 *  @remark __Win32:__ Window creation will fail if the Microsoft GDI software
 *  OpenGL implementation is the only one available.
 *
 *  @remark __Win32:__ If the executable has an icon resource named `GLFW_ICON,` it
 *  will be set as the initial icon for the window.  If no such icon is present,
 *  the `IDI_APPLICATION` icon will be used instead.  To set a different icon,
 *  see @ref sc_wsi_win_set_icon.
 *
 *  @remark __Win32:__ The context to share resources with must not be current on
 *  any other thread.
 *
 *  @remark __macOS:__ The OS only supports core profile contexts for OpenGL
 *  versions 3.2 and later.  Before creating an OpenGL context of version 3.2 or
 *  later you must set the [GLFW_OPENGL_PROFILE](@ref GLFW_OPENGL_PROFILE_hint)
 *  hint accordingly.  OpenGL 3.0 and 3.1 contexts are not supported at all
 *  on macOS.
 *
 *  @remark __macOS:__ The GLFW window has no icon, as it is not a document
 *  window, but the dock icon will be the same as the application bundle's icon.
 *  For more information on bundles, see the
 *  [Bundle Programming Guide][bundle-guide] in the Mac Developer Library.
 *
 *  [bundle-guide]: https://developer.apple.com/library/mac/documentation/CoreFoundation/Conceptual/CFBundles/
 *
 *  @remark __macOS:__  The window frame will not be rendered at full resolution
 *  on Retina displays unless the
 *  [GLFW_SCALE_FRAMEBUFFER](@ref GLFW_SCALE_FRAMEBUFFER_hint)
 *  hint is `true` and the `NSHighResolutionCapable` key is enabled in the
 *  application bundle's `Info.plist`.  For more information, see
 *  [High Resolution Guidelines for OS X][hidpi-guide] in the Mac Developer
 *  Library.  The GLFW test and example programs use a custom `Info.plist`
 *  template for this, which can be found as `CMake/Info.plist.in` in the source
 *  tree.
 *
 *  [hidpi-guide]: https://developer.apple.com/library/mac/documentation/GraphicsAnimation/Conceptual/HighResolutionOSX/Explained/Explained.html
 *
 *  @remark __macOS:__ When activating frame autosaving with
 *  [SC_COCOA_FRAME_NAME](@ref GLFW_COCOA_FRAME_NAME_hint), the specified
 *  window size and position may be overridden by previously saved values.
 *
 *  @remark __Wayland:__ GLFW uses [libdecor][] where available to create its window
 *  decorations.  This in turn uses server-side XDG decorations where available
 *  and provides high quality client-side decorations on compositors like GNOME.
 *  If both XDG decorations and libdecor are unavailable, GLFW falls back to
 *  a very simple set of window decorations that only support moving, resizing
 *  and the window manager's right-click menu.
 *
 *  [libdecor]: https://gitlab.freedesktop.org/libdecor/libdecor
 *
 *  @remark __X11:__ Some window managers will not respect the placement of
 *  initially hidden windows.
 *
 *  @remark __X11:__ Due to the asynchronous nature of X11, it may take a moment for
 *  a window to reach its requested state.  This means you may not be able to
 *  query the final size, position or other attributes directly after window
 *  creation.
 *
 *  @remark __X11:__ The class part of the `WM_CLASS` window property will by
 *  default be set to the window title passed to this function.  The instance
 *  part will use the contents of the `RESOURCE_NAME` environment variable, if
 *  present and not empty, or fall back to the window title.  Set the
 *  [SC_X11_CLASS_NAME](@ref GLFW_X11_CLASS_NAME_hint) and
 *  [SC_X11_INSTANCE_NAME](@ref GLFW_X11_INSTANCE_NAME_hint) window hints to
 *  override this.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup window
 */
WSI_API sc_window* sc_wsi_win_create(int width, int height, const char* title, sc_monitor* monitor, sc_window* share);

/*! @brief 返回当前 WSI 已选择的平台 ID。
 *
 *  返回值为 `SC_PLATFORM_*` 常量之一；若未初始化则返回 `SC_PLATFORM_ANY`。
 *
 *  @return 平台 ID，失败返回 `SC_PLATFORM_ANY`。
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED。
 *
 *  @ingroup window
 */
WSI_API int sc_wsi_get_platform(void);

/*! @brief 销毁指定窗口及其上下文。
 *
 *  此函数销毁指定窗口及其上下文。  On calling
 *  this function, no further callbacks will be called for that window.
 *
 *  If the context of the specified window is current on the main thread, it is
 *  detached before being destroyed.
 *
 *  @param[in] window 要销毁的窗口。
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @note The context of the specified window must not be current on any other
 *  thread when this function is called.
 *
 *  @reentrancy 此函数禁止在回调中调用。
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup window
 */
WSI_API void sc_wsi_win_destroy(sc_window* window);

/*! @brief Checks/Sets the close flag of the specified window.
 *
 *  此函数返回/设置指定窗口的关闭标志值。
 *
 *  可用于覆盖用户关闭窗口的尝试，或发出应关闭窗口的信号。
 *
 *  @param[in] window 要查询的窗口。
 *  @return The value of the close flag.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED。
 *
 *  @thread_safety 此函数可以从任何线程调用。  Access is not
 *  synchronized.
 *
 *  @ingroup window
 */
WSI_API int sc_wsi_win_get_should_close(sc_window* window);
WSI_API void sc_wsi_win_set_should_close(sc_window* window, int value);

/*! @brief 返回/设置指定窗口的标题
 *
 *  This function returns the window title, encoded as UTF-8, of the specified
 *  window.
 *
 *  @param[in] window 要查询的窗口。
 *  @return The UTF-8 encoded window title, or `NULL` if an
 *  [error](@ref error_handling) occurred.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED。
 *
 *  @remark The returned title is currently a copy of the title last set by @ref
 *  sc_wsi_win_create or @ref sc_wsi_win_set_title.  It does not include any
 *  additional text which may be appended by the platform or another program.
 *
 *  @pointer_lifetime The returned string is allocated and freed by GLFW.  You
 *  should not free it yourself.  It is valid until the next call to @ref
 *  sc_wsi_win_get_title or @ref sc_wsi_win_set_title, or until the library is
 *  terminated.
 *
 *  @remark __macOS:__ The window title will not be updated until the next time you
 *  process events.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup window
 */
WSI_API const char* sc_wsi_win_get_title(sc_window* window);
WSI_API void sc_wsi_win_set_title(sc_window* window, const char* title);

/*! @brief 设置指定窗口的图标。
 *
 *  This function sets the icon of the specified window.  If passed an array of
 *  candidate images, those of or closest to the sizes desired by the system are
 *  selected.  If no images are specified, the window reverts to its default
 *  icon.
 *
 *  The pixels are 32-bit, little-endian, non-premultiplied RGBA, i.e. eight
 *  bits per channel with the red channel first.  They are arranged canonically
 *  as packed sequential rows, starting from the top-left corner.
 *
 *  The desired image sizes varies depending on platform and system settings.
 *  The selected images will be rescaled as needed.  Good sizes include 16x16,
 *  32x32 and 48x48.
 *
 *  @param[in] window The window whose icon to set.
 *  @param[in] count The number of images in the specified array, or zero to
 *  revert to the default window icon.
 *  @param[in] images 用于创建图标的图像。  This is ignored if
 *  count is zero.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED, @ref
 *  SC_WSI_ERR_INVALID_VALUE, @ref SC_WSI_ERR_PLATFORM_ERROR and @ref
 *  SC_WSI_ERR_FEATURE_UNAVAILABLE (see remarks).
 *
 *  @pointer_lifetime The specified image data is copied before this function
 *  returns.
 *
 *  @remark __macOS:__ Regular windows do not have icons on macOS.  This function
 *  will emit @ref SC_WSI_ERR_FEATURE_UNAVAILABLE.  The dock icon will be the same as
 *  the application bundle's icon.  For more information on bundles, see the
 *  [Bundle Programming Guide][bundle-guide] in the Mac Developer Library.
 *
 *  [bundle-guide]: https://developer.apple.com/library/mac/documentation/CoreFoundation/Conceptual/CFBundles/
 *
 *  @remark __Wayland:__ There is no existing protocol to change an icon, the
 *  window will thus inherit the one defined in the application's desktop file.
 *  This function will emit @ref SC_WSI_ERR_FEATURE_UNAVAILABLE.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup window
 */
WSI_API void sc_wsi_win_set_icon(sc_window* window, int count, const GLFWimage* images);

/*! @brief 获取/设置指定窗口内容区域的位置。
 *
 *  此函数获取/设置指定窗口内容区域左上角的位置（屏幕坐标）。
 *  If the window is a full screen window, this function does nothing.
 *
 *  位置参数中任何一个或全部都可以是 `NULL`。  If an error occurs, all
 *  non-`NULL` position arguments will be set to zero.
 *
 *  __Do not use this function__ to move an already visible window unless you
 *  have very good reasons for doing so, as it will confuse and annoy the user.
 *
 *  The window manager may put limits on what positions are allowed.  GLFW
 *  cannot and should not override these limits.
 *
 *  @param[in] window 要查询的窗口。
 *  @param[out] xpos Where to store the x-coordinate of the upper-left corner of
 *  the content area, or `NULL`.
 *  @param[out] ypos Where to store the y-coordinate of the upper-left corner of
 *  the content area, or `NULL`.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED, @ref
 *  SC_WSI_ERR_PLATFORM_ERROR and @ref SC_WSI_ERR_FEATURE_UNAVAILABLE (see remarks).
 *
 *  @remark __Wayland:__ Window positions are not currently part of any common
 *  Wayland protocol, so this function cannot be implemented and will emit @ref
 *  SC_WSI_ERR_FEATURE_UNAVAILABLE.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup window
 */
WSI_API void sc_wsi_win_get_pos(sc_window* window, int* xpos, int* ypos);
WSI_API void sc_wsi_win_set_pos(sc_window* window, int xpos, int ypos);

/*! @brief 获取/设置指定窗口内容区域的大小。
 *
 *  此函数获取/设置指定窗口内容区域的大小（屏幕坐标）。  If you wish to retrieve the size of the
 *  framebuffer of the window in pixels, see @ref glfwGetFramebufferSize.
 *
 *  Any or all of the size arguments may be `NULL`.  If an error occurs, all
 *  non-`NULL` size arguments will be set to zero.
 *
 *  For full screen windows, this function updates the resolution of its desired
 *  video mode and switches to the video mode closest to it, without affecting
 *  the window's context.  As the context is unaffected, the bit depths of the
 *  framebuffer remain unchanged.
 *
 *  If you wish to update the refresh rate of the desired video mode in addition
 *  to its resolution, see @ref sc_wsi_win_set_monitor.
 *
 *  The window manager may put limits on what sizes are allowed.  GLFW cannot
 *  and should not override these limits.
 *
 *  @param[in] window The window whose size to retrieve.
 *  @param[out] width Where to store the width, in screen coordinates, of the
 *  content area, or `NULL`.
 *  @param[out] height Where to store the height, in screen coordinates, of the
 *  content area, or `NULL`.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup window
 */
WSI_API void sc_wsi_win_get_size(sc_window* window, int* width, int* height);
WSI_API void sc_wsi_win_get_framebuffer_size(sc_window* window, int* width, int* height);
WSI_API void sc_wsi_win_set_size(sc_window* window, int width, int height);

/*! @brief Sets the size limits of the specified window.
 *
 *  此函数设置指定窗口内容区域的大小限制。  If the window is full screen, the size limits only take effect
 *  once it is made windowed.  If the window is not resizable, this function
 *  does nothing.
 *
 *  The size limits are applied immediately to a windowed mode window and may
 *  cause it to be resized.
 *
 *  The maximum dimensions must be greater than or equal to the minimum
 *  dimensions and all must be greater than or equal to zero.
 *
 *  @param[in] window The window to set limits for.
 *  @param[in] minwidth The minimum width, in screen coordinates, of the content
 *  area, or `SC_DONT_CARE`.
 *  @param[in] minheight The minimum height, in screen coordinates, of the
 *  content area, or `SC_DONT_CARE`.
 *  @param[in] maxwidth The maximum width, in screen coordinates, of the content
 *  area, or `SC_DONT_CARE`.
 *  @param[in] maxheight The maximum height, in screen coordinates, of the
 *  content area, or `SC_DONT_CARE`.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED、@ref SC_WSI_ERR_INVALID_VALUE 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @remark If you set size limits and an aspect ratio that conflict, the
 *  results are undefined.
 *
 *  @remark __Wayland:__ The size limits will not be applied until the window is
 *  actually resized, either by the user or by the compositor.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup window
 */
WSI_API void sc_wsi_win_set_size_limits(sc_window* window, int minwidth, int minheight, int maxwidth, int maxheight);

/*! @brief Sets the aspect ratio of the specified window.
 *
 *  此函数设置指定窗口内容区域的所需宽高比。  If the window is full screen, the aspect ratio only takes
 *  effect once it is made windowed.  If the window is not resizable, this
 *  function does nothing.
 *
 *  The aspect ratio is specified as a numerator and a denominator and both
 *  values must be greater than zero.  For example, the common 16:9 aspect ratio
 *  is specified as 16 and 9, respectively.
 *
 *  If the numerator and denominator is set to `SC_DONT_CARE` then the aspect
 *  ratio limit is disabled.
 *
 *  The aspect ratio is applied immediately to a windowed mode window and may
 *  cause it to be resized.
 *
 *  @param[in] window The window to set limits for.
 *  @param[in] numer The numerator of the desired aspect ratio, or
 *  `SC_DONT_CARE`.
 *  @param[in] denom The denominator of the desired aspect ratio, or
 *  `SC_DONT_CARE`.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED、@ref SC_WSI_ERR_INVALID_VALUE 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @remark If you set size limits and an aspect ratio that conflict, the
 *  results are undefined.
 *
 *  @remark __Wayland:__ The aspect ratio will not be applied until the window is
 *  actually resized, either by the user or by the compositor.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup window
 */
WSI_API void sc_wsi_win_set_size_aspect_ratio(sc_window* window, int numer, int denom);

/*! @brief 获取指定窗口边框的大小。
 *
 *  此函数获取指定窗口边框各边缘的大小（屏幕坐标）。  This size includes the title bar, if the
 *  window has one.  The size of the frame may vary depending on the
 *  [window-related hints](@ref window_hints_wnd) used to create it.
 *
 *  Because this function retrieves the size of each window frame edge and not
 *  the offset along a particular coordinate axis, the retrieved values will
 *  always be zero or positive.
 *
 *  Any or all of the size arguments may be `NULL`.  If an error occurs, all
 *  non-`NULL` size arguments will be set to zero.
 *
 *  @param[in] window The window whose frame size to query.
 *  @param[out] left Where to store the size, in screen coordinates, of the left
 *  edge of the window frame, or `NULL`.
 *  @param[out] top Where to store the size, in screen coordinates, of the top
 *  edge of the window frame, or `NULL`.
 *  @param[out] right Where to store the size, in screen coordinates, of the
 *  right edge of the window frame, or `NULL`.
 *  @param[out] bottom Where to store the size, in screen coordinates, of the
 *  bottom edge of the window frame, or `NULL`.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup window
 */
WSI_API void sc_wsi_win_get_frame_size(sc_window* window, int* left, int* top, int* right, int* bottom);

/*! @brief 获取指定窗口的内容缩放比例。
 *
 *  此函数获取指定窗口的内容缩放比例。  The
 *  content scale is the ratio between the current DPI and the platform's
 *  default DPI.  这对文本和 UI 元素尤其重要。  If
 *  the pixel dimensions of your UI scaled by this look appropriate on your
 *  machine then it should appear at a reasonable size on other machines
 *  regardless of their DPI and scaling settings.  This relies on the system DPI
 *  and scaling settings being somewhat correct.
 *
 *  On platforms where each monitors can have its own content scale, the window
 *  content scale will depend on which monitor the system considers the window
 *  to be on.
 *
 *  @param[in] window 要查询的窗口。
 *  @param[out] xscale 存储 x 轴内容缩放比例的位置，或 `NULL`。
 *  @param[out] yscale 存储 y 轴内容缩放比例的位置，或 `NULL`。
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup window
 */
WSI_API void sc_wsi_win_get_content_scale(sc_window* window, float* xscale, float* yscale);

/*! @brief 返回/设置整个窗口的不透明度。
 *
 *  This function returns the opacity of the window, including any decorations.
 *
 *  The opacity (or alpha) value is a positive finite number between zero and
 *  one, where zero is fully transparent and one is fully opaque.  If the system
 *  does not support whole window transparency, this function always returns one.
 *
 *  The initial opacity value for newly created windows is one.
 *
 *  A window created with framebuffer transparency may not use whole window
 *  transparency.  The results of doing this are undefined.
 *
 *  @param[in] window 要查询的窗口。
 *  @return The opacity value of the specified window.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @remark __Wayland:__ There is no way to set an opacity factor for a window.
 *  This function will emit @ref SC_WSI_ERR_FEATURE_UNAVAILABLE.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup window
 */
WSI_API float sc_wsi_win_get_opacity(sc_window* window);
WSI_API void sc_wsi_win_set_opacity(sc_window* window, float opacity);

/*! @brief 最小化指定窗口。
 *
 *  此函数最小化指定窗口（如果之前处于恢复状态）。  If the window is already iconified, this function does
 *  nothing.
 *
 *  If the specified window is a full screen window, GLFW restores the original
 *  video mode of the monitor.  The window's desired video mode is set again
 *  when the window is restored.
 *
 *  @param[in] window The window to iconify.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup window
 */
WSI_API void sc_wsi_win_iconify(sc_window* window);

/*! @brief 恢复指定窗口。
 *
 *  此函数恢复指定窗口（如果之前被最小化或最大化）。  If the window is already restored, this function
 *  does nothing.
 *
 *  If the specified window is an iconified full screen window, its desired
 *  video mode is set again for its monitor when the window is restored.
 *
 *  @param[in] window The window to restore.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @remark __Wayland:__ Restoring a window from maximization is not currently
 *  part of any common Wayland protocol, so this function can only restore
 *  windows from maximization.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup window
 */
WSI_API void sc_wsi_win_restore(sc_window* window);

/*! @brief 最大化指定窗口。
 *
 *  此函数最大化指定窗口（如果之前未最大化）。  如果窗口已经最大化，此函数不执行任何操作。
 *
 *  If the specified window is a full screen window, this function does nothing.
 *
 *  @param[in] window The window to maximize.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @par Thread Safety
 *  This function may only be called from the main thread.
 *
 *  @ingroup window
 */
WSI_API void sc_wsi_win_maximize(sc_window* window);

/*! @brief 显示指定窗口。
 *
 *  此函数显示指定窗口（如果之前被隐藏）。  If the window is already visible or is in full screen mode, this
 *  function does nothing.
 *
 *  By default, windowed mode windows are focused when shown
 *  Set the [SC_WIN_FOCUS_ON_SHOW](@ref GLFW_FOCUS_ON_SHOW_hint) window hint
 *  to change this behavior for all newly created windows, or change the
 *  behavior for an existing window with @ref sc_wsi_win_set_attrib.
 *
 *  @param[in] window The window to make visible.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @remark __Wayland:__ Because Wayland wants every frame of the desktop to be
 *  complete, this function does not immediately make the window visible.
 *  Instead it will become visible the next time the window framebuffer is
 *  updated after this call.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup window
 */
WSI_API void sc_wsi_win_show(sc_window* window);

/*! @brief 隐藏指定窗口。
 *
 *  此函数隐藏指定窗口（如果之前可见）。  If
 *  the window is already hidden or is in full screen mode, this function does
 *  nothing.
 *
 *  @param[in] window The window to hide.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup window
 */
WSI_API void sc_wsi_win_hide(sc_window* window);

/*! @brief Brings the specified window to front and sets input focus.
 *
 *  This function brings the specified window to front and sets input focus.
 *  窗口应已可见且未最小化。
 *
 *  By default, both windowed and full screen mode windows are focused when
 *  initially created.  Set the [SC_WIN_FOCUSED](@ref GLFW_FOCUSED_hint) to
 *  disable this behavior.
 *
 *  Also by default, windowed mode windows are focused when shown
 *  with @ref sc_wsi_win_show. Set the
 *  [SC_WIN_FOCUS_ON_SHOW](@ref GLFW_FOCUS_ON_SHOW_hint) to disable this behavior.
 *
 *  __Do not use this function__ to steal focus from other applications unless
 *  you are certain that is what the user wants.  Focus stealing can be
 *  extremely disruptive.
 *
 *  For a less disruptive way of getting the user's attention, see
 *  [attention requests](@ref window_attention).
 *
 *  @param[in] window The window to give input focus.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @remark __Wayland:__ The compositor will likely ignore focus requests unless
 *  another window created by the same application already has input focus.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup window
 */
WSI_API void sc_wsi_win_focus(sc_window* window);

/*! @brief 请求用户注意指定窗口。
 *
 *  此函数请求用户注意指定窗口。  On
 *  platforms where this is not supported, attention is requested to the
 *  application as a whole.
 *
 *  Once the user has given attention, usually by focusing the window or
 *  application, the system will end the request automatically.
 *
 *  @param[in] window The window to request attention to.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @remark __macOS:__ Attention is requested to the application as a whole, not the
 *  specific window.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup window
 */
WSI_API void sc_wsi_win_request_attention(sc_window* window);

/*! @brief 返回窗口全屏模式使用的监视器。和设置窗口的模式、监视器、视频模式和位置。
 *
 *  此函数返回指定窗口全屏所在监视器的句柄。
 *
 *  This function sets the monitor that the window uses for full screen mode or,
 *  if the monitor is `NULL`, makes it windowed mode.
 *
 *  When setting a monitor, this function updates the width, height and refresh
 *  rate of the desired video mode and switches to the video mode closest to it.
 *  The window position is ignored when setting a monitor.
 *
 *  When the monitor is `NULL`, the position, width and height are used to
 *  place the window content area.  The refresh rate is ignored when no monitor
 *  is specified.
 *
 *  If you only wish to update the resolution of a full screen window or the
 *  size of a windowed mode window, see @ref sc_wsi_win_set_size.
 *
 *  When a window transitions from full screen to windowed mode, this function
 *  restores any previous window settings such as whether it is decorated,
 *  floating, resizable, has size or aspect ratio limits, etc.
 *
 *  @param[in] window 要查询的窗口。
 *  @return The monitor, or `NULL` if the window is in windowed mode or an
 *  [error](@ref error_handling) occurred.
 *
 *  @param[in] monitor 所需的监视器，或 `NULL` 设置为窗口模式。
 *  @param[in] xpos The desired x-coordinate of the upper-left corner of the
 *  content area.
 *  @param[in] ypos The desired y-coordinate of the upper-left corner of the
 *  content area.
 *  @param[in] width The desired with, in screen coordinates, of the content
 *  area or video mode.
 *  @param[in] height The desired height, in screen coordinates, of the content
 *  area or video mode.
 *  @param[in] refreshRate The desired refresh rate, in Hz, of the video mode,
 *  or `SC_DONT_CARE`.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED。
 *
 *  @remark The OpenGL or OpenGL ES context will not be destroyed or otherwise
 *  affected by any resizing or mode switching, although you may need to update
 *  your viewport if the framebuffer size has changed.
 *
 *  @remark __Wayland:__ Window positions are not currently part of any common
 *  Wayland protocol.  The window position arguments are ignored.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup window
 */
WSI_API sc_monitor* sc_wsi_win_get_monitor(sc_window* window);
WSI_API void sc_wsi_win_set_monitor(sc_window* window, sc_monitor* monitor,
                                    int xpos, int ypos, int width, int height, int refreshRate);

/*! @brief 返回/设置指定窗口的属性。
 *
 *  This function returns the value of an attribute of the specified window or
 *  its OpenGL or OpenGL ES context.
 *
 *  支持的属性有 [SC_WIN_DECORATED](@ref GLFW_DECORATED_attrib)、[SC_WIN_RESIZABLE](@ref GLFW_RESIZABLE_attrib)、[SC_WIN_FLOATING](@ref GLFW_FLOATING_attrib)、[SC_WIN_AUTO_ICONIFY](@ref GLFW_AUTO_ICONIFY_attrib) 和 [SC_WIN_FOCUS_ON_SHOW](@ref GLFW_FOCUS_ON_SHOW_attrib)。
 *  [SC_WIN_MOUSE_PASSTHROUGH](@ref SC_MOUSE_PASSTHROUGH_attrib)
 *
 *  Some of these attributes are ignored for full screen windows.  The new
 *  value will take effect if the window is later made windowed.
 *
 *  Some of these attributes are ignored for windowed mode windows.  The new
 *  value will take effect if the window is later made full screen.
 *
 *
 *  @param[in] window 要查询的窗口。
 *  @param[in] attrib The [window attribute](@ref window_attribs) whose value to
 *  return.
 *  @return The value of the attribute, or zero if an
 *  [error](@ref error_handling) occurred.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED, @ref
 *  SC_WSI_ERR_INVALID_ENUM, @ref SC_WSI_ERR_INVALID_VALUE, @ref SC_WSI_ERR_PLATFORM_ERROR and @ref
 *  SC_WSI_ERR_FEATURE_UNAVAILABLE (see remarks).
 *
 *  @remark Framebuffer related hints are not window attributes.  See @ref
 *  window_attribs_fb for more information.
 *
 *  @remark Zero is a valid value for many window and context related
 *  attributes so you cannot use a return value of zero as an indication of
 *  errors.  However, this function should not fail as long as it is passed
 *  valid arguments and the library has been [initialized](@ref intro_init).
 *
 *  @remark __Wayland:__ Checking whether a window is iconified is not currently
 *  part of any common Wayland protocol, so the @ref SC_WIN_ICONIFIED attribute
 *  cannot be implemented and is always `false`.
 *
 *  @remark Calling @ref sc_wsi_win_get_attrib will always return the latest
 *  value, even if that value is ignored by the current mode of the window.
 *
 *  @remark __Wayland:__ The [SC_WIN_FLOATING](@ref GLFW_FLOATING_attrib) window attribute is
 *  not supported.  Setting this will emit @ref SC_WSI_ERR_FEATURE_UNAVAILABLE.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup window
 */
WSI_API int sc_wsi_win_get_attrib(sc_window* window, int attrib);
WSI_API void sc_wsi_win_set_attrib(sc_window* window, int attrib, int value);

/*! @brief 返回/设置指定窗口的用户指针。
 *
 *  This function returns the current value of the user-defined pointer of the
 *  specified window.  The initial value is `NULL`.
 *
 *  @param[in] window The window whose pointer to return.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED。
 *
 *  @thread_safety 此函数可以从任何线程调用。  Access is not
 *  synchronized.
 *
 *  @ingroup window
 */
WSI_API void* sc_wsi_win_get_user_data(sc_window* window);
WSI_API void sc_wsi_win_set_user_data(sc_window* window, void* pointer);

/*! @brief 返回指定窗口的原生 display 句柄。
 *
 *  例如 X11 的 `Display*`、Wayland 的 `wl_display*`。在不需要 display
 *  句柄的平台返回 `NULL`。
 *
 *  交付含义适配 builtins/gpu 的「平台原生句柄标准」（gpu.h 文件头）：
 *  返回值可直接填入 sc_gpu_surface_desc.native_display。
 *
 *  @param[in] window 要查询的窗口。
 *  @return 原生 display 句柄，若无或失败返回 `NULL`。
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED、@ref SC_WSI_ERR_INVALID_VALUE 与
 *  @ref SC_WSI_ERR_PLATFORM_UNAVAILABLE。
 *
 *  @ingroup window
 */
WSI_API void* sc_wsi_win_get_native_display(sc_window* window);

/*! @brief 返回指定窗口的原生 window/surface 句柄。
 *
 *  例如 Win32 的 `HWND`、Cocoa 的 `NSView*`、X11 的 `Window`（经 `uintptr_t`
 *  转换为指针值）、Wayland 的 `wl_surface*`。
 *
 *  交付含义适配 builtins/gpu 的「平台原生句柄标准」（gpu.h 文件头）：
 *  返回值可直接填入 sc_gpu_surface_desc.native_window。
 *
 *  @param[in] window 要查询的窗口。
 *  @return 原生 window/surface 句柄，失败返回 `NULL`。
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED、@ref SC_WSI_ERR_INVALID_VALUE 与
 *  @ref SC_WSI_ERR_PLATFORM_UNAVAILABLE。
 *
 *  @ingroup window
 */
WSI_API void* sc_wsi_win_get_native_window(sc_window* window);

/*************************************************************************
 * GLFW API functions
 *************************************************************************/


/*! @brief 返回/设置指定窗口的输入选项值。
 *
 *  This function returns the value of an input option for the specified window.
 *  The mode must be one of @ref SC_CURSOR, @ref SC_STICKY_KEYS,
 *  @ref SC_STICKY_MOUSE_BUTTONS, @ref SC_LOCK_KEY_MODS or
 *  @ref SC_RAW_MOUSE_MOTION, or @ref SC_UNLIMITED_MOUSE_BUTTONS.
 *
 *  If the mode is `SC_CURSOR`, the value must be one of the following cursor
 *  modes:
 *  - `SC_CURSOR_NORMAL` makes the cursor visible and behaving normally.
 *  - `SC_CURSOR_HIDDEN` makes the cursor invisible when it is over the
 *    content area of the window but does not restrict the cursor from leaving.
 *  - `SC_CURSOR_DISABLED` hides and grabs the cursor, providing virtual
 *    and unlimited cursor movement.  This is useful for implementing for
 *    example 3D camera controls.
 *  - `SC_CURSOR_CAPTURED` makes the cursor visible and confines it to the
 *    content area of the window.
 *
 *  如果 mode 是 `SC_STICKY_KEYS`，值必须是 `true` 启用粘滞键，或 `false` 禁用。  If sticky keys are
 *  enabled, a key press will ensure that @ref sc_wsi_key returns `SC_PRESS`
 *  the next time it is called even if the key had been released before the
 *  call.  This is useful when you are only interested in whether keys have been
 *  pressed but not when or in which order.
 *
 *  如果 mode 是 `SC_STICKY_MOUSE_BUTTONS`，值必须是 `true` 启用粘滞鼠标按钮，或 `false` 禁用。
 *  If sticky mouse buttons are enabled, a mouse button press will ensure that
 *  @ref sc_wsi_mouse_button returns `SC_PRESS` the next time it is called even
 *  if the mouse button had been released before the call.  This is useful when
 *  you are only interested in whether mouse buttons have been pressed but not
 *  when or in which order.
 *
 *  If the mode is `SC_LOCK_KEY_MODS`, the value must be either `true` to
 *  enable lock key modifier bits, or `false` to disable them.  If enabled,
 *  callbacks that receive modifier bits will also have the @ref
 *  SC_MOD_CAPS_LOCK bit set when the event was generated with Caps Lock on,
 *  and the @ref SC_MOD_NUM_LOCK bit when Num Lock was on.
 *
 *  If the mode is `SC_RAW_MOUSE_MOTION`, the value must be either `true`
 *  to enable raw (unscaled and unaccelerated) mouse motion when the cursor is
 *  disabled, or `false` to disable it.  If raw motion is not supported,
 *  attempting to set this will emit @ref SC_WSI_ERR_FEATURE_UNAVAILABLE.  Call @ref
 *  sc_wsi_mouse_raw_motion_supported to check for support.
 *
 *  If the mode is `SC_UNLIMITED_MOUSE_BUTTONS`, the value must be either
 *  `true` to disable the mouse button limit when calling the mouse button
 *  callback, or `false` to limit the mouse buttons sent to the callback
 *  to the mouse button token values up to `SC_MOUSE_BUTTON_LAST`.
 *
 *  @param[in] window 要查询的窗口。
 *  @param[in] mode One of `SC_CURSOR`, `SC_STICKY_KEYS`,
 *  `SC_STICKY_MOUSE_BUTTONS`, `SC_LOCK_KEY_MODS` or
 *  `SC_RAW_MOUSE_MOTION`.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED, @ref
 *  SC_WSI_ERR_INVALID_ENUM, @ref SC_WSI_ERR_PLATFORM_ERROR and @ref
 *  SC_WSI_ERR_FEATURE_UNAVAILABLE (see above).
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup input
 */
WSI_API int sc_wsi_input_get_mode(sc_window* window, int mode);
WSI_API void sc_wsi_input_set_mode(sc_window* window, int mode, int value);

/*! @brief 返回是否支持原始鼠标运动。
 *
 *  此函数返回当前系统是否支持原始鼠标运动。  This status does not change after GLFW has been initialized so you
 *  only need to check this once.  If you attempt to enable raw motion on
 *  a system that does not support it, @ref SC_WSI_ERR_PLATFORM_ERROR will be emitted.
 *
 *  Raw mouse motion is closer to the actual motion of the mouse across
 *  a surface.  It is not affected by the scaling and acceleration applied to
 *  the motion of the desktop cursor.  That processing is suitable for a cursor
 *  while raw motion is better for controlling for example a 3D camera.  Because
 *  of this, raw mouse motion is only provided when the cursor is disabled.
 *
 *  @return `true` if raw mouse motion is supported on the current machine,
 *  or `false` otherwise.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED。
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup input
 */
WSI_API int sc_wsi_mouse_raw_motion_supported(void);

/*! @brief 返回指定可打印键的布局相关名称。
 *
 *  This function returns the name of the specified printable key, encoded as
 *  UTF-8.  This is typically the character that key would produce without any
 *  modifier keys, intended for displaying key bindings to the user.  For dead
 *  keys, it is typically the diacritic it would add to a character.
 *
 *  __Do not use this function__ for [text input](@ref input_char).  You will
 *  break text input for many languages even if it happens to work for yours.
 *
 *  If the key is `SC_KEY_UNKNOWN`, the scancode is used to identify the key,
 *  otherwise the scancode is ignored.  If you specify a non-printable key, or
 *  `SC_KEY_UNKNOWN` and a scancode that maps to a non-printable key, this
 *  function returns `NULL` but does not emit an error.
 *
 *  This behavior allows you to always pass in the arguments in the
 *  [key callback](@ref input_key) without modification.
 *
 *  The printable keys are:
 *  - `SC_KEY_APOSTROPHE`
 *  - `SC_KEY_COMMA`
 *  - `SC_KEY_MINUS`
 *  - `SC_KEY_PERIOD`
 *  - `SC_KEY_SLASH`
 *  - `SC_KEY_SEMICOLON`
 *  - `SC_KEY_EQUAL`
 *  - `SC_KEY_LEFT_BRACKET`
 *  - `SC_KEY_RIGHT_BRACKET`
 *  - `SC_KEY_BACKSLASH`
 *  - `SC_KEY_WORLD_1`
 *  - `SC_KEY_WORLD_2`
 *  - `SC_KEY_0` to `SC_KEY_9`
 *  - `SC_KEY_A` to `SC_KEY_Z`
 *  - `SC_KEY_KP_0` to `SC_KEY_KP_9`
 *  - `SC_KEY_KP_DECIMAL`
 *  - `SC_KEY_KP_DIVIDE`
 *  - `SC_KEY_KP_MULTIPLY`
 *  - `SC_KEY_KP_SUBTRACT`
 *  - `SC_KEY_KP_ADD`
 *  - `SC_KEY_KP_EQUAL`
 *
 *  Names for printable keys depend on keyboard layout, while names for
 *  non-printable keys are the same across layouts but depend on the application
 *  language and should be localized along with other user interface text.
 *
 *  @param[in] key The key to query, or `SC_KEY_UNKNOWN`.
 *  @param[in] scancode The scancode of the key to query.
 *  @return The UTF-8 encoded, layout-specific name of the key, or `NULL`.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED, @ref
 *  SC_WSI_ERR_INVALID_VALUE, @ref SC_WSI_ERR_INVALID_ENUM and @ref SC_WSI_ERR_PLATFORM_ERROR.
 *
 *  @remark The contents of the returned string may change when a keyboard
 *  layout change event is received.
 *
 *  @pointer_lifetime The returned string is allocated and freed by GLFW.  You
 *  should not free it yourself.  It is valid until the library is terminated.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup input
 */
WSI_API const char* sc_wsi_key_name(int key, int scancode);

/*! @brief 返回指定键的平台相关扫描码。
 *
 *  此函数返回指定键的平台相关扫描码。
 *
 *  If the specified [key token](@ref keys) corresponds to a physical key not
 *  supported on the current platform then this method will return `-1`.
 *  Calling this function with anything other than a key token will return `-1`
 *  and generate a @ref SC_WSI_ERR_INVALID_ENUM error.
 *
 *  @param[in] key Any [key token](@ref keys).
 *  @return The platform-specific scancode for the key, or `-1` if the key is
 *  not supported on the current platform or an [error](@ref error_handling)
 *  occurred.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_INVALID_ENUM。
 *
 *  @thread_safety 此函数可以从任何线程调用。
 *
 *  @ingroup input
 */
WSI_API int sc_wsi_key_scancode(int key);

/*! @brief Returns the last reported state of a keyboard key for the specified
 *  window.
 *
 *  此函数返回指定窗口中指定按键的最后报告状态。  返回的状态是 `SC_PRESS` 或 `SC_RELEASE` 之一。  The action `SC_REPEAT` is only reported to the key callback.
 *
 *  If the @ref SC_STICKY_KEYS input mode is enabled, this function returns
 *  `SC_PRESS` the first time you call it for a key that was pressed, even if
 *  that key has already been released.
 *
 *  The key functions deal with physical keys, with [key tokens](@ref keys)
 *  named after their use on the standard US keyboard layout.  If you want to
 *  input text, use the Unicode character callback instead.
 *
 *  The [modifier key bit masks](@ref mods) are not key tokens and cannot be
 *  used with this function.
 *
 *  __Do not use this function__ to implement [text input](@ref input_char).
 *
 *  @param[in] window 目标窗口。
 *  @param[in] key 所需的 [键盘按键](@ref keys)。  `SC_KEY_UNKNOWN` is
 *  not a valid key for this function.
 *  @return One of `SC_PRESS` or `SC_RELEASE`.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_INVALID_ENUM。
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup input
 */
WSI_API int sc_wsi_key(sc_window* window, int key);

/*! @brief Returns the last reported state of a mouse button for the specified
 *  window.
 *
 *  此函数返回指定窗口中指定鼠标按钮的最后报告状态。  返回的状态是 `SC_PRESS` 或 `SC_RELEASE` 之一。
 *
 *  If the @ref SC_STICKY_MOUSE_BUTTONS input mode is enabled, this function
 *  returns `SC_PRESS` the first time you call it for a mouse button that was
 *  pressed, even if that mouse button has already been released.
 *
 *  The @ref SC_UNLIMITED_MOUSE_BUTTONS input mode does not effect the
 *  limit on buttons which can be polled with this function.
 *
 *  @param[in] window 目标窗口。
 *  @param[in] button The desired [mouse button token](@ref buttons).
 *  @return One of `SC_PRESS` or `SC_RELEASE`.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_INVALID_ENUM。
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup input
 */
WSI_API int sc_wsi_mouse_button(sc_window* window, int button);

/*! @brief Retrieves the position of the cursor relative to the content area of
 *  the window.
 *
 *  此函数返回光标相对于指定窗口内容区域左上角的位置（屏幕坐标）。
 *
 *  如果光标被禁用（`SC_CURSOR_DISABLED`），光标位置不受限制，仅受 `double` 类型的最小值和最大值限制。
 *
 *  The coordinate can be converted to their integer equivalents with the
 *  `floor` function.  Casting directly to an integer type works for positive
 *  coordinates, but fails for negative ones.
 *
 *  位置参数中任何一个或全部都可以是 `NULL`。  If an error occurs, all
 *  non-`NULL` position arguments will be set to zero.
 *
 *  @param[in] window 目标窗口。
 *  @param[out] xpos Where to store the cursor x-coordinate, relative to the
 *  left edge of the content area, or `NULL`.
 *  @param[out] ypos Where to store the cursor y-coordinate, relative to the to
 *  top edge of the content area, or `NULL`.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup input
 */
WSI_API void sc_wsi_get_cursor_pos(sc_window* window, double* xpos, double* ypos);

/*! @brief Sets the position of the cursor, relative to the content area of the
 *  window.
 *
 *  此函数设置光标相对于指定窗口内容区域左上角的位置（屏幕坐标）。  窗口必须有输入焦点。  If the window does not have
 *  input focus when this function is called, it fails silently.
 *
 *  __Do not use this function__ to implement things like camera controls.  GLFW
 *  already provides the `SC_CURSOR_DISABLED` cursor mode that hides the
 *  cursor, transparently re-centers it and provides unconstrained cursor
 *  motion.  See @ref sc_wsi_input_set_mode for more information.
 *
 *  If the cursor mode is `SC_CURSOR_DISABLED` then the cursor position is
 *  unconstrained and limited only by the minimum and maximum values of
 *  a `double`.
 *
 *  @param[in] window 目标窗口。
 *  @param[in] xpos The desired x-coordinate, relative to the left edge of the
 *  content area.
 *  @param[in] ypos The desired y-coordinate, relative to the top edge of the
 *  content area.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED, @ref
 *  SC_WSI_ERR_PLATFORM_ERROR and @ref SC_WSI_ERR_FEATURE_UNAVAILABLE (see remarks).
 *
 *  @remark __Wayland:__ This function will only work when the cursor mode is
 *  `SC_CURSOR_DISABLED`, otherwise it will emit @ref SC_WSI_ERR_FEATURE_UNAVAILABLE.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup input
 */
WSI_API void sc_wsi_set_cursor_pos(sc_window* window, double xpos, double ypos);

/*! @brief 创建自定义光标。
 *
 *  Creates a new custom cursor image that can be set for a window with @ref
 *  sc_wsi_cursor_set.  The cursor can be destroyed with @ref sc_wsi_destroy_cursor.
 *  Any remaining cursors are destroyed by @ref sc_wsi_terminate.
 *
 *  The pixels are 32-bit, little-endian, non-premultiplied RGBA, i.e. eight
 *  bits per channel with the red channel first.  They are arranged canonically
 *  as packed sequential rows, starting from the top-left corner.
 *
 *  The cursor hotspot is specified in pixels, relative to the upper-left corner
 *  of the cursor image.  Like all other coordinate systems in GLFW, the X-axis
 *  points to the right and the Y-axis points down.
 *
 *  @param[in] image 所需的光标图像。
 *  @param[in] xhot The desired x-coordinate, in pixels, of the cursor hotspot.
 *  @param[in] yhot The desired y-coordinate, in pixels, of the cursor hotspot.
 *  @return The handle of the created cursor, or `NULL` if an
 *  [error](@ref error_handling) occurred.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED、@ref SC_WSI_ERR_INVALID_VALUE 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @pointer_lifetime The specified image data is copied before this function
 *  returns.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup input
 */
WSI_API sc_cursor* sc_wsi_create_cursor(const GLFWimage* image, int xhot, int yhot);

/*! @brief Creates a cursor with a standard shape.
 *
 *  Returns a cursor with a standard shape, that can be set for a window with
 *  @ref sc_wsi_cursor_set.  The images for these cursors come from the system
 *  cursor theme and their exact appearance will vary between platforms.
 *
 *  Most of these shapes are guaranteed to exist on every supported platform but
 *  a few may not be present.  See the table below for details.
 *
 *  Cursor shape                   | Windows | macOS | X11    | Wayland
 *  ------------------------------ | ------- | ----- | ------ | -------
 *  @ref SC_ARROW_CURSOR         | Yes     | Yes   | Yes    | Yes
 *  @ref SC_IBEAM_CURSOR         | Yes     | Yes   | Yes    | Yes
 *  @ref SC_CROSSHAIR_CURSOR     | Yes     | Yes   | Yes    | Yes
 *  @ref SC_POINTING_HAND_CURSOR | Yes     | Yes   | Yes    | Yes
 *  @ref SC_RESIZE_EW_CURSOR     | Yes     | Yes   | Yes    | Yes
 *  @ref SC_RESIZE_NS_CURSOR     | Yes     | Yes   | Yes    | Yes
 *  @ref SC_RESIZE_NWSE_CURSOR   | Yes     | Yes<sup>1</sup> | Maybe<sup>2</sup> | Maybe<sup>2</sup>
 *  @ref SC_RESIZE_NESW_CURSOR   | Yes     | Yes<sup>1</sup> | Maybe<sup>2</sup> | Maybe<sup>2</sup>
 *  @ref SC_RESIZE_ALL_CURSOR    | Yes     | Yes   | Yes    | Yes
 *  @ref SC_NOT_ALLOWED_CURSOR   | Yes     | Yes   | Maybe<sup>2</sup> | Maybe<sup>2</sup>
 *
 *  1) This uses a private system API and may fail in the future.
 *
 *  2) This uses a newer standard that not all cursor themes support.
 *
 *  If the requested shape is not available, this function emits a @ref
 *  SC_WSI_ERR_CURSOR_UNAVAILABLE error and returns `NULL`.
 *
 *  @param[in] shape One of the [standard shapes](@ref shapes).
 *  @return A new cursor ready to use or `NULL` if an
 *  [error](@ref error_handling) occurred.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED, @ref
 *  SC_WSI_ERR_INVALID_ENUM, @ref SC_WSI_ERR_CURSOR_UNAVAILABLE and @ref
 *  SC_WSI_ERR_PLATFORM_ERROR.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup input
 */
WSI_API sc_cursor* sc_wsi_create_standard_cursor(int shape);

/*! @brief 销毁光标。
 *
 *  此函数销毁之前通过 @ref sc_wsi_create_cursor 创建的光标。  Any remaining cursors will be destroyed by @ref
 *  sc_wsi_terminate.
 *
 *  If the specified cursor is current for any window, that window will be
 *  reverted to the default cursor.  This does not affect the cursor mode.
 *
 *  @param[in] cursor The cursor object to destroy.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @reentrancy 此函数禁止在回调中调用。
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup input
 */
WSI_API void sc_wsi_destroy_cursor(sc_cursor* cursor);

/*! @brief 设置窗口的光标。
 *
 *  此函数设置光标在指定窗口内容区域上方时使用的光标图像。  The set cursor will only be visible
 *  when the [cursor mode](@ref cursor_mode) of the window is
 *  `SC_CURSOR_NORMAL`.
 *
 *  在某些平台上，除非窗口有输入焦点，否则设置的光标可能不可见。
 *
 *  @param[in] window 要设置光标的窗口。
 *  @param[in] cursor 要设置的光标，或 `NULL` 切换回默认箭头光标。
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup input
 */
WSI_API void sc_wsi_cursor_set(sc_window* window, sc_cursor* cursor);


/*! @brief 以字符串形式返回剪贴板内容、将剪贴板设置为指定字符串。
 *
 *  This function sets the system clipboard to the specified, UTF-8 encoded
 *  string.
 *
 *  @param[in] window Deprecated.  Any valid window or `NULL`.
 *  @param[in] string A UTF-8 encoded string.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED 和 @ref SC_WSI_ERR_PLATFORM_ERROR。
 *
 *  @remark __Win32:__ The clipboard on Windows has a single global lock for reading and
 *  writing.  GLFW tries to acquire it a few times, which is almost always enough.  If it
 *  cannot acquire the lock then this function emits @ref SC_WSI_ERR_PLATFORM_ERROR and returns.
 *  It is safe to try this multiple times.
 *
 *  @pointer_lifetime The returned string is allocated and freed by GLFW.  You
 *  should not free it yourself.  It is valid until the next call to @ref
 *  sc_wsi_clipboard_get_string or @ref sc_wsi_clipboard_set_string, or until the library
 *  is terminated.
 *
 *  @thread_safety 此函数必须在主线程中调用。
 *
 *  @ingroup input
 */
WSI_API const char* sc_wsi_clipboard_get_string(sc_window* window);
WSI_API void sc_wsi_clipboard_set_string(sc_window* window, const char* string);

/*! @brief Returns/Sets the time.
 *
 *  This function sets the current GLFW time, in seconds.  The value must be
 *  a positive finite number less than or equal to 18446744073.0, which is
 *  approximately 584.5 years.
 *
 *  This function returns the current GLFW time, in seconds.  Unless the time
 *  has been set using @ref sc_wsi_set_time it measures time elapsed since GLFW was
 *  initialized.
 *
 *  This function and @ref sc_wsi_set_time are helper functions on top of @ref
 *  sc_wsi_timer_frequency and @ref sc_wsi_timer_value.
 *
 *  The resolution of the timer is system dependent, but is usually on the order
 *  of a few micro- or nanoseconds.  It uses the highest-resolution monotonic
 *  time source on each operating system.
 *
 *  @return The current time, in seconds, or zero if an
 *  [error](@ref error_handling) occurred.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED。
 *
 *  @remark The upper limit of GLFW time is calculated as
 *  floor((2<sup>64</sup> - 1) / 10<sup>9</sup>) and is due to implementations
 *  storing nanoseconds in 64 bits.  The limit may be increased in the future.
 *
 *  @thread_safety 此函数可以从任何线程调用。  Reading and
 *  writing of the internal base time is not atomic, so it needs to be
 *  externally synchronized with calls to @ref sc_wsi_set_time.
 *
 *  @ingroup input
 */
WSI_API double sc_wsi_get_time(void);
WSI_API void sc_wsi_set_time(double time);

/*! @brief Returns the current value of the raw timer.
 *
 *  This function returns the current value of the raw timer, measured in
 *  1&nbsp;/&nbsp;frequency seconds.  To get the frequency, call @ref
 *  sc_wsi_timer_frequency.
 *
 *  @return The value of the timer, or zero if an
 *  [error](@ref error_handling) occurred.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED。
 *
 *  @thread_safety 此函数可以从任何线程调用。
 *
 *  @ingroup input
 */
WSI_API uint64_t sc_wsi_timer_value(void);

/*! @brief Returns the frequency, in Hz, of the raw timer.
 *
 *  此函数返回原始计时器的频率（Hz）。
 *
 *  @return The frequency of the timer, in Hz, or zero if an
 *  [error](@ref error_handling) occurred.
 *
 *  @errors 可能的错误包括 @ref SC_WSI_ERR_NOT_INITIALIZED。
 *
 *  @thread_safety 此函数可以从任何线程调用。
 *
 *  @ingroup input
 */
WSI_API uint64_t sc_wsi_timer_frequency(void);

/*************************************************************************
 * Global definition cleanup
 *************************************************************************/

/* ------------------- BEGIN SYSTEM/COMPILER SPECIFIC -------------------- */

#ifdef SC_WINGDIAPI_DEFINED
 #undef WINGDIAPI
 #undef SC_WINGDIAPI_DEFINED
#endif

#ifdef SC_CALLBACK_DEFINED
 #undef CALLBACK
 #undef SC_CALLBACK_DEFINED
#endif

/* -------------------- END SYSTEM/COMPILER SPECIFIC --------------------- */

#ifdef __cplusplus
}
#endif

#endif /* wsi_h_ */

