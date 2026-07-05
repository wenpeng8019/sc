
// We don't need all the fancy stuff
#ifndef NOMINMAX
 #define NOMINMAX
#endif

#ifndef VC_EXTRALEAN
 #define VC_EXTRALEAN
#endif

#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif

// This is a workaround for the fact that glfw3.h needs to export APIENTRY (for
// example to allow applications to correctly declare a GL_KHR_debug callback)
// but windows.h assumes no one will define APIENTRY before it does
#undef APIENTRY

// GLFW on Windows is Unicode only and does not work in MBCS mode
#ifndef UNICODE
 #define UNICODE
#endif

// GLFW requires Windows 7 or later
#if WINVER < 0x0601
 #undef WINVER
 #define WINVER 0x0601
#endif
#if _WIN32_WINNT < 0x0601
 #undef _WIN32_WINNT
 #define _WIN32_WINNT 0x0601
#endif

// GLFW uses DirectInput8 interfaces
#define DIRECTINPUT_VERSION 0x0800

// GLFW uses OEM cursor resources
#define OEMRESOURCE

#include <wctype.h>
#include <windows.h>
#include <dwmapi.h>
#include <dinput.h>
#include <xinput.h>
#include <dbt.h>

// HACK: Define macros that some windows.h variants don't
#ifndef WM_COPYGLOBALDATA
 #define WM_COPYGLOBALDATA 0x0049
#endif
#ifndef WM_DPICHANGED
 #define WM_DPICHANGED 0x02E0
#endif
#ifndef EDS_ROTATEDMODE
 #define EDS_ROTATEDMODE 0x00000004
#endif
#ifndef _WIN32_WINNT_WINBLUE
 #define _WIN32_WINNT_WINBLUE 0x0603
#endif
#ifndef _WIN32_WINNT_WIN8
 #define _WIN32_WINNT_WIN8 0x0602
#endif
#ifndef WM_GETDPISCALEDSIZE
 #define WM_GETDPISCALEDSIZE 0x02e4
#endif
#ifndef USER_DEFAULT_SCREEN_DPI
 #define USER_DEFAULT_SCREEN_DPI 96
#endif

#ifndef DPI_ENUMS_DECLARED
typedef enum
{
    PROCESS_DPI_UNAWARE = 0,
    PROCESS_SYSTEM_DPI_AWARE = 1,
    PROCESS_PER_MONITOR_DPI_AWARE = 2
} PROCESS_DPI_AWARENESS;
typedef enum
{
    MDT_EFFECTIVE_DPI = 0,
    MDT_ANGULAR_DPI = 1,
    MDT_RAW_DPI = 2,
    MDT_DEFAULT = MDT_EFFECTIVE_DPI
} MONITOR_DPI_TYPE;
#endif /*DPI_ENUMS_DECLARED*/

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE) -4)
#endif /*DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2*/

// Replacement for versionhelpers.h macros, as we cannot rely on the
// application having a correct embedded manifest
//
#define IsWindows8OrGreater()                                         \
    _glfwIsWindowsVersionOrGreaterWin32(HIBYTE(_WIN32_WINNT_WIN8),    \
                                        LOBYTE(_WIN32_WINNT_WIN8), 0)
#define IsWindows8Point1OrGreater()                                   \
    _glfwIsWindowsVersionOrGreaterWin32(HIBYTE(_WIN32_WINNT_WINBLUE), \
                                        LOBYTE(_WIN32_WINNT_WINBLUE), 0)

// Windows 10 Anniversary Update
#define _glfwIsWindows10Version1607OrGreaterWin32() \
    _glfwIsWindows10BuildOrGreaterWin32(14393)
// Windows 10 Creators Update
#define _glfwIsWindows10Version1703OrGreaterWin32() \
    _glfwIsWindows10BuildOrGreaterWin32(15063)

// HACK: Define macros that some xinput.h variants don't
#ifndef XINPUT_CAPS_WIRELESS
 #define XINPUT_CAPS_WIRELESS 0x0002
