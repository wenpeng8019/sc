
#include "internal.h"

#if defined(WSI_X11)

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>


// Check whether the display mode should be included in enumeration
//
static bool modeIsGood(const XRRModeInfo* mi)
{
    return (mi->modeFlags & RR_Interlace) == 0;
}

// Calculates the refresh rate, in Hz, from the specified RandR mode info
//
static int calculateRefreshRate(const XRRModeInfo* mi)
{
    if (mi->hTotal && mi->vTotal)
        return (int) round((double) mi->dotClock / ((double) mi->hTotal * (double) mi->vTotal));
    else
        return 0;
}

// Returns the mode info for a RandR mode XID
//
static const XRRModeInfo* getModeInfo(const XRRScreenResources* sr, RRMode id)
{
    for (int i = 0;  i < sr->nmode;  i++)
    {
        if (sr->modes[i].id == id)
            return sr->modes + i;
    }

    return NULL;
}

// Convert RandR mode info to GLFW video mode
//
static GLFWvidmode vidmodeFromModeInfo(const XRRModeInfo* mi,
                                       const XRRCrtcInfo* ci)
{
    GLFWvidmode mode;

    if (ci->rotation == RR_Rotate_90 || ci->rotation == RR_Rotate_270)
    {
        mode.width  = mi->height;
        mode.height = mi->width;
    }
    else
    {
        mode.width  = mi->width;
        mode.height = mi->height;
    }

    mode.refreshRate = calculateRefreshRate(mi);

    wsi_split_bpp(DefaultDepth(g_wsi.x11.display, g_wsi.x11.screen),
                  &mode.redBits, &mode.greenBits, &mode.blueBits);

    return mode;
}


//////////////////////////////////////////////////////////////////////////
//////                       GLFW internal API                      //////
//////////////////////////////////////////////////////////////////////////

// Poll for changes in the set of connected monitors
//
void _glfwPollMonitorsX11(void)
{
    if (g_wsi.x11.randr.available && !g_wsi.x11.randr.monitorBroken)
    {
        int disconnectedCount, screenCount = 0;
        monitor_st** disconnected = NULL;
        XineramaScreenInfo* screens = NULL;
        XRRScreenResources* sr = XRRGetScreenResourcesCurrent(g_wsi.x11.display,
                                                              g_wsi.x11.root);
        RROutput primary = XRRGetOutputPrimary(g_wsi.x11.display,
                                               g_wsi.x11.root);

        if (g_wsi.x11.xinerama.available)
            screens = XineramaQueryScreens(g_wsi.x11.display, &screenCount);

        disconnectedCount = g_wsi.monitorCount;
        if (disconnectedCount)
        {
            disconnected = wsi_calloc(g_wsi.monitorCount, sizeof(monitor_st*));
            memcpy(disconnected,
                   g_wsi.monitors,
                   g_wsi.monitorCount * sizeof(monitor_st*));
        }

        for (int i = 0;  i < sr->noutput;  i++)
        {
            int j, type, widthMM, heightMM;

            XRROutputInfo* oi = XRRGetOutputInfo(g_wsi.x11.display, sr, sr->outputs[i]);
            if (oi->connection != RR_Connected || oi->crtc == None)
            {
                XRRFreeOutputInfo(oi);
                continue;
            }

            for (j = 0;  j < disconnectedCount;  j++)
            {
                if (disconnected[j] &&
                    disconnected[j]->x11.output == sr->outputs[i])
                {
                    disconnected[j] = NULL;
                    break;
                }
            }

            if (j < disconnectedCount)
            {
                XRRFreeOutputInfo(oi);
                continue;
            }

            XRRCrtcInfo* ci = XRRGetCrtcInfo(g_wsi.x11.display, sr, oi->crtc);
            if (!ci)
            {
                XRRFreeOutputInfo(oi);
                continue;
            }

            if (ci->rotation == RR_Rotate_90 || ci->rotation == RR_Rotate_270)
            {
                widthMM  = oi->mm_height;
                heightMM = oi->mm_width;
            }
            else
            {
                widthMM  = oi->mm_width;
                heightMM = oi->mm_height;
            }

            if (widthMM <= 0 || heightMM <= 0)
            {
                // HACK: If RandR does not provide a physical size, assume the
                //       X11 default 96 DPI and calculate from the CRTC viewport
                // NOTE: These members are affected by rotation, unlike the mode
                //       info and output info members
                widthMM  = (int) (ci->width * 25.4f / 96.f);
                heightMM = (int) (ci->height * 25.4f / 96.f);
            }

            monitor_st* monitor = wsi_alloc_monitor(oi->name, widthMM, heightMM);
            monitor->x11.output = sr->outputs[i];
            monitor->x11.crtc   = oi->crtc;

            for (j = 0;  j < screenCount;  j++)
            {
                if (screens[j].x_org == ci->x &&
                    screens[j].y_org == ci->y &&
                    screens[j].width == ci->width &&
                    screens[j].height == ci->height)
                {
                    monitor->x11.index = j;
                    break;
                }
            }

            if (monitor->x11.output == primary)
                type = _SC_INSERT_FIRST;
            else
                type = _SC_INSERT_LAST;

            impl_on_monitor(monitor, SC_CONNECTED, type);

            XRRFreeOutputInfo(oi);
            XRRFreeCrtcInfo(ci);
        }

        XRRFreeScreenResources(sr);

        if (screens)
            XFree(screens);

        for (int i = 0;  i < disconnectedCount;  i++)
        {
            if (disconnected[i])
                impl_on_monitor(disconnected[i], SC_DISCONNECTED, 0);
        }

        wsi_free(disconnected);
    }
    else
    {
        const int widthMM = DisplayWidthMM(g_wsi.x11.display, g_wsi.x11.screen);
        const int heightMM = DisplayHeightMM(g_wsi.x11.display, g_wsi.x11.screen);

        impl_on_monitor(wsi_alloc_monitor("Display", widthMM, heightMM),
                          SC_CONNECTED,
                          _SC_INSERT_FIRST);
    }
}

