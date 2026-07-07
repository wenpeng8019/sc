
#ifndef WL_PLATFORM_H
#define WL_PLATFORM_H

#include "../wsi.h"

#include <wayland-client-core.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>

typedef int (* PFN_wl_display_flush)(struct wl_display* display);
typedef void (* PFN_wl_display_cancel_read)(struct wl_display* display);
typedef int (* PFN_wl_display_dispatch_pending)(struct wl_display* display);
typedef int (* PFN_wl_display_read_events)(struct wl_display* display);
typedef struct wl_display* (* PFN_wl_display_connect)(const char*);
typedef void (* PFN_wl_display_disconnect)(struct wl_display*);
typedef int (* PFN_wl_display_roundtrip)(struct wl_display*);
typedef int (* PFN_wl_display_get_fd)(struct wl_display*);
typedef int (* PFN_wl_display_prepare_read)(struct wl_display*);
typedef struct wl_event_queue* (* PFN_wl_display_create_queue)(struct wl_display*);
typedef void (* PFN_wl_event_queue_destroy)(struct wl_event_queue*);
typedef int (* PFN_wl_display_prepare_read_queue)(struct wl_display*,struct wl_event_queue*);
typedef int (* PFN_wl_display_dispatch_queue_pending)(struct wl_display*,struct wl_event_queue*);
typedef void (* PFN_wl_proxy_marshal)(struct wl_proxy*,uint32_t,...);
typedef int (* PFN_wl_proxy_add_listener)(struct wl_proxy*,void(**)(void),void*);
typedef void (* PFN_wl_proxy_destroy)(struct wl_proxy*);
typedef struct wl_proxy* (* PFN_wl_proxy_marshal_constructor)(struct wl_proxy*,uint32_t,const struct wl_interface*,...);
typedef struct wl_proxy* (* PFN_wl_proxy_marshal_constructor_versioned)(struct wl_proxy*,uint32_t,const struct wl_interface*,uint32_t,...);
typedef void* (* PFN_wl_proxy_get_user_data)(struct wl_proxy*);
typedef void (* PFN_wl_proxy_set_user_data)(struct wl_proxy*,void*);
typedef void (* PFN_wl_proxy_set_tag)(struct wl_proxy*,const char*const*);
typedef const char* const* (* PFN_wl_proxy_get_tag)(struct wl_proxy*);
typedef uint32_t (* PFN_wl_proxy_get_version)(struct wl_proxy*);
typedef struct wl_proxy* (* PFN_wl_proxy_marshal_flags)(struct wl_proxy*,uint32_t,const struct wl_interface*,uint32_t,uint32_t,...);
typedef void* (* PFN_wl_proxy_create_wrapper)(void*);
typedef void (* PFN_wl_proxy_wrapper_destroy)(void*);
typedef void (* PFN_wl_proxy_set_queue)(struct wl_proxy*,struct wl_event_queue*);
#define wl_display_flush g_wsi.wl.client.display_flush
#define wl_display_cancel_read g_wsi.wl.client.display_cancel_read
#define wl_display_dispatch_pending g_wsi.wl.client.display_dispatch_pending
#define wl_display_read_events g_wsi.wl.client.display_read_events
#define wl_display_disconnect g_wsi.wl.client.display_disconnect
#define wl_display_roundtrip g_wsi.wl.client.display_roundtrip
#define wl_display_get_fd g_wsi.wl.client.display_get_fd
#define wl_display_prepare_read g_wsi.wl.client.display_prepare_read
#define wl_display_create_queue g_wsi.wl.client.display_create_queue
#define wl_display_prepare_read_queue g_wsi.wl.client.display_prepare_read_queue
#define wl_display_dispatch_queue_pending g_wsi.wl.client.display_dispatch_queue_pending
#define wl_event_queue_destroy g_wsi.wl.client.event_queue_destroy
#define wl_proxy_marshal g_wsi.wl.client.proxy_marshal
#define wl_proxy_add_listener g_wsi.wl.client.proxy_add_listener
#define wl_proxy_destroy g_wsi.wl.client.proxy_destroy
#define wl_proxy_marshal_constructor g_wsi.wl.client.proxy_marshal_constructor
#define wl_proxy_marshal_constructor_versioned g_wsi.wl.client.proxy_marshal_constructor_versioned
#define wl_proxy_get_user_data g_wsi.wl.client.proxy_get_user_data
#define wl_proxy_set_user_data g_wsi.wl.client.proxy_set_user_data
#define wl_proxy_get_tag g_wsi.wl.client.proxy_get_tag
#define wl_proxy_set_tag g_wsi.wl.client.proxy_set_tag
#define wl_proxy_get_version g_wsi.wl.client.proxy_get_version
#define wl_proxy_marshal_flags g_wsi.wl.client.proxy_marshal_flags
#define wl_proxy_create_wrapper g_wsi.wl.client.proxy_create_wrapper
#define wl_proxy_wrapper_destroy g_wsi.wl.client.proxy_wrapper_destroy
#define wl_proxy_set_queue g_wsi.wl.client.proxy_set_queue