#endif
#ifndef XINPUT_DEVSUBTYPE_WHEEL
 #define XINPUT_DEVSUBTYPE_WHEEL 0x02
#endif
#ifndef XINPUT_DEVSUBTYPE_ARCADE_STICK
 #define XINPUT_DEVSUBTYPE_ARCADE_STICK 0x03
#endif
#ifndef XINPUT_DEVSUBTYPE_FLIGHT_STICK
 #define XINPUT_DEVSUBTYPE_FLIGHT_STICK 0x04
#endif
#ifndef XINPUT_DEVSUBTYPE_DANCE_PAD
 #define XINPUT_DEVSUBTYPE_DANCE_PAD 0x05
#endif
#ifndef XINPUT_DEVSUBTYPE_GUITAR
 #define XINPUT_DEVSUBTYPE_GUITAR 0x06
#endif
#ifndef XINPUT_DEVSUBTYPE_DRUM_KIT
 #define XINPUT_DEVSUBTYPE_DRUM_KIT 0x08
#endif
#ifndef XINPUT_DEVSUBTYPE_ARCADE_PAD
 #define XINPUT_DEVSUBTYPE_ARCADE_PAD 0x13
#endif
#ifndef XUSER_MAX_COUNT
 #define XUSER_MAX_COUNT 4
#endif

// HACK: Define macros that some dinput.h variants don't
#ifndef DIDFT_OPTIONAL
 #define DIDFT_OPTIONAL 0x80000000
#endif

#define WGL_NUMBER_PIXEL_FORMATS_ARB 0x2000
#define WGL_SUPPORT_OPENGL_ARB 0x2010
#define WGL_DRAW_TO_WINDOW_ARB 0x2001
#define WGL_PIXEL_TYPE_ARB 0x2013
#define WGL_TYPE_RGBA_ARB 0x202b
#define WGL_ACCELERATION_ARB 0x2003
#define WGL_NO_ACCELERATION_ARB 0x2025
#define WGL_RED_BITS_ARB 0x2015
#define WGL_RED_SHIFT_ARB 0x2016
#define WGL_GREEN_BITS_ARB 0x2017
#define WGL_GREEN_SHIFT_ARB 0x2018
#define WGL_BLUE_BITS_ARB 0x2019
#define WGL_BLUE_SHIFT_ARB 0x201a
#define WGL_ALPHA_BITS_ARB 0x201b
#define WGL_ALPHA_SHIFT_ARB 0x201c
#define WGL_ACCUM_BITS_ARB 0x201d
#define WGL_ACCUM_RED_BITS_ARB 0x201e
#define WGL_ACCUM_GREEN_BITS_ARB 0x201f
#define WGL_ACCUM_BLUE_BITS_ARB 0x2020
#define WGL_ACCUM_ALPHA_BITS_ARB 0x2021
#define WGL_DEPTH_BITS_ARB 0x2022
#define WGL_STENCIL_BITS_ARB 0x2023
#define WGL_AUX_BUFFERS_ARB 0x2024
#define WGL_STEREO_ARB 0x2012
#define WGL_DOUBLE_BUFFER_ARB 0x2011
#define WGL_SAMPLES_ARB 0x2042
#define WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB 0x20a9
#define WGL_CONTEXT_DEBUG_BIT_ARB 0x00000001
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x00000002
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_FLAGS_ARB 0x2094
#define WGL_CONTEXT_ES2_PROFILE_BIT_EXT 0x00000004
#define WGL_CONTEXT_ROBUST_ACCESS_BIT_ARB 0x00000004
#define WGL_LOSE_CONTEXT_ON_RESET_ARB 0x8252
#define WGL_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB 0x8256
#define WGL_NO_RESET_NOTIFICATION_ARB 0x8261
#define WGL_CONTEXT_RELEASE_BEHAVIOR_ARB 0x2097
#define WGL_CONTEXT_RELEASE_BEHAVIOR_NONE_ARB 0
#define WGL_CONTEXT_RELEASE_BEHAVIOR_FLUSH_ARB 0x2098
#define WGL_CONTEXT_OPENGL_NO_ERROR_ARB 0x31b3
#define WGL_COLORSPACE_EXT 0x309d
#define WGL_COLORSPACE_SRGB_EXT 0x3089

