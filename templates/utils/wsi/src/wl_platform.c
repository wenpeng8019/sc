
#if !defined(_GNU_SOURCE)
 #define _GNU_SOURCE
#endif

#include "internal.h"

#if defined(WSI_WAYLAND)

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>

// NOTE: Versions of wayland-scanner prior to 1.17.91 named every global array of
//       wl_interface pointers 'types', making it impossible to combine several unmodified
//       private-code files into a single compilation unit
// HACK: We override this name with a macro for each file, allowing them to coexist

#include "wayland-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"
#include "pointer-constraints-unstable-v1-client-protocol.h"
#include "xdg-activation-v1-client-protocol.h"
#include "idle-inhibit-unstable-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"

#define types _wsi_wayland_types
#include "wayland-client-protocol-code.h"
#undef types

#define types _wsi_xdg_shell_types
#include "xdg-shell-client-protocol-code.h"
#undef types

#define types _wsi_xdg_decoration_types
#include "xdg-decoration-unstable-v1-client-protocol-code.h"
#undef types

#define types _wsi_viewporter_types
#include "viewporter-client-protocol-code.h"
#undef types

#define types _wsi_relative_pointer_types
#include "relative-pointer-unstable-v1-client-protocol-code.h"
#undef types

#define types _wsi_pointer_constraints_types
#include "pointer-constraints-unstable-v1-client-protocol-code.h"
#undef types

#define types _wsi_fractional_scale_types
#include "fractional-scale-v1-client-protocol-code.h"
#undef types

#define types _wsi_xdg_activation_types
#include "xdg-activation-v1-client-protocol-code.h"
#undef types

#define types _wsi_idle_inhibit_types
#include "idle-inhibit-unstable-v1-client-protocol-code.h"
#undef types

#include <limits.h>

#define WSI_BORDER_SIZE    4
#define WSI_CAPTION_HEIGHT 24

#define WSI_PENDING_SURFACE    1
#define WSI_PENDING_BUTTON     2
#define WSI_PENDING_MOTION     4
#define WSI_PENDING_SCROLL     8
#define WSI_PENDING_DISCRETE   16

///////////////////////////////////////////////////////////////////////////////
// platform utils
///////////////////////////////////////////////////////////////////////////////

static int createTmpfileCloexec(char* tmpname)
{
    int fd;

    fd = mkostemp(tmpname, O_CLOEXEC);
    if (fd >= 0)
        unlink(tmpname);

    return fd;
}

/*
 * Create a new, unique, anonymous file of the given size, and
 * return the file descriptor for it. The file descriptor is set
 * CLOEXEC. The file is immediately suitable for mmap()'ing
 * the given size at offset zero.
 *
 * The file should not have a permanent backing store like a disk,
 * but may have if XDG_RUNTIME_DIR is not properly implemented in OS.
 *
 * The file name is deleted from the file system.
 *
 * The file is suitable for buffer sharing between processes by
 * transmitting the file descriptor over Unix sockets using the
 * SCM_RIGHTS methods.
 *
 * posix_fallocate() is used to guarantee that disk space is available
 * for the file at the given size. If disk space is insufficient, errno
 * is set to ENOSPC. If posix_fallocate() is not supported, program may
 * receive SIGBUS on accessing mmap()'ed file contents instead.
 */
static int createAnonymousFile(off_t size)
{
    static const char template[] = "/sc-wsi-shared-XXXXXX";
    const char* path;
    char* name;
    int fd;
    int ret;

#ifdef HAVE_MEMFD_CREATE
    fd = memfd_create("sc-wsi-shared", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd >= 0)
    {
        // We can add this seal before calling posix_fallocate(), as the file
        // is currently zero-sized anyway.
        //
        // There is also no need to check for the return value, we couldn’t do
        // anything with it anyway.
        fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_SEAL);
    }
    else
#elif defined(SHM_ANON)
    fd = shm_open(SHM_ANON, O_RDWR | O_CLOEXEC, 0600);
    if (fd < 0)
#endif
    {
        path = getenv("XDG_RUNTIME_DIR");
        if (!path)
        {
            errno = ENOENT;
            return -1;
        }

        name = wsi_calloc(strlen(path) + sizeof(template), 1);
        strcpy(name, path);
        strcat(name, template);

        fd = createTmpfileCloexec(name);
        wsi_free(name);
        if (fd < 0)
            return -1;
    }

#if defined(SHM_ANON)
    // posix_fallocate does not work on SHM descriptors
    ret = ftruncate(fd, size);
#else
    ret = posix_fallocate(fd, 0, size);
#endif
    if (ret != 0)
    {
        close(fd);
        errno = ret;
        return -1;
    }
    return fd;
}

static struct wl_buffer* createShmBuffer(const sc_wsi_img* image)
{
    const int stride = image->width * 4;
    const int length = image->width * image->height * 4;

    const int fd = createAnonymousFile(length);
    if (fd < 0)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to create buffer file of size %d: %s",
                        length, strerror(errno));
        return NULL;
    }

    void* data = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to map file: %s", strerror(errno));
        close(fd);
        return NULL;
    }

    struct wl_shm_pool* pool = wl_shm_create_pool(g_wsi.wl.shm, fd, length);

    close(fd);

    unsigned char* source = (unsigned char*) image->pixels;
    unsigned char* target = data;
    for (int i = 0;  i < image->width * image->height;  i++, source += 4)
    {
        unsigned int alpha = source[3];

        *target++ = (unsigned char) ((source[2] * alpha) / 255);
        *target++ = (unsigned char) ((source[1] * alpha) / 255);
        *target++ = (unsigned char) ((source[0] * alpha) / 255);
        *target++ = (unsigned char) alpha;
    }

    struct wl_buffer* buffer =
        wl_shm_pool_create_buffer(pool, 0,
                                  image->width,
                                  image->height,
                                  stride, WL_SHM_FORMAT_ARGB8888);
    munmap(data, length);
    wl_shm_pool_destroy(pool);

    return buffer;
}

//-----------------------------------------------------------------------------

static void setIdleInhibitor(window_st* window, bool enable)
{
    if (enable && !window->wl.idleInhibitor && g_wsi.wl.idleInhibitManager)
    {
        window->wl.idleInhibitor =
            zwp_idle_inhibit_manager_v1_create_inhibitor(
                g_wsi.wl.idleInhibitManager, window->wl.surface);
        if (!window->wl.idleInhibitor)
            impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                            "Wayland: Failed to create idle inhibitor");
    }
    else if (!enable && window->wl.idleInhibitor)
    {
        zwp_idle_inhibitor_v1_destroy(window->wl.idleInhibitor);
        window->wl.idleInhibitor = NULL;
    }
}

//-----------------------------------------------------------------------------

/* 回退装饰（fallback CSD）的边框 buffer 策略——不要改回上游 WSI 的
 * 1×1 buffer + wp_viewport 放大方案！
 *
 * 上游做法：所有边共享一个 1×1 灰色 wl_shm buffer，靠
 * wp_viewport_set_destination 放大到边框尺寸。协议合法，常规合成器
 * （weston/GNOME/KDE 服务端合成）没有问题。
 *
 * 但 WSLg 上每个子表面被 rdprail 映射为 Windows 端 msrdc 的独立
 * RAIL_WINDOW，buffer 原样（1×1）经 RDP 发给 msrdc，缩放由
 * MapSurfaceToScaledWindow 交给 Windows 客户端执行。msrdc 对这种
 * 极端倍率（1→数百）的缩放映射存在 bug：特定窗口尺寸下分层窗口
 * 不绘制，表现为底边框随宽度阈值消失、左右边框随高度阈值消失、
 * resize 后标题条整块透明（HWND 存在且可见，内容从未上屏）。
 *
 * 因此这里给每条边分配与自身逻辑尺寸一致的真实 buffer，完全不用
 * viewport 放大。详见 wsi/README.md 第 7 节。 */
static struct wl_buffer* createFallbackEdgeBuffer(int width, int height)
{
    unsigned char* pixels = malloc((size_t) width * height * 4);
    if (!pixels)
        return NULL;

    for (int i = 0;  i < width * height;  i++)
    {
        pixels[i * 4 + 0] = 224;
        pixels[i * 4 + 1] = 224;
        pixels[i * 4 + 2] = 224;
        pixels[i * 4 + 3] = 255;
    }

    const sc_wsi_img image = { width, height, pixels };
    struct wl_buffer* buffer = createShmBuffer(&image);
    free(pixels);
    return buffer;
}

static void resizeFallbackEdge(SC_fallbackEdgeWayland* edge,
                               int x, int y,
                               int width, int height)
{
    struct wl_buffer* oldBuffer = edge->buffer;

    wl_subsurface_set_position(edge->subsurface, x, y);
    edge->buffer = createFallbackEdgeBuffer(width, height);
    wl_surface_attach(edge->surface, edge->buffer, 0, 0);

    struct wl_region* region = wl_compositor_create_region(g_wsi.wl.compositor);
    wl_region_add(region, 0, 0, width, height);
    wl_surface_set_opaque_region(edge->surface, region);
    wl_surface_damage(edge->surface, 0, 0, width, height);
    wl_surface_commit(edge->surface);
    wl_region_destroy(region);

    if (oldBuffer)
        wl_buffer_destroy(oldBuffer);
}

static void createFallbackEdge(window_st* window,
                               SC_fallbackEdgeWayland* edge,
                               struct wl_surface* parent,
                               int x, int y,
                               int width, int height)
{
    edge->surface = wl_compositor_create_surface(g_wsi.wl.compositor);
    wl_surface_set_user_data(edge->surface, window);
    wl_proxy_set_tag((struct wl_proxy*) edge->surface, &g_wsi.wl.tag);
    edge->subsurface = wl_subcompositor_get_subsurface(g_wsi.wl.subcompositor,
                                                       edge->surface, parent);
    wl_subsurface_set_position(edge->subsurface, x, y);
    edge->buffer = createFallbackEdgeBuffer(width, height);
    wl_surface_attach(edge->surface, edge->buffer, 0, 0);

    struct wl_region* region = wl_compositor_create_region(g_wsi.wl.compositor);
    wl_region_add(region, 0, 0, width, height);
    wl_surface_set_opaque_region(edge->surface, region);
    wl_surface_commit(edge->surface);
    wl_region_destroy(region);
}

static void createFallbackDecorations(window_st* window)
{
    createFallbackEdge(window, &window->wl.fallback.top, window->wl.surface,
                       0, -WSI_CAPTION_HEIGHT,
                       window->wl.width, WSI_CAPTION_HEIGHT);
    createFallbackEdge(window, &window->wl.fallback.left, window->wl.surface,
                       -WSI_BORDER_SIZE, -WSI_CAPTION_HEIGHT,
                       WSI_BORDER_SIZE, window->wl.height + WSI_CAPTION_HEIGHT);
    createFallbackEdge(window, &window->wl.fallback.right, window->wl.surface,
                       window->wl.width, -WSI_CAPTION_HEIGHT,
                       WSI_BORDER_SIZE, window->wl.height + WSI_CAPTION_HEIGHT);
    createFallbackEdge(window, &window->wl.fallback.bottom, window->wl.surface,
                       -WSI_BORDER_SIZE, window->wl.height,
                       window->wl.width + WSI_BORDER_SIZE * 2, WSI_BORDER_SIZE);

    window->wl.fallback.decorations = true;
}

static void destroyFallbackEdge(SC_fallbackEdgeWayland* edge)
{
    if (edge->surface == g_wsi.wl.pointerSurface)
        g_wsi.wl.pointerSurface = NULL;

    if (edge->subsurface)
        wl_subsurface_destroy(edge->subsurface);
    if (edge->surface)
        wl_surface_destroy(edge->surface);
    if (edge->buffer)
        wl_buffer_destroy(edge->buffer);

    edge->surface = NULL;
    edge->subsurface = NULL;
    edge->buffer = NULL;
}

static void destroyFallbackDecorations(window_st* window)
{
    window->wl.fallback.decorations = false;

    destroyFallbackEdge(&window->wl.fallback.top);
    destroyFallbackEdge(&window->wl.fallback.left);
    destroyFallbackEdge(&window->wl.fallback.right);
    destroyFallbackEdge(&window->wl.fallback.bottom);
}

static void updateFallbackDecorationCursor(window_st* window, double xpos, double ypos)
{
    window->wl.fallback.pointerX = xpos;
    window->wl.fallback.pointerY = ypos;

    const char* cursorName = "left_ptr";

    if (window->resizable)
    {
        if (g_wsi.wl.pointerSurface == window->wl.fallback.top.surface)
        {
            if (ypos < WSI_BORDER_SIZE)
                cursorName = "n-resize";
        }
        else if (g_wsi.wl.pointerSurface == window->wl.fallback.left.surface)
        {
            if (ypos < WSI_BORDER_SIZE)
                cursorName = "nw-resize";
            else
                cursorName = "w-resize";
        }
        else if (g_wsi.wl.pointerSurface == window->wl.fallback.right.surface)
        {
            if (ypos < WSI_BORDER_SIZE)
                cursorName = "ne-resize";
            else
                cursorName = "e-resize";
        }
        else if (g_wsi.wl.pointerSurface == window->wl.fallback.bottom.surface)
        {
            if (xpos < WSI_BORDER_SIZE)
                cursorName = "sw-resize";
            else if (xpos > window->wl.width + WSI_BORDER_SIZE)
                cursorName = "se-resize";
            else
                cursorName = "s-resize";
        }
    }

    if (window->wl.fallback.cursorName != cursorName)
    {
        struct wl_surface* surface = g_wsi.wl.cursorSurface;
        struct wl_cursor_theme* theme = g_wsi.wl.cursorTheme;
        int scale = 1;

        if (window->wl.bufferScale > 1 && g_wsi.wl.cursorThemeHiDPI)
        {
            // We only support up to scale=2 for now, since libwayland-cursor
            // requires us to load a different theme for each size.
            scale = 2;
            theme = g_wsi.wl.cursorThemeHiDPI;
        }

        struct wl_cursor* cursor = wl_cursor_theme_get_cursor(theme, cursorName);
        if (!cursor)
            return;

        // TODO: handle animated cursors too.
        struct wl_cursor_image* image = cursor->images[0];
        if (!image)
            return;

        struct wl_buffer* buffer = wl_cursor_image_get_buffer(image);
        if (!buffer)
            return;

        wl_pointer_set_cursor(g_wsi.wl.pointer, g_wsi.wl.pointerEnterSerial,
                              surface,
                              image->hotspot_x / scale,
                              image->hotspot_y / scale);
        wl_surface_set_buffer_scale(surface, scale);
        wl_surface_attach(surface, buffer, 0, 0);
        wl_surface_damage(surface, 0, 0, image->width, image->height);
        wl_surface_commit(surface);

        window->wl.fallback.cursorName = cursorName;
    }
}

static void handleFallbackDecorationButton(window_st* window, int button, int action)
{
    if (action != SC_PRESS)
        return;

    const double xpos = window->wl.fallback.pointerX;
    const double ypos = window->wl.fallback.pointerY;
    const uint32_t serial = window->wl.fallback.buttonPressSerial;

    if (button == SC_MOUSE_BUTTON_LEFT)
    {
        uint32_t edges = XDG_TOPLEVEL_RESIZE_EDGE_NONE;

        if (g_wsi.wl.pointerSurface == window->wl.fallback.top.surface)
        {
            if (ypos < WSI_BORDER_SIZE)
                edges = XDG_TOPLEVEL_RESIZE_EDGE_TOP;
            else
                xdg_toplevel_move(window->wl.xdg.toplevel, g_wsi.wl.seat, serial);
        }
        else if (g_wsi.wl.pointerSurface == window->wl.fallback.left.surface)
        {
            if (ypos < WSI_BORDER_SIZE)
                edges = XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
            else
                edges = XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
        }
        else if (g_wsi.wl.pointerSurface == window->wl.fallback.right.surface)
        {
            if (ypos < WSI_BORDER_SIZE)
                edges = XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
            else
                edges = XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
        }
        else if (g_wsi.wl.pointerSurface == window->wl.fallback.bottom.surface)
        {
            if (xpos < WSI_BORDER_SIZE)
                edges = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
            else if (xpos > window->wl.width + WSI_BORDER_SIZE)
                edges = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
            else
                edges = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
        }

        if (edges != XDG_TOPLEVEL_RESIZE_EDGE_NONE)
            xdg_toplevel_resize(window->wl.xdg.toplevel, g_wsi.wl.seat, serial, edges);
    }
    else if (button == SC_MOUSE_BUTTON_RIGHT)
    {
        if (!window->wl.xdg.toplevel)
            return;

        if (g_wsi.wl.pointerSurface != window->wl.fallback.top.surface)
            return;

        if (ypos < WSI_BORDER_SIZE)
            return;

        xdg_toplevel_show_window_menu(window->wl.xdg.toplevel, g_wsi.wl.seat, serial,
                                      xpos, ypos - WSI_CAPTION_HEIGHT - WSI_BORDER_SIZE);
    }
}

//-----------------------------------------------------------------------------

// Makes the surface considered as XRGB instead of ARGB.
static void setContentAreaOpaque(window_st* window)
{
    struct wl_region* region;

    region = wl_compositor_create_region(g_wsi.wl.compositor);
    if (!region)
        return;

    wl_region_add(region, 0, 0, window->wl.width, window->wl.height);
    wl_surface_set_opaque_region(window->wl.surface, region);
    wl_region_destroy(region);
}

//-----------------------------------------------------------------------------

static void resizeFramebuffer(window_st* window)
{
    if (window->wl.fractionalScale)
    {
        window->wl.fbWidth = (window->wl.width * window->wl.scalingNumerator) / 120;
        window->wl.fbHeight = (window->wl.height * window->wl.scalingNumerator) / 120;
    }
    else
    {
        window->wl.fbWidth = window->wl.width * window->wl.bufferScale;
        window->wl.fbHeight = window->wl.height * window->wl.bufferScale;
    }

    if (!window->wl.transparent)
        setContentAreaOpaque(window);
}

static bool resizeWindow(window_st* window, int width, int height)
{
    width = wsi_max(width, 1);
    height = wsi_max(height, 1);

    if (width == window->wl.width && height == window->wl.height)
        return false;

    window->wl.width = width;
    window->wl.height = height;

    resizeFramebuffer(window);

    if (window->wl.scalingViewport)
    {
        wp_viewport_set_destination(window->wl.scalingViewport,
                                    window->wl.width,
                                    window->wl.height);
    }

    if (window->wl.fallback.decorations)
    {
        resizeFallbackEdge(&window->wl.fallback.top,
                           0, -WSI_CAPTION_HEIGHT,
                           window->wl.width, WSI_CAPTION_HEIGHT);
        resizeFallbackEdge(&window->wl.fallback.left,
                           -WSI_BORDER_SIZE, -WSI_CAPTION_HEIGHT,
                           WSI_BORDER_SIZE,
                           window->wl.height + WSI_CAPTION_HEIGHT);
        resizeFallbackEdge(&window->wl.fallback.right,
                           window->wl.width, -WSI_CAPTION_HEIGHT,
                           WSI_BORDER_SIZE,
                           window->wl.height + WSI_CAPTION_HEIGHT);
        resizeFallbackEdge(&window->wl.fallback.bottom,
                           -WSI_BORDER_SIZE, window->wl.height,
                           window->wl.width + WSI_BORDER_SIZE * 2,
                           WSI_BORDER_SIZE);
    }

    return true;
}

//-----------------------------------------------------------------------------

