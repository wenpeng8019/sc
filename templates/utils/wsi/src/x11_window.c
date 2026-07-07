
#include "internal.h"

#if defined(WSI_X11)

#include <X11/cursorfont.h>
#include <X11/Xmd.h>

#include <poll.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <assert.h>

// Action for EWMH client messages
#define _NET_WM_STATE_REMOVE        0
#define _NET_WM_STATE_ADD           1
#define _NET_WM_STATE_TOGGLE        2

// Additional mouse button names for XButtonEvent
#define Button6            6
#define Button7            7

// Motif WM hints flags
#define MWM_HINTS_DECORATIONS   2
#define MWM_DECOR_ALL           1

#define _GLFW_XDND_VERSION 5

// Wait for event data to arrive on the X11 display socket
// This avoids blocking other threads via the per-display Xlib lock that also
// covers GLX functions
//
static bool waitForX11Event(double* timeout)
{
    struct pollfd fd = { ConnectionNumber(g_wsi.x11.display), POLLIN };

    while (!XPending(g_wsi.x11.display))
    {
        if (!_glfwPollPOSIX(&fd, 1, timeout))
            return false;
    }

    return true;
}

// Wait for event data to arrive on any event file descriptor
// This avoids blocking other threads via the per-display Xlib lock that also
// covers GLX functions
//
static bool waitForAnyEvent(double* timeout)
{
    enum { XLIB_FD, PIPE_FD, INOTIFY_FD };
    struct pollfd fds[] =
    {
        [XLIB_FD] = { ConnectionNumber(g_wsi.x11.display), POLLIN },
        [PIPE_FD] = { g_wsi.x11.emptyEventPipe[0], POLLIN },
        [INOTIFY_FD] = { -1, POLLIN }
    };


    while (!XPending(g_wsi.x11.display))
    {
        if (!_glfwPollPOSIX(fds, sizeof(fds) / sizeof(fds[0]), timeout))
            return false;

        for (int i = 1; i < sizeof(fds) / sizeof(fds[0]); i++)
        {
            if (fds[i].revents & POLLIN)
                return true;
        }
    }

    return true;
}

// Writes a byte to the empty event pipe
//
static void writeEmptyEvent(void)
{
    for (;;)
    {
        const char byte = 0;
        const ssize_t result = write(g_wsi.x11.emptyEventPipe[1], &byte, 1);
        if (result == 1 || (result == -1 && errno != EINTR))
            break;
    }
}

// Drains available data from the empty event pipe
//
static void drainEmptyEvents(void)
{
    for (;;)
    {
        char dummy[64];
        const ssize_t result = read(g_wsi.x11.emptyEventPipe[0], dummy, sizeof(dummy));
        if (result == -1 && errno != EINTR)
            break;
    }
}

// Waits until a VisibilityNotify event arrives for the specified window or the
// timeout period elapses (ICCCM section 4.2.2)
//
static bool waitForVisibilityNotify(window_st* window)
{
    XEvent dummy;
    double timeout = 0.1;

    while (!XCheckTypedWindowEvent(g_wsi.x11.display,
                                   window->x11.handle,
                                   VisibilityNotify,
                                   &dummy))
    {
        if (!waitForX11Event(&timeout))
            return false;
    }

    return true;
}

// Returns whether the window is iconified
//
static int getWindowState(window_st* window)
{
    int result = WithdrawnState;
    struct {
        CARD32 state;
        Window icon;
    } *state = NULL;

    if (x11_GetWindowProperty(window->x11.handle,
                                  g_wsi.x11.WM_STATE,
                                  g_wsi.x11.WM_STATE,
                                  (unsigned char**) &state) >= 2)
    {
        result = state->state;
    }

    if (state)
        XFree(state);

    return result;
}

// Returns whether the event is a selection event
//
static Bool isSelectionEvent(Display* display, XEvent* event, XPointer pointer)
{
    if (event->xany.window != g_wsi.x11.helperWindowHandle)
        return False;

    return event->type == SelectionRequest ||
           event->type == SelectionNotify ||
           event->type == SelectionClear;
}

// Returns whether it is a _NET_FRAME_EXTENTS event for the specified window
//
static Bool isFrameExtentsEvent(Display* display, XEvent* event, XPointer pointer)
{
    window_st* window = (window_st*) pointer;
    return event->type == PropertyNotify &&
           event->xproperty.state == PropertyNewValue &&
           event->xproperty.window == window->x11.handle &&
           event->xproperty.atom == g_wsi.x11.NET_FRAME_EXTENTS;
}

// Returns whether it is a property event for the specified selection transfer
//
static Bool isSelPropNewValueNotify(Display* display, XEvent* event, XPointer pointer)
{
    XEvent* notification = (XEvent*) pointer;
    return event->type == PropertyNotify &&
           event->xproperty.state == PropertyNewValue &&
           event->xproperty.window == notification->xselection.requestor &&
           event->xproperty.atom == notification->xselection.property;
}

// Translates an X event modifier state mask
//
static int translateState(int state)
{
    int mods = 0;

    if (state & ShiftMask)
        mods |= SC_MOD_SHIFT;
    if (state & ControlMask)
        mods |= SC_MOD_CONTROL;
    if (state & Mod1Mask)
        mods |= SC_MOD_ALT;
    if (state & Mod4Mask)
        mods |= SC_MOD_SUPER;
    if (state & LockMask)
        mods |= SC_MOD_CAPS_LOCK;
    if (state & Mod2Mask)
        mods |= SC_MOD_NUM_LOCK;

    return mods;
}

// Translates an X11 key code to a GLFW key token
//
static int translateKey(int scancode)
{
    // Use the pre-filled LUT (see createKeyTables() in x11_init.c)
    if (scancode < 0 || scancode > 255)
        return SC_KEY_UNKNOWN;

    return g_wsi.x11.keycodes[scancode];
}

// Sends an EWMH or ICCCM event to the window manager
//
static void sendEventToWM(window_st* window, Atom type,
                          long a, long b, long c, long d, long e)
{
    XEvent event = { ClientMessage };
    event.xclient.window = window->x11.handle;
    event.xclient.format = 32; // Data is 32-bit longs
    event.xclient.message_type = type;
    event.xclient.data.l[0] = a;
    event.xclient.data.l[1] = b;
    event.xclient.data.l[2] = c;
    event.xclient.data.l[3] = d;
    event.xclient.data.l[4] = e;

    XSendEvent(g_wsi.x11.display, g_wsi.x11.root,
               False,
               SubstructureNotifyMask | SubstructureRedirectMask,
               &event);
}

// Updates the normal hints according to the window settings
//
static void updateNormalHints(window_st* window, int width, int height)
{
    XSizeHints* hints = XAllocSizeHints();

    long supplied;
    XGetWMNormalHints(g_wsi.x11.display, window->x11.handle, hints, &supplied);

    hints->flags &= ~(PMinSize | PMaxSize | PAspect);

    if (!window->monitor)
    {
        if (window->resizable)
        {
            if (window->minwidth != SC_DONT_CARE &&
                window->minheight != SC_DONT_CARE)
            {
                hints->flags |= PMinSize;
                hints->min_width = window->minwidth;
                hints->min_height = window->minheight;
            }

            if (window->maxwidth != SC_DONT_CARE &&
                window->maxheight != SC_DONT_CARE)
            {
                hints->flags |= PMaxSize;
                hints->max_width = window->maxwidth;
                hints->max_height = window->maxheight;
            }

            if (window->numer != SC_DONT_CARE &&
                window->denom != SC_DONT_CARE)
            {
                hints->flags |= PAspect;
                hints->min_aspect.x = hints->max_aspect.x = window->numer;
                hints->min_aspect.y = hints->max_aspect.y = window->denom;
            }
        }
        else
        {
            hints->flags |= (PMinSize | PMaxSize);
            hints->min_width  = hints->max_width  = width;
            hints->min_height = hints->max_height = height;
        }
    }

    XSetWMNormalHints(g_wsi.x11.display, window->x11.handle, hints);
    XFree(hints);
}

// Updates the full screen status of the window
//
static void updateWindowMode(window_st* window)
{
    if (window->monitor)
    {
        if (g_wsi.x11.xinerama.available &&
            g_wsi.x11.NET_WM_FULLSCREEN_MONITORS)
        {
            sendEventToWM(window,
                          g_wsi.x11.NET_WM_FULLSCREEN_MONITORS,
                          window->monitor->x11.index,
                          window->monitor->x11.index,
                          window->monitor->x11.index,
                          window->monitor->x11.index,
                          0);
        }

        if (g_wsi.x11.NET_WM_STATE && g_wsi.x11.NET_WM_STATE_FULLSCREEN)
        {
            sendEventToWM(window,
                          g_wsi.x11.NET_WM_STATE,
                          _NET_WM_STATE_ADD,
                          g_wsi.x11.NET_WM_STATE_FULLSCREEN,
                          0, 1, 0);
        }
        else
        {
            // This is the butcher's way of removing window decorations
            // Setting the override-redirect attribute on a window makes the
            // window manager ignore the window completely (ICCCM, section 4)
            // The good thing is that this makes undecorated full screen windows
            // easy to do; the bad thing is that we have to do everything
            // manually and some things (like iconify/restore) won't work at
            // all, as those are tasks usually performed by the window manager

            XSetWindowAttributes attributes;
            attributes.override_redirect = True;
            XChangeWindowAttributes(g_wsi.x11.display,
                                    window->x11.handle,
                                    CWOverrideRedirect,
                                    &attributes);

            window->x11.overrideRedirect = true;
        }

        // Enable compositor bypass
        if (!window->x11.transparent)
        {
            const unsigned long value = 1;

            XChangeProperty(g_wsi.x11.display,  window->x11.handle,
                            g_wsi.x11.NET_WM_BYPASS_COMPOSITOR, XA_CARDINAL, 32,
                            PropModeReplace, (unsigned char*) &value, 1);
        }
    }
    else
    {
        if (g_wsi.x11.xinerama.available &&
            g_wsi.x11.NET_WM_FULLSCREEN_MONITORS)
        {
            XDeleteProperty(g_wsi.x11.display, window->x11.handle,
                            g_wsi.x11.NET_WM_FULLSCREEN_MONITORS);
        }

        if (g_wsi.x11.NET_WM_STATE && g_wsi.x11.NET_WM_STATE_FULLSCREEN)
        {
            sendEventToWM(window,
                          g_wsi.x11.NET_WM_STATE,
                          _NET_WM_STATE_REMOVE,
                          g_wsi.x11.NET_WM_STATE_FULLSCREEN,
                          0, 1, 0);
        }
        else
        {
            XSetWindowAttributes attributes;
            attributes.override_redirect = False;
            XChangeWindowAttributes(g_wsi.x11.display,
                                    window->x11.handle,
                                    CWOverrideRedirect,
                                    &attributes);

            window->x11.overrideRedirect = false;
        }

        // Disable compositor bypass
        if (!window->x11.transparent)
        {
            XDeleteProperty(g_wsi.x11.display, window->x11.handle,
                            g_wsi.x11.NET_WM_BYPASS_COMPOSITOR);
        }
    }
}

