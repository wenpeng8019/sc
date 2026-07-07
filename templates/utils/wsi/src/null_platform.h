
#ifndef NULL_PLATFORM_H
#define NULL_PLATFORM_H

#include "../wsi.h"

#define NULL_WINDOW_STATE          null_window_t null;
#define NULL_LIBRARY_WINDOW_STATE  null_library_t null;
#define NULL_MONITOR_STATE         null_monitor_t null;

#define NULL_CONTEXT_STATE
#define NULL_CURSOR_STATE
#define NULL_LIBRARY_CONTEXT_STATE

#define NULL_SC_FIRST          NULL_SC_SPACE
#define NULL_SC_SPACE          1
#define NULL_SC_APOSTROPHE     2
#define NULL_SC_COMMA          3
#define NULL_SC_MINUS          4
#define NULL_SC_PERIOD         5
#define NULL_SC_SLASH          6
#define NULL_SC_0              7
#define NULL_SC_1              8
#define NULL_SC_2              9
#define NULL_SC_3              10
#define NULL_SC_4              11
#define NULL_SC_5              12
#define NULL_SC_6              13
#define NULL_SC_7              14
#define NULL_SC_8              15
#define NULL_SC_9              16
#define NULL_SC_SEMICOLON      17
#define NULL_SC_EQUAL          18
#define NULL_SC_LEFT_BRACKET   19
#define NULL_SC_BACKSLASH      20
#define NULL_SC_RIGHT_BRACKET  21
#define NULL_SC_GRAVE_ACCENT   22
#define NULL_SC_WORLD_1        23
#define NULL_SC_WORLD_2        24
#define NULL_SC_ESCAPE         25
#define NULL_SC_ENTER          26
#define NULL_SC_TAB            27
#define NULL_SC_BACKSPACE      28
#define NULL_SC_INSERT         29
#define NULL_SC_DELETE         30
#define NULL_SC_RIGHT          31
#define NULL_SC_LEFT           32
#define NULL_SC_DOWN           33
#define NULL_SC_UP             34
#define NULL_SC_PAGE_UP        35
#define NULL_SC_PAGE_DOWN      36
#define NULL_SC_HOME           37
#define NULL_SC_END            38
#define NULL_SC_CAPS_LOCK      39
#define NULL_SC_SCROLL_LOCK    40
#define NULL_SC_NUM_LOCK       41
#define NULL_SC_PRINT_SCREEN   42
#define NULL_SC_PAUSE          43
#define NULL_SC_A              44
#define NULL_SC_B              45
#define NULL_SC_C              46
#define NULL_SC_D              47
#define NULL_SC_E              48
#define NULL_SC_F              49
#define NULL_SC_G              50
#define NULL_SC_H              51
#define NULL_SC_I              52
#define NULL_SC_J              53
#define NULL_SC_K              54
#define NULL_SC_L              55
#define NULL_SC_M              56
#define NULL_SC_N              57
#define NULL_SC_O              58
#define NULL_SC_P              59
#define NULL_SC_Q              60
#define NULL_SC_R              61
#define NULL_SC_S              62
#define NULL_SC_T              63
#define NULL_SC_U              64
#define NULL_SC_V              65
#define NULL_SC_W              66
#define NULL_SC_X              67
#define NULL_SC_Y              68
#define NULL_SC_Z              69
#define NULL_SC_F1             70
#define NULL_SC_F2             71
#define NULL_SC_F3             72
#define NULL_SC_F4             73
#define NULL_SC_F5             74
#define NULL_SC_F6             75
#define NULL_SC_F7             76
#define NULL_SC_F8             77
#define NULL_SC_F9             78
#define NULL_SC_F10            79
#define NULL_SC_F11            80
#define NULL_SC_F12            81
#define NULL_SC_F13            82
#define NULL_SC_F14            83
#define NULL_SC_F15            84
#define NULL_SC_F16            85
#define NULL_SC_F17            86
#define NULL_SC_F18            87
#define NULL_SC_F19            88
#define NULL_SC_F20            89
#define NULL_SC_F21            90
#define NULL_SC_F22            91
#define NULL_SC_F23            92
#define NULL_SC_F24            93
#define NULL_SC_F25            94
#define NULL_SC_KP_0           95
#define NULL_SC_KP_1           96
#define NULL_SC_KP_2           97
#define NULL_SC_KP_3           98
#define NULL_SC_KP_4           99
#define NULL_SC_KP_5           100
#define NULL_SC_KP_6           101
#define NULL_SC_KP_7           102
#define NULL_SC_KP_8           103
#define NULL_SC_KP_9           104
#define NULL_SC_KP_DECIMAL     105
#define NULL_SC_KP_DIVIDE      106
#define NULL_SC_KP_MULTIPLY    107
#define NULL_SC_KP_SUBTRACT    108
#define NULL_SC_KP_ADD         109
#define NULL_SC_KP_ENTER       110
#define NULL_SC_KP_EQUAL       111
#define NULL_SC_LEFT_SHIFT     112
#define NULL_SC_LEFT_CONTROL   113
#define NULL_SC_LEFT_ALT       114
#define NULL_SC_LEFT_SUPER     115
#define NULL_SC_RIGHT_SHIFT    116
#define NULL_SC_RIGHT_CONTROL  117
#define NULL_SC_RIGHT_ALT      118
#define NULL_SC_RIGHT_SUPER    119
#define NULL_SC_MENU           120
#define NULL_SC_LAST           NULL_SC_MENU