void wayland_UpdateBufferScaleFromOutputs(window_st* window)
{
    if (wl_compositor_get_version(g_wsi.wl.compositor) <
        WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION)
    {
        return;
    }

    if (!window->wl.scaleFramebuffer)
        return;

    // When using fractional scaling, the buffer scale should remain at 1
    if (window->wl.fractionalScale)
        return;

    // Get the scale factor from the highest scale monitor.
    int32_t maxScale = 1;

    for (size_t i = 0; i < window->wl.outputScaleCount; i++)
        maxScale = wsi_max(window->wl.outputScales[i].factor, maxScale);

    // Only change the framebuffer size if the scale changed.
    if (window->wl.bufferScale != maxScale)
    {
        window->wl.bufferScale = maxScale;
        wl_surface_set_buffer_scale(window->wl.surface, maxScale);
        impl_on_win_content_scale(window, maxScale, maxScale);
        resizeFramebuffer(window);

        if (window->wl.visible)
            impl_on_win_damage(window);
    }
}

//-----------------------------------------------------------------------------

// Make the specified window and its video mode active on its monitor
static void acquireMonitor(window_st* window)
{
    if (window->wl.libdecor.frame)
    {
        libdecor_frame_set_fullscreen(window->wl.libdecor.frame,
                                      window->monitor->wl.output);
    }
    else if (window->wl.xdg.toplevel)
    {
        xdg_toplevel_set_fullscreen(window->wl.xdg.toplevel,
                                    window->monitor->wl.output);
    }

    setIdleInhibitor(window, true);

    if (window->wl.fallback.decorations)
        destroyFallbackDecorations(window);
}

// Remove the window and restore the original video mode
static void releaseMonitor(window_st* window)
{
    if (window->wl.libdecor.frame)
        libdecor_frame_unset_fullscreen(window->wl.libdecor.frame);
    else if (window->wl.xdg.toplevel)
        xdg_toplevel_unset_fullscreen(window->wl.xdg.toplevel);

    setIdleInhibitor(window, false);

    if (!window->wl.libdecor.frame &&
        window->wl.xdg.decorationMode != ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE)
    {
        if (window->decorated)
            createFallbackDecorations(window);
    }
}

//-----------------------------------------------------------------------------

static bool flushDisplay(void)
{
    while (wl_display_flush(g_wsi.wl.display) == -1)
    {
        if (errno != EAGAIN)
            return false;

        struct pollfd fd = { wl_display_get_fd(g_wsi.wl.display), POLLOUT };

        while (poll(&fd, 1, -1) == -1)
        {
            if (errno != EINTR && errno != EAGAIN)
                return false;
        }
    }

    return true;
}

static int translateKey(uint32_t scancode)
{
    if (scancode < sizeof(g_wsi.wl.keycodes) / sizeof(g_wsi.wl.keycodes[0]))
        return g_wsi.wl.keycodes[scancode];

    return SC_KEY_UNKNOWN;
}

static xkb_keysym_t composeSymbol(xkb_keysym_t sym)
{
    if (sym == XKB_KEY_NoSymbol || !g_wsi.wl.xkb.composeState)
        return sym;
    if (xkb_compose_state_feed(g_wsi.wl.xkb.composeState, sym)
            != XKB_COMPOSE_FEED_ACCEPTED)
        return sym;
    switch (xkb_compose_state_get_status(g_wsi.wl.xkb.composeState))
    {
        case XKB_COMPOSE_COMPOSED:
            return xkb_compose_state_get_one_sym(g_wsi.wl.xkb.composeState);
        case XKB_COMPOSE_COMPOSING:
        case XKB_COMPOSE_CANCELLED:
            return XKB_KEY_NoSymbol;
        case XKB_COMPOSE_NOTHING:
        default:
            return sym;
    }
}

static void inputText(window_st* window, uint32_t scancode)
{
    const xkb_keysym_t* keysyms;
    const xkb_keycode_t keycode = scancode + 8;

    if (xkb_state_key_get_syms(g_wsi.wl.xkb.state, keycode, &keysyms) == 1)
    {
        const xkb_keysym_t keysym = composeSymbol(keysyms[0]);
        const uint32_t codepoint = xkb_keysym_to_utf32(keysym);
        if (codepoint != 0)
        {
            const int mods = g_wsi.wl.xkb.modifiers;
            const int plain = !(mods & (SC_MOD_CONTROL | SC_MOD_ALT));
            impl_on_chr(window, codepoint, mods, plain);
        }
    }
}

static void setCursorImage(window_st* window,
                           wl_cursor_t* cursorWayland)
{
    struct itimerspec timer = {0};
    struct wl_cursor* wlCursor = cursorWayland->cursor;
    struct wl_cursor_image* image;
    struct wl_buffer* buffer;
    struct wl_surface* surface = g_wsi.wl.cursorSurface;
    int scale = 1;

    if (!wlCursor)
        buffer = cursorWayland->buffer;
    else
    {
        if (window->wl.bufferScale > 1 && cursorWayland->cursorHiDPI)
        {
            wlCursor = cursorWayland->cursorHiDPI;
            scale = 2;
        }

        image = wlCursor->images[cursorWayland->currentImage];
        buffer = wl_cursor_image_get_buffer(image);
        if (!buffer)
            return;

        timer.it_value.tv_sec = image->delay / 1000;
        timer.it_value.tv_nsec = (image->delay % 1000) * 1000000;
        timerfd_settime(g_wsi.wl.cursorTimerfd, 0, &timer, NULL);

        cursorWayland->width = image->width;
        cursorWayland->height = image->height;
        cursorWayland->xhot = image->hotspot_x;
        cursorWayland->yhot = image->hotspot_y;
    }

    wl_pointer_set_cursor(g_wsi.wl.pointer, g_wsi.wl.pointerEnterSerial,
                          surface,
                          cursorWayland->xhot / scale,
                          cursorWayland->yhot / scale);
    wl_surface_set_buffer_scale(surface, scale);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage(surface, 0, 0,
                      cursorWayland->width, cursorWayland->height);
    wl_surface_commit(surface);
}

static void incrementCursorImage(void)
{
    if (!g_wsi.wl.pointerSurface)
        return;

    window_st* window = wl_surface_get_user_data(g_wsi.wl.pointerSurface);
    if (window->wl.surface != g_wsi.wl.pointerSurface)
        return;

    cursor_st* cursor = window->cursor;
    if (cursor && cursor->wl.cursor)
    {
        cursor->wl.currentImage += 1;
        cursor->wl.currentImage %= cursor->wl.cursor->image_count;
        setCursorImage(window, &cursor->wl);
    }
}

// Reads the specified data offer as the specified MIME type
static char* readDataOfferAsString(struct wl_data_offer* offer, const char* mimeType)
{
    int fds[2];

    if (pipe2(fds, O_CLOEXEC) == -1)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to create pipe for data offer: %s",
                        strerror(errno));
        return NULL;
    }

    wl_data_offer_receive(offer, mimeType, fds[1]);
    flushDisplay();
    close(fds[1]);

    char* string = NULL;
    size_t size = 0;
    size_t length = 0;

    for (;;)
    {
        const size_t readSize = 4096;
        const size_t requiredSize = length + readSize + 1;
        if (requiredSize > size)
        {
            char* longer = wsi_realloc(string, requiredSize);
            if (!longer)
            {
                impl_on_error(SC_WSI_ERR_OUT_OF_MEMORY, NULL);
                wsi_free(string);
                close(fds[0]);
                return NULL;
            }

            string = longer;
            size = requiredSize;
        }

        const ssize_t result = read(fds[0], string + length, readSize);
        if (result == 0)
            break;
        else if (result == -1)
        {
            if (errno == EINTR)
                continue;

            impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                            "Wayland: Failed to read from data offer pipe: %s",
                            strerror(errno));
            wsi_free(string);
            close(fds[0]);
            return NULL;
        }

        length += result;
    }

    close(fds[0]);

    string[length] = '\0';
    return string;
}

///////////////////////////////////////////////////////////////////////////////
// Monitor
///////////////////////////////////////////////////////////////////////////////

static void wsi_free_monitorWayland(monitor_st* monitor)
{
    if (monitor->wl.output)
        wl_output_destroy(monitor->wl.output);
}

static void wayland_get_monitor_pos(monitor_st* monitor, int* xpos, int* ypos)
{
    if (xpos)
        *xpos = monitor->wl.x;
    if (ypos)
        *ypos = monitor->wl.y;
}

static void wayland_get_monitor_content_scale(monitor_st* monitor,
                                        float* xscale, float* yscale)
{
    if (xscale)
        *xscale = (float) monitor->wl.scale;
    if (yscale)
        *yscale = (float) monitor->wl.scale;
}

static void wayland_get_monitor_work_area(monitor_st* monitor,
                                    int* xpos, int* ypos,
                                    int* width, int* height)
{
    if (xpos)
        *xpos = monitor->wl.x;
    if (ypos)
        *ypos = monitor->wl.y;
    if (width)
        *width = monitor->modes[monitor->wl.currentMode].width;
    if (height)
        *height = monitor->modes[monitor->wl.currentMode].height;
}

static sc_wsi_video_mode* wayland_get_video_modes(monitor_st* monitor, int* found)
{
    *found = monitor->modeCount;
    return monitor->modes;
}

static bool wayland_get_video_mode(monitor_st* monitor, sc_wsi_video_mode* mode)
{
    *mode = monitor->modes[monitor->wl.currentMode];
    return true;
}

static bool wayland_get_gamma_ramp(monitor_st* monitor, sc_wsi_gamma_ramp* ramp)
{
    impl_on_error(SC_WSI_ERR_FEATURE_UNAVAILABLE,
                    "Wayland: Gamma ramp access is not available");
    return false;
}

static void wayland_set_gamma_ramp(monitor_st* monitor, const sc_wsi_gamma_ramp* ramp)
{
    impl_on_error(SC_WSI_ERR_FEATURE_UNAVAILABLE,
                    "Wayland: Gamma ramp access is not available");
}


///////////////////////////////////////////////////////////////////////////////
// app loop
///////////////////////////////////////////////////////////////////////////////

static void handleEvents(double* timeout)
{

    bool event = false;
    enum { DISPLAY_FD, KEYREPEAT_FD, CURSOR_FD };
    struct pollfd fds[] =
    {
        [DISPLAY_FD] = { wl_display_get_fd(g_wsi.wl.display), POLLIN },
        [KEYREPEAT_FD] = { g_wsi.wl.keyRepeatTimerfd, POLLIN },
        [CURSOR_FD] = { g_wsi.wl.cursorTimerfd, POLLIN }
    };

    while (!event)
    {
        if (g_wsi.wl.libdecor.context)
        {
            // Dispatch unconditionally because it also processes non-Wayland events
            if (libdecor_dispatch(g_wsi.wl.libdecor.context, 0) > 0)
                event = true;
        }

        while (wl_display_prepare_read(g_wsi.wl.display) != 0)
        {
            if (wl_display_dispatch_pending(g_wsi.wl.display) > 0)
                event = true;
        }

        // If an error other than EAGAIN happens, we have likely been disconnected
        // from the Wayland session; try to handle that the best we can.
        if (!flushDisplay())
        {
            wl_display_cancel_read(g_wsi.wl.display);

            window_st* window = g_wsi.windowListHead;
            while (window)
            {
                impl_on_win_close_req(window);
                window = window->next;
            }

            return;
        }

        double immediate = 0.0;

        if (event)
            timeout = &immediate;

        if (!sc_poll_posix(fds, sizeof(fds) / sizeof(fds[0]), timeout))
        {
            wl_display_cancel_read(g_wsi.wl.display);
            return;
        }

        if (fds[DISPLAY_FD].revents & POLLIN)
        {
            wl_display_read_events(g_wsi.wl.display);
            if (wl_display_dispatch_pending(g_wsi.wl.display) > 0)
                event = true;
        }
        else
            wl_display_cancel_read(g_wsi.wl.display);

        if (fds[KEYREPEAT_FD].revents & POLLIN)
        {
            uint64_t repeats;

            if (read(g_wsi.wl.keyRepeatTimerfd, &repeats, sizeof(repeats)) == 8)
            {
                if (g_wsi.wl.keyboardFocus)
                {
                    for (uint64_t i = 0; i < repeats; i++)
                    {
                        impl_on_key(g_wsi.wl.keyboardFocus,
                                      translateKey(g_wsi.wl.keyRepeatScancode),
                                      g_wsi.wl.keyRepeatScancode,
                                      SC_PRESS,
                                      g_wsi.wl.xkb.modifiers);
                        inputText(g_wsi.wl.keyboardFocus, g_wsi.wl.keyRepeatScancode);
                    }

                    event = true;
                }

            }
        }

        if (fds[CURSOR_FD].revents & POLLIN)
        {
            uint64_t repeats;

            if (read(g_wsi.wl.cursorTimerfd, &repeats, sizeof(repeats)) == 8)
                incrementCursorImage();
        }
    }
}

static void wayland_poll_events(void)
{
    double timeout = 0.0;
    handleEvents(&timeout);
}

static void wayland_wait_events(void)
{
    handleEvents(NULL);
}

static void wayland_wait_eventsTimeout(double timeout)
{
    handleEvents(&timeout);
}

static void callbackHandleDone(void* userData, struct wl_callback* callback, uint32_t data)
{
    wl_callback_destroy(callback);
}

static const struct wl_callback_listener noopCallbackListener = {
    callbackHandleDone
};

static void wayland_post_empty_event(void)
{
    struct wl_callback* callback = wl_display_sync(g_wsi.wl.display);
    wl_callback_add_listener(callback, &noopCallbackListener, NULL);

    flushDisplay();
}

///////////////////////////////////////////////////////////////////////////////
// lib
///////////////////////////////////////////////////////////////////////////////

static void relativePointerHandleRelativeMotion(void* userData,
                                                struct zwp_relative_pointer_v1* pointer,
                                                uint32_t timeHi,
                                                uint32_t timeLo,
                                                wl_fixed_t dx,
                                                wl_fixed_t dy,
                                                wl_fixed_t dxUnaccel,
                                                wl_fixed_t dyUnaccel)
{
    window_st* window = userData;
    double xpos = window->virtualCursorPosX;
    double ypos = window->virtualCursorPosY;

    if (window->cursorMode != SC_CURSOR_DISABLED)
        return;

    if (window->rawMouseMotion)
    {
        xpos += wl_fixed_to_double(dxUnaccel);
        ypos += wl_fixed_to_double(dyUnaccel);
    }
    else
    {
        xpos += wl_fixed_to_double(dx);
        ypos += wl_fixed_to_double(dy);
    }

    impl_on_cursor_pos(window, xpos, ypos);
}

static const struct zwp_relative_pointer_v1_listener relativePointerListener =
{
    relativePointerHandleRelativeMotion
};

static void lockedPointerHandleLocked(void* userData,
                                      struct zwp_locked_pointer_v1* lockedPointer)
{
}

static void lockedPointerHandleUnlocked(void* userData,
                                        struct zwp_locked_pointer_v1* lockedPointer)
{
}

static const struct zwp_locked_pointer_v1_listener lockedPointerListener =
{
    lockedPointerHandleLocked,
    lockedPointerHandleUnlocked
};

static void lockPointer(window_st* window)
{
    if (!g_wsi.wl.relativePointerManager)
    {
        impl_on_error(SC_WSI_ERR_FEATURE_UNAVAILABLE,
                        "Wayland: The compositor does not support relative pointer motion");
        return;
    }

    if (!g_wsi.wl.pointerConstraints)
    {
        impl_on_error(SC_WSI_ERR_FEATURE_UNAVAILABLE,
                        "Wayland: The compositor does not support locking the pointer");
    }

    window->wl.relativePointer =
        zwp_relative_pointer_manager_v1_get_relative_pointer(
            g_wsi.wl.relativePointerManager,
            g_wsi.wl.pointer);
    zwp_relative_pointer_v1_add_listener(window->wl.relativePointer,
                                         &relativePointerListener,
                                         window);

    window->wl.lockedPointer =
        zwp_pointer_constraints_v1_lock_pointer(
            g_wsi.wl.pointerConstraints,
            window->wl.surface,
            g_wsi.wl.pointer,
            NULL,
            ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    zwp_locked_pointer_v1_add_listener(window->wl.lockedPointer,
                                       &lockedPointerListener,
                                       window);
}

static void unlockPointer(window_st* window)
{
    zwp_relative_pointer_v1_destroy(window->wl.relativePointer);
    window->wl.relativePointer = NULL;

    zwp_locked_pointer_v1_destroy(window->wl.lockedPointer);
    window->wl.lockedPointer = NULL;
}

static void confinedPointerHandleConfined(void* userData,
                                          struct zwp_confined_pointer_v1* confinedPointer)
{
}

static void confinedPointerHandleUnconfined(void* userData,
                                            struct zwp_confined_pointer_v1* confinedPointer)
{
}

static const struct zwp_confined_pointer_v1_listener confinedPointerListener =
{
    confinedPointerHandleConfined,
    confinedPointerHandleUnconfined
};

static void confinePointer(window_st* window)
{
    if (!g_wsi.wl.pointerConstraints)
    {
        impl_on_error(SC_WSI_ERR_FEATURE_UNAVAILABLE,
                        "Wayland: The compositor does not support confining the pointer");
    }

    window->wl.confinedPointer =
        zwp_pointer_constraints_v1_confine_pointer(
            g_wsi.wl.pointerConstraints,
            window->wl.surface,
            g_wsi.wl.pointer,
            NULL,
            ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);

    zwp_confined_pointer_v1_add_listener(window->wl.confinedPointer,
                                         &confinedPointerListener,
                                         window);
}

static void unconfinePointer(window_st* window)
{
    zwp_confined_pointer_v1_destroy(window->wl.confinedPointer);
    window->wl.confinedPointer = NULL;
}

static void wayland_set_cursor(window_st* window, cursor_st* cursor)
{
    if (!g_wsi.wl.pointer)
        return;

    if (window->wl.surface != g_wsi.wl.pointerSurface)
        return;

    // Update pointer lock to match cursor mode
    if (window->cursorMode == SC_CURSOR_DISABLED)
    {
        if (window->wl.confinedPointer)
            unconfinePointer(window);
        if (!window->wl.lockedPointer)
            lockPointer(window);
    }
    else if (window->cursorMode == SC_CURSOR_CAPTURED)
    {
        if (window->wl.lockedPointer)
            unlockPointer(window);
        if (!window->wl.confinedPointer)
            confinePointer(window);
    }
    else if (window->cursorMode == SC_CURSOR_NORMAL ||
             window->cursorMode == SC_CURSOR_HIDDEN)
    {
        if (window->wl.lockedPointer)
            unlockPointer(window);
        else if (window->wl.confinedPointer)
            unconfinePointer(window);
    }

    if (window->cursorMode == SC_CURSOR_NORMAL ||
        window->cursorMode == SC_CURSOR_CAPTURED)
    {
        if (cursor)
            setCursorImage(window, &cursor->wl);
        else
        {
            struct wl_cursor* defaultCursor =
                wl_cursor_theme_get_cursor(g_wsi.wl.cursorTheme, "left_ptr");
            if (!defaultCursor)
            {
                impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                                "Wayland: Standard cursor not found");
                return;
            }

            struct wl_cursor* defaultCursorHiDPI = NULL;
            if (g_wsi.wl.cursorThemeHiDPI)
            {
                defaultCursorHiDPI =
                    wl_cursor_theme_get_cursor(g_wsi.wl.cursorThemeHiDPI, "left_ptr");
            }

            wl_cursor_t cursorWayland =
            {
                defaultCursor,
                defaultCursorHiDPI,
                NULL,
                0, 0,
                0, 0,
                0
            };

            setCursorImage(window, &cursorWayland);
        }
    }
    else if (window->cursorMode == SC_CURSOR_HIDDEN ||
             window->cursorMode == SC_CURSOR_DISABLED)
    {
        wl_pointer_set_cursor(g_wsi.wl.pointer, g_wsi.wl.pointerEnterSerial, NULL, 0, 0);
    }
}