struct wl_shm;
struct wl_output;

#define wl_display_interface _glfw_wl_display_interface
#define wl_subcompositor_interface _glfw_wl_subcompositor_interface
#define wl_compositor_interface _glfw_wl_compositor_interface
#define wl_shm_interface _glfw_wl_shm_interface
#define wl_data_device_manager_interface _glfw_wl_data_device_manager_interface
#define wl_shell_interface _glfw_wl_shell_interface
#define wl_buffer_interface _glfw_wl_buffer_interface
#define wl_callback_interface _glfw_wl_callback_interface
#define wl_data_device_interface _glfw_wl_data_device_interface
#define wl_data_offer_interface _glfw_wl_data_offer_interface
#define wl_data_source_interface _glfw_wl_data_source_interface
#define wl_keyboard_interface _glfw_wl_keyboard_interface
#define wl_output_interface _glfw_wl_output_interface
#define wl_pointer_interface _glfw_wl_pointer_interface
#define wl_region_interface _glfw_wl_region_interface
#define wl_registry_interface _glfw_wl_registry_interface
#define wl_seat_interface _glfw_wl_seat_interface
#define wl_shell_surface_interface _glfw_wl_shell_surface_interface
#define wl_shm_pool_interface _glfw_wl_shm_pool_interface
#define wl_subsurface_interface _glfw_wl_subsurface_interface
#define wl_surface_interface _glfw_wl_surface_interface
#define wl_touch_interface _glfw_wl_touch_interface
#define zwp_idle_inhibitor_v1_interface _glfw_zwp_idle_inhibitor_v1_interface
#define zwp_idle_inhibit_manager_v1_interface _glfw_zwp_idle_inhibit_manager_v1_interface
#define zwp_confined_pointer_v1_interface _glfw_zwp_confined_pointer_v1_interface
#define zwp_locked_pointer_v1_interface _glfw_zwp_locked_pointer_v1_interface
#define zwp_pointer_constraints_v1_interface _glfw_zwp_pointer_constraints_v1_interface
#define zwp_relative_pointer_v1_interface _glfw_zwp_relative_pointer_v1_interface
#define zwp_relative_pointer_manager_v1_interface _glfw_zwp_relative_pointer_manager_v1_interface
#define wp_viewport_interface _glfw_wp_viewport_interface
#define wp_viewporter_interface _glfw_wp_viewporter_interface
#define xdg_toplevel_interface _glfw_xdg_toplevel_interface
#define zxdg_toplevel_decoration_v1_interface _glfw_zxdg_toplevel_decoration_v1_interface
#define zxdg_decoration_manager_v1_interface _glfw_zxdg_decoration_manager_v1_interface
#define xdg_popup_interface _glfw_xdg_popup_interface
#define xdg_positioner_interface _glfw_xdg_positioner_interface
#define xdg_surface_interface _glfw_xdg_surface_interface
#define xdg_toplevel_interface _glfw_xdg_toplevel_interface
#define xdg_wm_base_interface _glfw_xdg_wm_base_interface
#define xdg_activation_v1_interface _glfw_xdg_activation_v1_interface
#define xdg_activation_token_v1_interface _glfw_xdg_activation_token_v1_interface
#define wl_surface_interface _glfw_wl_surface_interface
#define wp_fractional_scale_v1_interface _glfw_wp_fractional_scale_v1_interface

#define GLFW_WAYLAND_WINDOW_STATE         _sc_windowWayland  wl;
#define GLFW_WAYLAND_LIBRARY_WINDOW_STATE _GLFWlibraryWayland wl;
#define GLFW_WAYLAND_MONITOR_STATE        _sc_monitorWayland wl;
#define GLFW_WAYLAND_CURSOR_STATE         _sc_cursorWayland  wl;

struct wl_cursor_image
{
    uint32_t width;
    uint32_t height;
    uint32_t hotspot_x;
    uint32_t hotspot_y;
    uint32_t delay;
};

struct wl_cursor
{
    unsigned int image_count;
    struct wl_cursor_image** images;
    char* name;
};

typedef struct wl_cursor_theme* (* PFN_wl_cursor_theme_load)(const char*, int, struct wl_shm*);
typedef void (* PFN_wl_cursor_theme_destroy)(struct wl_cursor_theme*);
typedef struct wl_cursor* (* PFN_wl_cursor_theme_get_cursor)(struct wl_cursor_theme*, const char*);
typedef struct wl_buffer* (* PFN_wl_cursor_image_get_buffer)(struct wl_cursor_image*);
#define wl_cursor_theme_load g_wsi.wl.cursor.theme_load
#define wl_cursor_theme_destroy g_wsi.wl.cursor.theme_destroy
#define wl_cursor_theme_get_cursor g_wsi.wl.cursor.theme_get_cursor
#define wl_cursor_image_get_buffer g_wsi.wl.cursor.image_get_buffer