// Decode a Unicode code point from a UTF-8 stream
// Based on cutef8 by Jeff Bezanson (Public Domain)
//
static uint32_t decodeUTF8(const char** s)
{
    uint32_t codepoint = 0, count = 0;
    static const uint32_t offsets[] =
    {
        0x00000000u, 0x00003080u, 0x000e2080u,
        0x03c82080u, 0xfa082080u, 0x82082080u
    };

    do
    {
        codepoint = (codepoint << 6) + (unsigned char) **s;
        (*s)++;
        count++;
    } while ((**s & 0xc0) == 0x80);

    assert(count <= 6);
    return codepoint - offsets[count - 1];
}

// Convert the specified Latin-1 string to UTF-8
//
static char* convertLatin1toUTF8(const char* source)
{
    size_t size = 1;
    const char* sp;

    for (sp = source;  *sp;  sp++)
        size += (*sp & 0x80) ? 2 : 1;

    char* target = wsi_calloc(size, 1);
    char* tp = target;

    for (sp = source;  *sp;  sp++)
        tp += wsi_encode_urf8(tp, *sp);

    return target;
}

// Updates the cursor image according to its cursor mode
//
static void updateCursorImage(window_st* window)
{
    if (window->cursorMode == SC_CURSOR_NORMAL ||
        window->cursorMode == SC_CURSOR_CAPTURED)
    {
        if (window->cursor)
        {
            XDefineCursor(g_wsi.x11.display, window->x11.handle,
                          window->cursor->x11.handle);
        }
        else
            XUndefineCursor(g_wsi.x11.display, window->x11.handle);
    }
    else
    {
        XDefineCursor(g_wsi.x11.display, window->x11.handle,
                      g_wsi.x11.hiddenCursorHandle);
    }
}

// Grabs the cursor and confines it to the window
//
static void captureCursor(window_st* window)
{
    XGrabPointer(g_wsi.x11.display, window->x11.handle, True,
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                 GrabModeAsync, GrabModeAsync,
                 window->x11.handle,
                 None,
                 CurrentTime);
}

// Ungrabs the cursor
//
static void releaseCursor(void)
{
    XUngrabPointer(g_wsi.x11.display, CurrentTime);
}

// Enable XI2 raw mouse motion events
//
static void enableRawMouseMotion(window_st* window)
{
    XIEventMask em;
    unsigned char mask[XIMaskLen(XI_RawMotion)] = { 0 };

    em.deviceid = XIAllMasterDevices;
    em.mask_len = sizeof(mask);
    em.mask = mask;
    XISetMask(mask, XI_RawMotion);

    XISelectEvents(g_wsi.x11.display, g_wsi.x11.root, &em, 1);
}

// Disable XI2 raw mouse motion events
//
static void disableRawMouseMotion(window_st* window)
{
    XIEventMask em;
    unsigned char mask[] = { 0 };

    em.deviceid = XIAllMasterDevices;
    em.mask_len = sizeof(mask);
    em.mask = mask;

    XISelectEvents(g_wsi.x11.display, g_wsi.x11.root, &em, 1);
}

// Apply disabled cursor mode to a focused window
//
static void disableCursor(window_st* window)
{
    if (window->rawMouseMotion)
        enableRawMouseMotion(window);

    g_wsi.x11.disabledCursorWindow = window;
    x11_get_cursor_pos(window,
                         &g_wsi.x11.restoreCursorPosX,
                         &g_wsi.x11.restoreCursorPosY);
    updateCursorImage(window);
    wsi_center_cursor_in_content_area(window);
    captureCursor(window);
}

// Exit disabled cursor mode for the specified window
//
static void enableCursor(window_st* window)
{
    if (window->rawMouseMotion)
        disableRawMouseMotion(window);

    g_wsi.x11.disabledCursorWindow = NULL;
    releaseCursor();
    x11_set_cursor_pos(window,
                         g_wsi.x11.restoreCursorPosX,
                         g_wsi.x11.restoreCursorPosY);
    updateCursorImage(window);
}

// Clear its handle when the input context has been destroyed
//
static void inputContextDestroyCallback(XIC ic, XPointer clientData, XPointer callData)
{
    window_st* window = (window_st*) clientData;
    window->x11.ic = NULL;
}

// Create the X11 window (and its colormap)
//
static bool createNativeWindow(window_st* window,
                                   const wnd_config_st* wndconfig,
                                   Visual* visual, int depth)
{
    int width = wndconfig->width;
    int height = wndconfig->height;

    if (wndconfig->scaleToMonitor)
    {
        width *= g_wsi.x11.contentScaleX;
        height *= g_wsi.x11.contentScaleY;
    }

    // The dimensions must be nonzero, or a BadValue error results.
    width = wsi_max(1, width);
    height = wsi_max(1, height);

    int xpos = 0, ypos = 0;

    if (wndconfig->xpos != SC_ANY_POSITION && wndconfig->ypos != SC_ANY_POSITION)
    {
        xpos = wndconfig->xpos;
        ypos = wndconfig->ypos;
    }

    // Create a colormap based on the visual used by the current context
    window->x11.colormap = XCreateColormap(g_wsi.x11.display,
                                           g_wsi.x11.root,
                                           visual,
                                           AllocNone);

    window->x11.transparent = x11_IsVisualTransparent(visual);

    XSetWindowAttributes wa = { 0 };
    wa.colormap = window->x11.colormap;
    wa.event_mask = StructureNotifyMask | KeyPressMask | KeyReleaseMask |
                    PointerMotionMask | ButtonPressMask | ButtonReleaseMask |
                    ExposureMask | FocusChangeMask | VisibilityChangeMask |
                    EnterWindowMask | LeaveWindowMask | PropertyChangeMask;

    x11_GrabErrorHandler();

    window->x11.parent = g_wsi.x11.root;
    window->x11.handle = XCreateWindow(g_wsi.x11.display,
                                       g_wsi.x11.root,
                                       xpos, ypos,
                                       width, height,
                                       0,      // Border width
                                       depth,  // Color depth
                                       InputOutput,
                                       visual,
                                       CWBorderPixel | CWColormap | CWEventMask,
                                       &wa);

    x11_ReleaseErrorHandler();

    if (!window->x11.handle)
    {
        x11_InputError(SC_WSI_ERR_PLATFORM_ERROR,
                           "X11: Failed to create window");
        return false;
    }

    XSaveContext(g_wsi.x11.display,
                 window->x11.handle,
                 g_wsi.x11.context,
                 (XPointer) window);

    if (!wndconfig->decorated)
        x11_set_window_decorated(window, false);

    if (g_wsi.x11.NET_WM_STATE && !window->monitor)
    {
        Atom states[3];
        int count = 0;

        if (wndconfig->floating)
        {
            if (g_wsi.x11.NET_WM_STATE_ABOVE)
                states[count++] = g_wsi.x11.NET_WM_STATE_ABOVE;
        }

        if (wndconfig->maximized)
        {
            if (g_wsi.x11.NET_WM_STATE_MAXIMIZED_VERT &&
                g_wsi.x11.NET_WM_STATE_MAXIMIZED_HORZ)
            {
                states[count++] = g_wsi.x11.NET_WM_STATE_MAXIMIZED_VERT;
                states[count++] = g_wsi.x11.NET_WM_STATE_MAXIMIZED_HORZ;
                window->x11.maximized = true;
            }
        }

        if (count)
        {
            XChangeProperty(g_wsi.x11.display, window->x11.handle,
                            g_wsi.x11.NET_WM_STATE, XA_ATOM, 32,
                            PropModeReplace, (unsigned char*) states, count);
        }
    }

    // Declare the WM protocols supported by GLFW
    {
        Atom protocols[] =
        {
            g_wsi.x11.WM_DELETE_WINDOW,
            g_wsi.x11.NET_WM_PING
        };

        XSetWMProtocols(g_wsi.x11.display, window->x11.handle,
                        protocols, sizeof(protocols) / sizeof(Atom));
    }

    // Declare our PID
    {
        const long pid = getpid();

        XChangeProperty(g_wsi.x11.display,  window->x11.handle,
                        g_wsi.x11.NET_WM_PID, XA_CARDINAL, 32,
                        PropModeReplace,
                        (unsigned char*) &pid, 1);
    }

    if (g_wsi.x11.NET_WM_WINDOW_TYPE && g_wsi.x11.NET_WM_WINDOW_TYPE_NORMAL)
    {
        Atom type = g_wsi.x11.NET_WM_WINDOW_TYPE_NORMAL;
        XChangeProperty(g_wsi.x11.display,  window->x11.handle,
                        g_wsi.x11.NET_WM_WINDOW_TYPE, XA_ATOM, 32,
                        PropModeReplace, (unsigned char*) &type, 1);
    }

    // Set ICCCM WM_HINTS property
    {
        XWMHints* hints = XAllocWMHints();
        if (!hints)
        {
            impl_on_error(SC_WSI_ERR_OUT_OF_MEMORY,
                            "X11: Failed to allocate WM hints");
            return false;
        }

        hints->flags = StateHint;
        hints->initial_state = NormalState;

        XSetWMHints(g_wsi.x11.display, window->x11.handle, hints);
        XFree(hints);
    }

    // Set ICCCM WM_NORMAL_HINTS property
    {
        XSizeHints* hints = XAllocSizeHints();
        if (!hints)
        {
            impl_on_error(SC_WSI_ERR_OUT_OF_MEMORY, "X11: Failed to allocate size hints");
            return false;
        }

        if (!wndconfig->resizable)
        {
            hints->flags |= (PMinSize | PMaxSize);
            hints->min_width  = hints->max_width  = width;
            hints->min_height = hints->max_height = height;
        }

        // HACK: Explicitly setting PPosition to any value causes some WMs, notably
        //       Compiz and Metacity, to honor the position of unmapped windows
        if (wndconfig->xpos != SC_ANY_POSITION && wndconfig->ypos != SC_ANY_POSITION)
        {
            hints->flags |= PPosition;
            hints->x = 0;
            hints->y = 0;
        }

        hints->flags |= PWinGravity;
        hints->win_gravity = StaticGravity;

        XSetWMNormalHints(g_wsi.x11.display, window->x11.handle, hints);
        XFree(hints);
    }

    // Set ICCCM WM_CLASS property
    {
        XClassHint* hint = XAllocClassHint();

        if (strlen(wndconfig->x11.instanceName) &&
            strlen(wndconfig->x11.className))
        {
            hint->res_name = (char*) wndconfig->x11.instanceName;
            hint->res_class = (char*) wndconfig->x11.className;
        }
        else
        {
            const char* resourceName = getenv("RESOURCE_NAME");
            if (resourceName && strlen(resourceName))
                hint->res_name = (char*) resourceName;
            else if (strlen(window->title))
                hint->res_name = (char*) window->title;
            else
                hint->res_name = (char*) "glfw-application";

            if (strlen(window->title))
                hint->res_class = (char*) window->title;
            else
                hint->res_class = (char*) "GLFW-Application";
        }

        XSetClassHint(g_wsi.x11.display, window->x11.handle, hint);
        XFree(hint);
    }

    // Announce support for Xdnd (drag and drop)
    {
        const Atom version = _GLFW_XDND_VERSION;
        XChangeProperty(g_wsi.x11.display, window->x11.handle,
                        g_wsi.x11.XdndAware, XA_ATOM, 32,
                        PropModeReplace, (unsigned char*) &version, 1);
    }

    if (g_wsi.x11.im)
        x11_CreateInputContext(window);

    x11_set_window_title(window, window->title);
    x11_get_window_pos(window, &window->x11.xpos, &window->x11.ypos);
    x11_get_window_size(window, &window->x11.width, &window->x11.height);

    return true;
}

