
#pragma once

#define WSI_INSERT_FIRST      0
#define WSI_INSERT_LAST       1

#define WSI_MESSAGE_SIZE      1024

typedef int (*GLFWproc)(void);

typedef struct error_t          error_st;
typedef struct init_config_t    init_config_st;
typedef struct wnd_config_t     wnd_config_st;
typedef struct window_t         window_st;
typedef struct monitor_t        monitor_st;
typedef struct cursor_t         cursor_st;
typedef struct platform_t       platform_st;
typedef struct library_t        library_st;

#include "../wsi.h"
#include "wsi_platform.h"

// Per-thread error structure
//
struct error_t
{
    int             code;
    char            description[WSI_MESSAGE_SIZE];
};

// Initialization configuration
//
// Parameters relating to the initialization of the library
//
struct init_config_t
{
    bool          hatButtons;
    int           angleType;
    int           platformID;
    struct {
        bool      menubar;
        bool      chdir;
    } ns;
    struct {
        char      _unused; // MSVC(C) 不允许零成员结构体，占位（x11 字段在 Linux 后端另行扩展）
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
struct wnd_config_t
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
struct window_t
{
    struct window_t* next;

    // Window settings and state
    bool            resizable;
    bool            decorated;
    bool            autoIconify;
    bool            floating;
    bool            focusOnShow;
    bool            mousePassthrough;
    bool            shouldClose;
    void*               userPointer;
    bool            doublebuffer;
    GLFWvidmode         videoMode;
    monitor_st*       monitor;
    cursor_st*        cursor;
    char*               title;

    int                 minwidth, minheight;
    int                 maxwidth, maxheight;
    int                 numer, denom;

    bool            stickyKeys;
    bool            stickyMouseButtons;
    bool            lockKeyMods;
    bool            disableMouseButtonLimit;
    int                 cursorMode;
    char                mouseButtons[SC_MOUSE_BUTTON_LAST + 1];
    char                keys[SC_KEY_LAST + 1];
    // Virtual cursor position when cursor is disabled
    double              virtualCursorPosX, virtualCursorPosY;
    bool            rawMouseMotion;


    sc_wsi_win_cb       callbacks;

    // This is defined in platform.h
    GLFW_PLATFORM_WINDOW_STATE
};

// Monitor structure
//
struct monitor_t
{
    char            name[128];
    void*           userPointer;

    // Physical dimensions in millimeters.
    int             widthMM, heightMM;

    // The window whose video mode is current on this monitor
    window_st*    window;

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
struct cursor_t
{
    struct cursor_t*    next;
    // This is defined in platform.h
    GLFW_PLATFORM_CURSOR_STATE
};

// Platform API structure
//
struct platform_t
{
    int platformID;
    // init
    int (*init)(void);
    void (*terminate)(void);
    // input
    void (*getCursorPos)(window_st*,double*,double*);
    void (*setCursorPos)(window_st*,double,double);
    void (*setCursorMode)(window_st*,int);
    void (*setRawMouseMotion)(window_st*,bool);
    bool (*rawMouseMotionSupported)(void);
    bool (*createCursor)(cursor_st*,const GLFWimage*,int,int);
    bool (*createStandardCursor)(cursor_st*,int);
    void (*destroyCursor)(cursor_st*);
    void (*setCursor)(window_st*,cursor_st*);
    const char* (*getScancodeName)(int);
    int (*getKeyScancode)(int);
    void (*setClipboardString)(const char*);
    const char* (*getClipboardString)(void);
    // monitor
    void (*freeMonitor)(monitor_st*);
    void (*getMonitorPos)(monitor_st*,int*,int*);
    void (*getMonitorContentScale)(monitor_st*,float*,float*);
    void (*getMonitorWorkarea)(monitor_st*,int*,int*,int*,int*);
    GLFWvidmode* (*getVideoModes)(monitor_st*,int*);
    bool (*getVideoMode)(monitor_st*,GLFWvidmode*);
    bool (*getGammaRamp)(monitor_st*,GLFWgammaramp*);
    void (*setGammaRamp)(monitor_st*,const GLFWgammaramp*);
    // window
    bool (*createWindow)(window_st*,const wnd_config_st*);
    void (*destroyWindow)(window_st*);
    void (*setWindowTitle)(window_st*,const char*);
    void (*setWindowIcon)(window_st*,int,const GLFWimage*);
    void (*getWindowPos)(window_st*,int*,int*);
    void (*setWindowPos)(window_st*,int,int);
    void (*getWindowSize)(window_st*,int*,int*);
    void (*getFramebufferSize)(window_st*,int*,int*);
    void (*setWindowSize)(window_st*,int,int);
    void (*setWindowSizeLimits)(window_st*,int,int,int,int);
    void (*setWindowAspectRatio)(window_st*,int,int);
    void (*getWindowFrameSize)(window_st*,int*,int*,int*,int*);
    void (*getWindowContentScale)(window_st*,float*,float*);
    void (*iconifyWindow)(window_st*);
    void (*restoreWindow)(window_st*);
    void (*maximizeWindow)(window_st*);
    void (*showWindow)(window_st*);
    void (*hideWindow)(window_st*);
    void (*requestWindowAttention)(window_st*);
    void (*focusWindow)(window_st*);
    void (*setWindowMonitor)(window_st*,monitor_st*,int,int,int,int,int);
    bool (*windowFocused)(window_st*);
    bool (*windowIconified)(window_st*);
    bool (*windowVisible)(window_st*);
    bool (*windowMaximized)(window_st*);
    bool (*windowHovered)(window_st*);
    float (*getWindowOpacity)(window_st*);
    void (*setWindowResizable)(window_st*,bool);
    void (*setWindowDecorated)(window_st*,bool);
    void (*setWindowFloating)(window_st*,bool);
    void (*setWindowOpacity)(window_st*,float);
    void (*setWindowMousePassthrough)(window_st*,bool);
    void (*pollEvents)(void);
    void (*waitEvents)(void);
    void (*waitEventsTimeout)(double);
    void (*postEmptyEvent)(void);
};

// Library global data
//
struct library_t
{
    bool                initialized;