//-----------------------------------------------------------------------------

static void outputHandleGeometry(void* userData,
                                 struct wl_output* output,
                                 int32_t x,
                                 int32_t y,
                                 int32_t physicalWidth,
                                 int32_t physicalHeight,
                                 int32_t subpixel,
                                 const char* make,
                                 const char* model,
                                 int32_t transform)
{
    monitor_st* monitor = userData;

    monitor->wl.x = x;
    monitor->wl.y = y;
    monitor->widthMM = physicalWidth;
    monitor->heightMM = physicalHeight;

    if (strlen(monitor->name) == 0)
        snprintf(monitor->name, sizeof(monitor->name), "%s %s", make, model);
}

static void outputHandleMode(void* userData,
                             struct wl_output* output,
                             uint32_t flags,
                             int32_t width,
                             int32_t height,
                             int32_t refresh)
{
    monitor_st* monitor = userData;
    sc_wsi_video_mode mode;

    mode.width = width;
    mode.height = height;
    mode.refreshRate = (int) round(refresh / 1000.0);

    monitor->modeCount++;
    monitor->modes =
        wsi_realloc(monitor->modes, monitor->modeCount * sizeof(sc_wsi_video_mode));
    monitor->modes[monitor->modeCount - 1] = mode;

    if (flags & WL_OUTPUT_MODE_CURRENT)
        monitor->wl.currentMode = monitor->modeCount - 1;
}

static void outputHandleDone(void* userData, struct wl_output* output)
{
    monitor_st* monitor = userData;

    if (monitor->widthMM <= 0 || monitor->heightMM <= 0)
    {
        // If Wayland does not provide a physical size, assume the default 96 DPI
        const sc_wsi_video_mode* mode = &monitor->modes[monitor->wl.currentMode];
        monitor->widthMM  = (int) (mode->width * 25.4f / 96.f);
        monitor->heightMM = (int) (mode->height * 25.4f / 96.f);
    }

    for (int i = 0; i < g_wsi.monitorCount; i++)
    {
        if (g_wsi.monitors[i] == monitor)
            return;
    }

    impl_on_monitor(monitor, SC_CONNECTED, WSI_INSERT_LAST);
}

static void outputHandleScale(void* userData,
                              struct wl_output* output,
                              int32_t factor)
{
    monitor_st* monitor = userData;

    monitor->wl.scale = factor;

    for (window_st* window = g_wsi.windowListHead; window; window = window->next)
    {
        for (size_t i = 0; i < window->wl.outputScaleCount; i++)
        {
            if (window->wl.outputScales[i].output == monitor->wl.output)
            {
                window->wl.outputScales[i].factor = monitor->wl.scale;
                wayland_UpdateBufferScaleFromOutputs(window);
                break;
            }
        }
    }
}

static void outputHandleName(void* userData, struct wl_output* wl_output, const char* name)
{
    monitor_st* monitor = userData;

    strncpy(monitor->name, name, sizeof(monitor->name) - 1);
}

static void outputHandleDescription(void* userData,
                             struct wl_output* wl_output,
                             const char* description)
{
}

static const struct wl_output_listener outputListener =
{
    outputHandleGeometry,
    outputHandleMode,
    outputHandleDone,
    outputHandleScale,
    outputHandleName,
    outputHandleDescription,
};

static void wayland_AddOutput(uint32_t name, uint32_t version)
{
    if (version < 2)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Unsupported output interface version");
        return;
    }

    version = wsi_min(version, WL_OUTPUT_NAME_SINCE_VERSION);

    struct wl_output* output = wl_registry_bind(g_wsi.wl.registry,
                                                name,
                                                &wl_output_interface,
                                                version);
    if (!output)
        return;

    // The actual name of this output will be set in the geometry handler
    monitor_st* monitor = wsi_alloc_monitor("", 0, 0);
    monitor->wl.scale = 1;
    monitor->wl.output = output;
    monitor->wl.name = name;

    wl_proxy_set_tag((struct wl_proxy*) output, &g_wsi.wl.tag);
    wl_output_add_listener(output, &outputListener, monitor);
}

static void wmBaseHandlePing(void* userData,
                             struct xdg_wm_base* wmBase,
                             uint32_t serial)
{
    xdg_wm_base_pong(wmBase, serial);
}

static const struct xdg_wm_base_listener wmBaseListener = {
    wmBaseHandlePing
};

//-----------------------------------------------------------------------------

static void processPointerEnterSurface(struct wl_surface* surface)
{
    g_wsi.wl.pointerSurface = surface;

    window_st* window = wl_surface_get_user_data(g_wsi.wl.pointerSurface);
    if (window->wl.surface == g_wsi.wl.pointerSurface)
    {
        wayland_set_cursor(window, window->cursor);
        impl_on_cursor_enter(window, true);
    }
}

static void processPointerLeaveSurface(struct wl_surface* surface)
{
    g_wsi.wl.pointerSurface = NULL;

    window_st* window = wl_surface_get_user_data(surface);
    if (window->wl.surface == surface)
        impl_on_cursor_enter(window, false);
    else
    {
        if (window->wl.fallback.decorations)
            window->wl.fallback.cursorName = NULL;
    }
}

static void processPointerMotion(double xpos, double ypos)
{
    window_st* window = wl_surface_get_user_data(g_wsi.wl.pointerSurface);
    if (window->wl.surface == g_wsi.wl.pointerSurface)
    {
        if (window->cursorMode != SC_CURSOR_DISABLED)
        {
            window->wl.cursorPosX = xpos;
            window->wl.cursorPosY = ypos;
            impl_on_cursor_pos(window, window->wl.cursorPosX, window->wl.cursorPosY);
        }
    }
    else
    {
        if (window->wl.fallback.decorations)
            updateFallbackDecorationCursor(window, xpos, ypos);
    }
}

static void processPointerButton(int button, int action)
{
    window_st* window = wl_surface_get_user_data(g_wsi.wl.pointerSurface);
    if (window->wl.surface == g_wsi.wl.pointerSurface)
        impl_on_mouse_click(window, button, action, g_wsi.wl.xkb.modifiers);
    else
    {
        if (window->wl.fallback.decorations)
            handleFallbackDecorationButton(window, button, action);
    }
}

static void processPointerScroll(double xoffset, double yoffset)
{
    window_st* window = wl_surface_get_user_data(g_wsi.wl.pointerSurface);
    if (window->wl.surface == g_wsi.wl.pointerSurface)
        impl_on_scroll(window, xoffset, yoffset);
}


static void pointerHandleEnter(void* userData,
                               struct wl_pointer* pointer,
                               uint32_t serial,
                               struct wl_surface* surface,
                               wl_fixed_t sx,
                               wl_fixed_t sy)
{
    // Happens in the case we just destroyed the surface.
    if (!surface)
        return;

    if (wl_proxy_get_tag((struct wl_proxy*) surface) != &g_wsi.wl.tag)
        return;

    g_wsi.wl.serial = serial;
    g_wsi.wl.pointerEnterSerial = serial;

    const double xpos = wl_fixed_to_double(sx);
    const double ypos = wl_fixed_to_double(sy);

    if (wl_pointer_get_version(pointer) >= WL_POINTER_FRAME_SINCE_VERSION)
    {
        g_wsi.wl.pending.events |= (WSI_PENDING_SURFACE | WSI_PENDING_MOTION);
        g_wsi.wl.pending.pointerSurface = surface;
        g_wsi.wl.pending.pointerX = xpos;
        g_wsi.wl.pending.pointerY = ypos;
    }
    else
    {
        processPointerEnterSurface(surface);
        processPointerMotion(xpos, ypos);
    }
}

static void pointerHandleLeave(void* userData,
                               struct wl_pointer* pointer,
                               uint32_t serial,
                               struct wl_surface* surface)
{
    if (!surface)
        return;

    if (wl_proxy_get_tag((struct wl_proxy*) surface) != &g_wsi.wl.tag)
        return;

    g_wsi.wl.serial = serial;

    if (wl_pointer_get_version(pointer) >= WL_POINTER_FRAME_SINCE_VERSION)
    {
        g_wsi.wl.pending.events |= WSI_PENDING_SURFACE;
        g_wsi.wl.pending.pointerSurface = NULL;
    }
    else
        processPointerLeaveSurface(surface);
}

static void pointerHandleMotion(void* userData,
                                struct wl_pointer* pointer,
                                uint32_t time,
                                wl_fixed_t sx,
                                wl_fixed_t sy)
{
    if (!g_wsi.wl.pointerSurface)
        return;

    const double xpos = wl_fixed_to_double(sx);
    const double ypos = wl_fixed_to_double(sy);

    if (wl_pointer_get_version(pointer) >= WL_POINTER_FRAME_SINCE_VERSION)
    {
        g_wsi.wl.pending.events |= WSI_PENDING_MOTION;
        g_wsi.wl.pending.pointerX = xpos;
        g_wsi.wl.pending.pointerY = ypos;
    }
    else
        processPointerMotion(xpos, ypos);
}

static void pointerHandleButton(void* userData,
                                struct wl_pointer* pointer,
                                uint32_t serial,
                                uint32_t time,
                                uint32_t buttonID,
                                uint32_t state)
{
    if (!g_wsi.wl.pointerSurface)
        return;

    g_wsi.wl.serial = serial;

    const int button = buttonID - BTN_LEFT;
    const int action = (state == WL_POINTER_BUTTON_STATE_PRESSED);

    window_st* window = wl_surface_get_user_data(g_wsi.wl.pointerSurface);
    if (window->wl.fallback.decorations)
    {
        if (action == SC_PRESS)
            window->wl.fallback.buttonPressSerial = serial;
    }

    if (wl_pointer_get_version(pointer) >= WL_POINTER_FRAME_SINCE_VERSION)
    {
        g_wsi.wl.pending.events |= WSI_PENDING_BUTTON;
        g_wsi.wl.pending.button = button;
        g_wsi.wl.pending.action = action;
    }
    else
        processPointerButton(button, action);
}

static void pointerHandleAxis(void* userData,
                              struct wl_pointer* pointer,
                              uint32_t time,
                              uint32_t axis,
                              wl_fixed_t value)
{
    if (!g_wsi.wl.pointerSurface)
        return;

    if (wl_pointer_get_version(pointer) >= WL_POINTER_FRAME_SINCE_VERSION)
    {
        g_wsi.wl.pending.events |= WSI_PENDING_SCROLL;
        if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL)
            g_wsi.wl.pending.scrollX = -wl_fixed_to_double(value) / 10.0;
        else if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
            g_wsi.wl.pending.scrollY = -wl_fixed_to_double(value) / 10.0;
    }
    else
    {
        // NOTE: 10 units of motion per mouse wheel step seems to be a common ratio
        if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL)
            processPointerScroll(-wl_fixed_to_double(value) / 10.0, 0.0);
        else if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
            processPointerScroll(0.0, -wl_fixed_to_double(value) / 10.0);
    }
}

static void pointerHandleFrame(void* userData, struct wl_pointer* pointer)
{
    if (g_wsi.wl.pending.events & WSI_PENDING_SURFACE)
    {
        if (g_wsi.wl.pointerSurface)
            processPointerLeaveSurface(g_wsi.wl.pointerSurface);

        if (g_wsi.wl.pending.pointerSurface)
            processPointerEnterSurface(g_wsi.wl.pending.pointerSurface);
    }

    if (!g_wsi.wl.pointerSurface)
        return;

    if (g_wsi.wl.pending.events & WSI_PENDING_MOTION)
        processPointerMotion(g_wsi.wl.pending.pointerX, g_wsi.wl.pending.pointerY);

    if (g_wsi.wl.pending.events & WSI_PENDING_BUTTON)
        processPointerButton(g_wsi.wl.pending.button, g_wsi.wl.pending.action);

    if (g_wsi.wl.pending.events & WSI_PENDING_DISCRETE)
        processPointerScroll(g_wsi.wl.pending.discreteX, g_wsi.wl.pending.discreteY);
    else if (g_wsi.wl.pending.events & WSI_PENDING_SCROLL)
        processPointerScroll(g_wsi.wl.pending.scrollX, g_wsi.wl.pending.scrollY);

    memset(&g_wsi.wl.pending, 0, sizeof(g_wsi.wl.pending));
}

static void pointerHandleAxisSource(void* userData,
                                    struct wl_pointer* pointer,
                                    uint32_t axisSource)
{
}

static void pointerHandleAxisStop(void* userData,
                                  struct wl_pointer* pointer,
                                  uint32_t time,
                                  uint32_t axis)
{
}

static void pointerHandleAxisDiscrete(void* userData,
                                      struct wl_pointer* pointer,
                                      uint32_t axis,
                                      int32_t discrete)
{
}

static void pointerHandleAxisValue120(void* data,
                                      struct wl_pointer* pointer,
                                      uint32_t axis,
                                      int32_t value120)
{
    if (!g_wsi.wl.pointerSurface)
        return;

    g_wsi.wl.pending.events |= WSI_PENDING_DISCRETE;
    if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL)
        g_wsi.wl.pending.discreteX = -(value120 / 120.0);
    else if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        g_wsi.wl.pending.discreteY = -(value120 / 120.0);
}

static const struct wl_pointer_listener pointerListener =
{
    pointerHandleEnter,
    pointerHandleLeave,
    pointerHandleMotion,
    pointerHandleButton,
    pointerHandleAxis,
    pointerHandleFrame,
    pointerHandleAxisSource,
    pointerHandleAxisStop,
    pointerHandleAxisDiscrete,
    pointerHandleAxisValue120
};

//-----------------------------------------------------------------------------

static void keyboardHandleKeymap(void* userData,
                                 struct wl_keyboard* keyboard,
                                 uint32_t format,
                                 int fd,
                                 uint32_t size)
{
    struct xkb_keymap* keymap;
    struct xkb_state* state;
    struct xkb_compose_table* composeTable;
    struct xkb_compose_state* composeState;

    char* mapStr;
    const char* locale;

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
    {
        close(fd);
        return;
    }

    mapStr = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapStr == MAP_FAILED)
    {
        close(fd);
        return;
    }

    keymap = xkb_keymap_new_from_string(g_wsi.wl.xkb.context,
                                        mapStr,
                                        XKB_KEYMAP_FORMAT_TEXT_V1,
                                        XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(mapStr, size);
    close(fd);

    if (!keymap)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to compile keymap");
        return;
    }

    state = xkb_state_new(keymap);
    if (!state)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to create XKB state");
        xkb_keymap_unref(keymap);
        return;
    }

    // Look up the preferred locale, falling back to "C" as default.
    locale = getenv("LC_ALL");
    if (!locale)
        locale = getenv("LC_CTYPE");
    if (!locale)
        locale = getenv("LANG");
    if (!locale)
        locale = "C";

    composeTable =
        xkb_compose_table_new_from_locale(g_wsi.wl.xkb.context, locale,
                                          XKB_COMPOSE_COMPILE_NO_FLAGS);
    if (composeTable)
    {
        composeState =
            xkb_compose_state_new(composeTable, XKB_COMPOSE_STATE_NO_FLAGS);
        xkb_compose_table_unref(composeTable);
        if (composeState)
            g_wsi.wl.xkb.composeState = composeState;
        else
            impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                            "Wayland: Failed to create XKB compose state");
    }
    else
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to create XKB compose table");
    }

    xkb_keymap_unref(g_wsi.wl.xkb.keymap);
    xkb_state_unref(g_wsi.wl.xkb.state);
    g_wsi.wl.xkb.keymap = keymap;
    g_wsi.wl.xkb.state = state;

    g_wsi.wl.xkb.controlIndex  = xkb_keymap_mod_get_index(g_wsi.wl.xkb.keymap, "Control");
    g_wsi.wl.xkb.altIndex      = xkb_keymap_mod_get_index(g_wsi.wl.xkb.keymap, "Mod1");
    g_wsi.wl.xkb.shiftIndex    = xkb_keymap_mod_get_index(g_wsi.wl.xkb.keymap, "Shift");
    g_wsi.wl.xkb.superIndex    = xkb_keymap_mod_get_index(g_wsi.wl.xkb.keymap, "Mod4");
    g_wsi.wl.xkb.capsLockIndex = xkb_keymap_mod_get_index(g_wsi.wl.xkb.keymap, "Lock");
    g_wsi.wl.xkb.numLockIndex  = xkb_keymap_mod_get_index(g_wsi.wl.xkb.keymap, "Mod2");
}

static void keyboardHandleEnter(void* userData,
                                struct wl_keyboard* keyboard,
                                uint32_t serial,
                                struct wl_surface* surface,
                                struct wl_array* keys)
{
    // Happens in the case we just destroyed the surface.
    if (!surface)
        return;

    if (wl_proxy_get_tag((struct wl_proxy*) surface) != &g_wsi.wl.tag)
        return;

    window_st* window = wl_surface_get_user_data(surface);
    if (surface != window->wl.surface)
        return;

    g_wsi.wl.serial = serial;
    g_wsi.wl.keyboardFocus = window;
    impl_on_win_focus(window, true);
}

static void keyboardHandleLeave(void* userData,
                                struct wl_keyboard* keyboard,
                                uint32_t serial,
                                struct wl_surface* surface)
{
    window_st* window = g_wsi.wl.keyboardFocus;

    if (!window)
        return;

    struct itimerspec timer = {0};
    timerfd_settime(g_wsi.wl.keyRepeatTimerfd, 0, &timer, NULL);

    g_wsi.wl.serial = serial;
    g_wsi.wl.keyboardFocus = NULL;
    impl_on_win_focus(window, false);
}

static void keyboardHandleKey(void* userData,
                              struct wl_keyboard* keyboard,
                              uint32_t serial,
                              uint32_t time,
                              uint32_t scancode,
                              uint32_t state)
{
    window_st* window = g_wsi.wl.keyboardFocus;
    if (!window)
        return;

    const int key = translateKey(scancode);
    const int action =
        state == WL_KEYBOARD_KEY_STATE_PRESSED ? SC_PRESS : SC_RELEASE;

    g_wsi.wl.serial = serial;

    struct itimerspec timer = {0};

    if (action == SC_PRESS)
    {
        const xkb_keycode_t keycode = scancode + 8;

        if (xkb_keymap_key_repeats(g_wsi.wl.xkb.keymap, keycode) &&
            g_wsi.wl.keyRepeatRate > 0)
        {
            g_wsi.wl.keyRepeatScancode = scancode;
            if (g_wsi.wl.keyRepeatRate > 1)
                timer.it_interval.tv_nsec = 1000000000 / g_wsi.wl.keyRepeatRate;
            else
                timer.it_interval.tv_sec = 1;

            timer.it_value.tv_sec = g_wsi.wl.keyRepeatDelay / 1000;
            timer.it_value.tv_nsec = (g_wsi.wl.keyRepeatDelay % 1000) * 1000000;
            timerfd_settime(g_wsi.wl.keyRepeatTimerfd, 0, &timer, NULL);
        }
    }
    else if (scancode == g_wsi.wl.keyRepeatScancode)
    {
        timerfd_settime(g_wsi.wl.keyRepeatTimerfd, 0, &timer, NULL);
    }

    impl_on_key(window, key, scancode, action, g_wsi.wl.xkb.modifiers);

    if (action == SC_PRESS)
        inputText(window, scancode);
}

