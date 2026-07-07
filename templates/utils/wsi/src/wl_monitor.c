
#include "internal.h"

#if defined(WSI_WAYLAND)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <assert.h>

#include "wayland-client-protocol.h"


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
    GLFWvidmode mode;

    mode.width = width;
    mode.height = height;
    mode.refreshRate = (int) round(refresh / 1000.0);

    monitor->modeCount++;
    monitor->modes =
        wsi_realloc(monitor->modes, monitor->modeCount * sizeof(GLFWvidmode));
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
        const GLFWvidmode* mode = &monitor->modes[monitor->wl.currentMode];
        monitor->widthMM  = (int) (mode->width * 25.4f / 96.f);
        monitor->heightMM = (int) (mode->height * 25.4f / 96.f);
    }

    for (int i = 0; i < g_wsi.monitorCount; i++)
    {
        if (g_wsi.monitors[i] == monitor)
            return;
    }

    impl_on_monitor(monitor, SC_CONNECTED, _SC_INSERT_LAST);
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

void outputHandleName(void* userData, struct wl_output* wl_output, const char* name)
{
    monitor_st* monitor = userData;

    strncpy(monitor->name, name, sizeof(monitor->name) - 1);
}

void outputHandleDescription(void* userData,
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


//////////////////////////////////////////////////////////////////////////
//////                       GLFW internal API                      //////
//////////////////////////////////////////////////////////////////////////

void wayland_AddOutput(uint32_t name, uint32_t version)
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


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

void wsi_free_monitorWayland(monitor_st* monitor)
{
    if (monitor->wl.output)
        wl_output_destroy(monitor->wl.output);
}

void wayland_get_monitor_pos(monitor_st* monitor, int* xpos, int* ypos)
{
    if (xpos)
        *xpos = monitor->wl.x;
    if (ypos)
        *ypos = monitor->wl.y;
}

void wayland_get_monitor_content_scale(monitor_st* monitor,
                                        float* xscale, float* yscale)
{
    if (xscale)
        *xscale = (float) monitor->wl.scale;
    if (yscale)
        *yscale = (float) monitor->wl.scale;
}

void wayland_get_monitor_work_area(monitor_st* monitor,
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

GLFWvidmode* wayland_get_video_modes(monitor_st* monitor, int* found)
{
    *found = monitor->modeCount;
    return monitor->modes;
}

bool wayland_get_video_mode(monitor_st* monitor, GLFWvidmode* mode)
{
    *mode = monitor->modes[monitor->wl.currentMode];
    return true;
}

bool wayland_get_gamma_ramp(monitor_st* monitor, GLFWgammaramp* ramp)
{
    impl_on_error(SC_WSI_ERR_FEATURE_UNAVAILABLE,
                    "Wayland: Gamma ramp access is not available");
    return false;
}

void wayland_set_gamma_ramp(monitor_st* monitor, const GLFWgammaramp* ramp)
{
    impl_on_error(SC_WSI_ERR_FEATURE_UNAVAILABLE,
                    "Wayland: Gamma ramp access is not available");
}


//////////////////////////////////////////////////////////////////////////
//////                        GLFW native API                       //////
//////////////////////////////////////////////////////////////////////////

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

#endif // WSI_WAYLAND