    platform_st         platform;

    struct {
        init_config_st  init;
        wnd_config_st   window;
        int             refreshRate;
    } hints;

    cursor_st*          cursorListHead;
    window_st*          windowListHead;

    monitor_st**        monitors;
    int                 monitorCount;

    struct {
        uint64_t        offset;   // 基准时刻（单调时钟纳秒），get_time 以此为零点
    } timer;

    struct {
        sc_monitor_cb   monitor;
    } callbacks;

    // These are defined in platform.h
    GLFW_PLATFORM_LIBRARY_WINDOW_STATE
    GLFW_PLATFORM_LIBRARY_CONTEXT_STATE
};

// Global state shared between compilation units of GLFW
//
extern library_st g_wsi;

//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

void* impl_platform_load_module(const char* path);
void impl_platform_unload_module(void* module);
GLFWproc impl_platform_get_module_symbol(void* module, const char* name);

//////////////////////////////////////////////////////////////////////////
//////                         GLFW event API                       //////
//////////////////////////////////////////////////////////////////////////

void impl_on_win_focus(window_st* window, bool focused);
void impl_on_win_pos(window_st* window, int xpos, int ypos);
void impl_on_win_size(window_st* window, int width, int height);
void impl_on_win_content_scale(window_st* window,
                                  float xscale, float yscale);
void impl_on_win_iconify(window_st* window, bool iconified);
void impl_on_win_maximize(window_st* window, bool maximized);
void impl_on_win_damage(window_st* window);
void impl_on_win_close_req(window_st* window);

void impl_on_key(window_st* window,
                   int key, int scancode, int action, int mods);
void impl_on_chr(window_st* window,
                    uint32_t codepoint, int mods, bool plain);
void impl_on_cursor_pos(window_st* window, double xpos, double ypos);
void impl_on_cursor_enter(window_st* window, bool entered);
void impl_on_mouse_click(window_st* window, int button, int action, int mods);
void impl_on_scroll(window_st* window, double xoffset, double yoffset);
void impl_on_drop(window_st* window, int count, const char** names);

void impl_on_win_monitor(window_st* window, monitor_st* monitor);
void impl_on_monitor(monitor_st* monitor, int action, int placement);
void impl_on_monitor_window(monitor_st* monitor, window_st* window);

#if defined(__GNUC__)
void impl_on_error(int code, const char* format, ...)
    __attribute__((format(printf, 2, 3)));
#else
void impl_on_error(int code, const char* format, ...);
#endif


//////////////////////////////////////////////////////////////////////////
//////                       GLFW internal API                      //////
//////////////////////////////////////////////////////////////////////////

bool wsi_select_platform(int platformID, platform_st* platform);

const GLFWvidmode* wsi_choose_video_mode(monitor_st* monitor,
                                        const GLFWvidmode* desired);
int wsi_compare_video_mode(const GLFWvidmode* first, const GLFWvidmode* second);
monitor_st* wsi_alloc_monitor(const char* name, int widthMM, int heightMM);
void wsi_free_monitor(monitor_st* monitor);

void wsi_alloc_gamma_arrays(GLFWgammaramp* ramp, unsigned int size);
void wsi_free_gamma_arrays(GLFWgammaramp* ramp);

void wsi_split_bpp(int bpp, int* red, int* green, int* blue);

void wsi_center_cursor_in_content_area(window_st* window);

size_t wsi_encode_urf8(char* s, uint32_t codepoint);
char** wsi_parse_url_list(char* text, int* count);

char* wsi_strdup(const char* source);
int wsi_min(int a, int b);
int wsi_max(int a, int b);

// 单调时钟当前值（纳秒）。定义于 init.c，经 sc builtins/platform.h 的 P_clock_now 实现。
uint64_t wsi_clock_ns(void);

void* wsi_calloc(size_t count, size_t size);
void* wsi_realloc(void* pointer, size_t size);
void wsi_free(void* pointer);