static void keyboardHandleModifiers(void* userData,
                                    struct wl_keyboard* keyboard,
                                    uint32_t serial,
                                    uint32_t modsDepressed,
                                    uint32_t modsLatched,
                                    uint32_t modsLocked,
                                    uint32_t group)
{
    g_wsi.wl.serial = serial;

    if (!g_wsi.wl.xkb.keymap)
        return;

    xkb_state_update_mask(g_wsi.wl.xkb.state,
                          modsDepressed,
                          modsLatched,
                          modsLocked,
                          0,
                          0,
                          group);

    g_wsi.wl.xkb.modifiers = 0;

    struct
    {
        xkb_mod_index_t index;
        unsigned int bit;
    } modifiers[] =
    {
        { g_wsi.wl.xkb.controlIndex,  SC_MOD_CONTROL },
        { g_wsi.wl.xkb.altIndex,      SC_MOD_ALT },
        { g_wsi.wl.xkb.shiftIndex,    SC_MOD_SHIFT },
        { g_wsi.wl.xkb.superIndex,    SC_MOD_SUPER },
        { g_wsi.wl.xkb.capsLockIndex, SC_MOD_CAPS_LOCK },
        { g_wsi.wl.xkb.numLockIndex,  SC_MOD_NUM_LOCK }
    };

    for (size_t i = 0; i < sizeof(modifiers) / sizeof(modifiers[0]); i++)
    {
        if (xkb_state_mod_index_is_active(g_wsi.wl.xkb.state,
                                          modifiers[i].index,
                                          XKB_STATE_MODS_EFFECTIVE) == 1)
        {
            g_wsi.wl.xkb.modifiers |= modifiers[i].bit;
        }
    }
}

static void keyboardHandleRepeatInfo(void* userData,
                                     struct wl_keyboard* keyboard,
                                     int32_t rate,
                                     int32_t delay)
{
    if (keyboard != g_wsi.wl.keyboard)
        return;

    g_wsi.wl.keyRepeatRate = rate;
    g_wsi.wl.keyRepeatDelay = delay;
}

static const struct wl_keyboard_listener keyboardListener =
{
    keyboardHandleKeymap,
    keyboardHandleEnter,
    keyboardHandleLeave,
    keyboardHandleKey,
    keyboardHandleModifiers,
    keyboardHandleRepeatInfo,
};

//-----------------------------------------------------------------------------

static void seatHandleCapabilities(void* userData,
                                   struct wl_seat* seat,
                                   enum wl_seat_capability caps)
{
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !g_wsi.wl.pointer)
    {
        g_wsi.wl.pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(g_wsi.wl.pointer, &pointerListener, NULL);
    }
    else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && g_wsi.wl.pointer)
    {
        if (wl_pointer_get_version(g_wsi.wl.pointer) >= WL_POINTER_RELEASE_SINCE_VERSION)
            wl_pointer_release(g_wsi.wl.pointer);
        else
            wl_pointer_destroy(g_wsi.wl.pointer);

        g_wsi.wl.pointer = NULL;
    }

    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !g_wsi.wl.keyboard)
    {
        g_wsi.wl.keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(g_wsi.wl.keyboard, &keyboardListener, NULL);

        if (wl_keyboard_get_version(g_wsi.wl.keyboard) <
            WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION)
        {
            g_wsi.wl.keyRepeatRate = 4;
            g_wsi.wl.keyRepeatDelay = 500;
        }
    }
    else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && g_wsi.wl.keyboard)
    {
        if (wl_keyboard_get_version(g_wsi.wl.keyboard) >= WL_KEYBOARD_RELEASE_SINCE_VERSION)
            wl_keyboard_release(g_wsi.wl.keyboard);
        else
            wl_keyboard_destroy(g_wsi.wl.keyboard);

        g_wsi.wl.keyboard = NULL;
    }
}

static void seatHandleName(void* userData,
                           struct wl_seat* seat,
                           const char* name)
{
}

static const struct wl_seat_listener seatListener =
{
    seatHandleCapabilities,
    seatHandleName,
};

//-----------------------------------------------------------------------------

static void dataOfferHandleOffer(void* userData,
                                 struct wl_data_offer* offer,
                                 const char* mimeType)
{
    for (unsigned int i = 0; i < g_wsi.wl.offerCount; i++)
    {
        if (g_wsi.wl.offers[i].offer == offer)
        {
            if (strcmp(mimeType, "text/plain;charset=utf-8") == 0)
                g_wsi.wl.offers[i].text_plain_utf8 = true;
            else if (strcmp(mimeType, "text/uri-list") == 0)
                g_wsi.wl.offers[i].text_uri_list = true;

            break;
        }
    }
}

static const struct wl_data_offer_listener dataOfferListener =
{
    dataOfferHandleOffer
};


static void dataDeviceHandleDataOffer(void* userData,
                                      struct wl_data_device* device,
                                      struct wl_data_offer* offer)
{
    SC_offerWayland* offers =
        wsi_realloc(g_wsi.wl.offers,
                      sizeof(SC_offerWayland) * (g_wsi.wl.offerCount + 1));
    if (!offers)
    {
        impl_on_error(SC_WSI_ERR_OUT_OF_MEMORY, NULL);
        return;
    }

    g_wsi.wl.offers = offers;
    g_wsi.wl.offerCount++;

    g_wsi.wl.offers[g_wsi.wl.offerCount - 1] = (SC_offerWayland) { offer };
    wl_data_offer_add_listener(offer, &dataOfferListener, NULL);
}

static void dataDeviceHandleEnter(void* userData,
                                  struct wl_data_device* device,
                                  uint32_t serial,
                                  struct wl_surface* surface,
                                  wl_fixed_t x,
                                  wl_fixed_t y,
                                  struct wl_data_offer* offer)
{
    if (g_wsi.wl.dragOffer)
    {
        wl_data_offer_destroy(g_wsi.wl.dragOffer);
        g_wsi.wl.dragOffer = NULL;
        g_wsi.wl.dragFocus = NULL;
    }

    unsigned int i;

    for (i = 0; i < g_wsi.wl.offerCount; i++)
    {
        if (g_wsi.wl.offers[i].offer == offer)
            break;
    }

    if (i == g_wsi.wl.offerCount)
        return;

    if (surface && wl_proxy_get_tag((struct wl_proxy*) surface) == &g_wsi.wl.tag)
    {
        window_st* window = wl_surface_get_user_data(surface);
        if (window->wl.surface == surface)
        {
            if (g_wsi.wl.offers[i].text_uri_list)
            {
                g_wsi.wl.dragOffer = offer;
                g_wsi.wl.dragFocus = window;
                g_wsi.wl.dragSerial = serial;

                wl_data_offer_accept(offer, serial, "text/uri-list");
            }
        }
    }

    if (!g_wsi.wl.dragOffer)
    {
        wl_data_offer_accept(offer, serial, NULL);
        wl_data_offer_destroy(offer);
    }

    g_wsi.wl.offers[i] = g_wsi.wl.offers[g_wsi.wl.offerCount - 1];
    g_wsi.wl.offerCount--;
}

static void dataDeviceHandleLeave(void* userData,
                                  struct wl_data_device* device)
{
    if (g_wsi.wl.dragOffer)
    {
        wl_data_offer_destroy(g_wsi.wl.dragOffer);
        g_wsi.wl.dragOffer = NULL;
        g_wsi.wl.dragFocus = NULL;
    }
}

static void dataDeviceHandleMotion(void* userData,
                                   struct wl_data_device* device,
                                   uint32_t time,
                                   wl_fixed_t x,
                                   wl_fixed_t y)
{
}

static void dataDeviceHandleDrop(void* userData,
                                 struct wl_data_device* device)
{
    if (!g_wsi.wl.dragOffer)
        return;

    char* string = readDataOfferAsString(g_wsi.wl.dragOffer, "text/uri-list");
    if (string)
    {
        int count;
        char** paths = wsi_parse_url_list(string, &count);
        if (paths)
        {
            impl_on_drop(g_wsi.wl.dragFocus, count, (const char**) paths);

            for (int i = 0; i < count; i++)
                wsi_free(paths[i]);

            wsi_free(paths);
        }

        wsi_free(string);
    }
}

static void dataDeviceHandleSelection(void* userData,
                                      struct wl_data_device* device,
                                      struct wl_data_offer* offer)
{
    if (g_wsi.wl.selectionOffer)
    {
        wl_data_offer_destroy(g_wsi.wl.selectionOffer);
        g_wsi.wl.selectionOffer = NULL;
    }

    for (unsigned int i = 0; i < g_wsi.wl.offerCount; i++)
    {
        if (g_wsi.wl.offers[i].offer == offer)
        {
            if (g_wsi.wl.offers[i].text_plain_utf8)
                g_wsi.wl.selectionOffer = offer;
            else
                wl_data_offer_destroy(offer);

            g_wsi.wl.offers[i] = g_wsi.wl.offers[g_wsi.wl.offerCount - 1];
            g_wsi.wl.offerCount--;
            break;
        }
    }
}

static const struct wl_data_device_listener dataDeviceListener =
{
    dataDeviceHandleDataOffer,
    dataDeviceHandleEnter,
    dataDeviceHandleLeave,
    dataDeviceHandleMotion,
    dataDeviceHandleDrop,
    dataDeviceHandleSelection,
};

//-----------------------------------------------------------------------------

static void registryHandleGlobal(void* userData,
                                 struct wl_registry* registry,
                                 uint32_t name,
                                 const char* interface,
                                 uint32_t version)
{
    if (strcmp(interface, "wl_compositor") == 0)
    {
        g_wsi.wl.compositor =
            wl_registry_bind(registry, name, &wl_compositor_interface,
                             wsi_min(3, version));
    }
    else if (strcmp(interface, "wl_subcompositor") == 0)
    {
        g_wsi.wl.subcompositor =
            wl_registry_bind(registry, name, &wl_subcompositor_interface, 1);
    }
    else if (strcmp(interface, "wl_shm") == 0)
    {
        g_wsi.wl.shm =
            wl_registry_bind(registry, name, &wl_shm_interface, 1);
    }
    else if (strcmp(interface, "wl_output") == 0)
    {
        wayland_AddOutput(name, version);
    }
    else if (strcmp(interface, "wl_seat") == 0)
    {
        if (!g_wsi.wl.seat)
        {
            g_wsi.wl.seat =
                wl_registry_bind(registry, name, &wl_seat_interface,
                                 wsi_min(8, version));
            wl_seat_add_listener(g_wsi.wl.seat, &seatListener, NULL);
        }
    }
    else if (strcmp(interface, "wl_data_device_manager") == 0)
    {
        if (!g_wsi.wl.dataDeviceManager)
        {
            g_wsi.wl.dataDeviceManager =
                wl_registry_bind(registry, name,
                                 &wl_data_device_manager_interface, 1);
        }
    }
    else if (strcmp(interface, "xdg_wm_base") == 0)
    {
        g_wsi.wl.wmBase =
            wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(g_wsi.wl.wmBase, &wmBaseListener, NULL);
    }
    else if (strcmp(interface, "zxdg_decoration_manager_v1") == 0)
    {
        g_wsi.wl.decorationManager =
            wl_registry_bind(registry, name,
                             &zxdg_decoration_manager_v1_interface,
                             1);
    }
    else if (strcmp(interface, "wp_viewporter") == 0)
    {
        g_wsi.wl.viewporter =
            wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
    }
    else if (strcmp(interface, "zwp_relative_pointer_manager_v1") == 0)
    {
        g_wsi.wl.relativePointerManager =
            wl_registry_bind(registry, name,
                             &zwp_relative_pointer_manager_v1_interface,
                             1);
    }
    else if (strcmp(interface, "zwp_pointer_constraints_v1") == 0)
    {
        g_wsi.wl.pointerConstraints =
            wl_registry_bind(registry, name,
                             &zwp_pointer_constraints_v1_interface,
                             1);
    }
    else if (strcmp(interface, "zwp_idle_inhibit_manager_v1") == 0)
    {
        g_wsi.wl.idleInhibitManager =
            wl_registry_bind(registry, name,
                             &zwp_idle_inhibit_manager_v1_interface,
                             1);
    }
    else if (strcmp(interface, "xdg_activation_v1") == 0)
    {
        g_wsi.wl.activationManager =
            wl_registry_bind(registry, name,
                             &xdg_activation_v1_interface,
                             1);
    }
    else if (strcmp(interface, "wp_fractional_scale_manager_v1") == 0)
    {
        g_wsi.wl.fractionalScaleManager =
            wl_registry_bind(registry, name,
                             &wp_fractional_scale_manager_v1_interface,
                             1);
    }
}

//-----------------------------------------------------------------------------

static void registryHandleGlobalRemove(void* userData,
                                       struct wl_registry* registry,
                                       uint32_t name)
{
    for (int i = 0; i < g_wsi.monitorCount; ++i)
    {
        monitor_st* monitor = g_wsi.monitors[i];
        if (monitor->wl.name == name)
        {
            impl_on_monitor(monitor, SC_DISCONNECTED, 0);
            return;
        }
    }
}


static const struct wl_registry_listener registryListener = {
    registryHandleGlobal,
    registryHandleGlobalRemove
};

static void libdecorHandleError(struct libdecor* context,
                         enum libdecor_error error,
                         const char* message)
{
    impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                    "Wayland: libdecor error %u: %s",
                    error, message);
}

static const struct libdecor_interface libdecorInterface = {
    libdecorHandleError
};

static void libdecorReadyCallback(void* userData,
                                  struct wl_callback* callback,
                                  uint32_t time)
{
    g_wsi.wl.libdecor.ready = true;
    wl_callback_destroy(callback);
}

static const struct wl_callback_listener libdecorReadyListener = {
    libdecorReadyCallback
};

static bool loadCursorTheme(void)
{
    int cursorSize = 16;

    const char* sizeString = getenv("XCURSOR_SIZE");
    if (sizeString)
    {
        errno = 0;
        const long cursorSizeLong = strtol(sizeString, NULL, 10);
        if (errno == 0 && cursorSizeLong > 0 && cursorSizeLong < INT_MAX)
            cursorSize = (int) cursorSizeLong;
    }

    const char* themeName = getenv("XCURSOR_THEME");

    g_wsi.wl.cursorTheme = wl_cursor_theme_load(themeName, cursorSize, g_wsi.wl.shm);
    if (!g_wsi.wl.cursorTheme)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to load default cursor theme");
        return false;
    }

    // If this happens to be NULL, we just fallback to the scale=1 version.
    g_wsi.wl.cursorThemeHiDPI =
        wl_cursor_theme_load(themeName, cursorSize * 2, g_wsi.wl.shm);

    g_wsi.wl.cursorSurface = wl_compositor_create_surface(g_wsi.wl.compositor);
    g_wsi.wl.cursorTimerfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    return true;
}