typedef struct xkb_context* (* PFN_xkb_context_new)(enum xkb_context_flags);
typedef void (* PFN_xkb_context_unref)(struct xkb_context*);
typedef struct xkb_keymap* (* PFN_xkb_keymap_new_from_string)(struct xkb_context*, const char*, enum xkb_keymap_format, enum xkb_keymap_compile_flags);
typedef void (* PFN_xkb_keymap_unref)(struct xkb_keymap*);
typedef xkb_mod_index_t (* PFN_xkb_keymap_mod_get_index)(struct xkb_keymap*, const char*);
typedef int (* PFN_xkb_keymap_key_repeats)(struct xkb_keymap*, xkb_keycode_t);
typedef int (* PFN_xkb_keymap_key_get_syms_by_level)(struct xkb_keymap*,xkb_keycode_t,xkb_layout_index_t,xkb_level_index_t,const xkb_keysym_t**);
typedef struct xkb_state* (* PFN_xkb_state_new)(struct xkb_keymap*);
typedef void (* PFN_xkb_state_unref)(struct xkb_state*);
typedef int (* PFN_xkb_state_key_get_syms)(struct xkb_state*, xkb_keycode_t, const xkb_keysym_t**);
typedef enum xkb_state_component (* PFN_xkb_state_update_mask)(struct xkb_state*, xkb_mod_mask_t, xkb_mod_mask_t, xkb_mod_mask_t, xkb_layout_index_t, xkb_layout_index_t, xkb_layout_index_t);
typedef xkb_layout_index_t (* PFN_xkb_state_key_get_layout)(struct xkb_state*,xkb_keycode_t);
typedef int (* PFN_xkb_state_mod_index_is_active)(struct xkb_state*,xkb_mod_index_t,enum xkb_state_component);
typedef uint32_t (* PFN_xkb_keysym_to_utf32)(xkb_keysym_t);
typedef int (* PFN_xkb_keysym_to_utf8)(xkb_keysym_t, char*, size_t);
#define xkb_context_new g_wsi.wl.xkb.context_new
#define xkb_context_unref g_wsi.wl.xkb.context_unref
#define xkb_keymap_new_from_string g_wsi.wl.xkb.keymap_new_from_string
#define xkb_keymap_unref g_wsi.wl.xkb.keymap_unref
#define xkb_keymap_mod_get_index g_wsi.wl.xkb.keymap_mod_get_index
#define xkb_keymap_key_repeats g_wsi.wl.xkb.keymap_key_repeats
#define xkb_keymap_key_get_syms_by_level g_wsi.wl.xkb.keymap_key_get_syms_by_level
#define xkb_state_new g_wsi.wl.xkb.state_new
#define xkb_state_unref g_wsi.wl.xkb.state_unref
#define xkb_state_key_get_syms g_wsi.wl.xkb.state_key_get_syms
#define xkb_state_update_mask g_wsi.wl.xkb.state_update_mask
#define xkb_state_key_get_layout g_wsi.wl.xkb.state_key_get_layout
#define xkb_state_mod_index_is_active g_wsi.wl.xkb.state_mod_index_is_active
#define xkb_keysym_to_utf32 g_wsi.wl.xkb.keysym_to_utf32
#define xkb_keysym_to_utf8 g_wsi.wl.xkb.keysym_to_utf8

typedef struct xkb_compose_table* (* PFN_xkb_compose_table_new_from_locale)(struct xkb_context*, const char*, enum xkb_compose_compile_flags);
typedef void (* PFN_xkb_compose_table_unref)(struct xkb_compose_table*);
typedef struct xkb_compose_state* (* PFN_xkb_compose_state_new)(struct xkb_compose_table*, enum xkb_compose_state_flags);
typedef void (* PFN_xkb_compose_state_unref)(struct xkb_compose_state*);
typedef enum xkb_compose_feed_result (* PFN_xkb_compose_state_feed)(struct xkb_compose_state*, xkb_keysym_t);
typedef enum xkb_compose_status (* PFN_xkb_compose_state_get_status)(struct xkb_compose_state*);
typedef xkb_keysym_t (* PFN_xkb_compose_state_get_one_sym)(struct xkb_compose_state*);
#define xkb_compose_table_new_from_locale g_wsi.wl.xkb.compose_table_new_from_locale
#define xkb_compose_table_unref g_wsi.wl.xkb.compose_table_unref
#define xkb_compose_state_new g_wsi.wl.xkb.compose_state_new
#define xkb_compose_state_unref g_wsi.wl.xkb.compose_state_unref
#define xkb_compose_state_feed g_wsi.wl.xkb.compose_state_feed
#define xkb_compose_state_get_status g_wsi.wl.xkb.compose_state_get_status
#define xkb_compose_state_get_one_sym g_wsi.wl.xkb.compose_state_get_one_sym

