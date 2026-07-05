
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
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <poll.h>
#include <linux/input-event-codes.h>

#include "wayland-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"
#include "pointer-constraints-unstable-v1-client-protocol.h"
#include "xdg-activation-v1-client-protocol.h"
#include "idle-inhibit-unstable-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"

#define GLFW_BORDER_SIZE    4
#define GLFW_CAPTION_HEIGHT 24

#define GLFW_PENDING_SURFACE    1
#define GLFW_PENDING_BUTTON     2
#define GLFW_PENDING_MOTION     4
#define GLFW_PENDING_SCROLL     8
#define GLFW_PENDING_DISCRETE   16

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
    static const char template[] = "/glfw-shared-XXXXXX";
    const char* path;
    char* name;
    int fd;
    int ret;

#ifdef HAVE_MEMFD_CREATE
    fd = memfd_create("glfw-shared", MFD_CLOEXEC | MFD_ALLOW_SEALING);
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

static struct wl_buffer* createShmBuffer(const GLFWimage* image)
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

static void callbackHandleDone(void* userData, struct wl_callback* callback, uint32_t data)
{
    wl_callback_destroy(callback);
}

static const struct wl_callback_listener noopCallbackListener =
{
    callbackHandleDone
};

static void createFallbackEdge(window_st* window,
                               _GLFWfallbackEdgeWayland* edge,
                               struct wl_surface* parent,
                               struct wl_buffer* buffer,
                               int x, int y,
                               int width, int height)
{
    edge->surface = wl_compositor_create_surface(g_wsi.wl.compositor);
    wl_surface_set_user_data(edge->surface, window);
    wl_proxy_set_tag((struct wl_proxy*) edge->surface, &g_wsi.wl.tag);
    edge->subsurface = wl_subcompositor_get_subsurface(g_wsi.wl.subcompositor,
                                                       edge->surface, parent);
    wl_subsurface_set_position(edge->subsurface, x, y);
    edge->viewport = wp_viewporter_get_viewport(g_wsi.wl.viewporter,
                                                edge->surface);
    wp_viewport_set_destination(edge->viewport, width, height);
    wl_surface_attach(edge->surface, buffer, 0, 0);

    struct wl_region* region = wl_compositor_create_region(g_wsi.wl.compositor);
    wl_region_add(region, 0, 0, width, height);
    wl_surface_set_opaque_region(edge->surface, region);
    wl_surface_commit(edge->surface);
    wl_region_destroy(region);
}

static void createFallbackDecorations(window_st* window)
{
    unsigned char data[] = { 224, 224, 224, 255 };
    const GLFWimage image = { 1, 1, data };

    if (!g_wsi.wl.viewporter)
        return;

    if (!window->wl.fallback.buffer)
        window->wl.fallback.buffer = createShmBuffer(&image);
    if (!window->wl.fallback.buffer)
        return;

    createFallbackEdge(window, &window->wl.fallback.top, window->wl.surface,
                       window->wl.fallback.buffer,
                       0, -GLFW_CAPTION_HEIGHT,
                       window->wl.width, GLFW_CAPTION_HEIGHT);
    createFallbackEdge(window, &window->wl.fallback.left, window->wl.surface,
                       window->wl.fallback.buffer,
                       -GLFW_BORDER_SIZE, -GLFW_CAPTION_HEIGHT,
                       GLFW_BORDER_SIZE, window->wl.height + GLFW_CAPTION_HEIGHT);
    createFallbackEdge(window, &window->wl.fallback.right, window->wl.surface,
                       window->wl.fallback.buffer,
                       window->wl.width, -GLFW_CAPTION_HEIGHT,
                       GLFW_BORDER_SIZE, window->wl.height + GLFW_CAPTION_HEIGHT);
    createFallbackEdge(window, &window->wl.fallback.bottom, window->wl.surface,
                       window->wl.fallback.buffer,
                       -GLFW_BORDER_SIZE, window->wl.height,
                       window->wl.width + GLFW_BORDER_SIZE * 2, GLFW_BORDER_SIZE);

    window->wl.fallback.decorations = true;
}