// Create key code translation tables
static void createKeyTables(void)
{
    memset(g_wsi.wl.keycodes, -1, sizeof(g_wsi.wl.keycodes));
    memset(g_wsi.wl.scancodes, -1, sizeof(g_wsi.wl.scancodes));

    g_wsi.wl.keycodes[KEY_GRAVE]      = SC_KEY_GRAVE_ACCENT;
    g_wsi.wl.keycodes[KEY_1]          = SC_KEY_1;
    g_wsi.wl.keycodes[KEY_2]          = SC_KEY_2;
    g_wsi.wl.keycodes[KEY_3]          = SC_KEY_3;
    g_wsi.wl.keycodes[KEY_4]          = SC_KEY_4;
    g_wsi.wl.keycodes[KEY_5]          = SC_KEY_5;
    g_wsi.wl.keycodes[KEY_6]          = SC_KEY_6;
    g_wsi.wl.keycodes[KEY_7]          = SC_KEY_7;
    g_wsi.wl.keycodes[KEY_8]          = SC_KEY_8;
    g_wsi.wl.keycodes[KEY_9]          = SC_KEY_9;
    g_wsi.wl.keycodes[KEY_0]          = SC_KEY_0;
    g_wsi.wl.keycodes[KEY_SPACE]      = SC_KEY_SPACE;
    g_wsi.wl.keycodes[KEY_MINUS]      = SC_KEY_MINUS;
    g_wsi.wl.keycodes[KEY_EQUAL]      = SC_KEY_EQUAL;
    g_wsi.wl.keycodes[KEY_Q]          = SC_KEY_Q;
    g_wsi.wl.keycodes[KEY_W]          = SC_KEY_W;
    g_wsi.wl.keycodes[KEY_E]          = SC_KEY_E;
    g_wsi.wl.keycodes[KEY_R]          = SC_KEY_R;
    g_wsi.wl.keycodes[KEY_T]          = SC_KEY_T;
    g_wsi.wl.keycodes[KEY_Y]          = SC_KEY_Y;
    g_wsi.wl.keycodes[KEY_U]          = SC_KEY_U;
    g_wsi.wl.keycodes[KEY_I]          = SC_KEY_I;
    g_wsi.wl.keycodes[KEY_O]          = SC_KEY_O;
    g_wsi.wl.keycodes[KEY_P]          = SC_KEY_P;
    g_wsi.wl.keycodes[KEY_LEFTBRACE]  = SC_KEY_LEFT_BRACKET;
    g_wsi.wl.keycodes[KEY_RIGHTBRACE] = SC_KEY_RIGHT_BRACKET;
    g_wsi.wl.keycodes[KEY_A]          = SC_KEY_A;
    g_wsi.wl.keycodes[KEY_S]          = SC_KEY_S;
    g_wsi.wl.keycodes[KEY_D]          = SC_KEY_D;
    g_wsi.wl.keycodes[KEY_F]          = SC_KEY_F;
    g_wsi.wl.keycodes[KEY_G]          = SC_KEY_G;
    g_wsi.wl.keycodes[KEY_H]          = SC_KEY_H;
    g_wsi.wl.keycodes[KEY_J]          = SC_KEY_J;
    g_wsi.wl.keycodes[KEY_K]          = SC_KEY_K;
    g_wsi.wl.keycodes[KEY_L]          = SC_KEY_L;
    g_wsi.wl.keycodes[KEY_SEMICOLON]  = SC_KEY_SEMICOLON;
    g_wsi.wl.keycodes[KEY_APOSTROPHE] = SC_KEY_APOSTROPHE;
    g_wsi.wl.keycodes[KEY_Z]          = SC_KEY_Z;
    g_wsi.wl.keycodes[KEY_X]          = SC_KEY_X;
    g_wsi.wl.keycodes[KEY_C]          = SC_KEY_C;
    g_wsi.wl.keycodes[KEY_V]          = SC_KEY_V;
    g_wsi.wl.keycodes[KEY_B]          = SC_KEY_B;
    g_wsi.wl.keycodes[KEY_N]          = SC_KEY_N;
    g_wsi.wl.keycodes[KEY_M]          = SC_KEY_M;
    g_wsi.wl.keycodes[KEY_COMMA]      = SC_KEY_COMMA;
    g_wsi.wl.keycodes[KEY_DOT]        = SC_KEY_PERIOD;
    g_wsi.wl.keycodes[KEY_SLASH]      = SC_KEY_SLASH;
    g_wsi.wl.keycodes[KEY_BACKSLASH]  = SC_KEY_BACKSLASH;
    g_wsi.wl.keycodes[KEY_ESC]        = SC_KEY_ESCAPE;
    g_wsi.wl.keycodes[KEY_TAB]        = SC_KEY_TAB;
    g_wsi.wl.keycodes[KEY_LEFTSHIFT]  = SC_KEY_LEFT_SHIFT;
    g_wsi.wl.keycodes[KEY_RIGHTSHIFT] = SC_KEY_RIGHT_SHIFT;
    g_wsi.wl.keycodes[KEY_LEFTCTRL]   = SC_KEY_LEFT_CONTROL;
    g_wsi.wl.keycodes[KEY_RIGHTCTRL]  = SC_KEY_RIGHT_CONTROL;
    g_wsi.wl.keycodes[KEY_LEFTALT]    = SC_KEY_LEFT_ALT;
    g_wsi.wl.keycodes[KEY_RIGHTALT]   = SC_KEY_RIGHT_ALT;
    g_wsi.wl.keycodes[KEY_LEFTMETA]   = SC_KEY_LEFT_SUPER;
    g_wsi.wl.keycodes[KEY_RIGHTMETA]  = SC_KEY_RIGHT_SUPER;
    g_wsi.wl.keycodes[KEY_COMPOSE]    = SC_KEY_MENU;
    g_wsi.wl.keycodes[KEY_NUMLOCK]    = SC_KEY_NUM_LOCK;
    g_wsi.wl.keycodes[KEY_CAPSLOCK]   = SC_KEY_CAPS_LOCK;
    g_wsi.wl.keycodes[KEY_PRINT]      = SC_KEY_PRINT_SCREEN;
    g_wsi.wl.keycodes[KEY_SCROLLLOCK] = SC_KEY_SCROLL_LOCK;
    g_wsi.wl.keycodes[KEY_PAUSE]      = SC_KEY_PAUSE;
    g_wsi.wl.keycodes[KEY_DELETE]     = SC_KEY_DELETE;
    g_wsi.wl.keycodes[KEY_BACKSPACE]  = SC_KEY_BACKSPACE;
    g_wsi.wl.keycodes[KEY_ENTER]      = SC_KEY_ENTER;
    g_wsi.wl.keycodes[KEY_HOME]       = SC_KEY_HOME;
    g_wsi.wl.keycodes[KEY_END]        = SC_KEY_END;
    g_wsi.wl.keycodes[KEY_PAGEUP]     = SC_KEY_PAGE_UP;
    g_wsi.wl.keycodes[KEY_PAGEDOWN]   = SC_KEY_PAGE_DOWN;
    g_wsi.wl.keycodes[KEY_INSERT]     = SC_KEY_INSERT;
    g_wsi.wl.keycodes[KEY_LEFT]       = SC_KEY_LEFT;
    g_wsi.wl.keycodes[KEY_RIGHT]      = SC_KEY_RIGHT;
    g_wsi.wl.keycodes[KEY_DOWN]       = SC_KEY_DOWN;
    g_wsi.wl.keycodes[KEY_UP]         = SC_KEY_UP;
    g_wsi.wl.keycodes[KEY_F1]         = SC_KEY_F1;
    g_wsi.wl.keycodes[KEY_F2]         = SC_KEY_F2;
    g_wsi.wl.keycodes[KEY_F3]         = SC_KEY_F3;
    g_wsi.wl.keycodes[KEY_F4]         = SC_KEY_F4;
    g_wsi.wl.keycodes[KEY_F5]         = SC_KEY_F5;
    g_wsi.wl.keycodes[KEY_F6]         = SC_KEY_F6;
    g_wsi.wl.keycodes[KEY_F7]         = SC_KEY_F7;
    g_wsi.wl.keycodes[KEY_F8]         = SC_KEY_F8;
    g_wsi.wl.keycodes[KEY_F9]         = SC_KEY_F9;
    g_wsi.wl.keycodes[KEY_F10]        = SC_KEY_F10;
    g_wsi.wl.keycodes[KEY_F11]        = SC_KEY_F11;
    g_wsi.wl.keycodes[KEY_F12]        = SC_KEY_F12;
    g_wsi.wl.keycodes[KEY_F13]        = SC_KEY_F13;
    g_wsi.wl.keycodes[KEY_F14]        = SC_KEY_F14;
    g_wsi.wl.keycodes[KEY_F15]        = SC_KEY_F15;
    g_wsi.wl.keycodes[KEY_F16]        = SC_KEY_F16;
    g_wsi.wl.keycodes[KEY_F17]        = SC_KEY_F17;
    g_wsi.wl.keycodes[KEY_F18]        = SC_KEY_F18;
    g_wsi.wl.keycodes[KEY_F19]        = SC_KEY_F19;
    g_wsi.wl.keycodes[KEY_F20]        = SC_KEY_F20;
    g_wsi.wl.keycodes[KEY_F21]        = SC_KEY_F21;
    g_wsi.wl.keycodes[KEY_F22]        = SC_KEY_F22;
    g_wsi.wl.keycodes[KEY_F23]        = SC_KEY_F23;
    g_wsi.wl.keycodes[KEY_F24]        = SC_KEY_F24;
    g_wsi.wl.keycodes[KEY_KPSLASH]    = SC_KEY_KP_DIVIDE;
    g_wsi.wl.keycodes[KEY_KPASTERISK] = SC_KEY_KP_MULTIPLY;
    g_wsi.wl.keycodes[KEY_KPMINUS]    = SC_KEY_KP_SUBTRACT;
    g_wsi.wl.keycodes[KEY_KPPLUS]     = SC_KEY_KP_ADD;
    g_wsi.wl.keycodes[KEY_KP0]        = SC_KEY_KP_0;
    g_wsi.wl.keycodes[KEY_KP1]        = SC_KEY_KP_1;
    g_wsi.wl.keycodes[KEY_KP2]        = SC_KEY_KP_2;
    g_wsi.wl.keycodes[KEY_KP3]        = SC_KEY_KP_3;
    g_wsi.wl.keycodes[KEY_KP4]        = SC_KEY_KP_4;
    g_wsi.wl.keycodes[KEY_KP5]        = SC_KEY_KP_5;
    g_wsi.wl.keycodes[KEY_KP6]        = SC_KEY_KP_6;
    g_wsi.wl.keycodes[KEY_KP7]        = SC_KEY_KP_7;
    g_wsi.wl.keycodes[KEY_KP8]        = SC_KEY_KP_8;
    g_wsi.wl.keycodes[KEY_KP9]        = SC_KEY_KP_9;
    g_wsi.wl.keycodes[KEY_KPDOT]      = SC_KEY_KP_DECIMAL;
    g_wsi.wl.keycodes[KEY_KPEQUAL]    = SC_KEY_KP_EQUAL;
    g_wsi.wl.keycodes[KEY_KPENTER]    = SC_KEY_KP_ENTER;
    g_wsi.wl.keycodes[KEY_102ND]      = SC_KEY_WORLD_2;

    for (int scancode = 0;  scancode < 256;  scancode++)
    {
        if (g_wsi.wl.keycodes[scancode] > 0)
            g_wsi.wl.scancodes[g_wsi.wl.keycodes[scancode]] = scancode;
    }
}

//-----------------------------------------------------------------------------

static int wayland_init(void)
{
    // These must be set before any failure checks
    g_wsi.wl.keyRepeatTimerfd = -1;
    g_wsi.wl.cursorTimerfd = -1;

    g_wsi.wl.tag = sc_wsi_get_version_string();

    g_wsi.wl.client.display_flush = (PFN_wl_display_flush)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_display_flush");
    g_wsi.wl.client.display_cancel_read = (PFN_wl_display_cancel_read)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_display_cancel_read");
    g_wsi.wl.client.display_dispatch_pending = (PFN_wl_display_dispatch_pending)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_display_dispatch_pending");
    g_wsi.wl.client.display_read_events = (PFN_wl_display_read_events)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_display_read_events");
    g_wsi.wl.client.display_disconnect = (PFN_wl_display_disconnect)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_display_disconnect");
    g_wsi.wl.client.display_roundtrip = (PFN_wl_display_roundtrip)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_display_roundtrip");
    g_wsi.wl.client.display_get_fd = (PFN_wl_display_get_fd)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_display_get_fd");
    g_wsi.wl.client.display_prepare_read = (PFN_wl_display_prepare_read)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_display_prepare_read");
    g_wsi.wl.client.display_create_queue = (PFN_wl_display_create_queue)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_display_create_queue");
    g_wsi.wl.client.display_prepare_read_queue = (PFN_wl_display_prepare_read_queue)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_display_prepare_read_queue");
    g_wsi.wl.client.display_dispatch_queue_pending = (PFN_wl_display_dispatch_queue_pending)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_display_dispatch_queue_pending");
    g_wsi.wl.client.event_queue_destroy = (PFN_wl_event_queue_destroy)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_event_queue_destroy");
    g_wsi.wl.client.proxy_marshal = (PFN_wl_proxy_marshal)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_proxy_marshal");
    g_wsi.wl.client.proxy_add_listener = (PFN_wl_proxy_add_listener)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_proxy_add_listener");
    g_wsi.wl.client.proxy_destroy = (PFN_wl_proxy_destroy)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_proxy_destroy");
    g_wsi.wl.client.proxy_marshal_constructor = (PFN_wl_proxy_marshal_constructor)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_proxy_marshal_constructor");
    g_wsi.wl.client.proxy_marshal_constructor_versioned = (PFN_wl_proxy_marshal_constructor_versioned)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_proxy_marshal_constructor_versioned");
    g_wsi.wl.client.proxy_get_user_data = (PFN_wl_proxy_get_user_data)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_proxy_get_user_data");
    g_wsi.wl.client.proxy_set_user_data = (PFN_wl_proxy_set_user_data)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_proxy_set_user_data");
    g_wsi.wl.client.proxy_get_tag = (PFN_wl_proxy_get_tag)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_proxy_get_tag");
    g_wsi.wl.client.proxy_set_tag = (PFN_wl_proxy_set_tag)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_proxy_set_tag");
    g_wsi.wl.client.proxy_get_version = (PFN_wl_proxy_get_version)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_proxy_get_version");
    g_wsi.wl.client.proxy_marshal_flags = (PFN_wl_proxy_marshal_flags)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_proxy_marshal_flags");
    g_wsi.wl.client.proxy_create_wrapper = (PFN_wl_proxy_create_wrapper)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_proxy_create_wrapper");
    g_wsi.wl.client.proxy_wrapper_destroy = (PFN_wl_proxy_wrapper_destroy)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_proxy_wrapper_destroy");
    g_wsi.wl.client.proxy_set_queue = (PFN_wl_proxy_set_queue)
        P_dl_get_proc(g_wsi.wl.client.handle, "wl_proxy_set_queue");

    if (!g_wsi.wl.client.display_flush ||
        !g_wsi.wl.client.display_cancel_read ||
        !g_wsi.wl.client.display_dispatch_pending ||
        !g_wsi.wl.client.display_read_events ||
        !g_wsi.wl.client.display_disconnect ||
        !g_wsi.wl.client.display_roundtrip ||
        !g_wsi.wl.client.display_get_fd ||
        !g_wsi.wl.client.display_prepare_read ||
        !g_wsi.wl.client.display_create_queue ||
        !g_wsi.wl.client.display_prepare_read_queue ||
        !g_wsi.wl.client.display_dispatch_queue_pending ||
        !g_wsi.wl.client.event_queue_destroy ||
        !g_wsi.wl.client.proxy_marshal ||
        !g_wsi.wl.client.proxy_add_listener ||
        !g_wsi.wl.client.proxy_destroy ||
        !g_wsi.wl.client.proxy_marshal_constructor ||
        !g_wsi.wl.client.proxy_marshal_constructor_versioned ||
        !g_wsi.wl.client.proxy_get_user_data ||
        !g_wsi.wl.client.proxy_set_user_data ||
        !g_wsi.wl.client.proxy_get_tag ||
        !g_wsi.wl.client.proxy_set_tag ||
        !g_wsi.wl.client.proxy_create_wrapper ||
        !g_wsi.wl.client.proxy_wrapper_destroy ||
        !g_wsi.wl.client.proxy_set_queue)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to load libwayland-client entry point");
        return false;
    }

    g_wsi.wl.cursor.handle = P_dl_load("libwayland-cursor.so.0");
    if (!g_wsi.wl.cursor.handle)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to load libwayland-cursor");
        return false;
    }

    g_wsi.wl.cursor.theme_load = (PFN_wl_cursor_theme_load)
        P_dl_get_proc(g_wsi.wl.cursor.handle, "wl_cursor_theme_load");
    g_wsi.wl.cursor.theme_destroy = (PFN_wl_cursor_theme_destroy)
        P_dl_get_proc(g_wsi.wl.cursor.handle, "wl_cursor_theme_destroy");
    g_wsi.wl.cursor.theme_get_cursor = (PFN_wl_cursor_theme_get_cursor)
        P_dl_get_proc(g_wsi.wl.cursor.handle, "wl_cursor_theme_get_cursor");
    g_wsi.wl.cursor.image_get_buffer = (PFN_wl_cursor_image_get_buffer)
        P_dl_get_proc(g_wsi.wl.cursor.handle, "wl_cursor_image_get_buffer");

    g_wsi.wl.xkb.handle = P_dl_load("libxkbcommon.so.0");
    if (!g_wsi.wl.xkb.handle)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to load libxkbcommon");
        return false;
    }

    g_wsi.wl.xkb.context_new = (PFN_xkb_context_new)
        P_dl_get_proc(g_wsi.wl.xkb.handle, "xkb_context_new");
    g_wsi.wl.xkb.context_unref = (PFN_xkb_context_unref)
        P_dl_get_proc(g_wsi.wl.xkb.handle, "xkb_context_unref");
    g_wsi.wl.xkb.keymap_new_from_string = (PFN_xkb_keymap_new_from_string)
        P_dl_get_proc(g_wsi.wl.xkb.handle, "xkb_keymap_new_from_string");
    g_wsi.wl.xkb.keymap_unref = (PFN_xkb_keymap_unref)
        P_dl_get_proc(g_wsi.wl.xkb.handle, "xkb_keymap_unref");
    g_wsi.wl.xkb.keymap_mod_get_index = (PFN_xkb_keymap_mod_get_index)
        P_dl_get_proc(g_wsi.wl.xkb.handle, "xkb_keymap_mod_get_index");
    g_wsi.wl.xkb.keymap_key_repeats = (PFN_xkb_keymap_key_repeats)
        P_dl_get_proc(g_wsi.wl.xkb.handle, "xkb_keymap_key_repeats");
    g_wsi.wl.xkb.keymap_key_get_syms_by_level = (PFN_xkb_keymap_key_get_syms_by_level)
        P_dl_get_proc(g_wsi.wl.xkb.handle, "xkb_keymap_key_get_syms_by_level");
    g_wsi.wl.xkb.state_new = (PFN_xkb_state_new)
        P_dl_get_proc(g_wsi.wl.xkb.handle, "xkb_state_new");
    g_wsi.wl.xkb.state_unref = (PFN_xkb_state_unref)
        P_dl_get_proc(g_wsi.wl.xkb.handle, "xkb_state_unref");
    g_wsi.wl.xkb.state_key_get_syms = (PFN_xkb_state_key_get_syms)
        P_dl_get_proc(g_wsi.wl.xkb.handle, "xkb_state_key_get_syms");
    g_wsi.wl.xkb.state_update_mask = (PFN_xkb_state_update_mask)
        P_dl_get_proc(g_wsi.wl.xkb.handle, "xkb_state_update_mask");
    g_wsi.wl.xkb.state_key_get_layout = (PFN_xkb_state_key_get_layout)
        P_dl_get_proc(g_wsi.wl.xkb.handle, "xkb_state_key_get_layout");
    g_wsi.wl.xkb.state_mod_index_is_active = (PFN_xkb_state_mod_index_is_active)
        P_dl_get_proc(g_wsi.wl.xkb.handle, "xkb_state_mod_index_is_active");
    g_wsi.wl.xkb.compose_table_new_from_locale = (PFN_xkb_compose_table_new_from_locale)
        P_dl_get_proc(g_wsi.wl.xkb.handle, "xkb_compose_table_new_from_locale");
    g_wsi.wl.xkb.compose_table_unref = (PFN_xkb_compose_table_unref)
        P_dl_get_proc(g_wsi.wl.xkb.handle, "xkb_compose_table_unref");
    g_wsi.wl.xkb.compose_state_new = (PFN_xkb_compose_state_new)
        P_dl_get_proc(g_wsi.wl.xkb.handle, "xkb_compose_state_new");
    g_wsi.wl.xkb.compose_state_unref = (PFN_xkb_compose_state_unref)
        P_dl_get_proc(g_wsi.wl.xkb.handle, "xkb_compose_state_unref");
    g_wsi.wl.xkb.compose_state_feed = (PFN_xkb_compose_state_feed)
        P_dl_get_proc(g_wsi.wl.xkb.handle, "xkb_compose_state_feed");
    g_wsi.wl.xkb.compose_state_get_status = (PFN_xkb_compose_state_get_status)
        P_dl_get_proc(g_wsi.wl.xkb.handle, "xkb_compose_state_get_status");
    g_wsi.wl.xkb.compose_state_get_one_sym = (PFN_xkb_compose_state_get_one_sym)
        P_dl_get_proc(g_wsi.wl.xkb.handle, "xkb_compose_state_get_one_sym");
    g_wsi.wl.xkb.keysym_to_utf32 = (PFN_xkb_keysym_to_utf32)
        P_dl_get_proc(g_wsi.wl.xkb.handle, "xkb_keysym_to_utf32");
    g_wsi.wl.xkb.keysym_to_utf8 = (PFN_xkb_keysym_to_utf8)
        P_dl_get_proc(g_wsi.wl.xkb.handle, "xkb_keysym_to_utf8");

    if (!g_wsi.wl.xkb.context_new ||
        !g_wsi.wl.xkb.context_unref ||
        !g_wsi.wl.xkb.keymap_new_from_string ||
        !g_wsi.wl.xkb.keymap_unref ||
        !g_wsi.wl.xkb.keymap_mod_get_index ||
        !g_wsi.wl.xkb.keymap_key_repeats ||
        !g_wsi.wl.xkb.keymap_key_get_syms_by_level ||
        !g_wsi.wl.xkb.state_new ||
        !g_wsi.wl.xkb.state_unref ||
        !g_wsi.wl.xkb.state_key_get_syms ||
        !g_wsi.wl.xkb.state_update_mask ||
        !g_wsi.wl.xkb.state_key_get_layout ||
        !g_wsi.wl.xkb.state_mod_index_is_active ||
        !g_wsi.wl.xkb.compose_table_new_from_locale ||
        !g_wsi.wl.xkb.compose_table_unref ||
        !g_wsi.wl.xkb.compose_state_new ||
        !g_wsi.wl.xkb.compose_state_unref ||
        !g_wsi.wl.xkb.compose_state_feed ||
        !g_wsi.wl.xkb.compose_state_get_status ||
        !g_wsi.wl.xkb.compose_state_get_one_sym)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to load all entry points from libxkbcommon");
        return false;
    }

    if (g_wsi.hints.init.wl.libdecorMode == SC_WAYLAND_PREFER_LIBDECOR)
        g_wsi.wl.libdecor.handle = P_dl_load("libdecor-0.so.0");

    if (g_wsi.wl.libdecor.handle)
    {
        g_wsi.wl.libdecor.libdecor_new_ = (PFN_libdecor_new)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_new");
        g_wsi.wl.libdecor.libdecor_unref_ = (PFN_libdecor_unref)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_unref");
        g_wsi.wl.libdecor.libdecor_get_fd_ = (PFN_libdecor_get_fd)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_get_fd");
        g_wsi.wl.libdecor.libdecor_dispatch_ = (PFN_libdecor_dispatch)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_dispatch");
        g_wsi.wl.libdecor.libdecor_decorate_ = (PFN_libdecor_decorate)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_decorate");
        g_wsi.wl.libdecor.libdecor_frame_unref_ = (PFN_libdecor_frame_unref)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_frame_unref");
        g_wsi.wl.libdecor.libdecor_frame_set_app_id_ = (PFN_libdecor_frame_set_app_id)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_frame_set_app_id");
        g_wsi.wl.libdecor.libdecor_frame_set_title_ = (PFN_libdecor_frame_set_title)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_frame_set_title");
        g_wsi.wl.libdecor.libdecor_frame_set_minimized_ = (PFN_libdecor_frame_set_minimized)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_frame_set_minimized");
        g_wsi.wl.libdecor.libdecor_frame_set_fullscreen_ = (PFN_libdecor_frame_set_fullscreen)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_frame_set_fullscreen");
        g_wsi.wl.libdecor.libdecor_frame_unset_fullscreen_ = (PFN_libdecor_frame_unset_fullscreen)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_frame_unset_fullscreen");
        g_wsi.wl.libdecor.libdecor_frame_map_ = (PFN_libdecor_frame_map)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_frame_map");
        g_wsi.wl.libdecor.libdecor_frame_commit_ = (PFN_libdecor_frame_commit)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_frame_commit");
        g_wsi.wl.libdecor.libdecor_frame_set_min_content_size_ = (PFN_libdecor_frame_set_min_content_size)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_frame_set_min_content_size");
        g_wsi.wl.libdecor.libdecor_frame_set_max_content_size_ = (PFN_libdecor_frame_set_max_content_size)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_frame_set_max_content_size");
        g_wsi.wl.libdecor.libdecor_frame_set_maximized_ = (PFN_libdecor_frame_set_maximized)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_frame_set_maximized");
        g_wsi.wl.libdecor.libdecor_frame_unset_maximized_ = (PFN_libdecor_frame_unset_maximized)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_frame_unset_maximized");
        g_wsi.wl.libdecor.libdecor_frame_set_capabilities_ = (PFN_libdecor_frame_set_capabilities)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_frame_set_capabilities");
        g_wsi.wl.libdecor.libdecor_frame_unset_capabilities_ = (PFN_libdecor_frame_unset_capabilities)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_frame_unset_capabilities");
        g_wsi.wl.libdecor.libdecor_frame_set_visibility_ = (PFN_libdecor_frame_set_visibility)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_frame_set_visibility");
        g_wsi.wl.libdecor.libdecor_frame_is_visible_ = (PFN_libdecor_frame_is_visible)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_frame_is_visible");
        g_wsi.wl.libdecor.libdecor_frame_get_xdg_toplevel_ = (PFN_libdecor_frame_get_xdg_toplevel)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_frame_get_xdg_toplevel");
        g_wsi.wl.libdecor.libdecor_configuration_get_content_size_ = (PFN_libdecor_configuration_get_content_size)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_configuration_get_content_size");
        g_wsi.wl.libdecor.libdecor_configuration_get_window_state_ = (PFN_libdecor_configuration_get_window_state)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_configuration_get_window_state");
        g_wsi.wl.libdecor.libdecor_state_new_ = (PFN_libdecor_state_new)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_state_new");
        g_wsi.wl.libdecor.libdecor_state_free_ = (PFN_libdecor_state_free)
            P_dl_get_proc(g_wsi.wl.libdecor.handle, "libdecor_state_free");

        if (!g_wsi.wl.libdecor.libdecor_new_ ||
            !g_wsi.wl.libdecor.libdecor_unref_ ||
            !g_wsi.wl.libdecor.libdecor_get_fd_ ||
            !g_wsi.wl.libdecor.libdecor_dispatch_ ||
            !g_wsi.wl.libdecor.libdecor_decorate_ ||
            !g_wsi.wl.libdecor.libdecor_frame_unref_ ||
            !g_wsi.wl.libdecor.libdecor_frame_set_app_id_ ||
            !g_wsi.wl.libdecor.libdecor_frame_set_title_ ||
            !g_wsi.wl.libdecor.libdecor_frame_set_minimized_ ||
            !g_wsi.wl.libdecor.libdecor_frame_set_fullscreen_ ||
            !g_wsi.wl.libdecor.libdecor_frame_unset_fullscreen_ ||
            !g_wsi.wl.libdecor.libdecor_frame_map_ ||
            !g_wsi.wl.libdecor.libdecor_frame_commit_ ||
            !g_wsi.wl.libdecor.libdecor_frame_set_min_content_size_ ||
            !g_wsi.wl.libdecor.libdecor_frame_set_max_content_size_ ||
            !g_wsi.wl.libdecor.libdecor_frame_set_maximized_ ||
            !g_wsi.wl.libdecor.libdecor_frame_unset_maximized_ ||
            !g_wsi.wl.libdecor.libdecor_frame_set_capabilities_ ||
            !g_wsi.wl.libdecor.libdecor_frame_unset_capabilities_ ||
            !g_wsi.wl.libdecor.libdecor_frame_set_visibility_ ||
            !g_wsi.wl.libdecor.libdecor_frame_is_visible_ ||
            !g_wsi.wl.libdecor.libdecor_frame_get_xdg_toplevel_ ||
            !g_wsi.wl.libdecor.libdecor_configuration_get_content_size_ ||
            !g_wsi.wl.libdecor.libdecor_configuration_get_window_state_ ||
            !g_wsi.wl.libdecor.libdecor_state_new_ ||
            !g_wsi.wl.libdecor.libdecor_state_free_)
        {
            P_dl_unload(g_wsi.wl.libdecor.handle);
            memset(&g_wsi.wl.libdecor, 0, sizeof(g_wsi.wl.libdecor));
        }
    }

    g_wsi.wl.registry = wl_display_get_registry(g_wsi.wl.display);
    wl_registry_add_listener(g_wsi.wl.registry, &registryListener, NULL);

    createKeyTables();

    g_wsi.wl.keyRepeatTimerfd =
        timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (g_wsi.wl.keyRepeatTimerfd == -1)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to create timerfd: %s",
                        strerror(errno));
        return false;
    }

    g_wsi.wl.xkb.context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!g_wsi.wl.xkb.context)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to initialize xkb context");
        return false;
    }

    // Sync so we got all registry objects
    wl_display_roundtrip(g_wsi.wl.display);

    // Sync so we got all initial output events
    wl_display_roundtrip(g_wsi.wl.display);

    if (g_wsi.wl.libdecor.handle)
    {
        g_wsi.wl.libdecor.context = libdecor_new(g_wsi.wl.display, &libdecorInterface);
        if (g_wsi.wl.libdecor.context)
        {
            // Perform an initial dispatch and flush to get the init started
            libdecor_dispatch(g_wsi.wl.libdecor.context, 0);

            // Create sync point to "know" when libdecor is ready for use
            struct wl_callback* callback = wl_display_sync(g_wsi.wl.display);
            wl_callback_add_listener(callback, &libdecorReadyListener, NULL);
        }
    }

    if (!g_wsi.wl.wmBase)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to find xdg-shell in your compositor");
        return false;
    }

    if (!g_wsi.wl.shm)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to find wl_shm in your compositor");
        return false;
    }

    if (!loadCursorTheme())
        return false;

    if (g_wsi.wl.seat && g_wsi.wl.dataDeviceManager)
    {
        g_wsi.wl.dataDevice =
            wl_data_device_manager_get_data_device(g_wsi.wl.dataDeviceManager,
                                                   g_wsi.wl.seat);
        wl_data_device_add_listener(g_wsi.wl.dataDevice, &dataDeviceListener, NULL);
    }

    return true;
}