struct libdecor;
struct libdecor_frame;
struct libdecor_state;
struct libdecor_configuration;
struct wl_surface;

enum libdecor_error
{
    LIBDECOR_ERROR_COMPOSITOR_INCOMPATIBLE,
    LIBDECOR_ERROR_INVALID_FRAME_CONFIGURATION,
};

enum libdecor_window_state
{
    LIBDECOR_WINDOW_STATE_NONE = 0,
    LIBDECOR_WINDOW_STATE_ACTIVE = 1,
    LIBDECOR_WINDOW_STATE_MAXIMIZED = 2,
    LIBDECOR_WINDOW_STATE_FULLSCREEN = 4,
    LIBDECOR_WINDOW_STATE_TILED_LEFT = 8,
    LIBDECOR_WINDOW_STATE_TILED_RIGHT = 16,
    LIBDECOR_WINDOW_STATE_TILED_TOP = 32,
    LIBDECOR_WINDOW_STATE_TILED_BOTTOM = 64
};

enum libdecor_capabilities
{
    LIBDECOR_ACTION_MOVE = 1,
    LIBDECOR_ACTION_RESIZE = 2,
    LIBDECOR_ACTION_MINIMIZE = 4,
    LIBDECOR_ACTION_FULLSCREEN = 8,
    LIBDECOR_ACTION_CLOSE = 16
};

struct libdecor_interface
{
    void (* error)(struct libdecor*,enum libdecor_error,const char*);
    void (* reserved0)(void);
    void (* reserved1)(void);
    void (* reserved2)(void);
    void (* reserved3)(void);
    void (* reserved4)(void);
    void (* reserved5)(void);
    void (* reserved6)(void);
    void (* reserved7)(void);
    void (* reserved8)(void);
    void (* reserved9)(void);
};

struct libdecor_frame_interface
{
    void (* configure)(struct libdecor_frame*,struct libdecor_configuration*,void*);
    void (* close)(struct libdecor_frame*,void*);
    void (* commit)(struct libdecor_frame*,void*);
    void (* dismiss_popup)(struct libdecor_frame*,const char*,void*);
    void (* reserved0)(void);
    void (* reserved1)(void);
    void (* reserved2)(void);
    void (* reserved3)(void);
    void (* reserved4)(void);
    void (* reserved5)(void);
    void (* reserved6)(void);
    void (* reserved7)(void);
    void (* reserved8)(void);
    void (* reserved9)(void);
};

