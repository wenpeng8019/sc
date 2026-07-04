/* ============================================================
 * win.h —— sc win 公共类型定义
 *
 * 内部实现使用 GLFW 风格的 CamelCase 命名（SC_WIN_window 等）。
 * sc FFI 层通过 @fnc 使用 snake_case（sc_win_create_window 等）。
 * ============================================================ */
#ifndef SC_WIN_H
#define SC_WIN_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 不透明句柄 ---------- */
typedef struct _SC_WINwindow  SC_WIN_window;
typedef struct _SC_WINmonitor SC_WIN_monitor;
typedef struct _SC_WINcursor  SC_WIN_cursor;

/* ---------- 结构体 ---------- */
typedef struct { int width,height; int redBits,greenBits,blueBits; int refreshRate; } SC_WIN_vidmode;
typedef struct { uint16_t* red; uint16_t* green; uint16_t* blue; unsigned int size; } SC_WIN_gammaramp;
typedef struct { int width,height; unsigned char* pixels; } SC_WIN_image;
typedef struct {
    void* (*allocate)(size_t sz, void* user);
    void* (*reallocate)(void* block, size_t sz, void* user);
    void  (*deallocate)(void* block, void* user);
    void* user;
} SC_WIN_allocator;

/* ---------- 基础常量 ---------- */
#define SC_WIN_TRUE   1
#define SC_WIN_FALSE  0
#define SC_WIN_PRESS   1
#define SC_WIN_RELEASE 0
#define SC_WIN_REPEAT  2
#define SC_WIN_DONT_CARE -1

/* ---------- 键盘 ---------- */
#define SC_WIN_KEY_UNKNOWN       -1
#define SC_WIN_KEY_SPACE         32
#define SC_WIN_KEY_APOSTROPHE    39
#define SC_WIN_KEY_COMMA         44
#define SC_WIN_KEY_PERIOD        46
#define SC_WIN_KEY_SLASH         47
#define SC_WIN_KEY_0    48
#define SC_WIN_KEY_1    49
#define SC_WIN_KEY_2    50
#define SC_WIN_KEY_3    51
#define SC_WIN_KEY_4    52
#define SC_WIN_KEY_5    53
#define SC_WIN_KEY_6    54
#define SC_WIN_KEY_7    55
#define SC_WIN_KEY_8    56
#define SC_WIN_KEY_9    57
#define SC_WIN_KEY_A    65
#define SC_WIN_KEY_B    66
#define SC_WIN_KEY_C    67
#define SC_WIN_KEY_D    68
#define SC_WIN_KEY_E    69
#define SC_WIN_KEY_F    70
#define SC_WIN_KEY_G    71
#define SC_WIN_KEY_H    72
#define SC_WIN_KEY_I    73
#define SC_WIN_KEY_J    74
#define SC_WIN_KEY_K    75
#define SC_WIN_KEY_L    76
#define SC_WIN_KEY_M    77
#define SC_WIN_KEY_N    78
#define SC_WIN_KEY_O    79
#define SC_WIN_KEY_P    80
#define SC_WIN_KEY_Q    81
#define SC_WIN_KEY_R    82
#define SC_WIN_KEY_S    83
#define SC_WIN_KEY_T    84
#define SC_WIN_KEY_U    85
#define SC_WIN_KEY_V    86
#define SC_WIN_KEY_W    87
#define SC_WIN_KEY_X    88
#define SC_WIN_KEY_Y    89
#define SC_WIN_KEY_Z    90
#define SC_WIN_KEY_ESCAPE       256
#define SC_WIN_KEY_ENTER        257
#define SC_WIN_KEY_TAB          258
#define SC_WIN_KEY_BACKSPACE    259
#define SC_WIN_KEY_INSERT       260
#define SC_WIN_KEY_DELETE       261
#define SC_WIN_KEY_RIGHT        262
#define SC_WIN_KEY_LEFT         263
#define SC_WIN_KEY_DOWN         264
#define SC_WIN_KEY_UP           265
#define SC_WIN_KEY_PAGE_UP      266
#define SC_WIN_KEY_PAGE_DOWN    267
#define SC_WIN_KEY_HOME         268
#define SC_WIN_KEY_END          269
#define SC_WIN_KEY_CAPS_LOCK    280
#define SC_WIN_KEY_SCROLL_LOCK  281
#define SC_WIN_KEY_NUM_LOCK     282
#define SC_WIN_KEY_PRINT_SCREEN 283
#define SC_WIN_KEY_PAUSE        284
#define SC_WIN_KEY_F1   290
#define SC_WIN_KEY_F2   291
#define SC_WIN_KEY_F3   292
#define SC_WIN_KEY_F4   293
#define SC_WIN_KEY_F5   294
#define SC_WIN_KEY_F6   295
#define SC_WIN_KEY_F7   296
#define SC_WIN_KEY_F8   297
#define SC_WIN_KEY_F9   298
#define SC_WIN_KEY_F10  299
#define SC_WIN_KEY_F11  300
#define SC_WIN_KEY_F12  301
#define SC_WIN_KEY_F13  302
#define SC_WIN_KEY_F14  303
#define SC_WIN_KEY_F15  304
#define SC_WIN_KEY_F16  305
#define SC_WIN_KEY_F17  306
#define SC_WIN_KEY_F18  307
#define SC_WIN_KEY_F19  308
#define SC_WIN_KEY_F20  309
#define SC_WIN_KEY_F21  310
#define SC_WIN_KEY_F22  311
#define SC_WIN_KEY_F23  312
#define SC_WIN_KEY_F24  313
#define SC_WIN_KEY_F25  314
#define SC_WIN_KEY_KP_0        320
#define SC_WIN_KEY_KP_1        321
#define SC_WIN_KEY_KP_2        322
#define SC_WIN_KEY_KP_3        323
#define SC_WIN_KEY_KP_4        324
#define SC_WIN_KEY_KP_5        325
#define SC_WIN_KEY_KP_6        326
#define SC_WIN_KEY_KP_7        327
#define SC_WIN_KEY_KP_8        328
#define SC_WIN_KEY_KP_9        329
#define SC_WIN_KEY_KP_DECIMAL  330
#define SC_WIN_KEY_KP_DIVIDE   331
#define SC_WIN_KEY_KP_MULTIPLY 332
#define SC_WIN_KEY_KP_SUBTRACT 333
#define SC_WIN_KEY_KP_ADD      334
#define SC_WIN_KEY_KP_ENTER    335
#define SC_WIN_KEY_KP_EQUAL    336
#define SC_WIN_KEY_LEFT_SHIFT      340
#define SC_WIN_KEY_LEFT_CONTROL    341
#define SC_WIN_KEY_LEFT_ALT        342
#define SC_WIN_KEY_LEFT_SUPER      343
#define SC_WIN_KEY_RIGHT_SHIFT     344
#define SC_WIN_KEY_RIGHT_CONTROL   345
#define SC_WIN_KEY_RIGHT_ALT       346
#define SC_WIN_KEY_RIGHT_SUPER     347
#define SC_WIN_KEY_MENU            348
#define SC_WIN_KEY_LAST            SC_WIN_KEY_MENU