static void wayland_terminate(void)
{
    if (g_wsi.wl.libdecor.context)
    {
        // Allow libdecor to finish receiving all its requested globals
        // and ensure the associated sync callback object is destroyed
        while (!g_wsi.wl.libdecor.ready)
            wayland_wait_events();

        libdecor_unref(g_wsi.wl.libdecor.context);
    }

    if (g_wsi.wl.xkb.composeState)
        xkb_compose_state_unref(g_wsi.wl.xkb.composeState);
    if (g_wsi.wl.xkb.keymap)
        xkb_keymap_unref(g_wsi.wl.xkb.keymap);
    if (g_wsi.wl.xkb.state)
        xkb_state_unref(g_wsi.wl.xkb.state);
    if (g_wsi.wl.xkb.context)
        xkb_context_unref(g_wsi.wl.xkb.context);

    if (g_wsi.wl.cursorTheme)
        wl_cursor_theme_destroy(g_wsi.wl.cursorTheme);
    if (g_wsi.wl.cursorThemeHiDPI)
        wl_cursor_theme_destroy(g_wsi.wl.cursorThemeHiDPI);

    for (unsigned int i = 0; i < g_wsi.wl.offerCount; i++)
        wl_data_offer_destroy(g_wsi.wl.offers[i].offer);

    wsi_free(g_wsi.wl.offers);

    if (g_wsi.wl.cursorSurface)
        wl_surface_destroy(g_wsi.wl.cursorSurface);
    if (g_wsi.wl.subcompositor)
        wl_subcompositor_destroy(g_wsi.wl.subcompositor);
    if (g_wsi.wl.compositor)
        wl_compositor_destroy(g_wsi.wl.compositor);
    if (g_wsi.wl.shm)
        wl_shm_destroy(g_wsi.wl.shm);
    if (g_wsi.wl.viewporter)
        wp_viewporter_destroy(g_wsi.wl.viewporter);
    if (g_wsi.wl.decorationManager)
        zxdg_decoration_manager_v1_destroy(g_wsi.wl.decorationManager);
    if (g_wsi.wl.wmBase)
        xdg_wm_base_destroy(g_wsi.wl.wmBase);
    if (g_wsi.wl.selectionOffer)
        wl_data_offer_destroy(g_wsi.wl.selectionOffer);
    if (g_wsi.wl.dragOffer)
        wl_data_offer_destroy(g_wsi.wl.dragOffer);
    if (g_wsi.wl.selectionSource)
        wl_data_source_destroy(g_wsi.wl.selectionSource);
    if (g_wsi.wl.dataDevice)
        wl_data_device_destroy(g_wsi.wl.dataDevice);
    if (g_wsi.wl.dataDeviceManager)
        wl_data_device_manager_destroy(g_wsi.wl.dataDeviceManager);
    if (g_wsi.wl.pointer)
        wl_pointer_destroy(g_wsi.wl.pointer);
    if (g_wsi.wl.keyboard)
        wl_keyboard_destroy(g_wsi.wl.keyboard);
    if (g_wsi.wl.seat)
        wl_seat_destroy(g_wsi.wl.seat);
    if (g_wsi.wl.relativePointerManager)
        zwp_relative_pointer_manager_v1_destroy(g_wsi.wl.relativePointerManager);
    if (g_wsi.wl.pointerConstraints)
        zwp_pointer_constraints_v1_destroy(g_wsi.wl.pointerConstraints);
    if (g_wsi.wl.idleInhibitManager)
        zwp_idle_inhibit_manager_v1_destroy(g_wsi.wl.idleInhibitManager);
    if (g_wsi.wl.activationManager)
        xdg_activation_v1_destroy(g_wsi.wl.activationManager);
    if (g_wsi.wl.fractionalScaleManager)
        wp_fractional_scale_manager_v1_destroy(g_wsi.wl.fractionalScaleManager);
    if (g_wsi.wl.registry)
        wl_registry_destroy(g_wsi.wl.registry);
    if (g_wsi.wl.display)
    {
        wl_display_flush(g_wsi.wl.display);
        wl_display_disconnect(g_wsi.wl.display);
    }

    if (g_wsi.wl.keyRepeatTimerfd >= 0)
        close(g_wsi.wl.keyRepeatTimerfd);
    if (g_wsi.wl.cursorTimerfd >= 0)
        close(g_wsi.wl.cursorTimerfd);

    // Free modules only after all Wayland termination functions are called

    P_dl_unload(g_wsi.wl.libdecor.handle);
    P_dl_unload(g_wsi.wl.xkb.handle);
    P_dl_unload(g_wsi.wl.cursor.handle);
    P_dl_unload(g_wsi.wl.client.handle);

    wsi_free(g_wsi.wl.clipboardString);

    memset(&g_wsi.wl, 0, sizeof(g_wsi.wl));
}

///////////////////////////////////////////////////////////////////////////////
// Interface functions
///////////////////////////////////////////////////////////////////////////////

static void libdecorFrameHandleConfigure(struct libdecor_frame* frame,
                                  struct libdecor_configuration* config,
                                  void* userData)
{
    window_st* window = userData;
    int width, height;

    enum libdecor_window_state windowState;
    bool fullscreen, activated, maximized;

    if (libdecor_configuration_get_window_state(config, &windowState))
    {
        fullscreen = (windowState & LIBDECOR_WINDOW_STATE_FULLSCREEN) != 0;
        activated = (windowState & LIBDECOR_WINDOW_STATE_ACTIVE) != 0;
        maximized = (windowState & LIBDECOR_WINDOW_STATE_MAXIMIZED) != 0;
    }
    else
    {
        fullscreen = window->wl.fullscreen;
        activated = window->wl.activated;
        maximized = window->wl.maximized;
    }

    if (!libdecor_configuration_get_content_size(config, frame, &width, &height))
    {
        width = window->wl.width;
        height = window->wl.height;
    }

    if (!maximized && !fullscreen)
    {
        if (window->numer != SC_DONT_CARE && window->denom != SC_DONT_CARE)
        {
            const float aspectRatio = (float) width / (float) height;
            const float targetRatio = (float) window->numer / (float) window->denom;
            if (aspectRatio < targetRatio)
                height = width / targetRatio;
            else if (aspectRatio > targetRatio)
                width = height * targetRatio;
        }
    }

    struct libdecor_state* frameState = libdecor_state_new(width, height);
    libdecor_frame_commit(frame, frameState, config);
    libdecor_state_free(frameState);

    // NOTE: Frame visibility must only be set after a frame state has been committed
    if (window->decorated != libdecor_frame_is_visible(window->wl.libdecor.frame))
        libdecor_frame_set_visibility(window->wl.libdecor.frame, window->decorated);

    if (window->wl.activated != activated)
    {
        window->wl.activated = activated;
        if (!window->wl.activated)
        {
            if (window->monitor && window->autoIconify)
                libdecor_frame_set_minimized(window->wl.libdecor.frame);
        }
    }

    if (window->wl.maximized != maximized)
    {
        window->wl.maximized = maximized;
        impl_on_win_maximize(window, window->wl.maximized);
    }

    window->wl.fullscreen = fullscreen;

    bool damaged = false;

    if (!window->wl.visible)
    {
        window->wl.visible = true;
        damaged = true;
    }

    if (resizeWindow(window, width, height))
    {
        impl_on_win_size(window, window->wl.width, window->wl.height);
        damaged = true;
    }

    if (damaged)
        impl_on_win_damage(window);
    else
        wl_surface_commit(window->wl.surface);
}

static void libdecorFrameHandleClose(struct libdecor_frame* frame, void* userData)
{
    window_st* window = userData;
    impl_on_win_close_req(window);
}

static void libdecorFrameHandleCommit(struct libdecor_frame* frame, void* userData)
{
    window_st* window = userData;
    wl_surface_commit(window->wl.surface);
}

static void libdecorFrameHandleDismissPopup(struct libdecor_frame* frame,
                                            const char* seatName,
                                            void* userData)
{
}

static const struct libdecor_frame_interface libdecorFrameInterface =
{
    libdecorFrameHandleConfigure,
    libdecorFrameHandleClose,
    libdecorFrameHandleCommit,
    libdecorFrameHandleDismissPopup
};

static bool createLibdecorFrame(window_st* window)
{
    // Allow libdecor to finish initialization of itself and its plugin
    while (!g_wsi.wl.libdecor.ready)
        wayland_wait_events();

    window->wl.libdecor.frame = libdecor_decorate(g_wsi.wl.libdecor.context,
                                                  window->wl.surface,
                                                  &libdecorFrameInterface,
                                                  window);
    if (!window->wl.libdecor.frame)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to create libdecor frame");
        return false;
    }

    if (strlen(window->wl.appId))
        libdecor_frame_set_app_id(window->wl.libdecor.frame, window->wl.appId);

    libdecor_frame_set_title(window->wl.libdecor.frame, window->title);

    if (window->resizable)
    {
        if (window->minwidth != SC_DONT_CARE &&
            window->minheight != SC_DONT_CARE)
        {
            libdecor_frame_set_min_content_size(window->wl.libdecor.frame,
                                                window->minwidth,
                                                window->minheight);
        }

        if (window->maxwidth != SC_DONT_CARE &&
            window->maxheight != SC_DONT_CARE)
        {
            libdecor_frame_set_max_content_size(window->wl.libdecor.frame,
                                                window->maxwidth,
                                                window->maxheight);
        }
    }
    else
    {
        libdecor_frame_unset_capabilities(window->wl.libdecor.frame,
                                          LIBDECOR_ACTION_RESIZE);
        libdecor_frame_set_min_content_size(window->wl.libdecor.frame,
                                            window->wl.width,
                                            window->wl.height);
        libdecor_frame_set_max_content_size(window->wl.libdecor.frame,
                                            window->wl.width,
                                            window->wl.height);
    }

    if (window->monitor)
    {
        libdecor_frame_set_fullscreen(window->wl.libdecor.frame,
                                      window->monitor->wl.output);
        setIdleInhibitor(window, true);
    }
    else
    {
        // Frame visibility is applied in libdecorFrameHandleConfigure

        if (window->wl.maximized)
            libdecor_frame_set_maximized(window->wl.libdecor.frame);

        setIdleInhibitor(window, false);
    }

    libdecor_frame_map(window->wl.libdecor.frame);
    wl_display_roundtrip(g_wsi.wl.display);
    return true;
}


static void updateXdgSizeLimits(window_st* window)
{
    int minwidth, minheight, maxwidth, maxheight;

    if (window->resizable)
    {
        if (window->minwidth == SC_DONT_CARE || window->minheight == SC_DONT_CARE)
            minwidth = minheight = 0;
        else
        {
            minwidth  = window->minwidth;
            minheight = window->minheight;

            if (window->wl.fallback.decorations)
            {
                minwidth  += WSI_BORDER_SIZE * 2;
                minheight += WSI_CAPTION_HEIGHT + WSI_BORDER_SIZE;
            }
        }

        if (window->maxwidth == SC_DONT_CARE || window->maxheight == SC_DONT_CARE)
            maxwidth = maxheight = 0;
        else
        {
            maxwidth  = window->maxwidth;
            maxheight = window->maxheight;

            if (window->wl.fallback.decorations)
            {
                maxwidth  += WSI_BORDER_SIZE * 2;
                maxheight += WSI_CAPTION_HEIGHT + WSI_BORDER_SIZE;
            }
        }
    }
    else
    {
        minwidth = maxwidth = window->wl.width;
        minheight = maxheight = window->wl.height;
    }

    xdg_toplevel_set_min_size(window->wl.xdg.toplevel, minwidth, minheight);
    xdg_toplevel_set_max_size(window->wl.xdg.toplevel, maxwidth, maxheight);
}