typedef struct libdecor* (* PFN_libdecor_new)(struct wl_display*,const struct libdecor_interface*);
typedef void (* PFN_libdecor_unref)(struct libdecor*);
typedef int (* PFN_libdecor_get_fd)(struct libdecor*);
typedef int (* PFN_libdecor_dispatch)(struct libdecor*,int);
typedef struct libdecor_frame* (* PFN_libdecor_decorate)(struct libdecor*,struct wl_surface*,const struct libdecor_frame_interface*,void*);
typedef void (* PFN_libdecor_frame_unref)(struct libdecor_frame*);
typedef void (* PFN_libdecor_frame_set_app_id)(struct libdecor_frame*,const char*);
typedef void (* PFN_libdecor_frame_set_title)(struct libdecor_frame*,const char*);
typedef void (* PFN_libdecor_frame_set_minimized)(struct libdecor_frame*);
typedef void (* PFN_libdecor_frame_set_fullscreen)(struct libdecor_frame*,struct wl_output*);
typedef void (* PFN_libdecor_frame_unset_fullscreen)(struct libdecor_frame*);
typedef void (* PFN_libdecor_frame_map)(struct libdecor_frame*);
typedef void (* PFN_libdecor_frame_commit)(struct libdecor_frame*,struct libdecor_state*,struct libdecor_configuration*);
typedef void (* PFN_libdecor_frame_set_min_content_size)(struct libdecor_frame*,int,int);
typedef void (* PFN_libdecor_frame_set_max_content_size)(struct libdecor_frame*,int,int);
typedef void (* PFN_libdecor_frame_set_maximized)(struct libdecor_frame*);
typedef void (* PFN_libdecor_frame_unset_maximized)(struct libdecor_frame*);
typedef void (* PFN_libdecor_frame_set_capabilities)(struct libdecor_frame*,enum libdecor_capabilities);
typedef void (* PFN_libdecor_frame_unset_capabilities)(struct libdecor_frame*,enum libdecor_capabilities);
typedef void (* PFN_libdecor_frame_set_visibility)(struct libdecor_frame*,bool visible);
typedef bool (* PFN_libdecor_frame_is_visible)(struct libdecor_frame*);
typedef struct xdg_toplevel* (* PFN_libdecor_frame_get_xdg_toplevel)(struct libdecor_frame*);
typedef bool (* PFN_libdecor_configuration_get_content_size)(struct libdecor_configuration*,struct libdecor_frame*,int*,int*);
typedef bool (* PFN_libdecor_configuration_get_window_state)(struct libdecor_configuration*,enum libdecor_window_state*);
typedef struct libdecor_state* (* PFN_libdecor_state_new)(int,int);
typedef void (* PFN_libdecor_state_free)(struct libdecor_state*);
#define libdecor_new g_wsi.wl.libdecor.libdecor_new_
#define libdecor_unref g_wsi.wl.libdecor.libdecor_unref_
#define libdecor_get_fd g_wsi.wl.libdecor.libdecor_get_fd_
#define libdecor_dispatch g_wsi.wl.libdecor.libdecor_dispatch_
#define libdecor_decorate g_wsi.wl.libdecor.libdecor_decorate_
#define libdecor_frame_unref g_wsi.wl.libdecor.libdecor_frame_unref_
#define libdecor_frame_set_app_id g_wsi.wl.libdecor.libdecor_frame_set_app_id_
#define libdecor_frame_set_title g_wsi.wl.libdecor.libdecor_frame_set_title_
#define libdecor_frame_set_minimized g_wsi.wl.libdecor.libdecor_frame_set_minimized_
#define libdecor_frame_set_fullscreen g_wsi.wl.libdecor.libdecor_frame_set_fullscreen_
#define libdecor_frame_unset_fullscreen g_wsi.wl.libdecor.libdecor_frame_unset_fullscreen_
#define libdecor_frame_map g_wsi.wl.libdecor.libdecor_frame_map_
#define libdecor_frame_commit g_wsi.wl.libdecor.libdecor_frame_commit_
#define libdecor_frame_set_min_content_size g_wsi.wl.libdecor.libdecor_frame_set_min_content_size_
#define libdecor_frame_set_max_content_size g_wsi.wl.libdecor.libdecor_frame_set_max_content_size_
#define libdecor_frame_set_maximized g_wsi.wl.libdecor.libdecor_frame_set_maximized_
#define libdecor_frame_unset_maximized g_wsi.wl.libdecor.libdecor_frame_unset_maximized_
#define libdecor_frame_set_capabilities g_wsi.wl.libdecor.libdecor_frame_set_capabilities_
#define libdecor_frame_unset_capabilities g_wsi.wl.libdecor.libdecor_frame_unset_capabilities_
#define libdecor_frame_set_visibility g_wsi.wl.libdecor.libdecor_frame_set_visibility_
#define libdecor_frame_is_visible g_wsi.wl.libdecor.libdecor_frame_is_visible_
#define libdecor_frame_get_xdg_toplevel g_wsi.wl.libdecor.libdecor_frame_get_xdg_toplevel_
#define libdecor_configuration_get_content_size g_wsi.wl.libdecor.libdecor_configuration_get_content_size_
#define libdecor_configuration_get_window_state g_wsi.wl.libdecor.libdecor_configuration_get_window_state_
#define libdecor_state_new g_wsi.wl.libdecor.libdecor_state_new_
#define libdecor_state_free g_wsi.wl.libdecor.libdecor_state_free_

typedef struct _GLFWfallbackEdgeWayland
{
    struct wl_surface*          surface;
    struct wl_subsurface*       subsurface;
    struct wl_buffer*           buffer;
} _GLFWfallbackEdgeWayland;

typedef struct _GLFWofferWayland
{
    struct wl_data_offer*       offer;
    bool                    text_plain_utf8;
    bool                    text_uri_list;
} _GLFWofferWayland;

typedef struct _GLFWscaleWayland
{
    struct wl_output*           output;
    int32_t                     factor;
} _GLFWscaleWayland;