// Set the specified property to the selection converted to the requested target
//
static Atom writeTargetToProperty(const XSelectionRequestEvent* request)
{
    char* selectionString = NULL;
    const Atom formats[] = { g_wsi.x11.UTF8_STRING, XA_STRING };
    const int formatCount = sizeof(formats) / sizeof(formats[0]);

    if (request->selection == g_wsi.x11.PRIMARY)
        selectionString = g_wsi.x11.primarySelectionString;
    else
        selectionString = g_wsi.x11.clipboardString;

    if (request->property == None)
    {
        // The requester is a legacy client (ICCCM section 2.2)
        // We don't support legacy clients, so fail here
        return None;
    }

    if (request->target == g_wsi.x11.TARGETS)
    {
        // The list of supported targets was requested

        const Atom targets[] = { g_wsi.x11.TARGETS,
                                 g_wsi.x11.MULTIPLE,
                                 g_wsi.x11.UTF8_STRING,
                                 XA_STRING };

        XChangeProperty(g_wsi.x11.display,
                        request->requestor,
                        request->property,
                        XA_ATOM,
                        32,
                        PropModeReplace,
                        (unsigned char*) targets,
                        sizeof(targets) / sizeof(targets[0]));

        return request->property;
    }

    if (request->target == g_wsi.x11.MULTIPLE)
    {
        // Multiple conversions were requested

        Atom* targets;
        const unsigned long count =
            x11_GetWindowProperty(request->requestor,
                                      request->property,
                                      g_wsi.x11.ATOM_PAIR,
                                      (unsigned char**) &targets);

        for (unsigned long i = 0;  i < count;  i += 2)
        {
            int j;

            for (j = 0;  j < formatCount;  j++)
            {
                if (targets[i] == formats[j])
                    break;
            }

            if (j < formatCount)
            {
                XChangeProperty(g_wsi.x11.display,
                                request->requestor,
                                targets[i + 1],
                                targets[i],
                                8,
                                PropModeReplace,
                                (unsigned char *) selectionString,
                                strlen(selectionString));
            }
            else
                targets[i + 1] = None;
        }

        XChangeProperty(g_wsi.x11.display,
                        request->requestor,
                        request->property,
                        g_wsi.x11.ATOM_PAIR,
                        32,
                        PropModeReplace,
                        (unsigned char*) targets,
                        count);

        XFree(targets);

        return request->property;
    }

    if (request->target == g_wsi.x11.SAVE_TARGETS)
    {
        // The request is a check whether we support SAVE_TARGETS
        // It should be handled as a no-op side effect target

        XChangeProperty(g_wsi.x11.display,
                        request->requestor,
                        request->property,
                        g_wsi.x11.NULL_,
                        32,
                        PropModeReplace,
                        NULL,
                        0);

        return request->property;
    }

    // Conversion to a data target was requested

    for (int i = 0;  i < formatCount;  i++)
    {
        if (request->target == formats[i])
        {
            // The requested target is one we support

            XChangeProperty(g_wsi.x11.display,
                            request->requestor,
                            request->property,
                            request->target,
                            8,
                            PropModeReplace,
                            (unsigned char *) selectionString,
                            strlen(selectionString));

            return request->property;
        }
    }

    // The requested target is not supported

    return None;
}

static void handleSelectionRequest(XEvent* event)
{
    const XSelectionRequestEvent* request = &event->xselectionrequest;

    XEvent reply = { SelectionNotify };
    reply.xselection.property = writeTargetToProperty(request);
    reply.xselection.display = request->display;
    reply.xselection.requestor = request->requestor;
    reply.xselection.selection = request->selection;
    reply.xselection.target = request->target;
    reply.xselection.time = request->time;

    XSendEvent(g_wsi.x11.display, request->requestor, False, 0, &reply);
}

static const char* getSelectionString(Atom selection)
{
    char** selectionString = NULL;
    const Atom targets[] = { g_wsi.x11.UTF8_STRING, XA_STRING };
    const size_t targetCount = sizeof(targets) / sizeof(targets[0]);

    if (selection == g_wsi.x11.PRIMARY)
        selectionString = &g_wsi.x11.primarySelectionString;
    else
        selectionString = &g_wsi.x11.clipboardString;

    if (XGetSelectionOwner(g_wsi.x11.display, selection) ==
        g_wsi.x11.helperWindowHandle)
    {
        // Instead of doing a large number of X round-trips just to put this
        // string into a window property and then read it back, just return it
        return *selectionString;
    }

    wsi_free(*selectionString);
    *selectionString = NULL;

    for (size_t i = 0;  i < targetCount;  i++)
    {
        char* data;
        Atom actualType;
        int actualFormat;
        unsigned long itemCount, bytesAfter;
        XEvent notification, dummy;

        XConvertSelection(g_wsi.x11.display,
                          selection,
                          targets[i],
                          g_wsi.x11.GLFW_SELECTION,
                          g_wsi.x11.helperWindowHandle,
                          CurrentTime);

        while (!XCheckTypedWindowEvent(g_wsi.x11.display,
                                       g_wsi.x11.helperWindowHandle,
                                       SelectionNotify,
                                       &notification))
        {
            waitForX11Event(NULL);
        }

        if (notification.xselection.property == None)
            continue;

        XCheckIfEvent(g_wsi.x11.display,
                      &dummy,
                      isSelPropNewValueNotify,
                      (XPointer) &notification);

        XGetWindowProperty(g_wsi.x11.display,
                           notification.xselection.requestor,
                           notification.xselection.property,
                           0,
                           LONG_MAX,
                           True,
                           AnyPropertyType,
                           &actualType,
                           &actualFormat,
                           &itemCount,
                           &bytesAfter,
                           (unsigned char**) &data);

        if (actualType == g_wsi.x11.INCR)
        {
            size_t size = 1;
            char* string = NULL;

            for (;;)
            {
                while (!XCheckIfEvent(g_wsi.x11.display,
                                      &dummy,
                                      isSelPropNewValueNotify,
                                      (XPointer) &notification))
                {
                    waitForX11Event(NULL);
                }

                XFree(data);
                XGetWindowProperty(g_wsi.x11.display,
                                   notification.xselection.requestor,
                                   notification.xselection.property,
                                   0,
                                   LONG_MAX,
                                   True,
                                   AnyPropertyType,
                                   &actualType,
                                   &actualFormat,
                                   &itemCount,
                                   &bytesAfter,
                                   (unsigned char**) &data);

                if (itemCount)
                {
                    size += itemCount;
                    string = wsi_realloc(string, size);
                    string[size - itemCount - 1] = '\0';
                    strcat(string, data);
                }

                if (!itemCount)
                {
                    if (string)
                    {
                        if (targets[i] == XA_STRING)
                        {
                            *selectionString = convertLatin1toUTF8(string);
                            wsi_free(string);
                        }
                        else
                            *selectionString = string;
                    }

                    break;
                }
            }
        }
        else if (actualType == targets[i])
        {
            if (targets[i] == XA_STRING)
                *selectionString = convertLatin1toUTF8(data);
            else
                *selectionString = wsi_strdup(data);
        }

        XFree(data);

        if (*selectionString)
            break;
    }

    if (!*selectionString)
    {
        impl_on_error(SC_WSI_ERR_FORMAT_UNAVAILABLE,
                        "X11: Failed to convert selection to string");
    }

    return *selectionString;
}

// Make the specified window and its video mode active on its monitor
//
static void acquireMonitor(window_st* window)
{
    if (g_wsi.x11.saver.count == 0)
    {
        // Remember old screen saver settings
        XGetScreenSaver(g_wsi.x11.display,
                        &g_wsi.x11.saver.timeout,
                        &g_wsi.x11.saver.interval,
                        &g_wsi.x11.saver.blanking,
                        &g_wsi.x11.saver.exposure);

        // Disable screen saver
        XSetScreenSaver(g_wsi.x11.display, 0, 0, DontPreferBlanking,
                        DefaultExposures);
    }

    if (!window->monitor->window)
        g_wsi.x11.saver.count++;

    x11_SetVideoMode(window->monitor, &window->videoMode);

    if (window->x11.overrideRedirect)
    {
        int xpos, ypos;
        GLFWvidmode mode;

        // Manually position the window over its monitor
        x11_get_monitor_pos(window->monitor, &xpos, &ypos);
        x11_get_video_mode(window->monitor, &mode);

        XMoveResizeWindow(g_wsi.x11.display, window->x11.handle,
                          xpos, ypos, mode.width, mode.height);
    }

    impl_on_monitor_window(window->monitor, window);
}

// Remove the window and restore the original video mode
//
static void releaseMonitor(window_st* window)
{
    if (window->monitor->window != window)
        return;

    impl_on_monitor_window(window->monitor, NULL);
    x11_RestoreVideoMode(window->monitor);

    g_wsi.x11.saver.count--;

    if (g_wsi.x11.saver.count == 0)
    {
        // Restore old screen saver settings
        XSetScreenSaver(g_wsi.x11.display,
                        g_wsi.x11.saver.timeout,
                        g_wsi.x11.saver.interval,
                        g_wsi.x11.saver.blanking,
                        g_wsi.x11.saver.exposure);
    }
}