#define ERROR_INVALID_VERSION_ARB 0x2095
#define ERROR_INVALID_PROFILE_ARB 0x2096
#define ERROR_INCOMPATIBLE_DEVICE_CONTEXTS_ARB 0x2054

// xinput.dll function pointer typedefs
typedef DWORD (WINAPI * PFN_XInputGetCapabilities)(DWORD,DWORD,XINPUT_CAPABILITIES*);
typedef DWORD (WINAPI * PFN_XInputGetState)(DWORD,XINPUT_STATE*);
#define XInputGetCapabilities g_wsi.win32.xinput.GetCapabilities
#define XInputGetState g_wsi.win32.xinput.GetState

// dinput8.dll function pointer typedefs
typedef HRESULT (WINAPI * PFN_DirectInput8Create)(HINSTANCE,DWORD,REFIID,LPVOID*,LPUNKNOWN);
#define DirectInput8Create g_wsi.win32.dinput8.Create

// user32.dll function pointer typedefs
typedef BOOL (WINAPI * PFN_EnableNonClientDpiScaling)(HWND);
typedef BOOL (WINAPI * PFN_SetProcessDpiAwarenessContext)(HANDLE);
typedef UINT (WINAPI * PFN_GetDpiForWindow)(HWND);
typedef BOOL (WINAPI * PFN_AdjustWindowRectExForDpi)(LPRECT,DWORD,BOOL,DWORD,UINT);
typedef int (WINAPI * PFN_GetSystemMetricsForDpi)(int,UINT);
#define EnableNonClientDpiScaling g_wsi.win32.user32.EnableNonClientDpiScaling_
#define SetProcessDpiAwarenessContext g_wsi.win32.user32.SetProcessDpiAwarenessContext_
#define GetDpiForWindow g_wsi.win32.user32.GetDpiForWindow_
#define AdjustWindowRectExForDpi g_wsi.win32.user32.AdjustWindowRectExForDpi_
#define GetSystemMetricsForDpi g_wsi.win32.user32.GetSystemMetricsForDpi_

// dwmapi.dll function pointer typedefs
typedef HRESULT (WINAPI * PFN_DwmIsCompositionEnabled)(BOOL*);
typedef HRESULT (WINAPI * PFN_DwmFlush)(VOID);
typedef HRESULT(WINAPI * PFN_DwmEnableBlurBehindWindow)(HWND,const DWM_BLURBEHIND*);
typedef HRESULT (WINAPI * PFN_DwmGetColorizationColor)(DWORD*,BOOL*);
#define DwmIsCompositionEnabled g_wsi.win32.dwmapi.IsCompositionEnabled
#define DwmFlush g_wsi.win32.dwmapi.Flush
#define DwmEnableBlurBehindWindow g_wsi.win32.dwmapi.EnableBlurBehindWindow
#define DwmGetColorizationColor g_wsi.win32.dwmapi.GetColorizationColor

// shcore.dll function pointer typedefs
typedef HRESULT (WINAPI * PFN_SetProcessDpiAwareness)(PROCESS_DPI_AWARENESS);
typedef HRESULT (WINAPI * PFN_GetDpiForMonitor)(HMONITOR,MONITOR_DPI_TYPE,UINT*,UINT*);
#define SetProcessDpiAwareness g_wsi.win32.shcore.SetProcessDpiAwareness_
#define GetDpiForMonitor g_wsi.win32.shcore.GetDpiForMonitor_