// Set the current video mode for the specified monitor
//
void _glfwSetVideoModeX11(monitor_st* monitor, const GLFWvidmode* desired)
{
    if (g_wsi.x11.randr.available && !g_wsi.x11.randr.monitorBroken)
    {
        GLFWvidmode current;
        RRMode native = None;

        const GLFWvidmode* best = wsi_choose_video_mode(monitor, desired);
        _glfwGetVideoModeX11(monitor, &current);
        if (wsi_compare_video_mode(&current, best) == 0)
            return;

        XRRScreenResources* sr =
            XRRGetScreenResourcesCurrent(g_wsi.x11.display, g_wsi.x11.root);
        XRRCrtcInfo* ci = XRRGetCrtcInfo(g_wsi.x11.display, sr, monitor->x11.crtc);
        XRROutputInfo* oi = XRRGetOutputInfo(g_wsi.x11.display, sr, monitor->x11.output);

        for (int i = 0;  i < oi->nmode;  i++)
        {
            const XRRModeInfo* mi = getModeInfo(sr, oi->modes[i]);
            if (!modeIsGood(mi))
                continue;

            const GLFWvidmode mode = vidmodeFromModeInfo(mi, ci);
            if (wsi_compare_video_mode(best, &mode) == 0)
            {
                native = mi->id;
                break;
            }
        }

        if (native)
        {
            if (monitor->x11.oldMode == None)
                monitor->x11.oldMode = ci->mode;

            XRRSetCrtcConfig(g_wsi.x11.display,
                             sr, monitor->x11.crtc,
                             CurrentTime,
                             ci->x, ci->y,
                             native,
                             ci->rotation,
                             ci->outputs,
                             ci->noutput);
        }

        XRRFreeOutputInfo(oi);
        XRRFreeCrtcInfo(ci);
        XRRFreeScreenResources(sr);
    }
}