// Process the specified X event
//
static void processEvent(XEvent *event)
{
    int keycode = 0;
    Bool filtered = False;

    // HACK: Save scancode as some IMs clear the field in XFilterEvent
    if (event->type == KeyPress || event->type == KeyRelease)
        keycode = event->xkey.keycode;

    filtered = XFilterEvent(event, None);

    if (g_wsi.x11.randr.available)
    {
        if (event->type == g_wsi.x11.randr.eventBase + RRNotify)
        {
            XRRUpdateConfiguration(event);
            x11_poll_monitors();
            return;
        }
    }

    if (g_wsi.x11.xkb.available)
    {
        if (event->type == g_wsi.x11.xkb.eventBase + XkbEventCode)
        {
            if (((XkbEvent*) event)->any.xkb_type == XkbStateNotify &&
                (((XkbEvent*) event)->state.changed & XkbGroupStateMask))
            {
                g_wsi.x11.xkb.group = ((XkbEvent*) event)->state.group;
            }

            return;
        }
    }

    if (event->type == GenericEvent)
    {
        if (g_wsi.x11.xi.available)
        {
            window_st* window = g_wsi.x11.disabledCursorWindow;

            if (window &&
                window->rawMouseMotion &&
                event->xcookie.extension == g_wsi.x11.xi.majorOpcode &&
                XGetEventData(g_wsi.x11.display, &event->xcookie) &&
                event->xcookie.evtype == XI_RawMotion)
            {
                XIRawEvent* re = event->xcookie.data;
                if (re->valuators.mask_len)
                {
                    const double* values = re->raw_values;
                    double xpos = window->virtualCursorPosX;
                    double ypos = window->virtualCursorPosY;

                    if (XIMaskIsSet(re->valuators.mask, 0))
                    {
                        xpos += *values;
                        values++;
                    }

                    if (XIMaskIsSet(re->valuators.mask, 1))
                        ypos += *values;

                    impl_on_cursor_pos(window, xpos, ypos);
                }
            }

            XFreeEventData(g_wsi.x11.display, &event->xcookie);
        }

        return;
    }

    if (event->type == SelectionRequest)
    {
        handleSelectionRequest(event);
        return;
    }

    window_st* window = NULL;
    if (XFindContext(g_wsi.x11.display,
                     event->xany.window,
                     g_wsi.x11.context,
                     (XPointer*) &window) != 0)
    {
        // This is an event for a window that has already been destroyed
        return;
    }

    switch (event->type)
    {
        case ReparentNotify:
        {
            window->x11.parent = event->xreparent.parent;
            return;
        }

        case KeyPress:
        {
            const int key = translateKey(keycode);
            const int mods = translateState(event->xkey.state);
            const int plain = !(mods & (SC_MOD_CONTROL | SC_MOD_ALT));

            if (window->x11.ic)
            {
                // HACK: Do not report the key press events duplicated by XIM
                //       Duplicate key releases are filtered out implicitly by
                //       the GLFW key repeat logic in impl_on_key
                //       A timestamp per key is used to handle simultaneous keys
                // NOTE: Always allow the first event for each key through
                //       (the server never sends a timestamp of zero)
                // NOTE: Timestamp difference is compared to handle wrap-around
                Time diff = event->xkey.time - window->x11.keyPressTimes[keycode];
                if (diff == event->xkey.time || (diff > 0 && diff < ((Time)1 << 31)))
                {
                    if (keycode)
                        impl_on_key(window, key, keycode, SC_PRESS, mods);

                    window->x11.keyPressTimes[keycode] = event->xkey.time;
                }

                if (!filtered)
                {
                    int count;
                    Status status;
                    char buffer[100];
                    char* chars = buffer;

                    count = Xutf8LookupString(window->x11.ic,
                                              &event->xkey,
                                              buffer, sizeof(buffer) - 1,
                                              NULL, &status);

                    if (status == XBufferOverflow)
                    {
                        chars = wsi_calloc(count + 1, 1);
                        count = Xutf8LookupString(window->x11.ic,
                                                  &event->xkey,
                                                  chars, count,
                                                  NULL, &status);
                    }

                    if (status == XLookupChars || status == XLookupBoth)
                    {
                        const char* c = chars;
                        chars[count] = '\0';
                        while (c - chars < count)
                            impl_on_chr(window, decodeUTF8(&c), mods, plain);
                    }

                    if (chars != buffer)
                        wsi_free(chars);
                }
            }
            else
            {
                KeySym keysym;
                XLookupString(&event->xkey, NULL, 0, &keysym, NULL);

                impl_on_key(window, key, keycode, SC_PRESS, mods);

                const uint32_t codepoint = x11_KeySym2Unicode(keysym);
                if (codepoint != GLFW_INVALID_CODEPOINT)
                    impl_on_chr(window, codepoint, mods, plain);
            }

            return;
        }

        case KeyRelease:
        {
            const int key = translateKey(keycode);
            const int mods = translateState(event->xkey.state);

            if (!g_wsi.x11.xkb.detectable)
            {
                // HACK: Key repeat events will arrive as KeyRelease/KeyPress
                //       pairs with similar or identical time stamps
                //       The key repeat logic in impl_on_key expects only key
                //       presses to repeat, so detect and discard release events
                if (XEventsQueued(g_wsi.x11.display, QueuedAfterReading))
                {
                    XEvent next;
                    XPeekEvent(g_wsi.x11.display, &next);

                    if (next.type == KeyPress &&
                        next.xkey.window == event->xkey.window &&
                        next.xkey.keycode == keycode)
                    {
                        // HACK: The time of repeat events sometimes doesn't
                        //       match that of the press event, so add an
                        //       epsilon
                        //       Toshiyuki Takahashi can press a button
                        //       16 times per second so it's fairly safe to
                        //       assume that no human is pressing the key 50
                        //       times per second (value is ms)
                        if ((next.xkey.time - event->xkey.time) < 20)
                        {
                            // This is very likely a server-generated key repeat
                            // event, so ignore it
                            return;
                        }
                    }
                }
            }

            impl_on_key(window, key, keycode, SC_RELEASE, mods);
            return;
        }

        case ButtonPress:
        {
            const int mods = translateState(event->xbutton.state);

            if (event->xbutton.button == Button1)
                impl_on_mouse_click(window, SC_MOUSE_BUTTON_LEFT, SC_PRESS, mods);
            else if (event->xbutton.button == Button2)
                impl_on_mouse_click(window, SC_MOUSE_BUTTON_MIDDLE, SC_PRESS, mods);
            else if (event->xbutton.button == Button3)
                impl_on_mouse_click(window, SC_MOUSE_BUTTON_RIGHT, SC_PRESS, mods);

            // Modern X provides scroll events as mouse button presses
            else if (event->xbutton.button == Button4)
                impl_on_scroll(window, 0.0, 1.0);
            else if (event->xbutton.button == Button5)
                impl_on_scroll(window, 0.0, -1.0);
            else if (event->xbutton.button == Button6)
                impl_on_scroll(window, 1.0, 0.0);
            else if (event->xbutton.button == Button7)
                impl_on_scroll(window, -1.0, 0.0);

            else
            {
                // Additional buttons after 7 are treated as regular buttons
                // We subtract 4 to fill the gap left by scroll input above
                impl_on_mouse_click(window,
                                     event->xbutton.button - Button1 - 4,
                                     SC_PRESS,
                                     mods);
            }

            return;
        }

        case ButtonRelease:
        {
            const int mods = translateState(event->xbutton.state);

            if (event->xbutton.button == Button1)
            {
                impl_on_mouse_click(window,
                                     SC_MOUSE_BUTTON_LEFT,
                                     SC_RELEASE,
                                     mods);
            }
            else if (event->xbutton.button == Button2)
            {
                impl_on_mouse_click(window,
                                     SC_MOUSE_BUTTON_MIDDLE,
                                     SC_RELEASE,
                                     mods);
            }
            else if (event->xbutton.button == Button3)
            {
                impl_on_mouse_click(window,
                                     SC_MOUSE_BUTTON_RIGHT,
                                     SC_RELEASE,
                                     mods);
            }
            else if (event->xbutton.button > Button7)
            {
                // Additional buttons after 7 are treated as regular buttons
                // We subtract 4 to fill the gap left by scroll input above
                impl_on_mouse_click(window,
                                     event->xbutton.button - Button1 - 4,
                                     SC_RELEASE,
                                     mods);
            }

            return;
        }

        case EnterNotify:
        {
            // XEnterWindowEvent is XCrossingEvent
            const int x = event->xcrossing.x;
            const int y = event->xcrossing.y;

            // HACK: This is a workaround for WMs (KWM, Fluxbox) that otherwise
            //       ignore the defined cursor for hidden cursor mode
            if (window->cursorMode == SC_CURSOR_HIDDEN)
                updateCursorImage(window);

            impl_on_cursor_enter(window, true);
            impl_on_cursor_pos(window, x, y);

            window->x11.lastCursorPosX = x;
            window->x11.lastCursorPosY = y;
            return;
        }

        case LeaveNotify:
        {
            impl_on_cursor_enter(window, false);
            return;
        }

        case MotionNotify:
        {
            const int x = event->xmotion.x;
            const int y = event->xmotion.y;

            if (x != window->x11.warpCursorPosX ||
                y != window->x11.warpCursorPosY)
            {
                // The cursor was moved by something other than GLFW

                if (window->cursorMode == SC_CURSOR_DISABLED)
                {
                    if (g_wsi.x11.disabledCursorWindow != window)
                        return;
                    if (window->rawMouseMotion)
                        return;

                    const int dx = x - window->x11.lastCursorPosX;
                    const int dy = y - window->x11.lastCursorPosY;

                    impl_on_cursor_pos(window,
                                        window->virtualCursorPosX + dx,
                                        window->virtualCursorPosY + dy);
                }
                else
                    impl_on_cursor_pos(window, x, y);
            }

            window->x11.lastCursorPosX = x;
            window->x11.lastCursorPosY = y;
            return;
        }

        case ConfigureNotify:
        {
            if (event->xconfigure.width != window->x11.width ||
                event->xconfigure.height != window->x11.height)
            {
                window->x11.width = event->xconfigure.width;
                window->x11.height = event->xconfigure.height;

                impl_on_win_size(window,
                                     event->xconfigure.width,
                                     event->xconfigure.height);
            }

            int xpos = event->xconfigure.x;
            int ypos = event->xconfigure.y;

            // NOTE: ConfigureNotify events from the server are in local
            //       coordinates, so if we are reparented we need to translate
            //       the position into root (screen) coordinates
            if (!event->xany.send_event && window->x11.parent != g_wsi.x11.root)
            {
                x11_GrabErrorHandler();

                Window dummy;
                XTranslateCoordinates(g_wsi.x11.display,
                                      window->x11.parent,
                                      g_wsi.x11.root,
                                      xpos, ypos,
                                      &xpos, &ypos,
                                      &dummy);

                x11_ReleaseErrorHandler();
                if (g_wsi.x11.errorCode == BadWindow)
                    return;
            }

            if (xpos != window->x11.xpos || ypos != window->x11.ypos)
            {
                window->x11.xpos = xpos;
                window->x11.ypos = ypos;

                impl_on_win_pos(window, xpos, ypos);
            }

            return;
        }

        case ClientMessage:
        {
            // Custom client message, probably from the window manager

            if (filtered)
                return;

            if (event->xclient.message_type == None)
                return;

            if (event->xclient.message_type == g_wsi.x11.WM_PROTOCOLS)
            {
                const Atom protocol = event->xclient.data.l[0];
                if (protocol == None)
                    return;

                if (protocol == g_wsi.x11.WM_DELETE_WINDOW)
                {
                    // The window manager was asked to close the window, for
                    // example by the user pressing a 'close' window decoration
                    // button
                    impl_on_win_close_req(window);
                }
                else if (protocol == g_wsi.x11.NET_WM_PING)
                {
                    // The window manager is pinging the application to ensure
                    // it's still responding to events

                    XEvent reply = *event;
                    reply.xclient.window = g_wsi.x11.root;

                    XSendEvent(g_wsi.x11.display, g_wsi.x11.root,
                               False,
                               SubstructureNotifyMask | SubstructureRedirectMask,
                               &reply);
                }
            }
            else if (event->xclient.message_type == g_wsi.x11.XdndEnter)
            {
                // A drag operation has entered the window
                unsigned long count;
                Atom* formats = NULL;
                const bool list = event->xclient.data.l[1] & 1;

                g_wsi.x11.xdnd.source  = event->xclient.data.l[0];
                g_wsi.x11.xdnd.version = event->xclient.data.l[1] >> 24;
                g_wsi.x11.xdnd.format  = None;

                if (g_wsi.x11.xdnd.version > _GLFW_XDND_VERSION)
                    return;

                if (list)
                {
                    count = x11_GetWindowProperty(g_wsi.x11.xdnd.source,
                                                      g_wsi.x11.XdndTypeList,
                                                      XA_ATOM,
                                                      (unsigned char**) &formats);
                }
                else
                {
                    count = 3;
                    formats = (Atom*) event->xclient.data.l + 2;
                }

                for (unsigned int i = 0;  i < count;  i++)
                {
                    if (formats[i] == g_wsi.x11.text_uri_list)
                    {
                        g_wsi.x11.xdnd.format = g_wsi.x11.text_uri_list;
                        break;
                    }
                }

                if (list && formats)
                    XFree(formats);
            }
            else if (event->xclient.message_type == g_wsi.x11.XdndDrop)
            {
                // The drag operation has finished by dropping on the window
                Time time = CurrentTime;

                if (g_wsi.x11.xdnd.version > _GLFW_XDND_VERSION)
                    return;

                if (g_wsi.x11.xdnd.format)
                {
                    if (g_wsi.x11.xdnd.version >= 1)
                        time = event->xclient.data.l[2];

                    // Request the chosen format from the source window
                    XConvertSelection(g_wsi.x11.display,
                                      g_wsi.x11.XdndSelection,
                                      g_wsi.x11.xdnd.format,
                                      g_wsi.x11.XdndSelection,
                                      window->x11.handle,
                                      time);
                }
                else if (g_wsi.x11.xdnd.version >= 2)
                {
                    XEvent reply = { ClientMessage };
                    reply.xclient.window = g_wsi.x11.xdnd.source;
                    reply.xclient.message_type = g_wsi.x11.XdndFinished;
                    reply.xclient.format = 32;
                    reply.xclient.data.l[0] = window->x11.handle;
                    reply.xclient.data.l[1] = 0; // The drag was rejected
                    reply.xclient.data.l[2] = None;

                    XSendEvent(g_wsi.x11.display, g_wsi.x11.xdnd.source,
                               False, NoEventMask, &reply);
                    XFlush(g_wsi.x11.display);
                }
            }
            else if (event->xclient.message_type == g_wsi.x11.XdndPosition)
            {
                // The drag operation has moved over the window
                const int xabs = (event->xclient.data.l[2] >> 16) & 0xffff;
                const int yabs = (event->xclient.data.l[2]) & 0xffff;
                Window dummy;
                int xpos, ypos;

                if (g_wsi.x11.xdnd.version > _GLFW_XDND_VERSION)
                    return;

                XTranslateCoordinates(g_wsi.x11.display,
                                      g_wsi.x11.root,
                                      window->x11.handle,
                                      xabs, yabs,
                                      &xpos, &ypos,
                                      &dummy);

                impl_on_cursor_pos(window, xpos, ypos);

                XEvent reply = { ClientMessage };
                reply.xclient.window = g_wsi.x11.xdnd.source;
                reply.xclient.message_type = g_wsi.x11.XdndStatus;
                reply.xclient.format = 32;
                reply.xclient.data.l[0] = window->x11.handle;
                reply.xclient.data.l[2] = 0; // Specify an empty rectangle
                reply.xclient.data.l[3] = 0;

                if (g_wsi.x11.xdnd.format)
                {
                    // Reply that we are ready to copy the dragged data
                    reply.xclient.data.l[1] = 1; // Accept with no rectangle
                    if (g_wsi.x11.xdnd.version >= 2)
                        reply.xclient.data.l[4] = g_wsi.x11.XdndActionCopy;
                }

                XSendEvent(g_wsi.x11.display, g_wsi.x11.xdnd.source,
                           False, NoEventMask, &reply);
                XFlush(g_wsi.x11.display);
            }

            return;
        }

        case SelectionNotify:
        {
            if (event->xselection.property == g_wsi.x11.XdndSelection)
            {
                // The converted data from the drag operation has arrived
                char* data;
                const unsigned long result =
                    x11_GetWindowProperty(event->xselection.requestor,
                                              event->xselection.property,
                                              event->xselection.target,
                                              (unsigned char**) &data);

                if (result)
                {
                    int count;
                    char** paths = wsi_parse_url_list(data, &count);

                    impl_on_drop(window, count, (const char**) paths);

                    for (int i = 0;  i < count;  i++)
                        wsi_free(paths[i]);
                    wsi_free(paths);
                }

                if (data)
                    XFree(data);

                if (g_wsi.x11.xdnd.version >= 2)
                {
                    XEvent reply = { ClientMessage };
                    reply.xclient.window = g_wsi.x11.xdnd.source;
                    reply.xclient.message_type = g_wsi.x11.XdndFinished;
                    reply.xclient.format = 32;
                    reply.xclient.data.l[0] = window->x11.handle;
                    reply.xclient.data.l[1] = result;
                    reply.xclient.data.l[2] = g_wsi.x11.XdndActionCopy;

                    XSendEvent(g_wsi.x11.display, g_wsi.x11.xdnd.source,
                               False, NoEventMask, &reply);
                    XFlush(g_wsi.x11.display);
                }
            }

            return;
        }

        case FocusIn:
        {
            if (event->xfocus.mode == NotifyGrab ||
                event->xfocus.mode == NotifyUngrab)
            {
                // Ignore focus events from popup indicator windows, window menu
                // key chords and window dragging
                return;
            }

            if (window->cursorMode == SC_CURSOR_DISABLED)
                disableCursor(window);
            else if (window->cursorMode == SC_CURSOR_CAPTURED)
                captureCursor(window);

            if (window->x11.ic)
                XSetICFocus(window->x11.ic);

            impl_on_win_focus(window, true);
            return;
        }

        case FocusOut:
        {
            if (event->xfocus.mode == NotifyGrab ||
                event->xfocus.mode == NotifyUngrab)
            {
                // Ignore focus events from popup indicator windows, window menu
                // key chords and window dragging
                return;
            }

            if (window->cursorMode == SC_CURSOR_DISABLED)
                enableCursor(window);
            else if (window->cursorMode == SC_CURSOR_CAPTURED)
                releaseCursor();

            if (window->x11.ic)
                XUnsetICFocus(window->x11.ic);

            if (window->monitor && window->autoIconify)
                x11_iconify_window(window);

            impl_on_win_focus(window, false);
            return;
        }

        case Expose:
        {
            impl_on_win_damage(window);
            return;
        }

        case PropertyNotify:
        {
            if (event->xproperty.state != PropertyNewValue)
                return;

            if (event->xproperty.atom == g_wsi.x11.WM_STATE)
            {
                const int state = getWindowState(window);
                if (state != IconicState && state != NormalState)
                    return;

                const bool iconified = (state == IconicState);
                if (window->x11.iconified != iconified)
                {
                    if (window->monitor)
                    {
                        if (iconified)
                            releaseMonitor(window);
                        else
                            acquireMonitor(window);
                    }

                    window->x11.iconified = iconified;
                    impl_on_win_iconify(window, iconified);
                }
            }
            else if (event->xproperty.atom == g_wsi.x11.NET_WM_STATE)
            {
                const bool maximized = x11_window_maximized(window);
                if (window->x11.maximized != maximized)
                {
                    window->x11.maximized = maximized;
                    impl_on_win_maximize(window, maximized);
                }
            }

            return;
        }

        case DestroyNotify:
            return;
    }
}