// Wayland-specific per-window data
//
typedef struct _sc_windowWayland
{
    int                         width, height;
    int                         fbWidth, fbHeight;
    bool                    visible;
    bool                    maximized;
    bool                    activated;
    bool                    fullscreen;
    bool                    transparent;
    bool                    scaleFramebuffer;
    struct wl_surface*          surface;

    struct {
        int                     width, height;
        bool                maximized;
        bool                iconified;
        bool                activated;
        bool                fullscreen;
    } pending;

    struct {
        struct xdg_surface*     surface;
        struct xdg_toplevel*    toplevel;
        struct zxdg_toplevel_decoration_v1* decoration;
        uint32_t                decorationMode;
    } xdg;

    struct {
        struct libdecor_frame*  frame;
    } libdecor;

    double                      cursorPosX, cursorPosY;

    char*                       appId;

    // We need to track the monitors the window spans on to calculate the
    // optimal scaling factor.
    int32_t                     bufferScale;
    _GLFWscaleWayland*          outputScales;
    size_t                      outputScaleCount;
    size_t                      outputScaleSize;

    struct wp_viewport*             scalingViewport;
    uint32_t                        scalingNumerator;
    struct wp_fractional_scale_v1*  fractionalScale;

    struct zwp_relative_pointer_v1* relativePointer;
    struct zwp_locked_pointer_v1*   lockedPointer;
    struct zwp_confined_pointer_v1* confinedPointer;

    struct zwp_idle_inhibitor_v1*   idleInhibitor;
    struct xdg_activation_token_v1* activationToken;

    struct {
        bool                    decorations;
        _GLFWfallbackEdgeWayland    top, left, right, bottom;
        double                      pointerX, pointerY;
        uint32_t                    buttonPressSerial;
        const char*                 cursorName;
    } fallback;
} _sc_windowWayland;

