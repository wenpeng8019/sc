
#include "internal.h"

#include <assert.h>
#include <math.h>
#include <float.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>


// Lexically compare video modes, used by qsort
//
static int compareVideoModes(const void* fp, const void* sp)
{
    const GLFWvidmode* fm = fp;
    const GLFWvidmode* sm = sp;
    const int farea = fm->width * fm->height;
    const int sarea = sm->width * sm->height;

    // First sort on screen area
    if (farea != sarea)
        return farea - sarea;

    // Then sort on width
    if (fm->width != sm->width)
        return fm->width - sm->width;

    // Lastly sort on refresh rate
    return fm->refreshRate - sm->refreshRate;
}

// Retrieves the available modes for the specified monitor
//
static bool refreshVideoModes(monitor_st* monitor)
{
    int modeCount;
    GLFWvidmode* modes;

    if (monitor->modes)
        return true;

    modes = g_wsi.platform.getVideoModes(monitor, &modeCount);
    if (!modes)
        return false;

    qsort(modes, modeCount, sizeof(GLFWvidmode), compareVideoModes);

    wsi_free(monitor->modes);
    monitor->modes = modes;
    monitor->modeCount = modeCount;

    return true;
}


//////////////////////////////////////////////////////////////////////////
//////                         GLFW event API                       //////
//////////////////////////////////////////////////////////////////////////

// Notifies shared code of a monitor connection or disconnection
//
void impl_on_monitor(monitor_st* monitor, int action, int placement)
{
    assert(monitor != NULL);
    assert(action == SC_CONNECTED || action == SC_DISCONNECTED);
    assert(placement == _SC_INSERT_FIRST || placement == _SC_INSERT_LAST);

    if (action == SC_CONNECTED)
    {
        g_wsi.monitorCount++;
        g_wsi.monitors =
            wsi_realloc(g_wsi.monitors,
                          sizeof(monitor_st*) * g_wsi.monitorCount);

        if (placement == _SC_INSERT_FIRST)
        {
            memmove(g_wsi.monitors + 1,
                    g_wsi.monitors,
                    ((size_t) g_wsi.monitorCount - 1) * sizeof(monitor_st*));
            g_wsi.monitors[0] = monitor;
        }
        else
            g_wsi.monitors[g_wsi.monitorCount - 1] = monitor;
    }
    else if (action == SC_DISCONNECTED)
    {
        int i;
        window_st* window;

        for (window = g_wsi.windowListHead;  window;  window = window->next)
        {
            if (window->monitor == monitor)
            {
                int width, height, xoff, yoff;
                g_wsi.platform.getWindowSize(window, &width, &height);
                g_wsi.platform.setWindowMonitor(window, NULL, 0, 0, width, height, 0);
                g_wsi.platform.getWindowFrameSize(window, &xoff, &yoff, NULL, NULL);
                g_wsi.platform.setWindowPos(window, xoff, yoff);
            }
        }

        for (i = 0;  i < g_wsi.monitorCount;  i++)
        {
            if (g_wsi.monitors[i] == monitor)
            {
                g_wsi.monitorCount--;
                memmove(g_wsi.monitors + i,
                        g_wsi.monitors + i + 1,
                        ((size_t) g_wsi.monitorCount - i) * sizeof(monitor_st*));
                break;
            }
        }
    }

    if (g_wsi.callbacks.monitor)
        g_wsi.callbacks.monitor((sc_monitor*) monitor, action);

    if (action == SC_DISCONNECTED)
        wsi_free_monitor(monitor);
}

// Notifies shared code that a full screen window has acquired or released
// a monitor
//
void impl_on_monitor_window(monitor_st* monitor, window_st* window)
{
    assert(monitor != NULL);
    monitor->window = window;
}


//////////////////////////////////////////////////////////////////////////
//////                       GLFW internal API                      //////
//////////////////////////////////////////////////////////////////////////

// Allocates and returns a monitor object with the specified name and dimensions
//
monitor_st* wsi_alloc_monitor(const char* name, int widthMM, int heightMM)
{
    monitor_st* monitor = wsi_calloc(1, sizeof(monitor_st));
    monitor->widthMM = widthMM;
    monitor->heightMM = heightMM;

    strncpy(monitor->name, name, sizeof(monitor->name) - 1);

    return monitor;
}

// Frees a monitor object and any data associated with it
//
void wsi_free_monitor(monitor_st* monitor)
{
    if (monitor == NULL)
        return;

    g_wsi.platform.freeMonitor(monitor);

    wsi_free_gamma_arrays(&monitor->originalRamp);
    wsi_free_gamma_arrays(&monitor->currentRamp);

    wsi_free(monitor->modes);
    wsi_free(monitor);
}