//////////////////////////////////////////////////////////////////////////
//////                       GLFW internal API                      //////
//////////////////////////////////////////////////////////////////////////

// Retrieve a single window property of the specified type
// Inspired by fghGetWindowProperty from freeglut
//
unsigned long x11_GetWindowProperty(Window window,
                                        Atom property,
                                        Atom type,
                                        unsigned char** value)
{
    Atom actualType;
    int actualFormat;
    unsigned long itemCount, bytesAfter;

    XGetWindowProperty(g_wsi.x11.display,
                       window,
                       property,
                       0,
                       LONG_MAX,
                       False,
                       type,
                       &actualType,
                       &actualFormat,
                       &itemCount,
                       &bytesAfter,
                       value);

    return itemCount;
}

bool x11_IsVisualTransparent(Visual* visual)
{
    if (!g_wsi.x11.xrender.available)
        return false;

    XRenderPictFormat* pf = XRenderFindVisualFormat(g_wsi.x11.display, visual);
    return pf && pf->direct.alphaMask;
}

// Push contents of our selection to clipboard manager
//
void x11_PushSelectionToManager(void)
{
    XConvertSelection(g_wsi.x11.display,
                      g_wsi.x11.CLIPBOARD_MANAGER,
                      g_wsi.x11.SAVE_TARGETS,
                      None,
                      g_wsi.x11.helperWindowHandle,
                      CurrentTime);

    for (;;)
    {
        XEvent event;

        while (XCheckIfEvent(g_wsi.x11.display, &event, isSelectionEvent, NULL))
        {
            switch (event.type)
            {
                case SelectionRequest:
                    handleSelectionRequest(&event);
                    break;

                case SelectionNotify:
                {
                    if (event.xselection.target == g_wsi.x11.SAVE_TARGETS)
                    {
                        // This means one of two things; either the selection
                        // was not owned, which means there is no clipboard
                        // manager, or the transfer to the clipboard manager has
                        // completed
                        // In either case, it means we are done here
                        return;
                    }

                    break;
                }
            }
        }

        waitForX11Event(NULL);
    }
}