// ntdll.dll function pointer typedefs
typedef LONG (WINAPI * PFN_RtlVerifyVersionInfo)(OSVERSIONINFOEXW*,ULONG,ULONGLONG);
#define RtlVerifyVersionInfo g_wsi.win32.ntdll.RtlVerifyVersionInfo_

// WGL extension pointer typedefs
typedef BOOL (WINAPI * PFNWGLSWAPINTERVALEXTPROC)(int);
typedef BOOL (WINAPI * PFNWGLGETPIXELFORMATATTRIBIVARBPROC)(HDC,int,int,UINT,const int*,int*);
typedef const char* (WINAPI * PFNWGLGETEXTENSIONSSTRINGEXTPROC)(void);
typedef const char* (WINAPI * PFNWGLGETEXTENSIONSSTRINGARBPROC)(HDC);
typedef HGLRC (WINAPI * PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC,HGLRC,const int*);
#define wglSwapIntervalEXT g_wsi.wgl.SwapIntervalEXT
#define wglGetPixelFormatAttribivARB g_wsi.wgl.GetPixelFormatAttribivARB
#define wglGetExtensionsStringEXT g_wsi.wgl.GetExtensionsStringEXT
#define wglGetExtensionsStringARB g_wsi.wgl.GetExtensionsStringARB
#define wglCreateContextAttribsARB g_wsi.wgl.CreateContextAttribsARB

// opengl32.dll function pointer typedefs
typedef HGLRC (WINAPI * PFN_wglCreateContext)(HDC);
typedef BOOL (WINAPI * PFN_wglDeleteContext)(HGLRC);
typedef PROC (WINAPI * PFN_wglGetProcAddress)(LPCSTR);
typedef HDC (WINAPI * PFN_wglGetCurrentDC)(void);
typedef HGLRC (WINAPI * PFN_wglGetCurrentContext)(void);
typedef BOOL (WINAPI * PFN_wglMakeCurrent)(HDC,HGLRC);
typedef BOOL (WINAPI * PFN_wglShareLists)(HGLRC,HGLRC);
#define wglCreateContext g_wsi.wgl.CreateContext
#define wglDeleteContext g_wsi.wgl.DeleteContext
#define wglGetProcAddress g_wsi.wgl.GetProcAddress
#define wglGetCurrentDC g_wsi.wgl.GetCurrentDC
#define wglGetCurrentContext g_wsi.wgl.GetCurrentContext
#define wglMakeCurrent g_wsi.wgl.MakeCurrent
#define wglShareLists g_wsi.wgl.ShareLists

typedef VkFlags VkWin32SurfaceCreateFlagsKHR;

typedef struct VkWin32SurfaceCreateInfoKHR
{
    VkStructureType                 sType;
    const void*                     pNext;
    VkWin32SurfaceCreateFlagsKHR    flags;
    HINSTANCE                       hinstance;
    HWND                            hwnd;
} VkWin32SurfaceCreateInfoKHR;

typedef VkResult (APIENTRY *PFN_vkCreateWin32SurfaceKHR)(VkInstance,const VkWin32SurfaceCreateInfoKHR*,const VkAllocationCallbacks*,VkSurfaceKHR*);
typedef VkBool32 (APIENTRY *PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR)(VkPhysicalDevice,uint32_t);

#define GLFW_WIN32_WINDOW_STATE         _sc_windowWin32  win32;
#define GLFW_WIN32_LIBRARY_WINDOW_STATE _GLFWlibraryWin32 win32;
#define GLFW_WIN32_MONITOR_STATE        _sc_monitorWin32 win32;
#define GLFW_WIN32_CURSOR_STATE         _sc_cursorWin32  win32;

#define GLFW_WGL_CONTEXT_STATE          _GLFWcontextWGL wgl;
#define GLFW_WGL_LIBRARY_CONTEXT_STATE  _GLFWlibraryWGL wgl;


// WGL-specific per-context data
//
typedef struct _GLFWcontextWGL
{
    HDC       dc;
    HGLRC     handle;
    int       interval;
} _GLFWcontextWGL;