// Wayland-specific global data
//
typedef struct _GLFWlibraryWayland
{
    struct wl_display*          display;
    struct wl_registry*         registry;
    struct wl_compositor*       compositor;
    struct wl_subcompositor*    subcompositor;
    struct wl_shm*              shm;
    struct wl_seat*             seat;
    struct wl_pointer*          pointer;
    struct wl_keyboard*         keyboard;
    struct wl_data_device_manager*          dataDeviceManager;
    struct wl_data_device*      dataDevice;
    struct xdg_wm_base*         wmBase;
    struct zxdg_decoration_manager_v1*      decorationManager;
    struct wp_viewporter*       viewporter;
    struct zwp_relative_pointer_manager_v1* relativePointerManager;
    struct zwp_pointer_constraints_v1*      pointerConstraints;
    struct zwp_idle_inhibit_manager_v1*     idleInhibitManager;
    struct xdg_activation_v1*               activationManager;
    struct wp_fractional_scale_manager_v1*  fractionalScaleManager;

    _GLFWofferWayland*          offers;
    unsigned int                offerCount;

    struct wl_data_offer*       selectionOffer;
    struct wl_data_source*      selectionSource;

    struct wl_data_offer*       dragOffer;
    window_st*                dragFocus;
    uint32_t                    dragSerial;

    const char*                 tag;

    struct wl_surface*          pointerSurface;
    struct wl_cursor_theme*     cursorTheme;
    struct wl_cursor_theme*     cursorThemeHiDPI;
    struct wl_surface*          cursorSurface;
    int                         cursorTimerfd;
    uint32_t                    serial;
    uint32_t                    pointerEnterSerial;

    int                         keyRepeatTimerfd;
    int32_t                     keyRepeatRate;
    int32_t                     keyRepeatDelay;
    int                         keyRepeatScancode;

    char*                       clipboardString;
    short int                   keycodes[256];
    short int                   scancodes[SC_KEY_LAST + 1];
    char                        keynames[SC_KEY_LAST + 1][5];

    struct {
        struct wl_surface*      pointerSurface;
        unsigned int            events;
        double                  pointerX;
        double                  pointerY;
        double                  scrollX;
        double                  scrollY;
        double                  discreteX;
        double                  discreteY;
        int                     button;
        int                     action;
    } pending;

    struct {
        void*                   handle;
        struct xkb_context*     context;
        struct xkb_keymap*      keymap;
        struct xkb_state*       state;

        struct xkb_compose_state* composeState;

        xkb_mod_index_t         controlIndex;
        xkb_mod_index_t         altIndex;
        xkb_mod_index_t         shiftIndex;
        xkb_mod_index_t         superIndex;
        xkb_mod_index_t         capsLockIndex;
        xkb_mod_index_t         numLockIndex;
        unsigned int            modifiers;

        PFN_xkb_context_new context_new;
        PFN_xkb_context_unref context_unref;
        PFN_xkb_keymap_new_from_string keymap_new_from_string;
        PFN_xkb_keymap_unref keymap_unref;
        PFN_xkb_keymap_mod_get_index keymap_mod_get_index;
        PFN_xkb_keymap_key_repeats keymap_key_repeats;
        PFN_xkb_keymap_key_get_syms_by_level keymap_key_get_syms_by_level;
        PFN_xkb_state_new state_new;
        PFN_xkb_state_unref state_unref;
        PFN_xkb_state_key_get_syms state_key_get_syms;
        PFN_xkb_state_update_mask state_update_mask;
        PFN_xkb_state_key_get_layout state_key_get_layout;
        PFN_xkb_state_mod_index_is_active state_mod_index_is_active;
        PFN_xkb_keysym_to_utf32 keysym_to_utf32;
        PFN_xkb_keysym_to_utf8 keysym_to_utf8;

        PFN_xkb_compose_table_new_from_locale compose_table_new_from_locale;
        PFN_xkb_compose_table_unref compose_table_unref;
        PFN_xkb_compose_state_new compose_state_new;
        PFN_xkb_compose_state_unref compose_state_unref;
        PFN_xkb_compose_state_feed compose_state_feed;
        PFN_xkb_compose_state_get_status compose_state_get_status;
        PFN_xkb_compose_state_get_one_sym compose_state_get_one_sym;
    } xkb;

    window_st*                keyboardFocus;

    struct {
        void*                                       handle;
        PFN_wl_display_flush                        display_flush;
        PFN_wl_display_cancel_read                  display_cancel_read;
        PFN_wl_display_dispatch_pending             display_dispatch_pending;
        PFN_wl_display_read_events                  display_read_events;
        PFN_wl_display_disconnect                   display_disconnect;
        PFN_wl_display_roundtrip                    display_roundtrip;
        PFN_wl_display_get_fd                       display_get_fd;
        PFN_wl_display_prepare_read                 display_prepare_read;
        PFN_wl_display_create_queue                 display_create_queue;
        PFN_wl_display_prepare_read_queue           display_prepare_read_queue;
        PFN_wl_display_dispatch_queue_pending       display_dispatch_queue_pending;
        PFN_wl_event_queue_destroy                  event_queue_destroy;
        PFN_wl_proxy_marshal                        proxy_marshal;
        PFN_wl_proxy_add_listener                   proxy_add_listener;
        PFN_wl_proxy_destroy                        proxy_destroy;
        PFN_wl_proxy_marshal_constructor            proxy_marshal_constructor;
        PFN_wl_proxy_marshal_constructor_versioned  proxy_marshal_constructor_versioned;
        PFN_wl_proxy_get_user_data                  proxy_get_user_data;
        PFN_wl_proxy_set_user_data                  proxy_set_user_data;
        PFN_wl_proxy_get_tag                        proxy_get_tag;
        PFN_wl_proxy_set_tag                        proxy_set_tag;
        PFN_wl_proxy_get_version                    proxy_get_version;
        PFN_wl_proxy_marshal_flags                  proxy_marshal_flags;
        PFN_wl_proxy_create_wrapper                 proxy_create_wrapper;
        PFN_wl_proxy_wrapper_destroy                proxy_wrapper_destroy;
        PFN_wl_proxy_set_queue                      proxy_set_queue;
    } client;

    struct {
        void*                   handle;

        PFN_wl_cursor_theme_load theme_load;
        PFN_wl_cursor_theme_destroy theme_destroy;
        PFN_wl_cursor_theme_get_cursor theme_get_cursor;
        PFN_wl_cursor_image_get_buffer image_get_buffer;
    } cursor;

    struct {
        void*                   handle;
        struct libdecor*        context;
        bool                ready;
        PFN_libdecor_new        libdecor_new_;
        PFN_libdecor_unref      libdecor_unref_;
        PFN_libdecor_get_fd     libdecor_get_fd_;
        PFN_libdecor_dispatch   libdecor_dispatch_;
        PFN_libdecor_decorate   libdecor_decorate_;
        PFN_libdecor_frame_unref libdecor_frame_unref_;
        PFN_libdecor_frame_set_app_id libdecor_frame_set_app_id_;
        PFN_libdecor_frame_set_title libdecor_frame_set_title_;
        PFN_libdecor_frame_set_minimized libdecor_frame_set_minimized_;
        PFN_libdecor_frame_set_fullscreen libdecor_frame_set_fullscreen_;
        PFN_libdecor_frame_unset_fullscreen libdecor_frame_unset_fullscreen_;
        PFN_libdecor_frame_map libdecor_frame_map_;
        PFN_libdecor_frame_commit libdecor_frame_commit_;
        PFN_libdecor_frame_set_min_content_size libdecor_frame_set_min_content_size_;
        PFN_libdecor_frame_set_max_content_size libdecor_frame_set_max_content_size_;
        PFN_libdecor_frame_set_maximized libdecor_frame_set_maximized_;
        PFN_libdecor_frame_unset_maximized libdecor_frame_unset_maximized_;
        PFN_libdecor_frame_set_capabilities libdecor_frame_set_capabilities_;
        PFN_libdecor_frame_unset_capabilities libdecor_frame_unset_capabilities_;
        PFN_libdecor_frame_set_visibility libdecor_frame_set_visibility_;
        PFN_libdecor_frame_is_visible libdecor_frame_is_visible_;
        PFN_libdecor_frame_get_xdg_toplevel libdecor_frame_get_xdg_toplevel_;
        PFN_libdecor_configuration_get_content_size libdecor_configuration_get_content_size_;
        PFN_libdecor_configuration_get_window_state libdecor_configuration_get_window_state_;
        PFN_libdecor_state_new libdecor_state_new_;
        PFN_libdecor_state_free libdecor_state_free_;
    } libdecor;
} _GLFWlibraryWayland;