/* ---------- 修饰键 ---------- */
#define SC_WIN_MOD_SHIFT     0x0001
#define SC_WIN_MOD_CONTROL   0x0002
#define SC_WIN_MOD_ALT       0x0004
#define SC_WIN_MOD_SUPER     0x0008
#define SC_WIN_MOD_CAPS_LOCK 0x0010
#define SC_WIN_MOD_NUM_LOCK  0x0020

/* ---------- 鼠标 ---------- */
#define SC_WIN_MOUSE_BUTTON_1  0
#define SC_WIN_MOUSE_BUTTON_2  1
#define SC_WIN_MOUSE_BUTTON_3  2
#define SC_WIN_MOUSE_BUTTON_4  3
#define SC_WIN_MOUSE_BUTTON_5  4
#define SC_WIN_MOUSE_BUTTON_6  5
#define SC_WIN_MOUSE_BUTTON_7  6
#define SC_WIN_MOUSE_BUTTON_8  7
#define SC_WIN_MOUSE_BUTTON_LAST   SC_WIN_MOUSE_BUTTON_8
#define SC_WIN_MOUSE_BUTTON_LEFT   SC_WIN_MOUSE_BUTTON_1
#define SC_WIN_MOUSE_BUTTON_RIGHT  SC_WIN_MOUSE_BUTTON_2
#define SC_WIN_MOUSE_BUTTON_MIDDLE SC_WIN_MOUSE_BUTTON_3

/* ---------- 手柄 ---------- */
#define SC_WIN_JOYSTICK_1    0
#define SC_WIN_JOYSTICK_2    1
#define SC_WIN_JOYSTICK_3    2
#define SC_WIN_JOYSTICK_4    3
#define SC_WIN_JOYSTICK_5    4
#define SC_WIN_JOYSTICK_6    5
#define SC_WIN_JOYSTICK_7    6
#define SC_WIN_JOYSTICK_8    7
#define SC_WIN_JOYSTICK_9    8
#define SC_WIN_JOYSTICK_10   9
#define SC_WIN_JOYSTICK_11   10
#define SC_WIN_JOYSTICK_12   11
#define SC_WIN_JOYSTICK_13   12
#define SC_WIN_JOYSTICK_14   13
#define SC_WIN_JOYSTICK_15   14
#define SC_WIN_JOYSTICK_16   15
#define SC_WIN_JOYSTICK_LAST SC_WIN_JOYSTICK_16