// WGL-specific global data
//
typedef struct _GLFWlibraryWGL
{
    HINSTANCE                           instance;
    PFN_wglCreateContext                CreateContext;
    PFN_wglDeleteContext                DeleteContext;
    PFN_wglGetProcAddress               GetProcAddress;
    PFN_wglGetCurrentDC                 GetCurrentDC;
    PFN_wglGetCurrentContext            GetCurrentContext;
    PFN_wglMakeCurrent                  MakeCurrent;
    PFN_wglShareLists                   ShareLists;

    PFNWGLSWAPINTERVALEXTPROC           SwapIntervalEXT;
    PFNWGLGETPIXELFORMATATTRIBIVARBPROC GetPixelFormatAttribivARB;
    PFNWGLGETEXTENSIONSSTRINGEXTPROC    GetExtensionsStringEXT;
    PFNWGLGETEXTENSIONSSTRINGARBPROC    GetExtensionsStringARB;
    PFNWGLCREATECONTEXTATTRIBSARBPROC   CreateContextAttribsARB;
    bool                                EXT_swap_control;
    bool                                EXT_colorspace;
    bool                                ARB_multisample;
    bool                                ARB_framebuffer_sRGB;
    bool                                EXT_framebuffer_sRGB;
    bool                                ARB_pixel_format;
    bool                                ARB_create_context;
    bool                                ARB_create_context_profile;
    bool                                EXT_create_context_es2_profile;
    bool                                ARB_create_context_robustness;
    bool                                ARB_create_context_no_error;
    bool                                ARB_context_flush_control;
} _GLFWlibraryWGL;

// Win32-specific per-window data
//
typedef struct _sc_windowWin32
{
    HWND                handle;
    HICON               bigIcon;
    HICON               smallIcon;

    bool            cursorTracked;
    bool            frameAction;
    bool            iconified;
    bool            maximized;
    // Whether to enable framebuffer transparency on DWM
    bool            transparent;
    bool            scaleToMonitor;
    bool            keymenu;
    bool            showDefault;

    // Cached size used to filter out duplicate events
    int                 width, height;

    // The last received cursor position, regardless of source
    int                 lastCursorPosX, lastCursorPosY;
    // The last received high surrogate when decoding pairs of UTF-16 messages
    WCHAR               highSurrogate;
} _sc_windowWin32;

// Win32-specific global data
//
typedef struct _GLFWlibraryWin32
{
    HINSTANCE           instance;
    HWND                helperWindowHandle;
    ATOM                helperWindowClass;
    ATOM                mainWindowClass;
    HDEVNOTIFY          deviceNotificationHandle;
    int                 acquiredMonitorCount;
    char*               clipboardString;
    short int           keycodes[512];
    short int           scancodes[SC_KEY_LAST + 1];
    char                keynames[SC_KEY_LAST + 1][5];
    // Where to place the cursor when re-enabled
    double              restoreCursorPosX, restoreCursorPosY;
    // The window whose disabled cursor mode is active
    window_st*        disabledCursorWindow;
    // The window the cursor is captured in
    window_st*        capturedCursorWindow;
    RAWINPUT*           rawInput;
    int                 rawInputSize;
    UINT                mouseTrailSize;
    // The cursor handle to use to hide the cursor (NULL or a transparent cursor)
    HCURSOR             blankCursor;

    struct {
        HINSTANCE                       instance;
        PFN_DirectInput8Create          Create;
        IDirectInput8W*                 api;
    } dinput8;

    struct {
        HINSTANCE                       instance;
        PFN_XInputGetCapabilities       GetCapabilities;
        PFN_XInputGetState              GetState;
    } xinput;

    struct {
        HINSTANCE                       instance;
        PFN_EnableNonClientDpiScaling   EnableNonClientDpiScaling_;
        PFN_SetProcessDpiAwarenessContext SetProcessDpiAwarenessContext_;
        PFN_GetDpiForWindow             GetDpiForWindow_;
        PFN_AdjustWindowRectExForDpi    AdjustWindowRectExForDpi_;
        PFN_GetSystemMetricsForDpi      GetSystemMetricsForDpi_;
    } user32;

    struct {
        HINSTANCE                       instance;
        PFN_DwmIsCompositionEnabled     IsCompositionEnabled;
        PFN_DwmFlush                    Flush;
        PFN_DwmEnableBlurBehindWindow   EnableBlurBehindWindow;
        PFN_DwmGetColorizationColor     GetColorizationColor;
    } dwmapi;

    struct {
        HINSTANCE                       instance;
        PFN_SetProcessDpiAwareness      SetProcessDpiAwareness_;
        PFN_GetDpiForMonitor            GetDpiForMonitor_;
    } shcore;

    struct {
        HINSTANCE                       instance;
        PFN_RtlVerifyVersionInfo        RtlVerifyVersionInfo_;
    } ntdll;
} _GLFWlibraryWin32;