void x11_CreateInputContext(window_st* window)
{
    XIMCallback callback;
    callback.callback = (XIMProc) inputContextDestroyCallback;
    callback.client_data = (XPointer) window;

    window->x11.ic = XCreateIC(g_wsi.x11.im,
                               XNInputStyle,
                               XIMPreeditNothing | XIMStatusNothing,
                               XNClientWindow,
                               window->x11.handle,
                               XNFocusWindow,
                               window->x11.handle,
                               XNDestroyCallback,
                               &callback,
                               NULL);

    if (window->x11.ic)
    {
        XWindowAttributes attribs;
        XGetWindowAttributes(g_wsi.x11.display, window->x11.handle, &attribs);

        unsigned long filter = 0;
        if (XGetICValues(window->x11.ic, XNFilterEvents, &filter, NULL) == NULL)
        {
            XSelectInput(g_wsi.x11.display,
                         window->x11.handle,
                         attribs.your_event_mask | filter);
        }
    }
}


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

bool x11_create_window(window_st* window,
                              const wnd_config_st* wndconfig)
{
    Visual* visual = DefaultVisual(g_wsi.x11.display, g_wsi.x11.screen);
    int depth = DefaultDepth(g_wsi.x11.display, g_wsi.x11.screen);

    if (!createNativeWindow(window, wndconfig, visual, depth))
        return false;

    if (wndconfig->mousePassthrough)
        x11_set_window_mouse_passthrough(window, true);

    if (window->monitor)
    {
        x11_show_window(window);
        updateWindowMode(window);
        acquireMonitor(window);

        if (wndconfig->centerCursor)
            wsi_center_cursor_in_content_area(window);
    }
    else
    {
        if (wndconfig->visible)
        {
            x11_show_window(window);
            if (wndconfig->focused)
                x11_focus_window(window);
        }
    }

    XFlush(g_wsi.x11.display);
    return true;
}

void x11_destroy_window(window_st* window)
{
    if (g_wsi.x11.disabledCursorWindow == window)
        enableCursor(window);

    if (window->monitor)
        releaseMonitor(window);

    if (window->x11.ic)
    {
        XDestroyIC(window->x11.ic);
        window->x11.ic = NULL;
    }

    if (window->x11.handle)
    {
        XDeleteContext(g_wsi.x11.display, window->x11.handle, g_wsi.x11.context);
        XUnmapWindow(g_wsi.x11.display, window->x11.handle);
        XDestroyWindow(g_wsi.x11.display, window->x11.handle);
        window->x11.handle = (Window) 0;
    }

    if (window->x11.colormap)
    {
        XFreeColormap(g_wsi.x11.display, window->x11.colormap);
        window->x11.colormap = (Colormap) 0;
    }

    XFlush(g_wsi.x11.display);
}

void x11_set_window_title(window_st* window, const char* title)
{
    if (g_wsi.x11.xlib.utf8)
    {
        Xutf8SetWMProperties(g_wsi.x11.display,
                             window->x11.handle,
                             title, title,
                             NULL, 0,
                             NULL, NULL, NULL);
    }

    XChangeProperty(g_wsi.x11.display,  window->x11.handle,
                    g_wsi.x11.NET_WM_NAME, g_wsi.x11.UTF8_STRING, 8,
                    PropModeReplace,
                    (unsigned char*) title, strlen(title));

    XChangeProperty(g_wsi.x11.display,  window->x11.handle,
                    g_wsi.x11.NET_WM_ICON_NAME, g_wsi.x11.UTF8_STRING, 8,
                    PropModeReplace,
                    (unsigned char*) title, strlen(title));

    XFlush(g_wsi.x11.display);
}

void x11_set_window_icon(window_st* window, int count, const GLFWimage* images)
{
    if (count)
    {
        int longCount = 0;

        for (int i = 0;  i < count;  i++)
            longCount += 2 + images[i].width * images[i].height;

        unsigned long* icon = wsi_calloc(longCount, sizeof(unsigned long));
        unsigned long* target = icon;

        for (int i = 0;  i < count;  i++)
        {
            *target++ = images[i].width;
            *target++ = images[i].height;

            for (int j = 0;  j < images[i].width * images[i].height;  j++)
            {
                *target++ = (((unsigned long) images[i].pixels[j * 4 + 0]) << 16) |
                            (((unsigned long) images[i].pixels[j * 4 + 1]) <<  8) |
                            (((unsigned long) images[i].pixels[j * 4 + 2]) <<  0) |
                            (((unsigned long) images[i].pixels[j * 4 + 3]) << 24);
            }
        }

        // NOTE: XChangeProperty expects 32-bit values like the image data above to be
        //       placed in the 32 least significant bits of individual longs.  This is
        //       true even if long is 64-bit and a WM protocol calls for "packed" data.
        //       This is because of a historical mistake that then became part of the Xlib
        //       ABI.  Xlib will pack these values into a regular array of 32-bit values
        //       before sending it over the wire.
        XChangeProperty(g_wsi.x11.display, window->x11.handle,
                        g_wsi.x11.NET_WM_ICON,
                        XA_CARDINAL, 32,
                        PropModeReplace,
                        (unsigned char*) icon,
                        longCount);

        wsi_free(icon);
    }
    else
    {
        XDeleteProperty(g_wsi.x11.display, window->x11.handle,
                        g_wsi.x11.NET_WM_ICON);
    }

    XFlush(g_wsi.x11.display);
}

void x11_get_window_pos(window_st* window, int* xpos, int* ypos)
{
    Window dummy;
    int x, y;

    XTranslateCoordinates(g_wsi.x11.display, window->x11.handle, g_wsi.x11.root,
                          0, 0, &x, &y, &dummy);

    if (xpos)
        *xpos = x;
    if (ypos)
        *ypos = y;
}

void x11_set_window_pos(window_st* window, int xpos, int ypos)
{
    // HACK: Explicitly setting PPosition to any value causes some WMs, notably
    //       Compiz and Metacity, to honor the position of unmapped windows
    if (!x11_window_visible(window))
    {
        long supplied;
        XSizeHints* hints = XAllocSizeHints();

        if (XGetWMNormalHints(g_wsi.x11.display, window->x11.handle, hints, &supplied))
        {
            hints->flags |= PPosition;
            hints->x = hints->y = 0;

            XSetWMNormalHints(g_wsi.x11.display, window->x11.handle, hints);
        }

        XFree(hints);
    }

    XMoveWindow(g_wsi.x11.display, window->x11.handle, xpos, ypos);
    XFlush(g_wsi.x11.display);
}

void x11_get_window_size(window_st* window, int* width, int* height)
{
    XWindowAttributes attribs;
    XGetWindowAttributes(g_wsi.x11.display, window->x11.handle, &attribs);

    if (width)
        *width = attribs.width;
    if (height)
        *height = attribs.height;
}

void x11_set_window_size(window_st* window, int width, int height)
{
    // The dimensions must be nonzero, or a BadValue error results
    width = wsi_max(1, width);
    height = wsi_max(1, height);

    if (window->monitor)
    {
        if (window->monitor->window == window)
            acquireMonitor(window);
    }
    else
    {
        if (!window->resizable)
            updateNormalHints(window, width, height);

        XResizeWindow(g_wsi.x11.display, window->x11.handle, width, height);
    }

    XFlush(g_wsi.x11.display);
}

void x11_set_window_size_limits(window_st* window,
                                 int minwidth, int minheight,
                                 int maxwidth, int maxheight)
{
    int width, height;
    x11_get_window_size(window, &width, &height);
    updateNormalHints(window, width, height);
    XFlush(g_wsi.x11.display);
}

void x11_set_window_aspect_ratio(window_st* window, int numer, int denom)
{
    int width, height;
    x11_get_window_size(window, &width, &height);
    updateNormalHints(window, width, height);
    XFlush(g_wsi.x11.display);
}

void x11_get_window_frame_size(window_st* window,
                                int* left, int* top,
                                int* right, int* bottom)
{
    long* extents = NULL;

    if (window->monitor || !window->decorated)
        return;

    if (g_wsi.x11.NET_FRAME_EXTENTS == None)
        return;

    if (!x11_window_visible(window) &&
        g_wsi.x11.NET_REQUEST_FRAME_EXTENTS)
    {
        XEvent event;
        double timeout = 0.5;

        // Ensure _NET_FRAME_EXTENTS is set, allowing sc_wsi_win_get_frame_size to
        // function before the window is mapped
        sendEventToWM(window, g_wsi.x11.NET_REQUEST_FRAME_EXTENTS,
                      0, 0, 0, 0, 0);

        // HACK: Use a timeout because earlier versions of some window managers
        //       (at least Unity, Fluxbox and Xfwm) failed to send the reply
        //       They have been fixed but broken versions are still in the wild
        //       If you are affected by this and your window manager is NOT
        //       listed above, PLEASE report it to their and our issue trackers
        while (!XCheckIfEvent(g_wsi.x11.display,
                              &event,
                              isFrameExtentsEvent,
                              (XPointer) window))
        {
            if (!waitForX11Event(&timeout))
            {
                impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                                "X11: The window manager has a broken _NET_REQUEST_FRAME_EXTENTS implementation; please report this issue");
                return;
            }
        }
    }

    if (x11_GetWindowProperty(window->x11.handle,
                                  g_wsi.x11.NET_FRAME_EXTENTS,
                                  XA_CARDINAL,
                                  (unsigned char**) &extents) == 4)
    {
        if (left)
            *left = extents[0];
        if (top)
            *top = extents[2];
        if (right)
            *right = extents[1];
        if (bottom)
            *bottom = extents[3];
    }

    if (extents)
        XFree(extents);
}

void x11_get_window_content_scale(window_st* window, float* xscale, float* yscale)
{
    if (xscale)
        *xscale = g_wsi.x11.contentScaleX;
    if (yscale)
        *yscale = g_wsi.x11.contentScaleY;
}

void x11_iconify_window(window_st* window)
{
    if (window->x11.overrideRedirect)
    {
        // Override-redirect windows cannot be iconified or restored, as those
        // tasks are performed by the window manager
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "X11: Iconification of full screen windows requires a WM that supports EWMH full screen");
        return;
    }

    XIconifyWindow(g_wsi.x11.display, window->x11.handle, g_wsi.x11.screen);
    XFlush(g_wsi.x11.display);
}

void x11_restore_window(window_st* window)
{
    if (window->x11.overrideRedirect)
    {
        // Override-redirect windows cannot be iconified or restored, as those
        // tasks are performed by the window manager
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "X11: Iconification of full screen windows requires a WM that supports EWMH full screen");
        return;
    }

    if (x11_window_iconified(window))
    {
        XMapWindow(g_wsi.x11.display, window->x11.handle);
        waitForVisibilityNotify(window);
    }
    else if (_glfwWindowVisibleX11(window))
    {
        if (g_wsi.x11.NET_WM_STATE &&
            g_wsi.x11.NET_WM_STATE_MAXIMIZED_VERT &&
            g_wsi.x11.NET_WM_STATE_MAXIMIZED_HORZ)
        {
            sendEventToWM(window,
                          g_wsi.x11.NET_WM_STATE,
                          _NET_WM_STATE_REMOVE,
                          g_wsi.x11.NET_WM_STATE_MAXIMIZED_VERT,
                          g_wsi.x11.NET_WM_STATE_MAXIMIZED_HORZ,
                          1, 0);
        }
    }

    XFlush(g_wsi.x11.display);
}

