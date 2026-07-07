
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


bool cocoa_connect(int platformID, platform_st* platform);
int cocoa_init(void);
void cocoa_terminate(void);

bool cocoa_create_window(window_st* window, const wnd_config_st* wndconfig);
void cocoa_destroy_window(window_st* window);
void cocoa_set_window_title(window_st* window, const char* title);
void cocoa_set_window_icon(window_st* window, int count, const GLFWimage* images);
void cocoa_get_window_pos(window_st* window, int* xpos, int* ypos);
void cocoa_set_window_pos(window_st* window, int xpos, int ypos);
void cocoa_get_window_size(window_st* window, int* width, int* height);
void cocoa_set_window_size(window_st* window, int width, int height);
void cocoa_set_window_size_limits(window_st* window, int minwidth, int minheight, int maxwidth, int maxheight);
void cocoa_set_window_aspect_ratio(window_st* window, int numer, int denom);
void cocoa_get_window_frame_size(window_st* window, int* left, int* top, int* right, int* bottom);
void cocoa_get_window_content_scale(window_st* window, float* xscale, float* yscale);
void cocoa_iconify_window(window_st* window);
void cocoa_restore_window(window_st* window);
void cocoa_maximize_window(window_st* window);
void cocoa_show_window(window_st* window);
void cocoa_hide_window(window_st* window);
void cocoa_request_window_attention(window_st* window);
void cocoa_focus_window(window_st* window);
void cocoa_set_window_monitor(window_st* window, monitor_st* monitor, int xpos, int ypos, int width, int height, int refreshRate);
bool cocoa_window_focused(window_st* window);
bool cocoa_window_iconified(window_st* window);
bool cocoa_window_visible(window_st* window);
bool cocoa_window_maximized(window_st* window);
bool cocoa_window_hovered(window_st* window);
void cocoa_set_window_resizable(window_st* window, bool enabled);
void cocoa_set_window_decorated(window_st* window, bool enabled);
void cocoa_set_window_floating(window_st* window, bool enabled);
float cocoa_get_window_opacity(window_st* window);
void cocoa_set_window_opacity(window_st* window, float opacity);
void cocoa_set_window_mouse_passthrough(window_st* window, bool enabled);

void cocoa_set_mouse_raw_motion(window_st *window, bool enabled);
bool cocoa_mouse_raw_motion_supported(void);

void cocoa_poll_events(void);
void cocoa_wait_events(void);
void cocoa_wait_eventsTimeout(double timeout);
void cocoa_post_empty_event(void);

void cocoa_get_cursor_pos(window_st* window, double* xpos, double* ypos);
void cocoa_set_cursor_pos(window_st* window, double xpos, double ypos);
void cocoa_set_cursorMode(window_st* window, int mode);
const char* cocoa_get_scancode_name(int scancode);
int cocoa_get_key_scancode(int key);
bool cocoa_create_cursor(cursor_st* cursor, const GLFWimage* image, int xhot, int yhot);
bool cocoa_create_standard_cursor(cursor_st* cursor, int shape);
void cocoa_destroy_cursor(cursor_st* cursor);
void cocoa_set_cursor(window_st* window, cursor_st* cursor);
void cocoa_set_clipboard_string(const char* string);
const char* cocoa_get_clipboard_string(void);


void cocoa_free_monitor(monitor_st* monitor);
void cocoa_get_monitor_pos(monitor_st* monitor, int* xpos, int* ypos);
void cocoa_get_monitor_content_scale(monitor_st* monitor, float* xscale, float* yscale);
void cocoa_get_monitor_work_area(monitor_st* monitor, int* xpos, int* ypos, int* width, int* height);
GLFWvidmode* cocoa_get_video_modes(monitor_st* monitor, int* count);
bool cocoa_get_video_mode(monitor_st* monitor, GLFWvidmode* mode);
bool cocoa_get_gamma_ramp(monitor_st* monitor, GLFWgammaramp* ramp);
void cocoa_set_gamma_ramp(monitor_st* monitor, const GLFWgammaramp* ramp);

void cocoa_poll_monitors(void);
void cocoa_SetVideoMode(monitor_st* monitor, const GLFWvidmode* desired);
void cocoa_RestoreVideoMode(monitor_st* monitor);

float cocoa_TransformY(float y);

