
#include "internal.h"
#include <stdint.h>

#include <Carbon/Carbon.h>
#include <IOKit/hid/IOHIDLib.h>

#if defined(__OBJC__)
#import <Cocoa/Cocoa.h>
#else
typedef void* id;
#endif

// NOTE: Many Cocoa enum values have been renamed and we need to build across
//       SDK versions where one is unavailable or deprecated.
//       We use the newer names in code and replace them with the older names if
//       the base SDK does not provide the newer names.

#if MAC_OS_X_VERSION_MAX_ALLOWED < 101200
 #define NSBitmapFormatAlphaNonpremultiplied NSAlphaNonpremultipliedBitmapFormat
 #define NSEventMaskAny NSAnyEventMask
 #define NSEventMaskKeyUp NSKeyUpMask
 #define NSEventModifierFlagCapsLock NSAlphaShiftKeyMask
 #define NSEventModifierFlagCommand NSCommandKeyMask
 #define NSEventModifierFlagControl NSControlKeyMask
 #define NSEventModifierFlagDeviceIndependentFlagsMask NSDeviceIndependentModifierFlagsMask
 #define NSEventModifierFlagOption NSAlternateKeyMask
 #define NSEventModifierFlagShift NSShiftKeyMask
 #define NSEventTypeApplicationDefined NSApplicationDefined
 #define NSWindowStyleMaskBorderless NSBorderlessWindowMask
 #define NSWindowStyleMaskClosable NSClosableWindowMask
 #define NSWindowStyleMaskMiniaturizable NSMiniaturizableWindowMask
 #define NSWindowStyleMaskResizable NSResizableWindowMask
 #define NSWindowStyleMaskTitled NSTitledWindowMask
#endif

// NOTE: Many Cocoa dynamically linked constants have been renamed and we need
//       to build across SDK versions where one is unavailable or deprecated.
//       We use the newer names in code and replace them with the older names if
//       the deployment target is older than the newer names.

#if MAC_OS_X_VERSION_MIN_REQUIRED < 101300
 #define NSPasteboardTypeURL NSURLPboardType
#endif

#define GLFW_COCOA_WINDOW_STATE         _sc_windowNS  ns;
#define GLFW_COCOA_LIBRARY_WINDOW_STATE _GLFWlibraryNS ns;
#define GLFW_COCOA_MONITOR_STATE        _sc_monitorNS ns;
#define GLFW_COCOA_CURSOR_STATE         _sc_cursorNS  ns;

// HIToolbox.framework pointer typedefs
#define kTISPropertyUnicodeKeyLayoutData g_wsi.ns.tis.kPropertyUnicodeKeyLayoutData
typedef TISInputSourceRef (*PFN_TISCopyCurrentKeyboardLayoutInputSource)(void);
#define TISCopyCurrentKeyboardLayoutInputSource g_wsi.ns.tis.CopyCurrentKeyboardLayoutInputSource
typedef void* (*PFN_TISGetInputSourceProperty)(TISInputSourceRef,CFStringRef);
#define TISGetInputSourceProperty g_wsi.ns.tis.GetInputSourceProperty
typedef UInt8 (*PFN_LMGetKbdType)(void);
#define LMGetKbdType g_wsi.ns.tis.GetKbdType


// Cocoa-specific per-window data
//
typedef struct _sc_windowNS
{
    id              object;
    id              delegate;
    id              view;
    id              layer;

    bool        maximized;
    bool        occluded;
    bool        scaleFramebuffer;

    // Cached window properties to filter out duplicate events
    int             width, height;
    int             fbWidth, fbHeight;
    float           xscale, yscale;

    // The total sum of the distances the cursor has been warped
    // since the last cursor motion event was processed
    // This is kept to counteract Cocoa doing the same internally
    double          cursorWarpDeltaX, cursorWarpDeltaY;
} _sc_windowNS;

// Cocoa-specific global data
//
typedef struct _GLFWlibraryNS
{
    CGEventSourceRef    eventSource;
    id                  delegate;
    bool            cursorHidden;
    TISInputSourceRef   inputSource;
    IOHIDManagerRef     hidManager;
    id                  unicodeData;
    id                  helper;
    id                  keyUpMonitor;
    id                  nibObjects;

    char                keynames[SC_KEY_LAST + 1][17];
    short int           keycodes[256];
    short int           scancodes[SC_KEY_LAST + 1];
    char*               clipboardString;
    CGPoint             cascadePoint;
    // Where to place the cursor when re-enabled
    double              restoreCursorPosX, restoreCursorPosY;
    // The window whose disabled cursor mode is active
    window_st*        disabledCursorWindow;

    struct {
        CFBundleRef     bundle;
        PFN_TISCopyCurrentKeyboardLayoutInputSource CopyCurrentKeyboardLayoutInputSource;
        PFN_TISGetInputSourceProperty GetInputSourceProperty;
        PFN_LMGetKbdType GetKbdType;
        CFStringRef     kPropertyUnicodeKeyLayoutData;
    } tis;
} _GLFWlibraryNS;

// Cocoa-specific per-monitor data
//
typedef struct _sc_monitorNS
{
    CGDirectDisplayID   displayID;
    CGDisplayModeRef    previousMode;
    uint32_t            unitNumber;
    id                  screen;
    double              fallbackRefreshRate;
} _sc_monitorNS;