// Win32-specific per-monitor data
//
typedef struct _sc_monitorWin32
{
    HMONITOR            handle;
    // This size matches the static size of DISPLAY_DEVICE.DeviceName
    WCHAR               adapterName[32];
    WCHAR               displayName[32];
    char                publicAdapterName[32];
    char                publicDisplayName[32];
    bool            modesPruned;
    bool            modeChanged;
} _sc_monitorWin32;

// Win32-specific per-cursor data
//
typedef struct _sc_cursorWin32
{
    HCURSOR             handle;
} _sc_cursorWin32;


bool _glfwConnectWin32(int platformID, platform_st* platform);
int _glfwInitWin32(void);
void _glfwTerminateWin32(void);

WCHAR* _glfwCreateWideStringFromUTF8Win32(const char* source);
char* _glfwCreateUTF8FromWideStringWin32(const WCHAR* source);
BOOL _glfwIsWindowsVersionOrGreaterWin32(WORD major, WORD minor, WORD sp);
BOOL _glfwIsWindows10BuildOrGreaterWin32(WORD build);
void _glfwInputErrorWin32(int error, const char* description);
void _glfwUpdateKeyNamesWin32(void);

void _glfwPollMonitorsWin32(void);
void _glfwSetVideoModeWin32(monitor_st* monitor, const GLFWvidmode* desired);
void _glfwRestoreVideoModeWin32(monitor_st* monitor);
void _glfwGetHMONITORContentScaleWin32(HMONITOR handle, float* xscale, float* yscale);

bool _glfwCreateWindowWin32(window_st* window, const wnd_config_st* wndconfig, const _GLFWctxconfig* ctxconfig, const _GLFWfbconfig* fbconfig);
void _glfwDestroyWindowWin32(window_st* window);
void _glfwSetWindowTitleWin32(window_st* window, const char* title);
void _glfwSetWindowIconWin32(window_st* window, int count, const GLFWimage* images);
void _glfwGetWindowPosWin32(window_st* window, int* xpos, int* ypos);
void _glfwSetWindowPosWin32(window_st* window, int xpos, int ypos);
void _glfwGetWindowSizeWin32(window_st* window, int* width, int* height);
void _glfwSetWindowSizeWin32(window_st* window, int width, int height);
void _glfwSetWindowSizeLimitsWin32(window_st* window, int minwidth, int minheight, int maxwidth, int maxheight);
void _glfwSetWindowAspectRatioWin32(window_st* window, int numer, int denom);
void _glfwGetFramebufferSizeWin32(window_st* window, int* width, int* height);
void _glfwGetWindowFrameSizeWin32(window_st* window, int* left, int* top, int* right, int* bottom);
void _glfwGetWindowContentScaleWin32(window_st* window, float* xscale, float* yscale);
void _glfwIconifyWindowWin32(window_st* window);
void _glfwRestoreWindowWin32(window_st* window);
void _glfwMaximizeWindowWin32(window_st* window);
void _glfwShowWindowWin32(window_st* window);
void _glfwHideWindowWin32(window_st* window);
void _glfwRequestWindowAttentionWin32(window_st* window);
void _glfwFocusWindowWin32(window_st* window);
void _glfwSetWindowMonitorWin32(window_st* window, monitor_st* monitor, int xpos, int ypos, int width, int height, int refreshRate);
bool _glfwWindowFocusedWin32(window_st* window);
bool _glfwWindowIconifiedWin32(window_st* window);
bool _glfwWindowVisibleWin32(window_st* window);
bool _glfwWindowMaximizedWin32(window_st* window);
bool _glfwWindowHoveredWin32(window_st* window);
bool _glfwFramebufferTransparentWin32(window_st* window);
void _glfwSetWindowResizableWin32(window_st* window, bool enabled);
void _glfwSetWindowDecoratedWin32(window_st* window, bool enabled);
void _glfwSetWindowFloatingWin32(window_st* window, bool enabled);
void _glfwSetWindowMousePassthroughWin32(window_st* window, bool enabled);
float _glfwGetWindowOpacityWin32(window_st* window);
void _glfwSetWindowOpacityWin32(window_st* window, float opacity);