void _glfwMaximizeWindowX11(window_st* window)
{
    if (!g_wsi.x11.NET_WM_STATE ||
        !g_wsi.x11.NET_WM_STATE_MAXIMIZED_VERT ||
        !g_wsi.x11.NET_WM_STATE_MAXIMIZED_HORZ)
    {
        return;
    }

    if (_glfwWindowVisibleX11(window))
    {
        sendEventToWM(window,
                    g_wsi.x11.NET_WM_STATE,
                    _NET_WM_STATE_ADD,
                    g_wsi.x11.NET_WM_STATE_MAXIMIZED_VERT,
                    g_wsi.x11.NET_WM_STATE_MAXIMIZED_HORZ,
                    1, 0);
    }
    else
    {
        Atom* states = NULL;
        unsigned long count =
            _glfwGetWindowPropertyX11(window->x11.handle,
                                      g_wsi.x11.NET_WM_STATE,
                                      XA_ATOM,
                                      (unsigned char**) &states);

        // NOTE: We don't check for failure as this property may not exist yet
        //       and that's fine (and we'll create it implicitly with append)

        Atom missing[2] =
        {
            g_wsi.x11.NET_WM_STATE_MAXIMIZED_VERT,
            g_wsi.x11.NET_WM_STATE_MAXIMIZED_HORZ
        };
        unsigned long missingCount = 2;

        for (unsigned long i = 0;  i < count;  i++)
        {
            for (unsigned long j = 0;  j < missingCount;  j++)
            {
                if (states[i] == missing[j])
                {
                    missing[j] = missing[missingCount - 1];
                    missingCount--;
                }
            }
        }

        if (states)
            XFree(states);

        if (!missingCount)
            return;

        XChangeProperty(g_wsi.x11.display, window->x11.handle,
                        g_wsi.x11.NET_WM_STATE, XA_ATOM, 32,
                        PropModeAppend,
                        (unsigned char*) missing,
                        missingCount);
    }

    XFlush(g_wsi.x11.display);
}

void _glfwShowWindowX11(window_st* window)
{
    if (_glfwWindowVisibleX11(window))
        return;

    if (window->floating && g_wsi.x11.NET_WM_STATE && g_wsi.x11.NET_WM_STATE_ABOVE)
    {
        Atom* states = NULL;
        const unsigned long count =
            _glfwGetWindowPropertyX11(window->x11.handle,
                                      g_wsi.x11.NET_WM_STATE,
                                      XA_ATOM, (unsigned char**) &states);

        // NOTE: We don't check for failure as this property may not exist yet
        //       and that's fine (and we'll create it implicitly with append)

        unsigned long i;

        for (i = 0;  i < count;  i++)
        {
            if (states[i] == g_wsi.x11.NET_WM_STATE_ABOVE)
                break;
        }

        if (i == count)
        {
            XChangeProperty(g_wsi.x11.display, window->x11.handle,
                            g_wsi.x11.NET_WM_STATE, XA_ATOM, 32,
                            PropModeAppend,
                            (unsigned char*) &g_wsi.x11.NET_WM_STATE_ABOVE,
                            1);
        }

        if (states)
            XFree(states);
    }

    XMapWindow(g_wsi.x11.display, window->x11.handle);
    waitForVisibilityNotify(window);
}

void _glfwHideWindowX11(window_st* window)
{
    XUnmapWindow(g_wsi.x11.display, window->x11.handle);
    XFlush(g_wsi.x11.display);
}

void _glfwRequestWindowAttentionX11(window_st* window)
{
    if (!g_wsi.x11.NET_WM_STATE || !g_wsi.x11.NET_WM_STATE_DEMANDS_ATTENTION)
        return;

    sendEventToWM(window,
                  g_wsi.x11.NET_WM_STATE,
                  _NET_WM_STATE_ADD,
                  g_wsi.x11.NET_WM_STATE_DEMANDS_ATTENTION,
                  0, 1, 0);
}

void _glfwFocusWindowX11(window_st* window)
{
    if (g_wsi.x11.NET_ACTIVE_WINDOW)
        sendEventToWM(window, g_wsi.x11.NET_ACTIVE_WINDOW, 1, 0, 0, 0, 0);
    else if (_glfwWindowVisibleX11(window))
    {
        XRaiseWindow(g_wsi.x11.display, window->x11.handle);
        XSetInputFocus(g_wsi.x11.display, window->x11.handle,
                       RevertToParent, CurrentTime);
    }

    XFlush(g_wsi.x11.display);
}

void _glfwSetWindowMonitorX11(window_st* window,
                              monitor_st* monitor,
                              int xpos, int ypos,
                              int width, int height,
                              int refreshRate)
{
    if (window->monitor == monitor)
    {
        if (monitor)
        {
            if (monitor->window == window)
                acquireMonitor(window);
        }
        else
        {
            if (!window->resizable)
                updateNormalHints(window, width, height);

            XMoveResizeWindow(g_wsi.x11.display, window->x11.handle,
                              xpos, ypos, width, height);
        }

        XFlush(g_wsi.x11.display);
        return;
    }

    if (window->monitor)
    {
        _glfwSetWindowDecoratedX11(window, window->decorated);
        _glfwSetWindowFloatingX11(window, window->floating);
        releaseMonitor(window);
    }

    impl_on_win_monitor(window, monitor);
    updateNormalHints(window, width, height);

    if (window->monitor)
    {
        if (!_glfwWindowVisibleX11(window))
        {
            XMapRaised(g_wsi.x11.display, window->x11.handle);
            waitForVisibilityNotify(window);
        }

        updateWindowMode(window);
        acquireMonitor(window);
    }
    else
    {
        updateWindowMode(window);
        XMoveResizeWindow(g_wsi.x11.display, window->x11.handle,
                          xpos, ypos, width, height);
    }

    XFlush(g_wsi.x11.display);
}

bool _glfwWindowFocusedX11(window_st* window)
{
    Window focused;
    int state;

    XGetInputFocus(g_wsi.x11.display, &focused, &state);
    return window->x11.handle == focused;
}

bool _glfwWindowIconifiedX11(window_st* window)
{
    return getWindowState(window) == IconicState;
}

bool _glfwWindowVisibleX11(window_st* window)
{
    XWindowAttributes wa;
    XGetWindowAttributes(g_wsi.x11.display, window->x11.handle, &wa);
    return wa.map_state == IsViewable;
}

bool _glfwWindowMaximizedX11(window_st* window)
{
    Atom* states;
    bool maximized = false;

    if (!g_wsi.x11.NET_WM_STATE ||
        !g_wsi.x11.NET_WM_STATE_MAXIMIZED_VERT ||
        !g_wsi.x11.NET_WM_STATE_MAXIMIZED_HORZ)
    {
        return maximized;
    }

    const unsigned long count =
        _glfwGetWindowPropertyX11(window->x11.handle,
                                  g_wsi.x11.NET_WM_STATE,
                                  XA_ATOM,
                                  (unsigned char**) &states);

    for (unsigned long i = 0;  i < count;  i++)
    {
        if (states[i] == g_wsi.x11.NET_WM_STATE_MAXIMIZED_VERT ||
            states[i] == g_wsi.x11.NET_WM_STATE_MAXIMIZED_HORZ)
        {
            maximized = true;
            break;
        }
    }

    if (states)
        XFree(states);

    return maximized;
}

bool _glfwWindowHoveredX11(window_st* window)
{
    Window w = g_wsi.x11.root;
    while (w)
    {
        Window root;
        int rootX, rootY, childX, childY;
        unsigned int mask;

        _glfwGrabErrorHandlerX11();

        const Bool result = XQueryPointer(g_wsi.x11.display, w,
                                          &root, &w, &rootX, &rootY,
                                          &childX, &childY, &mask);

        _glfwReleaseErrorHandlerX11();

        if (g_wsi.x11.errorCode == BadWindow)
            w = g_wsi.x11.root;
        else if (!result)
            return false;
        else if (w == window->x11.handle)
            return true;
    }

    return false;
}

void _glfwSetWindowResizableX11(window_st* window, bool enabled)
{
    int width, height;
    _glfwGetWindowSizeX11(window, &width, &height);
    updateNormalHints(window, width, height);
}

void _glfwSetWindowDecoratedX11(window_st* window, bool enabled)
{
    struct
    {
        unsigned long flags;
        unsigned long functions;
        unsigned long decorations;
        long input_mode;
        unsigned long status;
    } hints = {0};

    hints.flags = MWM_HINTS_DECORATIONS;
    hints.decorations = enabled ? MWM_DECOR_ALL : 0;

    XChangeProperty(g_wsi.x11.display, window->x11.handle,
                    g_wsi.x11.MOTIF_WM_HINTS,
                    g_wsi.x11.MOTIF_WM_HINTS, 32,
                    PropModeReplace,
                    (unsigned char*) &hints,
                    sizeof(hints) / sizeof(long));
}

void _glfwSetWindowFloatingX11(window_st* window, bool enabled)
{
    if (!g_wsi.x11.NET_WM_STATE || !g_wsi.x11.NET_WM_STATE_ABOVE)
        return;

    if (_glfwWindowVisibleX11(window))
    {
        const long action = enabled ? _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE;
        sendEventToWM(window,
                      g_wsi.x11.NET_WM_STATE,
                      action,
                      g_wsi.x11.NET_WM_STATE_ABOVE,
                      0, 1, 0);
    }
    else
    {
        // NOTE: _NET_WM_STATE_ABOVE is added when the window is shown
        if (enabled)
            return;

        Atom* states = NULL;
        const unsigned long count =
            _glfwGetWindowPropertyX11(window->x11.handle,
                                      g_wsi.x11.NET_WM_STATE,
                                      XA_ATOM,
                                      (unsigned char**) &states);

        // NOTE: We don't check for failure as this property may not exist yet
        //       and that's fine (and we'll create it implicitly with append)

        unsigned long i;

        for (i = 0;  i < count;  i++)
        {
            if (states[i] == g_wsi.x11.NET_WM_STATE_ABOVE)
                break;
        }

        if (i < count)
        {
            states[i] = states[count - 1];
            XChangeProperty(g_wsi.x11.display, window->x11.handle,
                            g_wsi.x11.NET_WM_STATE, XA_ATOM, 32,
                            PropModeReplace, (unsigned char*) states, count - 1);
        }

        if (states)
            XFree(states);
    }

    XFlush(g_wsi.x11.display);
}

void _glfwSetWindowMousePassthroughX11(window_st* window, bool enabled)
{
    if (!g_wsi.x11.xshape.available)
        return;

    if (enabled)
    {
        Region region = XCreateRegion();
        XShapeCombineRegion(g_wsi.x11.display, window->x11.handle,
                            ShapeInput, 0, 0, region, ShapeSet);
        XDestroyRegion(region);
    }
    else
    {
        XShapeCombineMask(g_wsi.x11.display, window->x11.handle,
                          ShapeInput, 0, 0, None, ShapeSet);
    }
}