// Wayland-specific per-monitor data
//
typedef struct _sc_monitorWayland
{
    struct wl_output*           output;
    uint32_t                    name;
    int                         currentMode;

    int                         x;
    int                         y;
    int32_t                     scale;
} _sc_monitorWayland;

// Wayland-specific per-cursor data
//
typedef struct _sc_cursorWayland
{
    struct wl_cursor*           cursor;
    struct wl_cursor*           cursorHiDPI;
    struct wl_buffer*           buffer;
    int                         width, height;
    int                         xhot, yhot;
    int                         currentImage;
} _sc_cursorWayland;

bool wayland_connect(int platformID, platform_st* platform);
int wayland_init(void);
void wayland_terminate(void);

bool wayland_create_window(window_st* window, const wnd_config_st* wndconfig);
void wayland_destroy_window(window_st* window);
void wayland_set_window_title(window_st* window, const char* title);
void wayland_set_window_icon(window_st* window, int count, const GLFWimage* images);
void wayland_get_window_pos(window_st* window, int* xpos, int* ypos);
void wayland_set_window_pos(window_st* window, int xpos, int ypos);
void wayland_get_window_size(window_st* window, int* width, int* height);
void wayland_set_window_size(window_st* window, int width, int height);
void wayland_set_window_size_limits(window_st* window, int minwidth, int minheight, int maxwidth, int maxheight);
void wayland_set_window_aspect_ratio(window_st* window, int numer, int denom);
void wayland_get_window_frame_size(window_st* window, int* left, int* top, int* right, int* bottom);
void wayland_get_window_content_scale(window_st* window, float* xscale, float* yscale);
void wayland_iconify_window(window_st* window);
void wayland_restore_window(window_st* window);
void wayland_maximize_window(window_st* window);
void wayland_show_window(window_st* window);
void wayland_hide_window(window_st* window);
void wayland_request_window_attention(window_st* window);
void wayland_focus_window(window_st* window);
void wayland_set_window_monitor(window_st* window, monitor_st* monitor, int xpos, int ypos, int width, int height, int refreshRate);
bool wayland_window_focused(window_st* window);
bool wayland_window_iconified(window_st* window);
bool wayland_window_visible(window_st* window);
bool wayland_window_maximized(window_st* window);
bool wayland_window_hovered(window_st* window);
void wayland_set_window_resizable(window_st* window, bool enabled);
void wayland_set_window_decorated(window_st* window, bool enabled);
void wayland_set_window_floating(window_st* window, bool enabled);
float wayland_get_window_opacity(window_st* window);
void wayland_set_window_opacity(window_st* window, float opacity);
void wayland_set_window_mouse_passthrough(window_st* window, bool enabled);

void wayland_set_mouse_raw_motion(window_st* window, bool enabled);
bool wayland_mouse_raw_motion_supported(void);

void wayland_poll_events(void);
void wayland_wait_events(void);
void wayland_wait_eventsTimeout(double timeout);
void wayland_post_empty_event(void);

void wayland_get_cursor_pos(window_st* window, double* xpos, double* ypos);
void wayland_set_cursor_pos(window_st* window, double xpos, double ypos);
void wayland_set_cursorMode(window_st* window, int mode);
const char* wayland_get_scancode_name(int scancode);
int wayland_get_key_scancode(int key);
bool wayland_create_cursor(cursor_st* cursor, const GLFWimage* image, int xhot, int yhot);
bool wayland_create_standard_cursor(cursor_st* cursor, int shape);
void wayland_destroy_cursor(cursor_st* cursor);
void wayland_set_cursor(window_st* window, cursor_st* cursor);
void wayland_set_clipboard_string(const char* string);
const char* wayland_get_clipboard_string(void);

void wsi_free_monitorWayland(monitor_st* monitor);
void wayland_get_monitor_pos(monitor_st* monitor, int* xpos, int* ypos);
void wayland_get_monitor_content_scale(monitor_st* monitor, float* xscale, float* yscale);
void wayland_get_monitor_work_area(monitor_st* monitor, int* xpos, int* ypos, int* width, int* height);
GLFWvidmode* wayland_get_video_modes(monitor_st* monitor, int* count);
bool wayland_get_video_mode(monitor_st* monitor, GLFWvidmode* mode);
bool wayland_get_gamma_ramp(monitor_st* monitor, GLFWgammaramp* ramp);
void wayland_set_gamma_ramp(monitor_st* monitor, const GLFWgammaramp* ramp);

void wayland_AddOutput(uint32_t name, uint32_t version);
void wayland_UpdateBufferScaleFromOutputs(window_st* window);

void wayland_AddSeatListener(struct wl_seat* seat);
void wayland_AddDataDeviceListener(struct wl_data_device* device);

#endif // WL_PLATFORM_H