/* ---------- 窗口提示 ---------- */
#define SC_WIN_FOCUSED                0x00020001
#define SC_WIN_ICONIFIED              0x00020002
#define SC_WIN_RESIZABLE              0x00020003
#define SC_WIN_VISIBLE                0x00020004
#define SC_WIN_DECORATED              0x00020005
#define SC_WIN_AUTO_ICONIFY           0x00020006
#define SC_WIN_FLOATING               0x00020007
#define SC_WIN_MAXIMIZED              0x00020008
#define SC_WIN_CENTER_CURSOR          0x00020009
#define SC_WIN_TRANSPARENT_FRAMEBUFFER 0x0002000A
#define SC_WIN_HOVERED                0x0002000B
#define SC_WIN_FOCUS_ON_SHOW          0x0002000C
#define SC_WIN_MOUSE_PASSTHROUGH      0x0002000D
#define SC_WIN_SCALE_TO_MONITOR       0x0002000E
#define SC_WIN_RED_BITS              0x00021001
#define SC_WIN_GREEN_BITS            0x00021002
#define SC_WIN_BLUE_BITS             0x00021003
#define SC_WIN_ALPHA_BITS            0x00021004
#define SC_WIN_DEPTH_BITS            0x00021005
#define SC_WIN_STENCIL_BITS          0x00021006
#define SC_WIN_ACCUM_RED_BITS        0x00021007
#define SC_WIN_ACCUM_GREEN_BITS      0x00021008
#define SC_WIN_ACCUM_BLUE_BITS       0x00021009
#define SC_WIN_ACCUM_ALPHA_BITS      0x0002100A
#define SC_WIN_AUX_BUFFERS           0x0002100B
#define SC_WIN_STEREO                0x0002100C
#define SC_WIN_SAMPLES               0x0002100D
#define SC_WIN_SRGB_CAPABLE          0x0002100E
#define SC_WIN_REFRESH_RATE          0x0002100F
#define SC_WIN_DOUBLEBUFFER          0x00021010
#define SC_WIN_CURSOR_HINT            0x00033001
#define SC_WIN_STICKY_KEYS           0x00033002
#define SC_WIN_STICKY_MOUSE_BUTTONS  0x00033003
#define SC_WIN_LOCK_KEY_MODS         0x00033004
#define SC_WIN_RAW_MOUSE_MOTION      0x00033005
#define SC_WIN_CURSOR_NORMAL         0x00034001
#define SC_WIN_CURSOR_HIDDEN         0x00034002
#define SC_WIN_CURSOR_DISABLED       0x00034003
#define SC_WIN_ARROW_CURSOR          0x00036001
#define SC_WIN_IBEAM_CURSOR          0x00036002
#define SC_WIN_CROSSHAIR_CURSOR      0x00036003
#define SC_WIN_POINTING_HAND_CURSOR  0x00036004
#define SC_WIN_RESIZE_EW_CURSOR      0x00036005
#define SC_WIN_RESIZE_NS_CURSOR      0x00036006
#define SC_WIN_RESIZE_NWSE_CURSOR    0x00036007
#define SC_WIN_RESIZE_NESW_CURSOR    0x00036008
#define SC_WIN_RESIZE_ALL_CURSOR     0x00036009
#define SC_WIN_NOT_ALLOWED_CURSOR    0x0003600A

#define SC_WIN_COCOA_FRAME_NAME         0x00023001
#define SC_WIN_COCOA_GRAPHICS_SWITCHING 0x00023003
#define SC_WIN_X11_CLASS_NAME           0x00024001
#define SC_WIN_X11_INSTANCE_NAME        0x00024002
#define SC_WIN_WAYLAND_APP_ID           0x00026001
#define SC_WIN_PLATFORM                 0x00050001

/* ---------- 回调类型 ---------- */
typedef void (*SC_WIN_errorfun)(int,const char*);
typedef void (*SC_WIN_windowposfun)(SC_WIN_window*,int,int);
typedef void (*SC_WIN_windowsizefun)(SC_WIN_window*,int,int);
typedef void (*SC_WIN_windowclosefun)(SC_WIN_window*);
typedef void (*SC_WIN_windowrefreshfun)(SC_WIN_window*);
typedef void (*SC_WIN_windowfocusfun)(SC_WIN_window*,int);
typedef void (*SC_WIN_windowiconifyfun)(SC_WIN_window*,int);
typedef void (*SC_WIN_windowmaximizefun)(SC_WIN_window*,int);
typedef void (*SC_WIN_framebuffersizefun)(SC_WIN_window*,int,int);
typedef void (*SC_WIN_windowcontentscalefun)(SC_WIN_window*,float,float);
typedef void (*SC_WIN_keyfun)(SC_WIN_window*,int,int,int,int);
typedef void (*SC_WIN_charfun)(SC_WIN_window*,unsigned int);
typedef void (*SC_WIN_charmodsfun)(SC_WIN_window*,unsigned int,int);
typedef void (*SC_WIN_mousebuttonfun)(SC_WIN_window*,int,int,int);
typedef void (*SC_WIN_cursorposfun)(SC_WIN_window*,double,double);
typedef void (*SC_WIN_cursorenterfun)(SC_WIN_window*,int);
typedef void (*SC_WIN_scrollfun)(SC_WIN_window*,double,double);
typedef void (*SC_WIN_dropfun)(SC_WIN_window*,int,const char**);
typedef void (*SC_WIN_joystickfun)(int,int);
typedef void (*SC_WIN_monitorfun)(SC_WIN_monitor*,int);

#ifdef __cplusplus
}
#endif
#endif