// Allocates red, green and blue value arrays of the specified size
//
void wsi_alloc_gamma_arrays(GLFWgammaramp* ramp, unsigned int size)
{
    ramp->red = wsi_calloc(size, sizeof(unsigned short));
    ramp->green = wsi_calloc(size, sizeof(unsigned short));
    ramp->blue = wsi_calloc(size, sizeof(unsigned short));
    ramp->size = size;
}

// Frees the red, green and blue value arrays and clears the struct
//
void wsi_free_gamma_arrays(GLFWgammaramp* ramp)
{
    wsi_free(ramp->red);
    wsi_free(ramp->green);
    wsi_free(ramp->blue);

    memset(ramp, 0, sizeof(GLFWgammaramp));
}

// Chooses the video mode most closely matching the desired one
//
const GLFWvidmode* wsi_choose_video_mode(monitor_st* monitor,
                                        const GLFWvidmode* desired)
{
    int i;
    unsigned int sizeDiff, leastSizeDiff = UINT_MAX;
    unsigned int rateDiff, leastRateDiff = UINT_MAX;
    const GLFWvidmode* current;
    const GLFWvidmode* closest = NULL;

    if (!refreshVideoModes(monitor))
        return NULL;

    for (i = 0;  i < monitor->modeCount;  i++)
    {
        current = monitor->modes + i;

        sizeDiff = abs((current->width - desired->width) *
                       (current->width - desired->width) +
                       (current->height - desired->height) *
                       (current->height - desired->height));

        if (desired->refreshRate != SC_DONT_CARE)
            rateDiff = abs(current->refreshRate - desired->refreshRate);
        else
            rateDiff = UINT_MAX - current->refreshRate;

        if (sizeDiff < leastSizeDiff ||
            (sizeDiff == leastSizeDiff && rateDiff < leastRateDiff))
        {
            closest = current;
            leastSizeDiff = sizeDiff;
            leastRateDiff = rateDiff;
        }
    }

    return closest;
}

// Performs lexical comparison between two @ref GLFWvidmode structures
//
int wsi_compare_video_mode(const GLFWvidmode* fm, const GLFWvidmode* sm)
{
    return compareVideoModes(fm, sm);
}

// Splits a color depth into red, green and blue bit depths
//
void wsi_split_bpp(int bpp, int* red, int* green, int* blue)
{
    int delta;

    // We assume that by 32 the user really meant 24
    if (bpp == 32)
        bpp = 24;

    // Convert "bits per pixel" to red, green & blue sizes

    *red = *green = *blue = bpp / 3;
    delta = bpp - (*red * 3);
    if (delta >= 1)
        *green = *green + 1;

    if (delta == 2)
        *red = *red + 1;
}


//////////////////////////////////////////////////////////////////////////
//////                        GLFW public API                       //////
//////////////////////////////////////////////////////////////////////////

WSI_API sc_monitor** sc_wsi_get_monitors(int* count)
{
    assert(count != NULL);

    *count = 0;

    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }

    *count = g_wsi.monitorCount;
    return (sc_monitor**) g_wsi.monitors;
}

WSI_API sc_monitor* sc_wsi_get_primary_monitor(void)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }

    if (!g_wsi.monitorCount)
        return NULL;

    return (sc_monitor*) g_wsi.monitors[0];
}

WSI_API void sc_wsi_monitor_get_pos(sc_monitor* handle, int* xpos, int* ypos)
{
    if (xpos)
        *xpos = 0;
    if (ypos)
        *ypos = 0;

    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    monitor_st* monitor = (monitor_st*) handle;
    assert(monitor != NULL);

    g_wsi.platform.getMonitorPos(monitor, xpos, ypos);
}

WSI_API void sc_wsi_monitor_get_work_area(sc_monitor* handle,
                                    int* xpos, int* ypos,
                                    int* width, int* height)
{
    if (xpos)
        *xpos = 0;
    if (ypos)
        *ypos = 0;
    if (width)
        *width = 0;
    if (height)
        *height = 0;

    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    monitor_st* monitor = (monitor_st*) handle;
    assert(monitor != NULL);

    g_wsi.platform.getMonitorWorkarea(monitor, xpos, ypos, width, height);
}

WSI_API void sc_wsi_monitor_get_physical_size(sc_monitor* handle, int* widthMM, int* heightMM)
{
    if (widthMM)
        *widthMM = 0;
    if (heightMM)
        *heightMM = 0;

    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    monitor_st* monitor = (monitor_st*) handle;
    assert(monitor != NULL);

    if (widthMM)
        *widthMM = monitor->widthMM;
    if (heightMM)
        *heightMM = monitor->heightMM;
}

