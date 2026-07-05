
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
#define kTISPropertyUnicodeKeyLayoutData _glfw.ns.tis.kPropertyUnicodeKeyLayoutData
typedef TISInputSourceRef (*PFN_TISCopyCurrentKeyboardLayoutInputSource)(void);
#define TISCopyCurrentKeyboardLayoutInputSource _glfw.ns.tis.CopyCurrentKeyboardLayoutInputSource
typedef void* (*PFN_TISGetInputSourceProperty)(TISInputSourceRef,CFStringRef);
#define TISGetInputSourceProperty _glfw.ns.tis.GetInputSourceProperty
typedef UInt8 (*PFN_LMGetKbdType)(void);
#define LMGetKbdType _glfw.ns.tis.GetKbdType


// Cocoa-specific per-window data
//
typedef struct _sc_windowNS
{
    id              object;
    id              delegate;
    id              view;
    id              layer;

    GLFWbool        maximized;
    GLFWbool        occluded;
    GLFWbool        scaleFramebuffer;

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
    GLFWbool            cursorHidden;
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
    _sc_window*        disabledCursorWindow;

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


GLFWbool _glfwConnectCocoa(int platformID, _GLFWplatform* platform);
int _glfwInitCocoa(void);
void _glfwTerminateCocoa(void);

GLFWbool _glfwCreateWindowCocoa(_sc_window* window, const _GLFWwndconfig* wndconfig);
void _glfwDestroyWindowCocoa(_sc_window* window);
void _glfwSetWindowTitleCocoa(_sc_window* window, const char* title);
void _glfwSetWindowIconCocoa(_sc_window* window, int count, const GLFWimage* images);
void _glfwGetWindowPosCocoa(_sc_window* window, int* xpos, int* ypos);
void _glfwSetWindowPosCocoa(_sc_window* window, int xpos, int ypos);
void _glfwGetWindowSizeCocoa(_sc_window* window, int* width, int* height);
void _glfwSetWindowSizeCocoa(_sc_window* window, int width, int height);
void _glfwSetWindowSizeLimitsCocoa(_sc_window* window, int minwidth, int minheight, int maxwidth, int maxheight);
void _glfwSetWindowAspectRatioCocoa(_sc_window* window, int numer, int denom);
void _glfwGetWindowFrameSizeCocoa(_sc_window* window, int* left, int* top, int* right, int* bottom);
void _glfwGetWindowContentScaleCocoa(_sc_window* window, float* xscale, float* yscale);
void _glfwIconifyWindowCocoa(_sc_window* window);
void _glfwRestoreWindowCocoa(_sc_window* window);
void _glfwMaximizeWindowCocoa(_sc_window* window);
void _glfwShowWindowCocoa(_sc_window* window);
void _glfwHideWindowCocoa(_sc_window* window);
void _glfwRequestWindowAttentionCocoa(_sc_window* window);
void _glfwFocusWindowCocoa(_sc_window* window);
void _glfwSetWindowMonitorCocoa(_sc_window* window, _sc_monitor* monitor, int xpos, int ypos, int width, int height, int refreshRate);
GLFWbool _glfwWindowFocusedCocoa(_sc_window* window);
GLFWbool _glfwWindowIconifiedCocoa(_sc_window* window);
GLFWbool _glfwWindowVisibleCocoa(_sc_window* window);
GLFWbool _glfwWindowMaximizedCocoa(_sc_window* window);
GLFWbool _glfwWindowHoveredCocoa(_sc_window* window);
void _glfwSetWindowResizableCocoa(_sc_window* window, GLFWbool enabled);
void _glfwSetWindowDecoratedCocoa(_sc_window* window, GLFWbool enabled);
void _glfwSetWindowFloatingCocoa(_sc_window* window, GLFWbool enabled);
float _glfwGetWindowOpacityCocoa(_sc_window* window);
void _glfwSetWindowOpacityCocoa(_sc_window* window, float opacity);
void _glfwSetWindowMousePassthroughCocoa(_sc_window* window, GLFWbool enabled);

void _glfwSetRawMouseMotionCocoa(_sc_window *window, GLFWbool enabled);
GLFWbool _glfwRawMouseMotionSupportedCocoa(void);

void _glfwPollEventsCocoa(void);
void _glfwWaitEventsCocoa(void);
void _glfwWaitEventsTimeoutCocoa(double timeout);
void _glfwPostEmptyEventCocoa(void);

void _glfwGetCursorPosCocoa(_sc_window* window, double* xpos, double* ypos);
void _glfwSetCursorPosCocoa(_sc_window* window, double xpos, double ypos);
void _glfwSetCursorModeCocoa(_sc_window* window, int mode);
const char* _glfwGetScancodeNameCocoa(int scancode);
int _glfwGetKeyScancodeCocoa(int key);
GLFWbool _glfwCreateCursorCocoa(_sc_cursor* cursor, const GLFWimage* image, int xhot, int yhot);
GLFWbool _glfwCreateStandardCursorCocoa(_sc_cursor* cursor, int shape);
void _glfwDestroyCursorCocoa(_sc_cursor* cursor);
void _glfwSetCursorCocoa(_sc_window* window, _sc_cursor* cursor);
void _glfwSetClipboardStringCocoa(const char* string);
const char* _glfwGetClipboardStringCocoa(void);


void _glfwFreeMonitorCocoa(_sc_monitor* monitor);
void _glfwGetMonitorPosCocoa(_sc_monitor* monitor, int* xpos, int* ypos);
void _glfwGetMonitorContentScaleCocoa(_sc_monitor* monitor, float* xscale, float* yscale);
void _glfwGetMonitorWorkareaCocoa(_sc_monitor* monitor, int* xpos, int* ypos, int* width, int* height);
GLFWvidmode* _glfwGetVideoModesCocoa(_sc_monitor* monitor, int* count);
GLFWbool _glfwGetVideoModeCocoa(_sc_monitor* monitor, GLFWvidmode* mode);
GLFWbool _glfwGetGammaRampCocoa(_sc_monitor* monitor, GLFWgammaramp* ramp);
void _glfwSetGammaRampCocoa(_sc_monitor* monitor, const GLFWgammaramp* ramp);

void _glfwPollMonitorsCocoa(void);
void _glfwSetVideoModeCocoa(_sc_monitor* monitor, const GLFWvidmode* desired);
void _glfwRestoreVideoModeCocoa(_sc_monitor* monitor);

float _glfwTransformYCocoa(float y);

