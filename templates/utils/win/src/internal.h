
#pragma once
#define GLFW_INCLUDE_NONE
#include "glfw3.h"

#include <stdbool.h>

#define _GLFW_INSERT_FIRST      0
#define _GLFW_INSERT_LAST       1

#define _GLFW_MESSAGE_SIZE      1024

typedef int GLFWbool;
typedef void (*GLFWproc)(void);

typedef struct _GLFWerror       _GLFWerror;
typedef struct _GLFWinitconfig  _GLFWinitconfig;
typedef struct _GLFWwndconfig   _GLFWwndconfig;
typedef struct _sc_window      _sc_window;
typedef struct _GLFWplatform    _GLFWplatform;
typedef struct _GLFWlibrary     _GLFWlibrary;
typedef struct _sc_monitor     _sc_monitor;
typedef struct _sc_cursor      _sc_cursor;
typedef struct _GLFWtls         _GLFWtls;
typedef struct _GLFWmutex       _GLFWmutex;


#include "platform.h"

// Checks for whether the library has been initialized
#define _GLFW_REQUIRE_INIT()                         \
    if (!_glfw.initialized)                          \
    {                                                \
        _glfwInputError(SC_WIN_ERR_NOT_INITIALIZED, NULL); \
        return;                                      \
    }
#define _GLFW_REQUIRE_INIT_OR_RETURN(x)              \
    if (!_glfw.initialized)                          \
    {                                                \
        _glfwInputError(SC_WIN_ERR_NOT_INITIALIZED, NULL); \
        return x;                                    \
    }

// Swaps the provided pointers
#define _GLFW_SWAP(type, x, y) \
    {                          \
        type t;                \
        t = x;                 \
        x = y;                 \
        y = t;                 \
    }

// Per-thread error structure
//
struct _GLFWerror
{
    _GLFWerror*     next;
    int             code;
    char            description[_GLFW_MESSAGE_SIZE];
};

// Initialization configuration
//
// Parameters relating to the initialization of the library
//
struct _GLFWinitconfig
{
    bool          hatButtons;
    int           angleType;
    int           platformID;
    struct {
        bool      menubar;
        bool      chdir;
    } ns;
    struct {
    } x11;
    struct {
        int       libdecorMode;
    } wl;
};

// Window configuration
//
// Parameters relating to the creation of the window but not directly related
// to the framebuffer.  This is used to pass window creation parameters from
// shared code to the platform API.
//
struct _GLFWwndconfig
{
    int           xpos;
    int           ypos;
    int           width;
    int           height;
    bool          resizable;
    bool          visible;
    bool          decorated;
    bool          focused;
    bool          autoIconify;
    bool          floating;
    bool          maximized;
    bool          centerCursor;
    bool          focusOnShow;
    bool          mousePassthrough;
    bool          scaleToMonitor;
    struct {
        char      frameName[256];
    } ns;
    struct {
        char      className[256];
        char      instanceName[256];
    } x11;
    struct {
        bool      keymenu;
        bool      showDefault;
    } win32;
    struct {
        char      appId[256];
    } wl;
};

// Context configuration
//
// Parameters relating to the creation of the context but not directly related
// to the framebuffer.  This is used to pass context creation parameters from
// shared code to the platform API.
//

// Framebuffer configuration
//
// This describes buffers and their sizes.  It also contains
// a platform-specific ID used to map back to the backend API object.
//
// It is used to pass framebuffer parameters from shared code to the platform
// API and also to enumerate and select available framebuffer configs.
//

// Context structure
//

// Window and context structure
//
struct _sc_window
{
    struct _sc_window* next;

    // Window settings and state
    GLFWbool            resizable;
    GLFWbool            decorated;
    GLFWbool            autoIconify;
    GLFWbool            floating;
    GLFWbool            focusOnShow;
    GLFWbool            mousePassthrough;
    GLFWbool            shouldClose;
    void*               userPointer;
    GLFWbool            doublebuffer;
    GLFWvidmode         videoMode;
    _sc_monitor*       monitor;
    _sc_cursor*        cursor;
    char*               title;