static void xdgSurfaceHandleConfigure(void* userData,
                                      struct xdg_surface* surface,
                                      uint32_t serial)
{
    window_st* window = userData;

    xdg_surface_ack_configure(surface, serial);

    if (window->wl.activated != window->wl.pending.activated)
    {
        window->wl.activated = window->wl.pending.activated;
        if (!window->wl.activated)
        {
            if (window->monitor && window->autoIconify)
                xdg_toplevel_set_minimized(window->wl.xdg.toplevel);
        }
    }

    if (window->wl.maximized != window->wl.pending.maximized)
    {
        window->wl.maximized = window->wl.pending.maximized;
        impl_on_win_maximize(window, window->wl.maximized);
    }

    window->wl.fullscreen = window->wl.pending.fullscreen;

    int width  = window->wl.pending.width;
    int height = window->wl.pending.height;

    if (!window->wl.maximized && !window->wl.fullscreen)
    {
        if (window->numer != SC_DONT_CARE && window->denom != SC_DONT_CARE)
        {
            const float aspectRatio = (float) width / (float) height;
            const float targetRatio = (float) window->numer / (float) window->denom;
            if (aspectRatio < targetRatio)
                height = width / targetRatio;
            else if (aspectRatio > targetRatio)
                width = height * targetRatio;
        }
    }

    if (resizeWindow(window, width, height))
    {
        impl_on_win_size(window, window->wl.width, window->wl.height);

        if (window->wl.visible)
            impl_on_win_damage(window);
    }

    if (!window->wl.visible)
    {
        // Allow the window to be mapped only if it either has no XDG
        // decorations or they have already received a configure event
        if (!window->wl.xdg.decoration || window->wl.xdg.decorationMode)
        {
            window->wl.visible = true;
            impl_on_win_damage(window);
        }
    }
}

static const struct xdg_surface_listener xdgSurfaceListener =
{
    xdgSurfaceHandleConfigure
};

static void xdgDecorationHandleConfigure(void* userData,
                                         struct zxdg_toplevel_decoration_v1* decoration,
                                         uint32_t mode)
{
    window_st* window = userData;

    window->wl.xdg.decorationMode = mode;

    if (mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE)
    {
        if (window->decorated && !window->monitor)
            createFallbackDecorations(window);
    }
    else
        destroyFallbackDecorations(window);
}

static const struct zxdg_toplevel_decoration_v1_listener xdgDecorationListener =
{
    xdgDecorationHandleConfigure,
};

static void xdgToplevelHandleConfigure(void* userData,
                                       struct xdg_toplevel* toplevel,
                                       int32_t width,
                                       int32_t height,
                                       struct wl_array* states)
{
    window_st* window = userData;
    uint32_t* state;

    window->wl.pending.activated  = false;
    window->wl.pending.maximized  = false;
    window->wl.pending.fullscreen = false;

    wl_array_for_each(state, states)
    {
        switch (*state)
        {
            case XDG_TOPLEVEL_STATE_MAXIMIZED:
                window->wl.pending.maximized = true;
                break;
            case XDG_TOPLEVEL_STATE_FULLSCREEN:
                window->wl.pending.fullscreen = true;
                break;
            case XDG_TOPLEVEL_STATE_RESIZING:
                break;
            case XDG_TOPLEVEL_STATE_ACTIVATED:
                window->wl.pending.activated = true;
                break;
        }
    }

    if (width && height)
    {
        if (window->wl.fallback.decorations)
        {
            window->wl.pending.width  = wsi_max(0, width - WSI_BORDER_SIZE * 2);
            window->wl.pending.height =
                wsi_max(0, height - WSI_BORDER_SIZE - WSI_CAPTION_HEIGHT);
        }
        else
        {
            window->wl.pending.width  = width;
            window->wl.pending.height = height;
        }
    }
    else
    {
        window->wl.pending.width  = window->wl.width;
        window->wl.pending.height = window->wl.height;
    }
}

static void xdgToplevelHandleClose(void* userData,
                                   struct xdg_toplevel* toplevel)
{
    window_st* window = userData;
    impl_on_win_close_req(window);
}

static const struct xdg_toplevel_listener xdgToplevelListener =
{
    xdgToplevelHandleConfigure,
    xdgToplevelHandleClose
};

static bool createXdgShellObjects(window_st* window)
{
    window->wl.xdg.surface = xdg_wm_base_get_xdg_surface(g_wsi.wl.wmBase,
                                                         window->wl.surface);
    if (!window->wl.xdg.surface)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to create xdg-surface for window");
        return false;
    }

    xdg_surface_add_listener(window->wl.xdg.surface, &xdgSurfaceListener, window);

    window->wl.xdg.toplevel = xdg_surface_get_toplevel(window->wl.xdg.surface);
    if (!window->wl.xdg.toplevel)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to create xdg-toplevel for window");
        return false;
    }

    xdg_toplevel_add_listener(window->wl.xdg.toplevel, &xdgToplevelListener, window);

    if (window->wl.appId)
        xdg_toplevel_set_app_id(window->wl.xdg.toplevel, window->wl.appId);

    xdg_toplevel_set_title(window->wl.xdg.toplevel, window->title);

    if (window->monitor)
    {
        xdg_toplevel_set_fullscreen(window->wl.xdg.toplevel, window->monitor->wl.output);
        setIdleInhibitor(window, true);
    }
    else
    {
        if (window->wl.maximized)
            xdg_toplevel_set_maximized(window->wl.xdg.toplevel);

        setIdleInhibitor(window, false);
    }

    if (g_wsi.wl.decorationManager)
    {
        window->wl.xdg.decoration =
            zxdg_decoration_manager_v1_get_toplevel_decoration(
                g_wsi.wl.decorationManager, window->wl.xdg.toplevel);
        zxdg_toplevel_decoration_v1_add_listener(window->wl.xdg.decoration,
                                                 &xdgDecorationListener,
                                                 window);

        uint32_t mode;

        if (window->decorated)
            mode = ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
        else
            mode = ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;

        zxdg_toplevel_decoration_v1_set_mode(window->wl.xdg.decoration, mode);
    }
    else
    {
        if (window->decorated && !window->monitor)
            createFallbackDecorations(window);
    }

    updateXdgSizeLimits(window);

    wl_surface_commit(window->wl.surface);
    wl_display_roundtrip(g_wsi.wl.display);
    return true;
}


static bool createShellObjects(window_st* window)
{
    if (g_wsi.wl.libdecor.context)
    {
        if (createLibdecorFrame(window))
            return true;
    }

    return createXdgShellObjects(window);
}

static void destroyShellObjects(window_st* window)
{
    destroyFallbackDecorations(window);

    if (window->wl.libdecor.frame)
        libdecor_frame_unref(window->wl.libdecor.frame);

    if (window->wl.xdg.decoration)
        zxdg_toplevel_decoration_v1_destroy(window->wl.xdg.decoration);

    if (window->wl.xdg.toplevel)
        xdg_toplevel_destroy(window->wl.xdg.toplevel);

    if (window->wl.xdg.surface)
        xdg_surface_destroy(window->wl.xdg.surface);

    window->wl.libdecor.frame = NULL;
    window->wl.xdg.decoration = NULL;
    window->wl.xdg.decorationMode = 0;
    window->wl.xdg.toplevel = NULL;
    window->wl.xdg.surface = NULL;
}

//-----------------------------------------------------------------------------

static void xdgActivationHandleDone(void* userData,
                                    struct xdg_activation_token_v1* activationToken,
                                    const char* token)
{
    window_st* window = userData;

    if (activationToken != window->wl.activationToken)
        return;

    xdg_activation_v1_activate(g_wsi.wl.activationManager, token, window->wl.surface);
    xdg_activation_token_v1_destroy(window->wl.activationToken);
    window->wl.activationToken = NULL;
}

static const struct xdg_activation_token_v1_listener xdgActivationListener =
{
    xdgActivationHandleDone
};

///////////////////////////////////////////////////////////////////////////////

static void wayland_set_window_title(window_st* window, const char* title)
{
    if (window->wl.libdecor.frame)
        libdecor_frame_set_title(window->wl.libdecor.frame, title);
    else if (window->wl.xdg.toplevel)
        xdg_toplevel_set_title(window->wl.xdg.toplevel, title);
}

static void wayland_set_window_icon(window_st* window,
                               int count, const sc_wsi_img* images)
{
    impl_on_error(SC_WSI_ERR_FEATURE_UNAVAILABLE,
                    "Wayland: The platform does not support setting the window icon");
}

static void wayland_set_window_mouse_passthrough(window_st* window, bool enabled)
{
    if (enabled)
    {
        struct wl_region* region = wl_compositor_create_region(g_wsi.wl.compositor);
        wl_surface_set_input_region(window->wl.surface, region);
        wl_region_destroy(region);
    }
    else
        wl_surface_set_input_region(window->wl.surface, NULL);
}


static void wayland_set_window_decorated(window_st* window, bool enabled)
{
    if (window->wl.libdecor.frame)
    {
        libdecor_frame_set_visibility(window->wl.libdecor.frame, enabled);
    }
    else if (window->wl.xdg.decoration)
    {
        uint32_t mode;

        if (enabled)
            mode = ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
        else
            mode = ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;

        zxdg_toplevel_decoration_v1_set_mode(window->wl.xdg.decoration, mode);
    }
    else if (window->wl.xdg.toplevel)
    {
        if (enabled)
            createFallbackDecorations(window);
        else
            destroyFallbackDecorations(window);
    }
}

static void wayland_set_window_floating(window_st* window, bool enabled)
{
    impl_on_error(SC_WSI_ERR_FEATURE_UNAVAILABLE,
                    "Wayland: Platform does not support making a window floating");
}

static float wayland_get_window_opacity(window_st* window)
{
    return 1.f;
}

static void wayland_set_window_opacity(window_st* window, float opacity)
{
    impl_on_error(SC_WSI_ERR_FEATURE_UNAVAILABLE,
                    "Wayland: The platform does not support setting the window opacity");
}


static void wayland_get_window_pos(window_st* window, int* xpos, int* ypos)
{
    // A Wayland client is not aware of its position, so just warn and leave it
    // as (0, 0)

    impl_on_error(SC_WSI_ERR_FEATURE_UNAVAILABLE,
                    "Wayland: The platform does not provide the window position");
}

static void wayland_set_window_pos(window_st* window, int xpos, int ypos)
{
    // A Wayland client can not set its position, so just warn

    impl_on_error(SC_WSI_ERR_FEATURE_UNAVAILABLE,
                    "Wayland: The platform does not support setting the window position");
}

static void wayland_get_window_size(window_st* window, int* width, int* height)
{
    if (width)
        *width = window->wl.width;
    if (height)
        *height = window->wl.height;
}

static void wayland_get_framebuffer_size(window_st* window, int* width, int* height)
{
    // Wayland 服务端缩放：帧缓冲像素 = 逻辑尺寸 × buffer_scale（同步于
    // output enter 事件，可在窗口 show 之后才更新）。
    if (width)
        *width = window->wl.fbWidth;
    if (height)
        *height = window->wl.fbHeight;
}

static void wayland_set_window_size(window_st* window, int width, int height)
{
    if (window->monitor)
    {
        // Video mode setting is not available on Wayland
    }
    else
    {
        if (!resizeWindow(window, width, height))
            return;

        if (window->wl.libdecor.frame)
        {
            struct libdecor_state* frameState =
                libdecor_state_new(window->wl.width, window->wl.height);
            libdecor_frame_commit(window->wl.libdecor.frame, frameState, NULL);
            libdecor_state_free(frameState);
        }

        if (window->wl.visible)
            impl_on_win_damage(window);
    }
}

static void wayland_get_window_frame_size(window_st* window,
                                    int* left, int* top,
                                    int* right, int* bottom)
{
    if (window->wl.fallback.decorations)
    {
        if (top)
            *top = WSI_CAPTION_HEIGHT;
        if (left)
            *left = WSI_BORDER_SIZE;
        if (right)
            *right = WSI_BORDER_SIZE;
        if (bottom)
            *bottom = WSI_BORDER_SIZE;
    }
}

static void wayland_set_window_size_limits(window_st* window,
                                     int minwidth, int minheight,
                                     int maxwidth, int maxheight)
{
    if (window->wl.libdecor.frame)
    {
        if (minwidth == SC_DONT_CARE || minheight == SC_DONT_CARE)
            minwidth = minheight = 0;

        if (maxwidth == SC_DONT_CARE || maxheight == SC_DONT_CARE)
            maxwidth = maxheight = 0;

        libdecor_frame_set_min_content_size(window->wl.libdecor.frame,
                                            minwidth, minheight);
        libdecor_frame_set_max_content_size(window->wl.libdecor.frame,
                                            maxwidth, maxheight);
    }
    else if (window->wl.xdg.toplevel)
        updateXdgSizeLimits(window);
}

static void wayland_get_window_content_scale(window_st* window,
                                       float* xscale, float* yscale)
{
    if (window->wl.fractionalScale)
    {
        if (xscale)
            *xscale = (float) window->wl.scalingNumerator / 120.f;
        if (yscale)
            *yscale = (float) window->wl.scalingNumerator / 120.f;
    }
    else
    {
        if (xscale)
            *xscale = (float) window->wl.bufferScale;
        if (yscale)
            *yscale = (float) window->wl.bufferScale;
    }
}

static void wayland_set_window_aspect_ratio(window_st* window, int numer, int denom)
{
    if (window->wl.maximized || window->wl.fullscreen)
        return;

    int width = window->wl.width, height = window->wl.height;

    if (numer != SC_DONT_CARE && denom != SC_DONT_CARE)
    {
        const float aspectRatio = (float) width / (float) height;
        const float targetRatio = (float) numer / (float) denom;
        if (aspectRatio < targetRatio)
            height /= targetRatio;
        else if (aspectRatio > targetRatio)
            width *= targetRatio;
    }

    if (resizeWindow(window, width, height))
    {
        if (window->wl.libdecor.frame)
        {
            struct libdecor_state* frameState =
                libdecor_state_new(window->wl.width, window->wl.height);
            libdecor_frame_commit(window->wl.libdecor.frame, frameState, NULL);
            libdecor_state_free(frameState);
        }

        impl_on_win_size(window, window->wl.width, window->wl.height);

        if (window->wl.visible)
            impl_on_win_damage(window);
    }
}


static void wayland_show_window(window_st* window)
{
    if (!window->wl.libdecor.frame && !window->wl.xdg.toplevel)
    {
        // NOTE: The XDG surface and role are created here so command-line applications
        //       with off-screen windows do not appear in for example the Unity dock
        createShellObjects(window);
    }
}

static void wayland_hide_window(window_st* window)
{
    if (window->wl.visible)
    {
        window->wl.visible = false;
        destroyShellObjects(window);

        wl_surface_attach(window->wl.surface, NULL, 0, 0);
        wl_surface_commit(window->wl.surface);

        flushDisplay();
    }
}

static void wayland_iconify_window(window_st* window)
{
    if (window->wl.libdecor.frame)
        libdecor_frame_set_minimized(window->wl.libdecor.frame);
    else if (window->wl.xdg.toplevel)
        xdg_toplevel_set_minimized(window->wl.xdg.toplevel);
}

static void wayland_restore_window(window_st* window)
{
    if (window->monitor)
    {
        // There is no way to unset minimized, or even to know if we are
        // minimized, so there is nothing to do in this case.
    }
    else
    {
        // We assume we are not minimized and act only on maximization

        if (window->wl.maximized)
        {
            if (window->wl.libdecor.frame)
                libdecor_frame_unset_maximized(window->wl.libdecor.frame);
            else if (window->wl.xdg.toplevel)
                xdg_toplevel_unset_maximized(window->wl.xdg.toplevel);
            else
                window->wl.maximized = false;
        }
    }
}

static void wayland_maximize_window(window_st* window)
{
    if (window->wl.libdecor.frame)
        libdecor_frame_set_maximized(window->wl.libdecor.frame);
    else if (window->wl.xdg.toplevel)
        xdg_toplevel_set_maximized(window->wl.xdg.toplevel);
    else
        window->wl.maximized = true;
}

static void wayland_request_window_attention(window_st* window)
{
    if (!g_wsi.wl.activationManager)
        return;

    // We're about to overwrite this with a new request
    if (window->wl.activationToken)
        xdg_activation_token_v1_destroy(window->wl.activationToken);

    window->wl.activationToken =
        xdg_activation_v1_get_activation_token(g_wsi.wl.activationManager);
    xdg_activation_token_v1_add_listener(window->wl.activationToken,
                                         &xdgActivationListener,
                                         window);

    xdg_activation_token_v1_commit(window->wl.activationToken);
}

static void wayland_focus_window(window_st* window)
{
    if (!g_wsi.wl.activationManager)
        return;

    if (window->wl.activationToken)
        xdg_activation_token_v1_destroy(window->wl.activationToken);

    window->wl.activationToken =
        xdg_activation_v1_get_activation_token(g_wsi.wl.activationManager);
    xdg_activation_token_v1_add_listener(window->wl.activationToken,
                                         &xdgActivationListener,
                                         window);

    xdg_activation_token_v1_set_serial(window->wl.activationToken,
                                       g_wsi.wl.serial,
                                       g_wsi.wl.seat);

    window_st* requester = g_wsi.wl.keyboardFocus;
    if (requester)
    {
        xdg_activation_token_v1_set_surface(window->wl.activationToken,
                                            requester->wl.surface);

        if (requester->wl.appId)
        {
            xdg_activation_token_v1_set_app_id(window->wl.activationToken,
                                               requester->wl.appId);
        }
    }

    xdg_activation_token_v1_commit(window->wl.activationToken);
}

static void wayland_set_window_monitor(window_st* window,
                                  monitor_st* monitor,
                                  int xpos, int ypos,
                                  int width, int height,
                                  int refreshRate)
{
    if (window->monitor == monitor)
    {
        if (!monitor)
            wayland_set_window_size(window, width, height);

        return;
    }

    if (window->monitor)
        releaseMonitor(window);

    impl_on_win_monitor(window, monitor);

    if (window->monitor)
        acquireMonitor(window);
    else
        wayland_set_window_size(window, width, height);
}


static bool wayland_window_focused(window_st* window)
{
    return g_wsi.wl.keyboardFocus == window;
}

static bool wayland_window_iconified(window_st* window)
{
    // xdg-shell doesn’t give any way to request whether a surface is
    // iconified.
    return false;
}

static bool wayland_window_visible(window_st* window)
{
    return window->wl.visible;
}

static bool wayland_window_maximized(window_st* window)
{
    return window->wl.maximized;
}

static bool wayland_window_hovered(window_st* window)
{
    return window->wl.surface == g_wsi.wl.pointerSurface;
}

static void wayland_set_window_resizable(window_st* window, bool enabled)
{
    if (window->wl.libdecor.frame)
    {
        if (enabled)
        {
            libdecor_frame_set_capabilities(window->wl.libdecor.frame,
                                            LIBDECOR_ACTION_RESIZE);
        }
        else
        {
            libdecor_frame_unset_capabilities(window->wl.libdecor.frame,
                                              LIBDECOR_ACTION_RESIZE);
        }
    }
    else if (window->wl.xdg.toplevel)
        updateXdgSizeLimits(window);
}


static void wayland_set_mouse_raw_motion(window_st* window, bool enabled)
{
    // This is handled in relativePointerHandleRelativeMotion
}

static bool wayland_mouse_raw_motion_supported(void)
{
    return true;
}

static void wayland_get_cursor_pos(window_st* window, double* xpos, double* ypos)
{
    if (xpos)
        *xpos = window->wl.cursorPosX;
    if (ypos)
        *ypos = window->wl.cursorPosY;
}

static void wayland_set_cursor_pos(window_st* window, double x, double y)
{
    impl_on_error(SC_WSI_ERR_FEATURE_UNAVAILABLE,
                    "Wayland: The platform does not support setting the cursor position");
}

static void wayland_set_cursorMode(window_st* window, int mode)
{
    wayland_set_cursor(window, window->cursor);
}