// Cocoa-specific per-cursor data
//
typedef struct _sc_cursorNS
{
    id              object;
} _sc_cursorNS;


bool _glfwConnectCocoa(int platformID, platform_st* platform);
int _glfwInitCocoa(void);
void _glfwTerminateCocoa(void);

bool _glfwCreateWindowCocoa(window_st* window, const wnd_config_st* wndconfig);
void _glfwDestroyWindowCocoa(window_st* window);
void _glfwSetWindowTitleCocoa(window_st* window, const char* title);
void _glfwSetWindowIconCocoa(window_st* window, int count, const GLFWimage* images);
void _glfwGetWindowPosCocoa(window_st* window, int* xpos, int* ypos);
void _glfwSetWindowPosCocoa(window_st* window, int xpos, int ypos);
void _glfwGetWindowSizeCocoa(window_st* window, int* width, int* height);
void _glfwSetWindowSizeCocoa(window_st* window, int width, int height);
void _glfwSetWindowSizeLimitsCocoa(window_st* window, int minwidth, int minheight, int maxwidth, int maxheight);
void _glfwSetWindowAspectRatioCocoa(window_st* window, int numer, int denom);
void _glfwGetWindowFrameSizeCocoa(window_st* window, int* left, int* top, int* right, int* bottom);
void _glfwGetWindowContentScaleCocoa(window_st* window, float* xscale, float* yscale);
void _glfwIconifyWindowCocoa(window_st* window);
void _glfwRestoreWindowCocoa(window_st* window);
void _glfwMaximizeWindowCocoa(window_st* window);
void _glfwShowWindowCocoa(window_st* window);
void _glfwHideWindowCocoa(window_st* window);
void _glfwRequestWindowAttentionCocoa(window_st* window);
void _glfwFocusWindowCocoa(window_st* window);
void _glfwSetWindowMonitorCocoa(window_st* window, monitor_st* monitor, int xpos, int ypos, int width, int height, int refreshRate);
bool _glfwWindowFocusedCocoa(window_st* window);
bool _glfwWindowIconifiedCocoa(window_st* window);
bool _glfwWindowVisibleCocoa(window_st* window);
bool _glfwWindowMaximizedCocoa(window_st* window);
bool _glfwWindowHoveredCocoa(window_st* window);
void _glfwSetWindowResizableCocoa(window_st* window, bool enabled);
void _glfwSetWindowDecoratedCocoa(window_st* window, bool enabled);
void _glfwSetWindowFloatingCocoa(window_st* window, bool enabled);
float _glfwGetWindowOpacityCocoa(window_st* window);
void _glfwSetWindowOpacityCocoa(window_st* window, float opacity);
void _glfwSetWindowMousePassthroughCocoa(window_st* window, bool enabled);

void _glfwSetRawMouseMotionCocoa(window_st *window, bool enabled);
bool _glfwRawMouseMotionSupportedCocoa(void);

void _glfwPollEventsCocoa(void);
void _glfwWaitEventsCocoa(void);
void _glfwWaitEventsTimeoutCocoa(double timeout);
void _glfwPostEmptyEventCocoa(void);

void _glfwGetCursorPosCocoa(window_st* window, double* xpos, double* ypos);
void _glfwSetCursorPosCocoa(window_st* window, double xpos, double ypos);
void _glfwSetCursorModeCocoa(window_st* window, int mode);
const char* _glfwGetScancodeNameCocoa(int scancode);
int _glfwGetKeyScancodeCocoa(int key);
bool _glfwCreateCursorCocoa(cursor_st* cursor, const GLFWimage* image, int xhot, int yhot);
bool _glfwCreateStandardCursorCocoa(cursor_st* cursor, int shape);
void _glfwDestroyCursorCocoa(cursor_st* cursor);
void _glfwSetCursorCocoa(window_st* window, cursor_st* cursor);
void _glfwSetClipboardStringCocoa(const char* string);
const char* _glfwGetClipboardStringCocoa(void);


void wsi_free_monitorCocoa(monitor_st* monitor);
void _glfwGetMonitorPosCocoa(monitor_st* monitor, int* xpos, int* ypos);
void _glfwGetMonitorContentScaleCocoa(monitor_st* monitor, float* xscale, float* yscale);
void _glfwGetMonitorWorkareaCocoa(monitor_st* monitor, int* xpos, int* ypos, int* width, int* height);
GLFWvidmode* _glfwGetVideoModesCocoa(monitor_st* monitor, int* count);
bool _glfwGetVideoModeCocoa(monitor_st* monitor, GLFWvidmode* mode);
bool _glfwGetGammaRampCocoa(monitor_st* monitor, GLFWgammaramp* ramp);
void _glfwSetGammaRampCocoa(monitor_st* monitor, const GLFWgammaramp* ramp);

void _glfwPollMonitorsCocoa(void);
void _glfwSetVideoModeCocoa(monitor_st* monitor, const GLFWvidmode* desired);
void _glfwRestoreVideoModeCocoa(monitor_st* monitor);

float _glfwTransformYCocoa(float y);