    int                 minwidth, minheight;
    int                 maxwidth, maxheight;
    int                 numer, denom;

    GLFWbool            stickyKeys;
    GLFWbool            stickyMouseButtons;
    GLFWbool            lockKeyMods;
    GLFWbool            disableMouseButtonLimit;
    int                 cursorMode;
    char                mouseButtons[SC_MOUSE_BUTTON_LAST + 1];
    char                keys[SC_KEY_LAST + 1];
    // Virtual cursor position when cursor is disabled
    double              virtualCursorPosX, virtualCursorPosY;
    GLFWbool            rawMouseMotion;


    struct {
        sc_win_pos_cb          pos;
        sc_win_size_cb         size;
        sc_win_close_cb        close;
        sc_win_refresh_cb      refresh;
        sc_win_focus_cb        focus;
        sc_win_iconify_cb      iconify;
        sc_win_maximize_cb     maximize;
        sc_win_content_scale_cb scale;
        sc_win_mouse_button_cb        mouseButton;
        sc_cursor_pos_cb          cursorPos;
        sc_cursor_enter_cb        cursorEnter;
        sc_scroll_cb             scroll;
        sc_key_cb                key;
        sc_char_cb               character;
        sc_char_mods_cb           charmods;
        sc_drop_cb               drop;
    } callbacks;

    // This is defined in platform.h
    GLFW_PLATFORM_WINDOW_STATE
};

// Monitor structure
//
struct _sc_monitor
{
    char            name[128];
    void*           userPointer;

    // Physical dimensions in millimeters.
    int             widthMM, heightMM;

    // The window whose video mode is current on this monitor
    _sc_window*    window;

    GLFWvidmode*    modes;
    int             modeCount;
    GLFWvidmode     currentMode;

    GLFWgammaramp   originalRamp;
    GLFWgammaramp   currentRamp;

    // This is defined in platform.h
    GLFW_PLATFORM_MONITOR_STATE
};

// Cursor structure
//
struct _sc_cursor
{
    _sc_cursor*    next;
    // This is defined in platform.h
    GLFW_PLATFORM_CURSOR_STATE
};

// Thread local storage structure
//
struct _GLFWtls
{
    // This is defined in platform.h
    GLFW_PLATFORM_TLS_STATE
};

// Mutex structure
//
struct _GLFWmutex
{
    // This is defined in platform.h
    GLFW_PLATFORM_MUTEX_STATE
};