WSI_API void sc_wsi_monitor_get_content_scale(sc_monitor* handle,
                                        float* xscale, float* yscale)
{
    if (xscale)
        *xscale = 0.f;
    if (yscale)
        *yscale = 0.f;

    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    monitor_st* monitor = (monitor_st*) handle;
    assert(monitor != NULL);

    g_wsi.platform.getMonitorContentScale(monitor, xscale, yscale);
}

WSI_API const char* sc_wsi_monitor_get_name(sc_monitor* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }

    monitor_st* monitor = (monitor_st*) handle;
    assert(monitor != NULL);

    return monitor->name;
}

WSI_API void sc_wsi_monitor_set_user_data(sc_monitor* handle, void* pointer)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    monitor_st* monitor = (monitor_st*) handle;
    assert(monitor != NULL);

    monitor->userPointer = pointer;
}

WSI_API void* sc_wsi_monitor_get_user_data(sc_monitor* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }

    monitor_st* monitor = (monitor_st*) handle;
    assert(monitor != NULL);

    return monitor->userPointer;
}

WSI_API sc_monitor_cb sc_wsi_monitor_set_callback(sc_monitor_cb cbfun)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }
    sc_monitor_cb t = g_wsi.callbacks.monitor;
    g_wsi.callbacks.monitor = cbfun;
    return t;
}

WSI_API const GLFWvidmode* sc_wsi_monitor_get_video_modes(sc_monitor* handle, int* count)
{
    assert(count != NULL);

    *count = 0;

    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }

    monitor_st* monitor = (monitor_st*) handle;
    assert(monitor != NULL);

    if (!refreshVideoModes(monitor))
        return NULL;

    *count = monitor->modeCount;
    return monitor->modes;
}

WSI_API const GLFWvidmode* sc_wsi_monitor_get_video_mode(sc_monitor* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }

    monitor_st* monitor = (monitor_st*) handle;
    assert(monitor != NULL);

    if (!g_wsi.platform.getVideoMode(monitor, &monitor->currentMode))
        return NULL;

    return &monitor->currentMode;
}

WSI_API void sc_wsi_monitor_get_gamma(sc_monitor* handle, float gamma)
{
    unsigned int i;
    unsigned short* values;
    GLFWgammaramp ramp;
    const GLFWgammaramp* original;

    assert(gamma > 0.f);
    assert(isfinite(gamma));

    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    assert(handle != NULL);

    if (!isfinite(gamma) || gamma <= 0.f)
    {
        impl_on_error(SC_WSI_ERR_INVALID_VALUE, "Invalid gamma value %f", gamma);
        return;
    }

    original = sc_wsi_monitor_get_gamma_ramp(handle);
    if (!original)
        return;

    values = wsi_calloc(original->size, sizeof(unsigned short));

    for (i = 0;  i < original->size;  i++)
    {
        float value;

        // Calculate intensity
        value = i / (float) (original->size - 1);
        // Apply gamma curve
        value = powf(value, 1.f / gamma) * 65535.f + 0.5f;
        // Clamp to value range
        value = fminf(value, 65535.f);

        values[i] = (unsigned short) value;
    }

    ramp.red = values;
    ramp.green = values;
    ramp.blue = values;
    ramp.size = original->size;

    sc_wsi_monitor_set_gamma_ramp(handle, &ramp);
    wsi_free(values);
}

WSI_API const GLFWgammaramp* sc_wsi_monitor_get_gamma_ramp(sc_monitor* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }

    monitor_st* monitor = (monitor_st*) handle;
    assert(monitor != NULL);

    wsi_free_gamma_arrays(&monitor->currentRamp);
    if (!g_wsi.platform.getGammaRamp(monitor, &monitor->currentRamp))
        return NULL;

    return &monitor->currentRamp;
}

WSI_API void sc_wsi_monitor_set_gamma_ramp(sc_monitor* handle, const GLFWgammaramp* ramp)
{
    assert(ramp != NULL);
    assert(ramp->size > 0);
    assert(ramp->red != NULL);
    assert(ramp->green != NULL);
    assert(ramp->blue != NULL);

    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    monitor_st* monitor = (monitor_st*) handle;
    assert(monitor != NULL);

    if (ramp->size <= 0)
    {
        impl_on_error(SC_WSI_ERR_INVALID_VALUE,
                        "Invalid gamma ramp size %i",
                        ramp->size);
        return;
    }

    if (!monitor->originalRamp.size)
    {
        if (!g_wsi.platform.getGammaRamp(monitor, &monitor->originalRamp))
            return;
    }

    g_wsi.platform.setGammaRamp(monitor, ramp);
}