// Restore the saved (original) video mode for the specified monitor
//
void _glfwRestoreVideoModeX11(monitor_st* monitor)
{
    if (g_wsi.x11.randr.available && !g_wsi.x11.randr.monitorBroken)
    {
        if (monitor->x11.oldMode == None)
            return;

        XRRScreenResources* sr =
            XRRGetScreenResourcesCurrent(g_wsi.x11.display, g_wsi.x11.root);
        XRRCrtcInfo* ci = XRRGetCrtcInfo(g_wsi.x11.display, sr, monitor->x11.crtc);

        XRRSetCrtcConfig(g_wsi.x11.display,
                         sr, monitor->x11.crtc,
                         CurrentTime,
                         ci->x, ci->y,
                         monitor->x11.oldMode,
                         ci->rotation,
                         ci->outputs,
                         ci->noutput);

        XRRFreeCrtcInfo(ci);
        XRRFreeScreenResources(sr);

        monitor->x11.oldMode = None;
    }
}


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

void wsi_free_monitorX11(monitor_st* monitor)
{
}

void _glfwGetMonitorPosX11(monitor_st* monitor, int* xpos, int* ypos)
{
    if (g_wsi.x11.randr.available && !g_wsi.x11.randr.monitorBroken)
    {
        XRRScreenResources* sr =
            XRRGetScreenResourcesCurrent(g_wsi.x11.display, g_wsi.x11.root);
        XRRCrtcInfo* ci = XRRGetCrtcInfo(g_wsi.x11.display, sr, monitor->x11.crtc);

        if (ci)
        {
            if (xpos)
                *xpos = ci->x;
            if (ypos)
                *ypos = ci->y;

            XRRFreeCrtcInfo(ci);
        }

        XRRFreeScreenResources(sr);
    }
}

void _glfwGetMonitorContentScaleX11(monitor_st* monitor,
                                    float* xscale, float* yscale)
{
    if (xscale)
        *xscale = g_wsi.x11.contentScaleX;
    if (yscale)
        *yscale = g_wsi.x11.contentScaleY;
}

void _glfwGetMonitorWorkareaX11(monitor_st* monitor,
                                int* xpos, int* ypos,
                                int* width, int* height)
{
    int areaX = 0, areaY = 0, areaWidth = 0, areaHeight = 0;

    if (g_wsi.x11.randr.available && !g_wsi.x11.randr.monitorBroken)
    {
        XRRScreenResources* sr =
            XRRGetScreenResourcesCurrent(g_wsi.x11.display, g_wsi.x11.root);
        XRRCrtcInfo* ci = XRRGetCrtcInfo(g_wsi.x11.display, sr, monitor->x11.crtc);

        areaX = ci->x;
        areaY = ci->y;

        const XRRModeInfo* mi = getModeInfo(sr, ci->mode);

        if (ci->rotation == RR_Rotate_90 || ci->rotation == RR_Rotate_270)
        {
            areaWidth  = mi->height;
            areaHeight = mi->width;
        }
        else
        {
            areaWidth  = mi->width;
            areaHeight = mi->height;
        }

        XRRFreeCrtcInfo(ci);
        XRRFreeScreenResources(sr);
    }
    else
    {
        areaWidth  = DisplayWidth(g_wsi.x11.display, g_wsi.x11.screen);
        areaHeight = DisplayHeight(g_wsi.x11.display, g_wsi.x11.screen);
    }

    if (g_wsi.x11.NET_WORKAREA && g_wsi.x11.NET_CURRENT_DESKTOP)
    {
        Atom* extents = NULL;
        Atom* desktop = NULL;
        const unsigned long extentCount =
            _glfwGetWindowPropertyX11(g_wsi.x11.root,
                                      g_wsi.x11.NET_WORKAREA,
                                      XA_CARDINAL,
                                      (unsigned char**) &extents);

        if (_glfwGetWindowPropertyX11(g_wsi.x11.root,
                                      g_wsi.x11.NET_CURRENT_DESKTOP,
                                      XA_CARDINAL,
                                      (unsigned char**) &desktop) > 0)
        {
            if (extentCount >= 4 && *desktop < extentCount / 4)
            {
                const int globalX = extents[*desktop * 4 + 0];
                const int globalY = extents[*desktop * 4 + 1];
                const int globalWidth  = extents[*desktop * 4 + 2];
                const int globalHeight = extents[*desktop * 4 + 3];

                if (areaX < globalX)
                {
                    areaWidth -= globalX - areaX;
                    areaX = globalX;
                }

                if (areaY < globalY)
                {
                    areaHeight -= globalY - areaY;
                    areaY = globalY;
                }

                if (areaX + areaWidth > globalX + globalWidth)
                    areaWidth = globalX - areaX + globalWidth;
                if (areaY + areaHeight > globalY + globalHeight)
                    areaHeight = globalY - areaY + globalHeight;
            }
        }

        if (extents)
            XFree(extents);
        if (desktop)
            XFree(desktop);
    }

    if (xpos)
        *xpos = areaX;
    if (ypos)
        *ypos = areaY;
    if (width)
        *width = areaWidth;
    if (height)
        *height = areaHeight;
}