// Platform API structure
//
struct _GLFWplatform
{
    int platformID;
    // init
    GLFWbool (*init)(void);
    void (*terminate)(void);
    // input
    void (*getCursorPos)(_sc_window*,double*,double*);
    void (*setCursorPos)(_sc_window*,double,double);
    void (*setCursorMode)(_sc_window*,int);
    void (*setRawMouseMotion)(_sc_window*,GLFWbool);
    GLFWbool (*rawMouseMotionSupported)(void);
    GLFWbool (*createCursor)(_sc_cursor*,const GLFWimage*,int,int);
    GLFWbool (*createStandardCursor)(_sc_cursor*,int);
    void (*destroyCursor)(_sc_cursor*);
    void (*setCursor)(_sc_window*,_sc_cursor*);
    const char* (*getScancodeName)(int);
    int (*getKeyScancode)(int);
    void (*setClipboardString)(const char*);
    const char* (*getClipboardString)(void);
    // monitor
    void (*freeMonitor)(_sc_monitor*);
    void (*getMonitorPos)(_sc_monitor*,int*,int*);
    void (*getMonitorContentScale)(_sc_monitor*,float*,float*);
    void (*getMonitorWorkarea)(_sc_monitor*,int*,int*,int*,int*);
    GLFWvidmode* (*getVideoModes)(_sc_monitor*,int*);
    GLFWbool (*getVideoMode)(_sc_monitor*,GLFWvidmode*);
    GLFWbool (*getGammaRamp)(_sc_monitor*,GLFWgammaramp*);
    void (*setGammaRamp)(_sc_monitor*,const GLFWgammaramp*);
    // window
    GLFWbool (*createWindow)(_sc_window*,const _GLFWwndconfig*);
    void (*destroyWindow)(_sc_window*);
    void (*setWindowTitle)(_sc_window*,const char*);
    void (*setWindowIcon)(_sc_window*,int,const GLFWimage*);
    void (*getWindowPos)(_sc_window*,int*,int*);
    void (*setWindowPos)(_sc_window*,int,int);
    void (*getWindowSize)(_sc_window*,int*,int*);
    void (*setWindowSize)(_sc_window*,int,int);
    void (*setWindowSizeLimits)(_sc_window*,int,int,int,int);
    void (*setWindowAspectRatio)(_sc_window*,int,int);
    void (*getWindowFrameSize)(_sc_window*,int*,int*,int*,int*);
    void (*getWindowContentScale)(_sc_window*,float*,float*);
    void (*iconifyWindow)(_sc_window*);
    void (*restoreWindow)(_sc_window*);
    void (*maximizeWindow)(_sc_window*);
    void (*showWindow)(_sc_window*);
    void (*hideWindow)(_sc_window*);
    void (*requestWindowAttention)(_sc_window*);
    void (*focusWindow)(_sc_window*);
    void (*setWindowMonitor)(_sc_window*,_sc_monitor*,int,int,int,int,int);
    GLFWbool (*windowFocused)(_sc_window*);
    GLFWbool (*windowIconified)(_sc_window*);
    GLFWbool (*windowVisible)(_sc_window*);
    GLFWbool (*windowMaximized)(_sc_window*);
    GLFWbool (*windowHovered)(_sc_window*);
    float (*getWindowOpacity)(_sc_window*);
    void (*setWindowResizable)(_sc_window*,GLFWbool);
    void (*setWindowDecorated)(_sc_window*,GLFWbool);
    void (*setWindowFloating)(_sc_window*,GLFWbool);
    void (*setWindowOpacity)(_sc_window*,float);
    void (*setWindowMousePassthrough)(_sc_window*,GLFWbool);
    void (*pollEvents)(void);
    void (*waitEvents)(void);
    void (*waitEventsTimeout)(double);
    void (*postEmptyEvent)(void);
};

// Library global data
//
struct _GLFWlibrary
{
    GLFWbool            initialized;
    sc_allocator_cb       allocator;

    _GLFWplatform       platform;

    struct {
        _GLFWinitconfig init;
        _GLFWwndconfig  window;
        int             refreshRate;
    } hints;

    _GLFWerror*         errorListHead;
    _sc_cursor*        cursorListHead;
    _sc_window*        windowListHead;

    _sc_monitor**      monitors;
    int                 monitorCount;


    _GLFWtls            errorSlot;
    _GLFWmutex          errorLock;

    struct {
        uint64_t        offset;
        // This is defined in platform.h
        GLFW_PLATFORM_LIBRARY_TIMER_STATE
    } timer;


    struct {
        sc_monitor_cb  monitor;
    } callbacks;

    // These are defined in platform.h
    GLFW_PLATFORM_LIBRARY_WINDOW_STATE
    GLFW_PLATFORM_LIBRARY_CONTEXT_STATE
};

// Global state shared between compilation units of GLFW
//
extern _GLFWlibrary _glfw;


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

void _glfwPlatformInitTimer(void);
uint64_t _glfwPlatformGetTimerValue(void);
uint64_t _glfwPlatformGetTimerFrequency(void);

