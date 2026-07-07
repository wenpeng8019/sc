
#include "internal.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

// The the sole (fake) video mode of our (sole) fake monitor
//
static GLFWvidmode getVideoMode(void)
{
    GLFWvidmode mode;
    mode.width = 1920;
    mode.height = 1080;
    mode.refreshRate = 60;
    return mode;
}

//////////////////////////////////////////////////////////////////////////
//////                       GLFW internal API                      //////
//////////////////////////////////////////////////////////////////////////

void null_poll_monitors(void)
{
    const float dpi = 141.f;
    const GLFWvidmode mode = getVideoMode();
    monitor_st* monitor = wsi_alloc_monitor("Null SuperNoop 0",
                                              (int) (mode.width * 25.4f / dpi),
                                              (int) (mode.height * 25.4f / dpi));
    impl_on_monitor(monitor, SC_CONNECTED, _SC_INSERT_FIRST);
}

//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

void null_free_monitor(monitor_st* monitor)
{
    wsi_free_gamma_arrays(&monitor->null.ramp);
}

void null_get_monitor_pos(monitor_st* monitor, int* xpos, int* ypos)
{
    if (xpos)
        *xpos = 0;
    if (ypos)
        *ypos = 0;
}

void null_get_monitor_content_scale(monitor_st* monitor,
                                     float* xscale, float* yscale)
{
    if (xscale)
        *xscale = 1.f;
    if (yscale)
        *yscale = 1.f;
}

void null_get_monitor_work_area(monitor_st* monitor,
                                 int* xpos, int* ypos,
                                 int* width, int* height)
{
    const GLFWvidmode mode = getVideoMode();

    if (xpos)
        *xpos = 0;
    if (ypos)
        *ypos = 10;
    if (width)
        *width = mode.width;
    if (height)
        *height = mode.height - 10;
}

GLFWvidmode* null_get_video_modes(monitor_st* monitor, int* found)
{
    GLFWvidmode* mode = wsi_calloc(1, sizeof(GLFWvidmode));
    *mode = getVideoMode();
    *found = 1;
    return mode;
}

bool null_get_video_mode(monitor_st* monitor, GLFWvidmode* mode)
{
    *mode = getVideoMode();
    return true;
}

bool null_get_gamma_ramp(monitor_st* monitor, GLFWgammaramp* ramp)
{
    if (!monitor->null.ramp.size)
    {
        unsigned int i;

        wsi_alloc_gamma_arrays(&monitor->null.ramp, 256);

        for (i = 0;  i < monitor->null.ramp.size;  i++)
        {
            const float gamma = 2.2f;
            float value;
            value = i / (float) (monitor->null.ramp.size - 1);
            value = powf(value, 1.f / gamma) * 65535.f + 0.5f;
            value = fminf(value, 65535.f);

            monitor->null.ramp.red[i]   = (unsigned short) value;
            monitor->null.ramp.green[i] = (unsigned short) value;
            monitor->null.ramp.blue[i]  = (unsigned short) value;
        }
    }

    wsi_alloc_gamma_arrays(ramp, monitor->null.ramp.size);
    memcpy(ramp->red,   monitor->null.ramp.red,   sizeof(short) * ramp->size);
    memcpy(ramp->green, monitor->null.ramp.green, sizeof(short) * ramp->size);
    memcpy(ramp->blue,  monitor->null.ramp.blue,  sizeof(short) * ramp->size);
    return true;
}

void null_set_gamma_ramp(monitor_st* monitor, const GLFWgammaramp* ramp)
{
    if (monitor->null.ramp.size != ramp->size)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Null: Gamma ramp size must match current ramp size");
        return;
    }

    memcpy(monitor->null.ramp.red,   ramp->red,   sizeof(short) * ramp->size);
    memcpy(monitor->null.ramp.green, ramp->green, sizeof(short) * ramp->size);
    memcpy(monitor->null.ramp.blue,  ramp->blue,  sizeof(short) * ramp->size);
}