typedef struct null_window_t
{
    int             xpos;
    int             ypos;
    int             width;
    int             height;
    bool        visible;
    bool        iconified;
    bool        maximized;
    bool        resizable;
    bool        decorated;
    bool        floating;
    bool        transparent;
    float           opacity;
} null_window_t;

typedef struct null_monitor_t
{
    GLFWgammaramp   ramp;
} null_monitor_t;

typedef struct null_library_t
{
    int             xcursor;
    int             ycursor;
    char*           clipboardString;
    window_st*    focusedWindow;
    uint16_t        keycodes[NULL_SC_LAST + 1];
    uint8_t         scancodes[SC_KEY_LAST + 1];
} null_library_t;

void null_poll_monitors(void);

bool null_connect(int platformID, platform_st* platform);

int null_init(void);
void null_terminate(void);

void null_poll_events(void);
void null_wait_events(void);
void null_wait_eventsTimeout(double timeout);
void null_post_empty_event(void);

bool null_create_window(window_st* window, const wnd_config_st* wndconfig);
void null_destroy_window(window_st* window);
void null_set_window_title(window_st* window, const char* title);
void null_set_window_icon(window_st* window, int count, const GLFWimage* images);
void null_set_window_mouse_passthrough(window_st* window, bool enabled);
void null_set_window_monitor(window_st* window, monitor_st* monitor, int xpos, int ypos, int width, int height, int refreshRate);

void null_set_window_decorated(window_st* window, bool enabled);
void null_set_window_resizable(window_st* window, bool enabled);
void null_set_window_floating(window_st* window, bool enabled);
void null_set_window_opacity(window_st* window, float opacity);
float null_get_window_opacity(window_st* window);

void null_get_window_pos(window_st* window, int* xpos, int* ypos);
void null_set_window_pos(window_st* window, int xpos, int ypos);
void null_get_window_size(window_st* window, int* width, int* height);
void null_set_window_size(window_st* window, int width, int height);
void null_get_window_frame_size(window_st* window, int* left, int* top, int* right, int* bottom);
void null_set_window_size_limits(window_st* window, int minwidth, int minheight, int maxwidth, int maxheight);
void null_get_window_content_scale(window_st* window, float* xscale, float* yscale);
void null_set_window_aspect_ratio(window_st* window, int n, int d);

void null_show_window(window_st* window);
void null_hide_window(window_st* window);
void null_maximize_window(window_st* window);
void null_restore_window(window_st* window);
void null_focus_window(window_st* window);
void null_iconify_window(window_st* window);
void null_request_window_attention(window_st* window);

bool null_window_visible(window_st* window);
bool null_window_maximized(window_st* window);
bool null_window_focused(window_st* window);
bool null_window_hovered(window_st* window);
bool null_window_iconified(window_st* window);

void null_set_cursor(window_st* window, cursor_st* cursor);
bool null_create_standard_cursor(cursor_st* cursor, int shape);
bool null_create_cursor(cursor_st* cursor, const GLFWimage* image, int xhot, int yhot);
void null_destroy_cursor(cursor_st* cursor);
void null_set_cursor_mode(window_st* window, int mode);
void null_set_cursor_pos(window_st* window, double x, double y);
void null_get_cursor_pos(window_st* window, double* xpos, double* ypos);
void null_set_mouse_raw_motion(window_st *window, bool enabled);
bool null_mouse_raw_motion_supported(void);

int null_get_key_scancode(int key);
const char* null_get_scancode_name(int scancode);
const char* null_get_clipboard_string(void);
void null_set_clipboard_string(const char* string);

void null_free_monitor(monitor_st* monitor);
void null_get_monitor_pos(monitor_st* monitor, int* xpos, int* ypos);
void null_get_monitor_work_area(monitor_st* monitor, int* xpos, int* ypos, int* width, int* height);
void null_get_monitor_content_scale(monitor_st* monitor, float* xscale, float* yscale);
GLFWvidmode* null_get_video_modes(monitor_st* monitor, int* found);
bool null_get_video_mode(monitor_st* monitor, GLFWvidmode* mode);
bool null_get_gamma_ramp(monitor_st* monitor, GLFWgammaramp* ramp);
void null_set_gamma_ramp(monitor_st* monitor, const GLFWgammaramp* ramp);

#endif // NULL_PLATFORM_H