static void destroyFallbackEdge(_GLFWfallbackEdgeWayland* edge)
{
    if (edge->surface == g_wsi.wl.pointerSurface)
        g_wsi.wl.pointerSurface = NULL;

    if (edge->subsurface)
        wl_subsurface_destroy(edge->subsurface);
    if (edge->surface)
        wl_surface_destroy(edge->surface);
    if (edge->viewport)
        wp_viewport_destroy(edge->viewport);

    edge->surface = NULL;
    edge->subsurface = NULL;
    edge->viewport = NULL;
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
            if (ypos < GLFW_BORDER_SIZE)
                cursorName = "n-resize";
        }
        else if (g_wsi.wl.pointerSurface == window->wl.fallback.left.surface)
        {
            if (ypos < GLFW_BORDER_SIZE)
                cursorName = "nw-resize";
            else
                cursorName = "w-resize";
        }
        else if (g_wsi.wl.pointerSurface == window->wl.fallback.right.surface)
        {
            if (ypos < GLFW_BORDER_SIZE)
                cursorName = "ne-resize";
            else
                cursorName = "e-resize";
        }
        else if (g_wsi.wl.pointerSurface == window->wl.fallback.bottom.surface)
        {
            if (xpos < GLFW_BORDER_SIZE)
                cursorName = "sw-resize";
            else if (xpos > window->wl.width + GLFW_BORDER_SIZE)
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
            if (ypos < GLFW_BORDER_SIZE)
                edges = XDG_TOPLEVEL_RESIZE_EDGE_TOP;
            else
                xdg_toplevel_move(window->wl.xdg.toplevel, g_wsi.wl.seat, serial);
        }
        else if (g_wsi.wl.pointerSurface == window->wl.fallback.left.surface)
        {
            if (ypos < GLFW_BORDER_SIZE)
                edges = XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
            else
                edges = XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
        }
        else if (g_wsi.wl.pointerSurface == window->wl.fallback.right.surface)
        {
            if (ypos < GLFW_BORDER_SIZE)
                edges = XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
            else
                edges = XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
        }
        else if (g_wsi.wl.pointerSurface == window->wl.fallback.bottom.surface)
        {
            if (xpos < GLFW_BORDER_SIZE)
                edges = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
            else if (xpos > window->wl.width + GLFW_BORDER_SIZE)
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

        if (ypos < GLFW_BORDER_SIZE)
            return;

        xdg_toplevel_show_window_menu(window->wl.xdg.toplevel, g_wsi.wl.seat, serial,
                                      xpos, ypos - GLFW_CAPTION_HEIGHT - GLFW_BORDER_SIZE);
    }
}

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

    if (window->wl.egl.window)
    {
        wl_egl_window_resize(window->wl.egl.window,
                             window->wl.fbWidth,
                             window->wl.fbHeight,
                             0, 0);
    }

    if (!window->wl.transparent)
        setContentAreaOpaque(window);

    _glfwInputFramebufferSize(window, window->wl.fbWidth, window->wl.fbHeight);
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
        wp_viewport_set_destination(window->wl.fallback.top.viewport,
                                    window->wl.width,
                                    GLFW_CAPTION_HEIGHT);
        wl_surface_commit(window->wl.fallback.top.surface);

        wp_viewport_set_destination(window->wl.fallback.left.viewport,
                                    GLFW_BORDER_SIZE,
                                    window->wl.height + GLFW_CAPTION_HEIGHT);
        wl_surface_commit(window->wl.fallback.left.surface);

        wl_subsurface_set_position(window->wl.fallback.right.subsurface,
                                window->wl.width, -GLFW_CAPTION_HEIGHT);
        wp_viewport_set_destination(window->wl.fallback.right.viewport,
                                    GLFW_BORDER_SIZE,
                                    window->wl.height + GLFW_CAPTION_HEIGHT);
        wl_surface_commit(window->wl.fallback.right.surface);

        wl_subsurface_set_position(window->wl.fallback.bottom.subsurface,
                                -GLFW_BORDER_SIZE, window->wl.height);
        wp_viewport_set_destination(window->wl.fallback.bottom.viewport,
                                    window->wl.width + GLFW_BORDER_SIZE * 2,
                                    GLFW_BORDER_SIZE);
        wl_surface_commit(window->wl.fallback.bottom.surface);
    }

    return true;
}