static bool wayland_create_standard_cursor(cursor_st* cursor, int shape)
{
    const char* name = NULL;

    // Try the XDG names first
    switch (shape)
    {
        case SC_ARROW_CURSOR:
            name = "default";
            break;
        case SC_IBEAM_CURSOR:
            name = "text";
            break;
        case SC_CROSSHAIR_CURSOR:
            name = "crosshair";
            break;
        case SC_POINTING_HAND_CURSOR:
            name = "pointer";
            break;
        case SC_RESIZE_EW_CURSOR:
            name = "ew-resize";
            break;
        case SC_RESIZE_NS_CURSOR:
            name = "ns-resize";
            break;
        case SC_RESIZE_NWSE_CURSOR:
            name = "nwse-resize";
            break;
        case SC_RESIZE_NESW_CURSOR:
            name = "nesw-resize";
            break;
        case SC_RESIZE_ALL_CURSOR:
            name = "all-scroll";
            break;
        case SC_NOT_ALLOWED_CURSOR:
            name = "not-allowed";
            break;
    }

    cursor->wl.cursor = wl_cursor_theme_get_cursor(g_wsi.wl.cursorTheme, name);

    if (g_wsi.wl.cursorThemeHiDPI)
    {
        cursor->wl.cursorHiDPI =
            wl_cursor_theme_get_cursor(g_wsi.wl.cursorThemeHiDPI, name);
    }

    if (!cursor->wl.cursor)
    {
        // Fall back to the core X11 names
        switch (shape)
        {
            case SC_ARROW_CURSOR:
                name = "left_ptr";
                break;
            case SC_IBEAM_CURSOR:
                name = "xterm";
                break;
            case SC_CROSSHAIR_CURSOR:
                name = "crosshair";
                break;
            case SC_POINTING_HAND_CURSOR:
                name = "hand2";
                break;
            case SC_RESIZE_EW_CURSOR:
                name = "sb_h_double_arrow";
                break;
            case SC_RESIZE_NS_CURSOR:
                name = "sb_v_double_arrow";
                break;
            case SC_RESIZE_ALL_CURSOR:
                name = "fleur";
                break;
            default:
                impl_on_error(SC_WSI_ERR_CURSOR_UNAVAILABLE,
                                "Wayland: Standard cursor shape unavailable");
                return false;
        }

        cursor->wl.cursor = wl_cursor_theme_get_cursor(g_wsi.wl.cursorTheme, name);
        if (!cursor->wl.cursor)
        {
            impl_on_error(SC_WSI_ERR_CURSOR_UNAVAILABLE,
                            "Wayland: Failed to create standard cursor \"%s\"",
                            name);
            return false;
        }

        if (g_wsi.wl.cursorThemeHiDPI)
        {
            if (!cursor->wl.cursorHiDPI)
            {
                cursor->wl.cursorHiDPI =
                    wl_cursor_theme_get_cursor(g_wsi.wl.cursorThemeHiDPI, name);
            }
        }
    }

    return true;
}

static bool wayland_create_cursor(cursor_st* cursor,
                                  const sc_wsi_img* image,
                                  int xhot, int yhot)
{
    cursor->wl.buffer = createShmBuffer(image);
    if (!cursor->wl.buffer)
        return false;

    cursor->wl.width = image->width;
    cursor->wl.height = image->height;
    cursor->wl.xhot = xhot;
    cursor->wl.yhot = yhot;
    return true;
}

static void wayland_destroy_cursor(cursor_st* cursor)
{
    // If it's a standard cursor we don't need to do anything here
    if (cursor->wl.cursor)
        return;

    if (cursor->wl.buffer)
        wl_buffer_destroy(cursor->wl.buffer);
}


static int wayland_get_key_scancode(int key)
{
    return g_wsi.wl.scancodes[key];
}

static const char* wayland_get_scancode_name(int scancode)
{
    if (scancode < 0 || scancode > 255)
    {
        impl_on_error(SC_WSI_ERR_INVALID_VALUE,
                        "Wayland: Invalid scancode %i",
                        scancode);
        return NULL;
    }

    const int key = g_wsi.wl.keycodes[scancode];
    if (key == SC_KEY_UNKNOWN)
        return NULL;

    const xkb_keycode_t keycode = scancode + 8;
    const xkb_layout_index_t layout =
        xkb_state_key_get_layout(g_wsi.wl.xkb.state, keycode);
    if (layout == XKB_LAYOUT_INVALID)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to retrieve layout for key name");
        return NULL;
    }

    const xkb_keysym_t* keysyms = NULL;
    xkb_keymap_key_get_syms_by_level(g_wsi.wl.xkb.keymap,
                                     keycode,
                                     layout,
                                     0,
                                     &keysyms);
    if (keysyms == NULL)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to retrieve keysym for key name");
        return NULL;
    }

    // WORKAROUND: xkb_keysym_to_utf8() requires the third parameter (size of the output buffer)
    // to be at least 7 (6 bytes + a null terminator), because it was written when UTF-8
    // sequences could be up to 6 bytes long. The g_wsi.wl.keynames buffers are only 5 bytes
    // long, because UTF-8 sequences are now limited to 4 bytes and no codepoints were ever assigned
    // that needed more than that. To work around this, we first copy to a temporary buffer.
    //
    // See: https://github.com/xkbcommon/libxkbcommon/issues/418
    char temp_buffer[7];
    const int bytes_written = xkb_keysym_to_utf8(keysyms[0], temp_buffer, sizeof(temp_buffer));
    if (bytes_written <= 0 || bytes_written > 5)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to encode keysym as UTF-8");
        return NULL;
    }
    memcpy(g_wsi.wl.keynames[key], temp_buffer, bytes_written);

    return g_wsi.wl.keynames[key];
}

static void dataSourceHandleTarget(void* userData,
                                   struct wl_data_source* source,
                                   const char* mimeType)
{
    if (g_wsi.wl.selectionSource != source)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Unknown clipboard data source");
        return;
    }
}

static void dataSourceHandleSend(void* userData,
                                 struct wl_data_source* source,
                                 const char* mimeType,
                                 int fd)
{
    // Ignore it if this is an outdated or invalid request
    if (g_wsi.wl.selectionSource != source ||
        strcmp(mimeType, "text/plain;charset=utf-8") != 0)
    {
        close(fd);
        return;
    }

    char* string = g_wsi.wl.clipboardString;
    size_t length = strlen(string);

    while (length > 0)
    {
        const ssize_t result = write(fd, string, length);
        if (result == -1)
        {
            if (errno == EINTR)
                continue;

            impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                            "Wayland: Error while writing the clipboard: %s",
                            strerror(errno));
            break;
        }

        length -= result;
        string += result;
    }

    close(fd);
}

static void dataSourceHandleCancelled(void* userData,
                                      struct wl_data_source* source)
{
    wl_data_source_destroy(source);

    if (g_wsi.wl.selectionSource != source)
        return;

    g_wsi.wl.selectionSource = NULL;
}

static const struct wl_data_source_listener dataSourceListener =
{
    dataSourceHandleTarget,
    dataSourceHandleSend,
    dataSourceHandleCancelled,
};

static void wayland_set_clipboard_string(const char* string)
{
    if (g_wsi.wl.selectionSource)
    {
        wl_data_source_destroy(g_wsi.wl.selectionSource);
        g_wsi.wl.selectionSource = NULL;
    }

    char* copy = wsi_strdup(string);
    if (!copy)
    {
        impl_on_error(SC_WSI_ERR_OUT_OF_MEMORY, NULL);
        return;
    }

    wsi_free(g_wsi.wl.clipboardString);
    g_wsi.wl.clipboardString = copy;

    g_wsi.wl.selectionSource =
        wl_data_device_manager_create_data_source(g_wsi.wl.dataDeviceManager);
    if (!g_wsi.wl.selectionSource)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to create clipboard data source");
        return;
    }
    wl_data_source_add_listener(g_wsi.wl.selectionSource,
                                &dataSourceListener,
                                NULL);
    wl_data_source_offer(g_wsi.wl.selectionSource, "text/plain;charset=utf-8");
    wl_data_device_set_selection(g_wsi.wl.dataDevice,
                                 g_wsi.wl.selectionSource,
                                 g_wsi.wl.serial);
}

static const char* wayland_get_clipboard_string(void)
{
    if (!g_wsi.wl.selectionOffer)
    {
        impl_on_error(SC_WSI_ERR_FORMAT_UNAVAILABLE,
                        "Wayland: No clipboard data available");
        return NULL;
    }

    if (g_wsi.wl.selectionSource)
        return g_wsi.wl.clipboardString;

    wsi_free(g_wsi.wl.clipboardString);
    g_wsi.wl.clipboardString =
        readDataOfferAsString(g_wsi.wl.selectionOffer, "text/plain;charset=utf-8");
    return g_wsi.wl.clipboardString;
}

///////////////////////////////////////////////////////////////////////////////
// Window 
///////////////////////////////////////////////////////////////////////////////

static void fractionalScaleHandlePreferredScale(void* userData,
                                         struct wp_fractional_scale_v1* fractionalScale,
                                         uint32_t numerator)
{
    window_st* window = userData;

    window->wl.scalingNumerator = numerator;
    impl_on_win_content_scale(window, numerator / 120.f, numerator / 120.f);
    resizeFramebuffer(window);

    if (window->wl.visible)
        impl_on_win_damage(window);
}

static const struct wp_fractional_scale_v1_listener fractionalScaleListener =
{
    fractionalScaleHandlePreferredScale,
};

static void surfaceHandleEnter(void* userData,
                               struct wl_surface* surface,
                               struct wl_output* output)
{
    if (wl_proxy_get_tag((struct wl_proxy*) output) != &g_wsi.wl.tag)
        return;

    window_st* window = userData;
    monitor_st* monitor = wl_output_get_user_data(output);
    if (!window || !monitor)
        return;

    if (window->wl.outputScaleCount + 1 > window->wl.outputScaleSize)
    {
        window->wl.outputScaleSize++;
        window->wl.outputScales =
            wsi_realloc(window->wl.outputScales,
                          window->wl.outputScaleSize * sizeof(SC_scaleWayland));
    }

    window->wl.outputScaleCount++;
    window->wl.outputScales[window->wl.outputScaleCount - 1] =
        (SC_scaleWayland) { output, monitor->wl.scale };

    wayland_UpdateBufferScaleFromOutputs(window);
}

static void surfaceHandleLeave(void* userData,
                               struct wl_surface* surface,
                               struct wl_output* output)
{
    if (wl_proxy_get_tag((struct wl_proxy*) output) != &g_wsi.wl.tag)
        return;

    window_st* window = userData;

    for (size_t i = 0; i < window->wl.outputScaleCount; i++)
    {
        if (window->wl.outputScales[i].output == output)
        {
            window->wl.outputScales[i] =
                window->wl.outputScales[window->wl.outputScaleCount - 1];
            window->wl.outputScaleCount--;
            break;
        }
    }

    wayland_UpdateBufferScaleFromOutputs(window);
}

static const struct wl_surface_listener surfaceListener =
{
    surfaceHandleEnter,
    surfaceHandleLeave
};

static bool createNativeSurface(window_st* window,
                                    const wnd_config_st* wndconfig)
{
    window->wl.surface = wl_compositor_create_surface(g_wsi.wl.compositor);
    if (!window->wl.surface)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR, "Wayland: Failed to create window surface");
        return false;
    }

    wl_proxy_set_tag((struct wl_proxy*) window->wl.surface, &g_wsi.wl.tag);
    wl_surface_add_listener(window->wl.surface,
                            &surfaceListener,
                            window);

    window->wl.width = wndconfig->width;
    window->wl.height = wndconfig->height;
    window->wl.fbWidth = wndconfig->width;
    window->wl.fbHeight = wndconfig->height;
    window->wl.appId = wsi_strdup(wndconfig->wl.appId);

    window->wl.bufferScale = 1;
    window->wl.scalingNumerator = 120;
    window->wl.scaleFramebuffer = true;

    window->wl.maximized = wndconfig->maximized;

    if (!window->wl.transparent)
        setContentAreaOpaque(window);

    if (g_wsi.wl.fractionalScaleManager)
    {
        if (window->wl.scaleFramebuffer)
        {
            window->wl.scalingViewport =
                wp_viewporter_get_viewport(g_wsi.wl.viewporter, window->wl.surface);

            wp_viewport_set_destination(window->wl.scalingViewport,
                                        window->wl.width,
                                        window->wl.height);

            window->wl.fractionalScale =
                wp_fractional_scale_manager_v1_get_fractional_scale(
                    g_wsi.wl.fractionalScaleManager,
                    window->wl.surface);

            wp_fractional_scale_v1_add_listener(window->wl.fractionalScale,
                                                &fractionalScaleListener,
                                                window);
        }
    }

    return true;
}

static bool wayland_create_window(window_st* window,
                                  const wnd_config_st* wndconfig)
{
    if (!createNativeSurface(window, wndconfig))
        return false;

    if (wndconfig->mousePassthrough)
        wayland_set_window_mouse_passthrough(window, true);

    if (window->monitor || wndconfig->visible)
    {
        if (!createShellObjects(window))
            return false;
    }

    return true;
}

static void wayland_destroy_window(window_st* window)
{
    if (window->wl.surface == g_wsi.wl.pointerSurface)
        g_wsi.wl.pointerSurface = NULL;

    if (window == g_wsi.wl.keyboardFocus)
    {
        struct itimerspec timer = {0};
        timerfd_settime(g_wsi.wl.keyRepeatTimerfd, 0, &timer, NULL);

        g_wsi.wl.keyboardFocus = NULL;
    }

    if (window->wl.fractionalScale)
        wp_fractional_scale_v1_destroy(window->wl.fractionalScale);

    if (window->wl.scalingViewport)
        wp_viewport_destroy(window->wl.scalingViewport);

    if (window->wl.activationToken)
        xdg_activation_token_v1_destroy(window->wl.activationToken);

    if (window->wl.idleInhibitor)
        zwp_idle_inhibitor_v1_destroy(window->wl.idleInhibitor);

    if (window->wl.relativePointer)
        zwp_relative_pointer_v1_destroy(window->wl.relativePointer);

    if (window->wl.lockedPointer)
        zwp_locked_pointer_v1_destroy(window->wl.lockedPointer);

    if (window->wl.confinedPointer)
        zwp_confined_pointer_v1_destroy(window->wl.confinedPointer);

    destroyShellObjects(window);

    if (window->wl.surface)
        wl_surface_destroy(window->wl.surface);

    wsi_free(window->wl.appId);
    wsi_free(window->wl.outputScales);
}

///////////////////////////////////////////////////////////////////////////////
// 接口集成
///////////////////////////////////////////////////////////////////////////////

WSI_API struct wl_output* wsi_get_wayland_monitor(sc_monitor* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }

    if (g_wsi.platform.platformID != SC_PLATFORM_WAYLAND)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_UNAVAILABLE, "Wayland: Platform not initialized");
        return NULL;
    }

    monitor_st* monitor = (monitor_st*) handle;
    assert(monitor != NULL);

    return monitor->wl.output;
}

WSI_API struct wl_display* wsi_get_wayland_display(void)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }

    if (g_wsi.platform.platformID != SC_PLATFORM_WAYLAND)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_UNAVAILABLE,
                        "Wayland: Platform not initialized");
        return NULL;
    }

    return g_wsi.wl.display;
}

WSI_API struct wl_surface* wsi_get_wayland_window(sc_window* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }

    if (g_wsi.platform.platformID != SC_PLATFORM_WAYLAND)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_UNAVAILABLE,
                        "Wayland: Platform not initialized");
        return NULL;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    return window->wl.surface;
}

bool wayland_connect(int platformID, platform_st* platform)
{
    const platform_st wayland =
    {
        .platformID = SC_PLATFORM_WAYLAND,
        .init = wayland_init,
        .terminate = wayland_terminate,

        .pollEvents = wayland_poll_events,
        .waitEvents = wayland_wait_events,
        .waitEventsTimeout = wayland_wait_eventsTimeout,
        .postEmptyEvent = wayland_post_empty_event,

        .createWindow = wayland_create_window,
        .destroyWindow = wayland_destroy_window,
        .setWindowTitle = wayland_set_window_title,
        .setWindowIcon = wayland_set_window_icon,
        .setWindowMonitor = wayland_set_window_monitor,
        .setWindowMousePassthrough = wayland_set_window_mouse_passthrough,

        .setWindowDecorated = wayland_set_window_decorated,
        .setWindowResizable = wayland_set_window_resizable,
        .setWindowFloating = wayland_set_window_floating,
        .setWindowOpacity = wayland_set_window_opacity,
        .getWindowOpacity = wayland_get_window_opacity,

        .getWindowPos = wayland_get_window_pos,
        .setWindowPos = wayland_set_window_pos,
        .getWindowSize = wayland_get_window_size,
        .getFramebufferSize = wayland_get_framebuffer_size,
        .setWindowSize = wayland_set_window_size,
        .getWindowFrameSize = wayland_get_window_frame_size,
        .setWindowSizeLimits = wayland_set_window_size_limits,
        .getWindowContentScale = wayland_get_window_content_scale,
        .setWindowAspectRatio = wayland_set_window_aspect_ratio,

        .showWindow = wayland_show_window,
        .hideWindow = wayland_hide_window,
        .maximizeWindow = wayland_maximize_window,
        .restoreWindow = wayland_restore_window,
        .focusWindow = wayland_focus_window,
        .iconifyWindow = wayland_iconify_window,
        .requestWindowAttention = wayland_request_window_attention,

        .windowVisible = wayland_window_visible,
        .windowMaximized = wayland_window_maximized,
        .windowFocused = wayland_window_focused,
        .windowHovered = wayland_window_hovered,
        .windowIconified = wayland_window_iconified,

        .setCursor = wayland_set_cursor,
        .createStandardCursor = wayland_create_standard_cursor,
        .createCursor = wayland_create_cursor,
        .destroyCursor = wayland_destroy_cursor,
        .setCursorMode = wayland_set_cursorMode,
        .setCursorPos = wayland_set_cursor_pos,
        .getCursorPos = wayland_get_cursor_pos,
        .setRawMouseMotion = wayland_set_mouse_raw_motion,
        .rawMouseMotionSupported = wayland_mouse_raw_motion_supported,

        .getKeyScancode = wayland_get_key_scancode,
        .getScancodeName = wayland_get_scancode_name,
        .getClipboardString = wayland_get_clipboard_string,
        .setClipboardString = wayland_set_clipboard_string,

        .freeMonitor = wsi_free_monitorWayland,
        .getMonitorPos = wayland_get_monitor_pos,
        .getMonitorWorkarea = wayland_get_monitor_work_area,
        .getMonitorContentScale = wayland_get_monitor_content_scale,
        .getVideoModes = wayland_get_video_modes,
        .getVideoMode = wayland_get_video_mode,
        .getGammaRamp = wayland_get_gamma_ramp,
        .setGammaRamp = wayland_set_gamma_ramp,
    };

    void* module = P_dl_load("libwayland-client.so.0");
    if (!module)
    {
        if (platformID == SC_PLATFORM_WAYLAND)
        {
            impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                            "Wayland: Failed to load libwayland-client");
        }

        return false;
    }

    PFN_wl_display_connect wl_display_connect = (PFN_wl_display_connect)
        P_dl_get_proc(module, "wl_display_connect");
    if (!wl_display_connect)
    {
        if (platformID == SC_PLATFORM_WAYLAND)
        {
            impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                            "Wayland: Failed to load libwayland-client entry point");
        }

        P_dl_unload(module);
        return false;
    }

    struct wl_display* display = wl_display_connect(NULL);
    if (!display)
    {
        if (platformID == SC_PLATFORM_WAYLAND)
            impl_on_error(SC_WSI_ERR_PLATFORM_ERROR, "Wayland: Failed to connect to display");

        P_dl_unload(module);
        return false;
    }

    g_wsi.wl.display = display;
    g_wsi.wl.client.handle = module;

    *platform = wayland;
    return true;
}

///////////////////////////////////////////////////////////////////////////////

#endif // WSI_WAYLAND