GLFWvidmode* _glfwGetVideoModesX11(monitor_st* monitor, int* count)
{
    GLFWvidmode* result;

    *count = 0;

    if (g_wsi.x11.randr.available && !g_wsi.x11.randr.monitorBroken)
    {
        XRRScreenResources* sr =
            XRRGetScreenResourcesCurrent(g_wsi.x11.display, g_wsi.x11.root);
        XRRCrtcInfo* ci = XRRGetCrtcInfo(g_wsi.x11.display, sr, monitor->x11.crtc);
        XRROutputInfo* oi = XRRGetOutputInfo(g_wsi.x11.display, sr, monitor->x11.output);

        result = wsi_calloc(oi->nmode, sizeof(GLFWvidmode));

        for (int i = 0;  i < oi->nmode;  i++)
        {
            const XRRModeInfo* mi = getModeInfo(sr, oi->modes[i]);
            if (!modeIsGood(mi))
                continue;

            const GLFWvidmode mode = vidmodeFromModeInfo(mi, ci);
            int j;

            for (j = 0;  j < *count;  j++)
            {
                if (wsi_compare_video_mode(result + j, &mode) == 0)
                    break;
            }

            // Skip duplicate modes
            if (j < *count)
                continue;

            (*count)++;
            result[*count - 1] = mode;
        }

        XRRFreeOutputInfo(oi);
        XRRFreeCrtcInfo(ci);
        XRRFreeScreenResources(sr);
    }
    else
    {
        *count = 1;
        result = wsi_calloc(1, sizeof(GLFWvidmode));
        _glfwGetVideoModeX11(monitor, result);
    }

    return result;
}

bool _glfwGetVideoModeX11(monitor_st* monitor, GLFWvidmode* mode)
{
    if (g_wsi.x11.randr.available && !g_wsi.x11.randr.monitorBroken)
    {
        XRRScreenResources* sr =
            XRRGetScreenResourcesCurrent(g_wsi.x11.display, g_wsi.x11.root);
        const XRRModeInfo* mi = NULL;

        XRRCrtcInfo* ci = XRRGetCrtcInfo(g_wsi.x11.display, sr, monitor->x11.crtc);
        if (ci)
        {
            mi = getModeInfo(sr, ci->mode);
            if (mi)
                *mode = vidmodeFromModeInfo(mi, ci);

            XRRFreeCrtcInfo(ci);
        }

        XRRFreeScreenResources(sr);

        if (!mi)
        {
            impl_on_error(SC_WSI_ERR_PLATFORM_ERROR, "X11: Failed to query video mode");
            return false;
        }
    }
    else
    {
        mode->width = DisplayWidth(g_wsi.x11.display, g_wsi.x11.screen);
        mode->height = DisplayHeight(g_wsi.x11.display, g_wsi.x11.screen);
        mode->refreshRate = 0;

        wsi_split_bpp(DefaultDepth(g_wsi.x11.display, g_wsi.x11.screen),
                      &mode->redBits, &mode->greenBits, &mode->blueBits);
    }

    return true;
}