void _glfwUpdateBufferScaleFromOutputsWayland(window_st* window)
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
                          window->wl.outputScaleSize * sizeof(_GLFWscaleWayland));
    }

    window->wl.outputScaleCount++;
    window->wl.outputScales[window->wl.outputScaleCount - 1] =
        (_GLFWscaleWayland) { output, monitor->wl.scale };

    _glfwUpdateBufferScaleFromOutputsWayland(window);
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

    _glfwUpdateBufferScaleFromOutputsWayland(window);
}

static const struct wl_surface_listener surfaceListener =
{
    surfaceHandleEnter,
    surfaceHandleLeave
};

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

// Make the specified window and its video mode active on its monitor
//
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
//
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

void fractionalScaleHandlePreferredScale(void* userData,
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

const struct wp_fractional_scale_v1_listener fractionalScaleListener =
{
    fractionalScaleHandlePreferredScale,
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
            window->wl.pending.width  = wsi_max(0, width - GLFW_BORDER_SIZE * 2);
            window->wl.pending.height =
                wsi_max(0, height - GLFW_BORDER_SIZE - GLFW_CAPTION_HEIGHT);
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

void libdecorFrameHandleConfigure(struct libdecor_frame* frame,
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

void libdecorFrameHandleClose(struct libdecor_frame* frame, void* userData)
{
    window_st* window = userData;
    impl_on_win_close_req(window);
}

void libdecorFrameHandleCommit(struct libdecor_frame* frame, void* userData)
{
    window_st* window = userData;
    wl_surface_commit(window->wl.surface);
}

void libdecorFrameHandleDismissPopup(struct libdecor_frame* frame,
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
        _glfwWaitEventsWayland();

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
                minwidth  += GLFW_BORDER_SIZE * 2;
                minheight += GLFW_CAPTION_HEIGHT + GLFW_BORDER_SIZE;
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
                maxwidth  += GLFW_BORDER_SIZE * 2;
                maxheight += GLFW_CAPTION_HEIGHT + GLFW_BORDER_SIZE;
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

static bool createNativeSurface(window_st* window,
                                    const wnd_config_st* wndconfig,
                                    const _GLFWfbconfig* fbconfig)
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
    window->wl.scaleFramebuffer = wndconfig->scaleFramebuffer;

    window->wl.maximized = wndconfig->maximized;

    window->wl.transparent = fbconfig->transparent;
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

static void setCursorImage(window_st* window,
                           _sc_cursorWayland* cursorWayland)
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

        if (!_glfwPollPOSIX(fds, sizeof(fds) / sizeof(fds[0]), timeout))
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

// Reads the specified data offer as the specified MIME type
//
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

static void processPointerEnterSurface(struct wl_surface* surface)
{
    g_wsi.wl.pointerSurface = surface;

    window_st* window = wl_surface_get_user_data(g_wsi.wl.pointerSurface);
    if (window->wl.surface == g_wsi.wl.pointerSurface)
    {
        _glfwSetCursorWayland(window, window->cursor);
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
        g_wsi.wl.pending.events |= (GLFW_PENDING_SURFACE | GLFW_PENDING_MOTION);
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
        g_wsi.wl.pending.events |= GLFW_PENDING_SURFACE;
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
        g_wsi.wl.pending.events |= GLFW_PENDING_MOTION;
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
        g_wsi.wl.pending.events |= GLFW_PENDING_BUTTON;
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
        g_wsi.wl.pending.events |= GLFW_PENDING_SCROLL;
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
    if (g_wsi.wl.pending.events & GLFW_PENDING_SURFACE)
    {
        if (g_wsi.wl.pointerSurface)
            processPointerLeaveSurface(g_wsi.wl.pointerSurface);

        if (g_wsi.wl.pending.pointerSurface)
            processPointerEnterSurface(g_wsi.wl.pending.pointerSurface);
    }

    if (!g_wsi.wl.pointerSurface)
        return;

    if (g_wsi.wl.pending.events & GLFW_PENDING_MOTION)
        processPointerMotion(g_wsi.wl.pending.pointerX, g_wsi.wl.pending.pointerY);

    if (g_wsi.wl.pending.events & GLFW_PENDING_BUTTON)
        processPointerButton(g_wsi.wl.pending.button, g_wsi.wl.pending.action);

    if (g_wsi.wl.pending.events & GLFW_PENDING_DISCRETE)
        processPointerScroll(g_wsi.wl.pending.discreteX, g_wsi.wl.pending.discreteY);
    else if (g_wsi.wl.pending.events & GLFW_PENDING_SCROLL)
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

    g_wsi.wl.pending.events |= GLFW_PENDING_DISCRETE;
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
    _GLFWofferWayland* offers =
        wsi_realloc(g_wsi.wl.offers,
                      sizeof(_GLFWofferWayland) * (g_wsi.wl.offerCount + 1));
    if (!offers)
    {
        impl_on_error(SC_WSI_ERR_OUT_OF_MEMORY, NULL);
        return;
    }

    g_wsi.wl.offers = offers;
    g_wsi.wl.offerCount++;

    g_wsi.wl.offers[g_wsi.wl.offerCount - 1] = (_GLFWofferWayland) { offer };
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

const struct wl_data_device_listener dataDeviceListener =
{
    dataDeviceHandleDataOffer,
    dataDeviceHandleEnter,
    dataDeviceHandleLeave,
    dataDeviceHandleMotion,
    dataDeviceHandleDrop,
    dataDeviceHandleSelection,
};

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

static void callbackHandleFrame(void* userData, struct wl_callback* callback, uint32_t data)
{
    window_st* window = userData;
    wl_callback_destroy(callback);
    window->wl.egl.callback = NULL;
}

static const struct wl_callback_listener frameCallbackListener =
{
    callbackHandleFrame
};

void _glfwAddSeatListenerWayland(struct wl_seat* seat)
{
    wl_seat_add_listener(seat, &seatListener, NULL);
}

void _glfwAddDataDeviceListenerWayland(struct wl_data_device* device)
{
    wl_data_device_add_listener(device, &dataDeviceListener, NULL);
}

bool _glfwWaitForEGLFrameWayland(window_st* window)
{
    double timeout = 0.02;

    while (window->wl.egl.callback)
    {
        if (wl_display_prepare_read_queue(g_wsi.wl.display, window->wl.egl.queue) != 0)
        {
            wl_display_dispatch_queue_pending(g_wsi.wl.display, window->wl.egl.queue);
            continue;
        }

        if (!flushDisplay())
        {
            wl_display_cancel_read(g_wsi.wl.display);
            return false;
        }

        struct pollfd fd = { wl_display_get_fd(g_wsi.wl.display), POLLIN };

        if (!_glfwPollPOSIX(&fd, 1, &timeout))
        {
            wl_display_cancel_read(g_wsi.wl.display);
            return false;
        }

        wl_display_read_events(g_wsi.wl.display);
        wl_display_dispatch_queue_pending(g_wsi.wl.display, window->wl.egl.queue);
    }

    window->wl.egl.callback = wl_surface_frame(window->wl.egl.wrapper);
    wl_callback_add_listener(window->wl.egl.callback, &frameCallbackListener, window);

    // If the window is hidden when the wait is over then don't swap
    return window->wl.visible;
}

//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

bool _glfwCreateWindowWayland(window_st* window,
                                  const wnd_config_st* wndconfig,
                                  const _GLFWctxconfig* ctxconfig,
                                  const _GLFWfbconfig* fbconfig)
{
    if (!createNativeSurface(window, wndconfig, fbconfig))
        return false;

    if (ctxconfig->client != GLFW_NO_API)
    {
        if (ctxconfig->source == GLFW_EGL_CONTEXT_API ||
            ctxconfig->source == WSI_NATIVE_CONTEXT_API)
        {
            window->wl.egl.window = wl_egl_window_create(window->wl.surface,
                                                         window->wl.fbWidth,
                                                         window->wl.fbHeight);
            if (!window->wl.egl.window)
            {
                impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                                "Wayland: Failed to create EGL window");
                return false;
            }

            window->wl.egl.queue = wl_display_create_queue(g_wsi.wl.display);
            if (!window->wl.egl.queue)
            {
                impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                                "Wayland: Failed to create EGL frame queue");
                return false;
            }

            window->wl.egl.wrapper = wl_proxy_create_wrapper(window->wl.surface);
            if (!window->wl.egl.wrapper)
            {
                impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                                "Wayland: Failed to create surface wrapper");
                return false;
            }

            wl_proxy_set_queue((struct wl_proxy*) window->wl.egl.wrapper,
                               window->wl.egl.queue);

            window->wl.egl.interval = 1;

            if (!_glfwInitEGL())
                return false;
            if (!_glfwCreateContextEGL(window, ctxconfig, fbconfig))
                return false;
        }
        else if (ctxconfig->source == GLFW_OSMESA_CONTEXT_API)
        {
            if (!_glfwInitOSMesa())
                return false;
            if (!_glfwCreateContextOSMesa(window, ctxconfig, fbconfig))
                return false;
        }

        if (!_glfwRefreshContextAttribs(window, ctxconfig))
            return false;
    }

    if (wndconfig->mousePassthrough)
        _glfwSetWindowMousePassthroughWayland(window, true);

    if (window->monitor || wndconfig->visible)
    {
        if (!createShellObjects(window))
            return false;
    }

    return true;
}

void _glfwDestroyWindowWayland(window_st* window)
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

    if (window->context.destroy)
        window->context.destroy(window);

    destroyShellObjects(window);

    if (window->wl.fallback.buffer)
        wl_buffer_destroy(window->wl.fallback.buffer);

    if (window->wl.egl.callback)
        wl_callback_destroy(window->wl.egl.callback);

    if (window->wl.egl.wrapper)
        wl_proxy_wrapper_destroy(window->wl.egl.wrapper);

    if (window->wl.egl.queue)
        wl_event_queue_destroy(window->wl.egl.queue);

    if (window->wl.egl.window)
        wl_egl_window_destroy(window->wl.egl.window);

    if (window->wl.surface)
        wl_surface_destroy(window->wl.surface);

    wsi_free(window->wl.appId);
    wsi_free(window->wl.outputScales);
}

void _glfwSetWindowTitleWayland(window_st* window, const char* title)
{
    if (window->wl.libdecor.frame)
        libdecor_frame_set_title(window->wl.libdecor.frame, title);
    else if (window->wl.xdg.toplevel)
        xdg_toplevel_set_title(window->wl.xdg.toplevel, title);
}

void _glfwSetWindowIconWayland(window_st* window,
                               int count, const GLFWimage* images)
{
    impl_on_error(SC_WSI_ERR_FEATURE_UNAVAILABLE,
                    "Wayland: The platform does not support setting the window icon");
}

void _glfwGetWindowPosWayland(window_st* window, int* xpos, int* ypos)
{
    // A Wayland client is not aware of its position, so just warn and leave it
    // as (0, 0)

    impl_on_error(SC_WSI_ERR_FEATURE_UNAVAILABLE,
                    "Wayland: The platform does not provide the window position");
}

void _glfwSetWindowPosWayland(window_st* window, int xpos, int ypos)
{
    // A Wayland client can not set its position, so just warn

    impl_on_error(SC_WSI_ERR_FEATURE_UNAVAILABLE,
                    "Wayland: The platform does not support setting the window position");
}

void _glfwGetWindowSizeWayland(window_st* window, int* width, int* height)
{
    if (width)
        *width = window->wl.width;
    if (height)
        *height = window->wl.height;
}

void _glfwSetWindowSizeWayland(window_st* window, int width, int height)
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

void _glfwSetWindowSizeLimitsWayland(window_st* window,
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

void _glfwSetWindowAspectRatioWayland(window_st* window, int numer, int denom)
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

void _glfwGetFramebufferSizeWayland(window_st* window, int* width, int* height)
{
    if (width)
        *width = window->wl.fbWidth;
    if (height)
        *height = window->wl.fbHeight;
}

void _glfwGetWindowFrameSizeWayland(window_st* window,
                                    int* left, int* top,
                                    int* right, int* bottom)
{
    if (window->wl.fallback.decorations)
    {
        if (top)
            *top = GLFW_CAPTION_HEIGHT;
        if (left)
            *left = GLFW_BORDER_SIZE;
        if (right)
            *right = GLFW_BORDER_SIZE;
        if (bottom)
            *bottom = GLFW_BORDER_SIZE;
    }
}

void _glfwGetWindowContentScaleWayland(window_st* window,
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

void _glfwIconifyWindowWayland(window_st* window)
{
    if (window->wl.libdecor.frame)
        libdecor_frame_set_minimized(window->wl.libdecor.frame);
    else if (window->wl.xdg.toplevel)
        xdg_toplevel_set_minimized(window->wl.xdg.toplevel);
}

void _glfwRestoreWindowWayland(window_st* window)
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

void _glfwMaximizeWindowWayland(window_st* window)
{
    if (window->wl.libdecor.frame)
        libdecor_frame_set_maximized(window->wl.libdecor.frame);
    else if (window->wl.xdg.toplevel)
        xdg_toplevel_set_maximized(window->wl.xdg.toplevel);
    else
        window->wl.maximized = true;
}

void _glfwShowWindowWayland(window_st* window)
{
    if (!window->wl.libdecor.frame && !window->wl.xdg.toplevel)
    {
        // NOTE: The XDG surface and role are created here so command-line applications
        //       with off-screen windows do not appear in for example the Unity dock
        createShellObjects(window);
    }
}

void _glfwHideWindowWayland(window_st* window)
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

void _glfwRequestWindowAttentionWayland(window_st* window)
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

void _glfwFocusWindowWayland(window_st* window)
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

void _glfwSetWindowMonitorWayland(window_st* window,
                                  monitor_st* monitor,
                                  int xpos, int ypos,
                                  int width, int height,
                                  int refreshRate)
{
    if (window->monitor == monitor)
    {
        if (!monitor)
            _glfwSetWindowSizeWayland(window, width, height);

        return;
    }

    if (window->monitor)
        releaseMonitor(window);

    impl_on_win_monitor(window, monitor);

    if (window->monitor)
        acquireMonitor(window);
    else
        _glfwSetWindowSizeWayland(window, width, height);
}

bool _glfwWindowFocusedWayland(window_st* window)
{
    return g_wsi.wl.keyboardFocus == window;
}

bool _glfwWindowIconifiedWayland(window_st* window)
{
    // xdg-shell doesn’t give any way to request whether a surface is
    // iconified.
    return false;
}

bool _glfwWindowVisibleWayland(window_st* window)
{
    return window->wl.visible;
}

bool _glfwWindowMaximizedWayland(window_st* window)
{
    return window->wl.maximized;
}

bool _glfwWindowHoveredWayland(window_st* window)
{
    return window->wl.surface == g_wsi.wl.pointerSurface;
}

bool _glfwFramebufferTransparentWayland(window_st* window)
{
    return window->wl.transparent;
}

void _glfwSetWindowResizableWayland(window_st* window, bool enabled)
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

void _glfwSetWindowDecoratedWayland(window_st* window, bool enabled)
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

void _glfwSetWindowFloatingWayland(window_st* window, bool enabled)
{
    impl_on_error(SC_WSI_ERR_FEATURE_UNAVAILABLE,
                    "Wayland: Platform does not support making a window floating");
}

void _glfwSetWindowMousePassthroughWayland(window_st* window, bool enabled)
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

float _glfwGetWindowOpacityWayland(window_st* window)
{
    return 1.f;
}

void _glfwSetWindowOpacityWayland(window_st* window, float opacity)
{
    impl_on_error(SC_WSI_ERR_FEATURE_UNAVAILABLE,
                    "Wayland: The platform does not support setting the window opacity");
}

void _glfwSetRawMouseMotionWayland(window_st* window, bool enabled)
{
    // This is handled in relativePointerHandleRelativeMotion
}

bool _glfwRawMouseMotionSupportedWayland(void)
{
    return true;
}

void _glfwPollEventsWayland(void)
{
    double timeout = 0.0;
    handleEvents(&timeout);
}

void _glfwWaitEventsWayland(void)
{
    handleEvents(NULL);
}

void _glfwWaitEventsTimeoutWayland(double timeout)
{
    handleEvents(&timeout);
}

void _glfwPostEmptyEventWayland(void)
{
    struct wl_callback* callback = wl_display_sync(g_wsi.wl.display);
    wl_callback_add_listener(callback, &noopCallbackListener, NULL);

    flushDisplay();
}

void _glfwGetCursorPosWayland(window_st* window, double* xpos, double* ypos)
{
    if (xpos)
        *xpos = window->wl.cursorPosX;
    if (ypos)
        *ypos = window->wl.cursorPosY;
}

void _glfwSetCursorPosWayland(window_st* window, double x, double y)
{
    impl_on_error(SC_WSI_ERR_FEATURE_UNAVAILABLE,
                    "Wayland: The platform does not support setting the cursor position");
}

void _glfwSetCursorModeWayland(window_st* window, int mode)
{
    _glfwSetCursorWayland(window, window->cursor);
}

const char* _glfwGetScancodeNameWayland(int scancode)
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

int _glfwGetKeyScancodeWayland(int key)
{
    return g_wsi.wl.scancodes[key];
}

bool _glfwCreateCursorWayland(cursor_st* cursor,
                                  const GLFWimage* image,
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

bool _glfwCreateStandardCursorWayland(cursor_st* cursor, int shape)
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

void _glfwDestroyCursorWayland(cursor_st* cursor)
{
    // If it's a standard cursor we don't need to do anything here
    if (cursor->wl.cursor)
        return;

    if (cursor->wl.buffer)
        wl_buffer_destroy(cursor->wl.buffer);
}

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

void _glfwSetCursorWayland(window_st* window, cursor_st* cursor)
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

            _sc_cursorWayland cursorWayland =
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

void _glfwSetClipboardStringWayland(const char* string)
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

const char* _glfwGetClipboardStringWayland(void)
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

EGLenum _glfwGetEGLPlatformWayland(EGLint** attribs)
{
    if (g_wsi.egl.EXT_platform_base && g_wsi.egl.EXT_platform_wayland)
        return EGL_PLATFORM_WAYLAND_EXT;
    else
        return 0;
}

EGLNativeDisplayType _glfwGetEGLNativeDisplayWayland(void)
{
    return g_wsi.wl.display;
}

EGLNativeWindowType _glfwGetEGLNativeWindowWayland(window_st* window)
{
    return window->wl.egl.window;
}

void _glfwGetRequiredInstanceExtensionsWayland(char** extensions)
{
    if (!g_wsi.vk.KHR_surface || !g_wsi.vk.KHR_wayland_surface)
        return;

    extensions[0] = "VK_KHR_surface";
    extensions[1] = "VK_KHR_wayland_surface";
}

bool _glfwGetPhysicalDevicePresentationSupportWayland(VkInstance instance,
                                                          VkPhysicalDevice device,
                                                          uint32_t queuefamily)
{
    PFN_vkGetPhysicalDeviceWaylandPresentationSupportKHR
        vkGetPhysicalDeviceWaylandPresentationSupportKHR =
        (PFN_vkGetPhysicalDeviceWaylandPresentationSupportKHR)
        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceWaylandPresentationSupportKHR");
    if (!vkGetPhysicalDeviceWaylandPresentationSupportKHR)
    {
        impl_on_error(SC_WSI_ERR_API_UNAVAILABLE,
                        "Wayland: Vulkan instance missing VK_KHR_wayland_surface extension");
        return VK_NULL_HANDLE;
    }

    return vkGetPhysicalDeviceWaylandPresentationSupportKHR(device,
                                                            queuefamily,
                                                            g_wsi.wl.display);
}

VkResult _glfwCreateWindowSurfaceWayland(VkInstance instance,
                                         window_st* window,
                                         const VkAllocationCallbacks* allocator,
                                         VkSurfaceKHR* surface)
{
    VkResult err;
    VkWaylandSurfaceCreateInfoKHR sci;
    PFN_vkCreateWaylandSurfaceKHR vkCreateWaylandSurfaceKHR;

    vkCreateWaylandSurfaceKHR = (PFN_vkCreateWaylandSurfaceKHR)
        vkGetInstanceProcAddr(instance, "vkCreateWaylandSurfaceKHR");
    if (!vkCreateWaylandSurfaceKHR)
    {
        impl_on_error(SC_WSI_ERR_API_UNAVAILABLE,
                        "Wayland: Vulkan instance missing VK_KHR_wayland_surface extension");
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    memset(&sci, 0, sizeof(sci));
    sci.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    sci.display = g_wsi.wl.display;
    sci.surface = window->wl.surface;

    err = vkCreateWaylandSurfaceKHR(instance, &sci, allocator, surface);
    if (err)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Wayland: Failed to create Vulkan surface: %s",
                        _glfwGetVulkanResultString(err));
    }

    return err;
}


//////////////////////////////////////////////////////////////////////////
//////                        GLFW native API                       //////
//////////////////////////////////////////////////////////////////////////

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

#endif // WSI_WAYLAND