void _glfwSetRawMouseMotionWin32(window_st *window, bool enabled);
bool _glfwRawMouseMotionSupportedWin32(void);

void _glfwPollEventsWin32(void);
void _glfwWaitEventsWin32(void);
void _glfwWaitEventsTimeoutWin32(double timeout);
void _glfwPostEmptyEventWin32(void);

void _glfwGetCursorPosWin32(window_st* window, double* xpos, double* ypos);
void _glfwSetCursorPosWin32(window_st* window, double xpos, double ypos);
void _glfwSetCursorModeWin32(window_st* window, int mode);
const char* _glfwGetScancodeNameWin32(int scancode);
int _glfwGetKeyScancodeWin32(int key);
bool _glfwCreateCursorWin32(cursor_st* cursor, const GLFWimage* image, int xhot, int yhot);
bool _glfwCreateStandardCursorWin32(cursor_st* cursor, int shape);
void _glfwDestroyCursorWin32(cursor_st* cursor);
void _glfwSetCursorWin32(window_st* window, cursor_st* cursor);
void _glfwSetClipboardStringWin32(const char* string);
const char* _glfwGetClipboardStringWin32(void);

EGLenum _glfwGetEGLPlatformWin32(EGLint** attribs);
EGLNativeDisplayType _glfwGetEGLNativeDisplayWin32(void);
EGLNativeWindowType _glfwGetEGLNativeWindowWin32(window_st* window);

void _glfwGetRequiredInstanceExtensionsWin32(char** extensions);
bool _glfwGetPhysicalDevicePresentationSupportWin32(VkInstance instance, VkPhysicalDevice device, uint32_t queuefamily);
VkResult _glfwCreateWindowSurfaceWin32(VkInstance instance, window_st* window, const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface);

void wsi_free_monitorWin32(monitor_st* monitor);
void _glfwGetMonitorPosWin32(monitor_st* monitor, int* xpos, int* ypos);
void _glfwGetMonitorContentScaleWin32(monitor_st* monitor, float* xscale, float* yscale);
void _glfwGetMonitorWorkareaWin32(monitor_st* monitor, int* xpos, int* ypos, int* width, int* height);
GLFWvidmode* _glfwGetVideoModesWin32(monitor_st* monitor, int* count);
bool _glfwGetVideoModeWin32(monitor_st* monitor, GLFWvidmode* mode);
bool _glfwGetGammaRampWin32(monitor_st* monitor, GLFWgammaramp* ramp);
void _glfwSetGammaRampWin32(monitor_st* monitor, const GLFWgammaramp* ramp);


bool _glfwInitWGL(void);
void _glfwTerminateWGL(void);
bool _glfwCreateContextWGL(window_st* window,
                               const _GLFWctxconfig* ctxconfig,
                               const _GLFWfbconfig* fbconfig);