GLFWbool _glfwPlatformCreateTls(_GLFWtls* tls);
void _glfwPlatformDestroyTls(_GLFWtls* tls);
void* _glfwPlatformGetTls(_GLFWtls* tls);
void _glfwPlatformSetTls(_GLFWtls* tls, void* value);

GLFWbool _glfwPlatformCreateMutex(_GLFWmutex* mutex);
void _glfwPlatformDestroyMutex(_GLFWmutex* mutex);
void _glfwPlatformLockMutex(_GLFWmutex* mutex);
void _glfwPlatformUnlockMutex(_GLFWmutex* mutex);

void* _glfwPlatformLoadModule(const char* path);
void _glfwPlatformFreeModule(void* module);
GLFWproc _glfwPlatformGetModuleSymbol(void* module, const char* name);


//////////////////////////////////////////////////////////////////////////
//////                         GLFW event API                       //////
//////////////////////////////////////////////////////////////////////////

void _glfwInputWindowFocus(_sc_window* window, GLFWbool focused);
void _glfwInputWindowPos(_sc_window* window, int xpos, int ypos);
void _glfwInputWindowSize(_sc_window* window, int width, int height);
void _glfwInputWindowContentScale(_sc_window* window,
                                  float xscale, float yscale);
void _glfwInputWindowIconify(_sc_window* window, GLFWbool iconified);
void _glfwInputWindowMaximize(_sc_window* window, GLFWbool maximized);
void _glfwInputWindowDamage(_sc_window* window);
void _glfwInputWindowCloseRequest(_sc_window* window);
void _glfwInputWindowMonitor(_sc_window* window, _sc_monitor* monitor);

void _glfwInputKey(_sc_window* window,
                   int key, int scancode, int action, int mods);
void _glfwInputChar(_sc_window* window,
                    uint32_t codepoint, int mods, GLFWbool plain);
void _glfwInputScroll(_sc_window* window, double xoffset, double yoffset);
void _glfwInputMouseClick(_sc_window* window, int button, int action, int mods);
void _glfwInputCursorPos(_sc_window* window, double xpos, double ypos);
void _glfwInputCursorEnter(_sc_window* window, GLFWbool entered);
void _glfwInputDrop(_sc_window* window, int count, const char** names);

void _glfwInputMonitor(_sc_monitor* monitor, int action, int placement);
void _glfwInputMonitorWindow(_sc_monitor* monitor, _sc_window* window);

#if defined(__GNUC__)
void _glfwInputError(int code, const char* format, ...)
    __attribute__((format(printf, 2, 3)));
#else
void _glfwInputError(int code, const char* format, ...);
#endif


//////////////////////////////////////////////////////////////////////////
//////                       GLFW internal API                      //////
//////////////////////////////////////////////////////////////////////////

GLFWbool _glfwSelectPlatform(int platformID, _GLFWplatform* platform);


const GLFWvidmode* _glfwChooseVideoMode(_sc_monitor* monitor,
                                        const GLFWvidmode* desired);
int _glfwCompareVideoModes(const GLFWvidmode* first, const GLFWvidmode* second);
_sc_monitor* _glfwAllocMonitor(const char* name, int widthMM, int heightMM);
void _glfwFreeMonitor(_sc_monitor* monitor);
void _glfwAllocGammaArrays(GLFWgammaramp* ramp, unsigned int size);
void _glfwFreeGammaArrays(GLFWgammaramp* ramp);
void _glfwSplitBPP(int bpp, int* red, int* green, int* blue);

void _glfwCenterCursorInContentArea(_sc_window* window);

size_t _glfwEncodeUTF8(char* s, uint32_t codepoint);
char** _glfwParseUriList(char* text, int* count);

char* _glfw_strdup(const char* source);
int _glfw_min(int a, int b);
int _glfw_max(int a, int b);

void* _glfw_calloc(size_t count, size_t size);
void* _glfw_realloc(void* pointer, size_t size);
void _glfw_free(void* pointer);