float _glfwGetWindowOpacityX11(window_st* window)
{
    float opacity = 1.f;

    if (XGetSelectionOwner(g_wsi.x11.display, g_wsi.x11.NET_WM_CM_Sx))
    {
        CARD32* value = NULL;

        if (_glfwGetWindowPropertyX11(window->x11.handle,
                                      g_wsi.x11.NET_WM_WINDOW_OPACITY,
                                      XA_CARDINAL,
                                      (unsigned char**) &value))
        {
            opacity = (float) (*value / (double) 0xffffffffu);
        }

        if (value)
            XFree(value);
    }

    return opacity;
}

void _glfwSetWindowOpacityX11(window_st* window, float opacity)
{
    const CARD32 value = (CARD32) (0xffffffffu * (double) opacity);
    XChangeProperty(g_wsi.x11.display, window->x11.handle,
                    g_wsi.x11.NET_WM_WINDOW_OPACITY, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char*) &value, 1);
}

void _glfwSetRawMouseMotionX11(window_st *window, bool enabled)
{
    if (!g_wsi.x11.xi.available)
        return;

    if (g_wsi.x11.disabledCursorWindow != window)
        return;

    if (enabled)
        enableRawMouseMotion(window);
    else
        disableRawMouseMotion(window);
}

bool _glfwRawMouseMotionSupportedX11(void)
{
    return g_wsi.x11.xi.available;
}

void _glfwPollEventsX11(void)
{
    drainEmptyEvents();

    XPending(g_wsi.x11.display);

    while (QLength(g_wsi.x11.display))
    {
        XEvent event;
        XNextEvent(g_wsi.x11.display, &event);
        processEvent(&event);
    }

    window_st* window = g_wsi.x11.disabledCursorWindow;
    if (window)
    {
        int width, height;
        _glfwGetWindowSizeX11(window, &width, &height);

        // NOTE: Re-center the cursor only if it has moved since the last call,
        //       to avoid breaking sc_wsi_wait_events with MotionNotify
        if (window->x11.lastCursorPosX != width / 2 ||
            window->x11.lastCursorPosY != height / 2)
        {
            _glfwSetCursorPosX11(window, width / 2, height / 2);
        }
    }

    XFlush(g_wsi.x11.display);
}

void _glfwWaitEventsX11(void)
{
    waitForAnyEvent(NULL);
    _glfwPollEventsX11();
}

void _glfwWaitEventsTimeoutX11(double timeout)
{
    waitForAnyEvent(&timeout);
    _glfwPollEventsX11();
}

void _glfwPostEmptyEventX11(void)
{
    writeEmptyEvent();
}

void _glfwGetCursorPosX11(window_st* window, double* xpos, double* ypos)
{
    Window root, child;
    int rootX, rootY, childX, childY;
    unsigned int mask;

    XQueryPointer(g_wsi.x11.display, window->x11.handle,
                  &root, &child,
                  &rootX, &rootY, &childX, &childY,
                  &mask);

    if (xpos)
        *xpos = childX;
    if (ypos)
        *ypos = childY;
}

void _glfwSetCursorPosX11(window_st* window, double x, double y)
{
    // Store the new position so it can be recognized later
    window->x11.warpCursorPosX = (int) x;
    window->x11.warpCursorPosY = (int) y;

    XWarpPointer(g_wsi.x11.display, None, window->x11.handle,
                 0,0,0,0, (int) x, (int) y);
    XFlush(g_wsi.x11.display);
}

void _glfwSetCursorModeX11(window_st* window, int mode)
{
    if (_glfwWindowFocusedX11(window))
    {
        if (mode == SC_CURSOR_DISABLED)
        {
            _glfwGetCursorPosX11(window,
                                 &g_wsi.x11.restoreCursorPosX,
                                 &g_wsi.x11.restoreCursorPosY);
            wsi_center_cursor_in_content_area(window);
            if (window->rawMouseMotion)
                enableRawMouseMotion(window);
        }
        else if (g_wsi.x11.disabledCursorWindow == window)
        {
            if (window->rawMouseMotion)
                disableRawMouseMotion(window);
        }

        if (mode == SC_CURSOR_DISABLED || mode == SC_CURSOR_CAPTURED)
            captureCursor(window);
        else
            releaseCursor();

        if (mode == SC_CURSOR_DISABLED)
            g_wsi.x11.disabledCursorWindow = window;
        else if (g_wsi.x11.disabledCursorWindow == window)
        {
            g_wsi.x11.disabledCursorWindow = NULL;
            _glfwSetCursorPosX11(window,
                                 g_wsi.x11.restoreCursorPosX,
                                 g_wsi.x11.restoreCursorPosY);
        }
    }

    updateCursorImage(window);
    XFlush(g_wsi.x11.display);
}

const char* _glfwGetScancodeNameX11(int scancode)
{
    if (!g_wsi.x11.xkb.available)
        return NULL;

    if (scancode < 0 || scancode > 0xff)
    {
        impl_on_error(SC_WSI_ERR_INVALID_VALUE, "Invalid scancode %i", scancode);
        return NULL;
    }

    const int key = g_wsi.x11.keycodes[scancode];
    if (key == SC_KEY_UNKNOWN)
        return NULL;

    const KeySym keysym = XkbKeycodeToKeysym(g_wsi.x11.display,
                                             scancode, g_wsi.x11.xkb.group, 0);
    if (keysym == NoSymbol)
        return NULL;

    const uint32_t codepoint = _glfwKeySym2UnicodeX11(keysym);
    if (codepoint == GLFW_INVALID_CODEPOINT)
        return NULL;

    const size_t count = wsi_encode_urf8(g_wsi.x11.keynames[key], codepoint);
    if (count == 0)
        return NULL;

    g_wsi.x11.keynames[key][count] = '\0';
    return g_wsi.x11.keynames[key];
}

int _glfwGetKeyScancodeX11(int key)
{
    return g_wsi.x11.scancodes[key];
}

bool _glfwCreateCursorX11(cursor_st* cursor,
                              const GLFWimage* image,
                              int xhot, int yhot)
{
    cursor->x11.handle = _glfwCreateNativeCursorX11(image, xhot, yhot);
    if (!cursor->x11.handle)
        return false;

    return true;
}

bool _glfwCreateStandardCursorX11(cursor_st* cursor, int shape)
{
    if (g_wsi.x11.xcursor.handle)
    {
        char* theme = XcursorGetTheme(g_wsi.x11.display);
        if (theme)
        {
            const int size = XcursorGetDefaultSize(g_wsi.x11.display);
            const char* name = NULL;

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

            XcursorImage* image = XcursorLibraryLoadImage(name, theme, size);
            if (image)
            {
                cursor->x11.handle = XcursorImageLoadCursor(g_wsi.x11.display, image);
                XcursorImageDestroy(image);
            }
        }
    }

    if (!cursor->x11.handle)
    {
        unsigned int native = 0;

        switch (shape)
        {
            case SC_ARROW_CURSOR:
                native = XC_left_ptr;
                break;
            case SC_IBEAM_CURSOR:
                native = XC_xterm;
                break;
            case SC_CROSSHAIR_CURSOR:
                native = XC_crosshair;
                break;
            case SC_POINTING_HAND_CURSOR:
                native = XC_hand2;
                break;
            case SC_RESIZE_EW_CURSOR:
                native = XC_sb_h_double_arrow;
                break;
            case SC_RESIZE_NS_CURSOR:
                native = XC_sb_v_double_arrow;
                break;
            case SC_RESIZE_ALL_CURSOR:
                native = XC_fleur;
                break;
            default:
                impl_on_error(SC_WSI_ERR_CURSOR_UNAVAILABLE,
                                "X11: Standard cursor shape unavailable");
                return false;
        }

        cursor->x11.handle = XCreateFontCursor(g_wsi.x11.display, native);
        if (!cursor->x11.handle)
        {
            impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                            "X11: Failed to create standard cursor");
            return false;
        }
    }

    return true;
}

void _glfwDestroyCursorX11(cursor_st* cursor)
{
    if (cursor->x11.handle)
        XFreeCursor(g_wsi.x11.display, cursor->x11.handle);
}

void _glfwSetCursorX11(window_st* window, cursor_st* cursor)
{
    if (window->cursorMode == SC_CURSOR_NORMAL ||
        window->cursorMode == SC_CURSOR_CAPTURED)
    {
        updateCursorImage(window);
        XFlush(g_wsi.x11.display);
    }
}

void _glfwSetClipboardStringX11(const char* string)
{
    char* copy = wsi_strdup(string);
    wsi_free(g_wsi.x11.clipboardString);
    g_wsi.x11.clipboardString = copy;

    XSetSelectionOwner(g_wsi.x11.display,
                       g_wsi.x11.CLIPBOARD,
                       g_wsi.x11.helperWindowHandle,
                       CurrentTime);

    if (XGetSelectionOwner(g_wsi.x11.display, g_wsi.x11.CLIPBOARD) !=
        g_wsi.x11.helperWindowHandle)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "X11: Failed to become owner of clipboard selection");
    }
}

const char* _glfwGetClipboardStringX11(void)
{
    return getSelectionString(g_wsi.x11.CLIPBOARD);
}


//////////////////////////////////////////////////////////////////////////
//////                        GLFW native API                       //////
//////////////////////////////////////////////////////////////////////////

WSI_API Display* wsi_get_x11_display(void)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }

    if (g_wsi.platform.platformID != SC_PLATFORM_X11)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_UNAVAILABLE, "X11: Platform not initialized");
        return NULL;
    }

    return g_wsi.x11.display;
}

WSI_API Window wsi_get_x11_window(sc_window* handle)
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

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    return window->x11.handle;
}

WSI_API void wsi_set_x11_selection_string(const char* string)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return;
    }

    if (g_wsi.platform.platformID != SC_PLATFORM_X11)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_UNAVAILABLE, "X11: Platform not initialized");
        return;
    }

    assert(string != NULL);

    wsi_free(g_wsi.x11.primarySelectionString);
    g_wsi.x11.primarySelectionString = wsi_strdup(string);

    XSetSelectionOwner(g_wsi.x11.display,
                       g_wsi.x11.PRIMARY,
                       g_wsi.x11.helperWindowHandle,
                       CurrentTime);

    if (XGetSelectionOwner(g_wsi.x11.display, g_wsi.x11.PRIMARY) !=
        g_wsi.x11.helperWindowHandle)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "X11: Failed to become owner of primary selection");
    }
}

WSI_API const char* wsi_get_x11_selection_string(void)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }

    if (g_wsi.platform.platformID != SC_PLATFORM_X11)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_UNAVAILABLE, "X11: Platform not initialized");
        return NULL;
    }

    return getSelectionString(g_wsi.x11.PRIMARY);
}

#endif // WSI_X11