bool _glfwGetGammaRampX11(monitor_st* monitor, GLFWgammaramp* ramp)
{
    if (g_wsi.x11.randr.available && !g_wsi.x11.randr.gammaBroken)
    {
        const size_t size = XRRGetCrtcGammaSize(g_wsi.x11.display,
                                                monitor->x11.crtc);
        XRRCrtcGamma* gamma = XRRGetCrtcGamma(g_wsi.x11.display,
                                              monitor->x11.crtc);

        wsi_alloc_gamma_arrays(ramp, size);

        memcpy(ramp->red,   gamma->red,   size * sizeof(unsigned short));
        memcpy(ramp->green, gamma->green, size * sizeof(unsigned short));
        memcpy(ramp->blue,  gamma->blue,  size * sizeof(unsigned short));

        XRRFreeGamma(gamma);
        return true;
    }
    else if (g_wsi.x11.vidmode.available)
    {
        int size;
        XF86VidModeGetGammaRampSize(g_wsi.x11.display, g_wsi.x11.screen, &size);

        wsi_alloc_gamma_arrays(ramp, size);

        XF86VidModeGetGammaRamp(g_wsi.x11.display,
                                g_wsi.x11.screen,
                                ramp->size, ramp->red, ramp->green, ramp->blue);
        return true;
    }
    else
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "X11: Gamma ramp access not supported by server");
        return false;
    }
}

void _glfwSetGammaRampX11(monitor_st* monitor, const GLFWgammaramp* ramp)
{
    if (g_wsi.x11.randr.available && !g_wsi.x11.randr.gammaBroken)
    {
        if (XRRGetCrtcGammaSize(g_wsi.x11.display, monitor->x11.crtc) != ramp->size)
        {
            impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                            "X11: Gamma ramp size must match current ramp size");
            return;
        }

        XRRCrtcGamma* gamma = XRRAllocGamma(ramp->size);

        memcpy(gamma->red,   ramp->red,   ramp->size * sizeof(unsigned short));
        memcpy(gamma->green, ramp->green, ramp->size * sizeof(unsigned short));
        memcpy(gamma->blue,  ramp->blue,  ramp->size * sizeof(unsigned short));

        XRRSetCrtcGamma(g_wsi.x11.display, monitor->x11.crtc, gamma);
        XRRFreeGamma(gamma);
    }
    else if (g_wsi.x11.vidmode.available)
    {
        XF86VidModeSetGammaRamp(g_wsi.x11.display,
                                g_wsi.x11.screen,
                                ramp->size,
                                (unsigned short*) ramp->red,
                                (unsigned short*) ramp->green,
                                (unsigned short*) ramp->blue);
    }
    else
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "X11: Gamma ramp access not supported by server");
    }
}


//////////////////////////////////////////////////////////////////////////
//////                        GLFW native API                       //////
//////////////////////////////////////////////////////////////////////////

WSI_API RRCrtc wsi_get_x11_adapter(sc_monitor* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return None;
    }

    if (g_wsi.platform.platformID != SC_PLATFORM_X11)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_UNAVAILABLE, "X11: Platform not initialized");
        return None;
    }

    monitor_st* monitor = (monitor_st*) handle;
    assert(monitor != NULL);

    return monitor->x11.crtc;
}

WSI_API RROutput wsi_get_x11_monitor(sc_monitor* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return None;
    }

    if (g_wsi.platform.platformID != SC_PLATFORM_X11)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_UNAVAILABLE, "X11: Platform not initialized");
        return None;
    }

    monitor_st* monitor = (monitor_st*) handle;
    assert(monitor != NULL);

    return monitor->x11.output;
}

#endif // WSI_X11

