
#include "internal.h"

#if defined(WSI_X11)

#include <X11/cursorfont.h>
#include <X11/Xmd.h>

#include <poll.h>
#include <locale.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>

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

///////////////////////////////////////////////////////////////////////////////
// platform utils
///////////////////////////////////////////////////////////////////////////////

// Decode a Unicode code point from a UTF-8 stream
// Based on cutef8 by Jeff Bezanson (Public Domain)
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

// X error handler
static int errorHandler(Display *display, XErrorEvent* event)
{
    if (g_wsi.x11.display != display)
        return 0;

    g_wsi.x11.errorCode = event->error_code;
    return 0;
}

// Sets the X error handler callback
void x11_GrabErrorHandler(void)
{
    assert(g_wsi.x11.errorHandler == NULL);
    g_wsi.x11.errorCode = Success;
    g_wsi.x11.errorHandler = XSetErrorHandler(errorHandler);
}

// Clears the X error handler callback
void x11_ReleaseErrorHandler(void)
{
    // Synchronize to make sure all commands are processed
    XSync(g_wsi.x11.display, False);
    XSetErrorHandler(g_wsi.x11.errorHandler);
    g_wsi.x11.errorHandler = NULL;
}

// Reports the specified error, appending information about the last X error
void x11_InputError(int error, const char* message)
{
    char buffer[WSI_MESSAGE_SIZE];
    XGetErrorText(g_wsi.x11.display, g_wsi.x11.errorCode,
                  buffer, sizeof(buffer));

    impl_on_error(error, "%s: %s", message, buffer);
}

// Wait for event data to arrive on the X11 display socket
// This avoids blocking other threads via the per-display Xlib lock that also
// covers GLX functions
static bool waitForX11Event(double* timeout)
{
    struct pollfd fd = { ConnectionNumber(g_wsi.x11.display), POLLIN };

    while (!XPending(g_wsi.x11.display))
    {
        if (!sc_poll_posix(&fd, 1, timeout))
            return false;
    }

    return true;
}

// Waits until a VisibilityNotify event arrives for the specified window or the
// timeout period elapses (ICCCM section 4.2.2)
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

// Retrieve system content scale via folklore heuristics
static void getSystemContentScale(float* xscale, float* yscale)
{
    // Start by assuming the default X11 DPI
    // NOTE: Some desktop environments (KDE) may remove the Xft.dpi field when it
    //       would be set to 96, so assume that is the case if we cannot find it
    float xdpi = 96.f, ydpi = 96.f;

    // NOTE: Basing the scale on Xft.dpi where available should provide the most
    //       consistent user experience (matches Qt, Gtk, etc), although not
    //       always the most accurate one
    char* rms = XResourceManagerString(g_wsi.x11.display);
    if (rms)
    {
        XrmDatabase db = XrmGetStringDatabase(rms);
        if (db)
        {
            XrmValue value;
            char* type = NULL;

            if (XrmGetResource(db, "Xft.dpi", "Xft.Dpi", &type, &value))
            {
                if (type && strcmp(type, "String") == 0)
                    xdpi = ydpi = atof(value.addr);
            }

            XrmDestroyDatabase(db);
        }
    }

    *xscale = xdpi / 96.f;
    *yscale = ydpi / 96.f;
}

// Creates a native cursor object from the specified image and hotspot
Cursor x11_CreateNativeCursor(const GLFWimage* image, int xhot, int yhot)
{
    Cursor cursor;

    if (!g_wsi.x11.xcursor.handle)
        return None;

    XcursorImage* native = XcursorImageCreate(image->width, image->height);
    if (native == NULL)
        return None;

    native->xhot = xhot;
    native->yhot = yhot;

    unsigned char* source = (unsigned char*) image->pixels;
    XcursorPixel* target = native->pixels;

    for (int i = 0;  i < image->width * image->height;  i++, target++, source += 4)
    {
        unsigned int alpha = source[3];

        *target = (alpha << 24) |
                  ((unsigned char) ((source[0] * alpha) / 255) << 16) |
                  ((unsigned char) ((source[1] * alpha) / 255) <<  8) |
                  ((unsigned char) ((source[2] * alpha) / 255) <<  0);
    }

    cursor = XcursorImageLoadCursor(g_wsi.x11.display, native);
    XcursorImageDestroy(native);

    return cursor;
}

// Check whether the IM has a usable style
static bool hasUsableInputMethodStyle(void)
{
    bool found = false;
    XIMStyles* styles = NULL;

    if (XGetIMValues(g_wsi.x11.im, XNQueryInputStyle, &styles, NULL) != NULL)
        return false;

    for (unsigned int i = 0;  i < styles->count_styles;  i++)
    {
        if (styles->supported_styles[i] == (XIMPreeditNothing | XIMStatusNothing))
        {
            found = true;
            break;
        }
    }

    XFree(styles);
    return found;
}

// Retrieve a single window property of the specified type
// Inspired by fghGetWindowProperty from freeglut
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

// Set the specified property to the selection converted to the requested target
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

bool x11_IsVisualTransparent(Visual* visual)
{
    if (!g_wsi.x11.xrender.available)
        return false;

    XRenderPictFormat* pf = XRenderFindVisualFormat(g_wsi.x11.display, visual);
    return pf && pf->direct.alphaMask;
}

// Returns whether it is a _NET_FRAME_EXTENTS event for the specified window
static Bool isFrameExtentsEvent(Display* display, XEvent* event, XPointer pointer)
{
    window_st* window = (window_st*) pointer;
    return event->type == PropertyNotify &&
           event->xproperty.state == PropertyNewValue &&
           event->xproperty.window == window->x11.handle &&
           event->xproperty.atom == g_wsi.x11.NET_FRAME_EXTENTS;
}

// Returns whether it is a property event for the specified selection transfer
static Bool isSelPropNewValueNotify(Display* display, XEvent* event, XPointer pointer)
{
    XEvent* notification = (XEvent*) pointer;
    return event->type == PropertyNotify &&
           event->xproperty.state == PropertyNewValue &&
           event->xproperty.window == notification->xselection.requestor &&
           event->xproperty.atom == notification->xselection.property;
}

// Returns whether the event is a selection event
static Bool isSelectionEvent(Display* display, XEvent* event, XPointer pointer)
{
    if (event->xany.window != g_wsi.x11.helperWindowHandle)
        return False;
    return event->type == SelectionRequest ||
           event->type == SelectionNotify ||
           event->type == SelectionClear;
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

// Push contents of our selection to clipboard manager
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

// Clear its handle when the input context has been destroyed
//
static void inputContextDestroyCallback(XIC ic, XPointer clientData, XPointer callData)
{
    window_st* window = (window_st*) clientData;
    window->x11.ic = NULL;
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

// Sends an EWMH or ICCCM event to the window manager
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

// Updates the full screen status of the window
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

// Returns whether the window is iconified
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

// Translates an X event modifier state mask
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
static int translateKey(int scancode)
{
    // Use the pre-filled LUT (see createKeyTables() in x11_init.c)
    if (scancode < 0 || scancode > 255)
        return SC_KEY_UNKNOWN;

    return g_wsi.x11.keycodes[scancode];
}

// Updates the normal hints according to the window settings
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

// Enable XI2 raw mouse motion events
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
static void disableRawMouseMotion(window_st* window)
{
    XIEventMask em;
    unsigned char mask[] = { 0 };

    em.deviceid = XIAllMasterDevices;
    em.mask_len = sizeof(mask);
    em.mask = mask;

    XISelectEvents(g_wsi.x11.display, g_wsi.x11.root, &em, 1);
}

// Updates the cursor image according to its cursor mode
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
static void releaseCursor(void)
{
    XUngrabPointer(g_wsi.x11.display, CurrentTime);
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

///////////////////////////////////////////////////////////////////////////////
// Monitor
///////////////////////////////////////////////////////////////////////////////

// Check whether the display mode should be included in enumeration
static bool modeIsGood(const XRRModeInfo* mi)
{
    return (mi->modeFlags & RR_Interlace) == 0;
}

// Calculates the refresh rate, in Hz, from the specified RandR mode info
static int calculateRefreshRate(const XRRModeInfo* mi)
{
    if (mi->hTotal && mi->vTotal)
        return (int) round((double) mi->dotClock / ((double) mi->hTotal * (double) mi->vTotal));
    else
        return 0;
}

// Returns the mode info for a RandR mode XID
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

    return mode;
}

//-----------------------------------------------------------------------------

// Poll for changes in the set of connected monitors
void x11_poll_monitors(void)
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
                type = WSI_INSERT_FIRST;
            else
                type = WSI_INSERT_LAST;

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
                          WSI_INSERT_FIRST);
    }
}

///////////////////////////////////////////////////////////////////////////////

void x11_free_monitor(monitor_st* monitor)
{
}

void x11_get_monitor_pos(monitor_st* monitor, int* xpos, int* ypos)
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

void x11_get_monitor_content_scale(monitor_st* monitor, float* xscale, float* yscale)
{
    if (xscale)
        *xscale = g_wsi.x11.contentScaleX;
    if (yscale)
        *yscale = g_wsi.x11.contentScaleY;
}

void x11_get_monitor_work_area(monitor_st* monitor,
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
            x11_GetWindowProperty(g_wsi.x11.root,
                                      g_wsi.x11.NET_WORKAREA,
                                      XA_CARDINAL,
                                      (unsigned char**) &extents);

        if (x11_GetWindowProperty(g_wsi.x11.root,
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

bool x11_get_video_mode(monitor_st* monitor, GLFWvidmode* mode)
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
    }

    return true;
}

// Set the current video mode for the specified monitor
void x11_set_video_mode(monitor_st* monitor, const GLFWvidmode* desired)
{
    if (g_wsi.x11.randr.available && !g_wsi.x11.randr.monitorBroken)
    {
        GLFWvidmode current;
        RRMode native = None;

        const GLFWvidmode* best = wsi_choose_video_mode(monitor, desired);
        x11_get_video_mode(monitor, &current);
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

GLFWvidmode* x11_get_video_modes(monitor_st* monitor, int* count)
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
        x11_get_video_mode(monitor, result);
    }

    return result;
}

// Restore the saved (original) video mode for the specified monitor
void x11_RestoreVideoMode(monitor_st* monitor)
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

bool x11_get_gamma_ramp(monitor_st* monitor, GLFWgammaramp* ramp)
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

void x11_set_gamma_ramp(monitor_st* monitor, const GLFWgammaramp* ramp)
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

//-----------------------------------------------------------------------------

// Make the specified window and its video mode active on its monitor
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

    x11_set_video_mode(window->monitor, &window->videoMode);

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

///////////////////////////////////////////////////////////////////////////////
// lib
///////////////////////////////////////////////////////////////////////////////

// Translate the X11 KeySyms for a key to a GLFW key code
// NOTE: This is only used as a fallback, in case the XKB method fails
//       It is layout-dependent and will fail partially on most non-US layouts
static int translateKeySyms(const KeySym* keysyms, int width)
{
    if (width > 1)
    {
        switch (keysyms[1])
        {
            case XK_KP_0:           return SC_KEY_KP_0;
            case XK_KP_1:           return SC_KEY_KP_1;
            case XK_KP_2:           return SC_KEY_KP_2;
            case XK_KP_3:           return SC_KEY_KP_3;
            case XK_KP_4:           return SC_KEY_KP_4;
            case XK_KP_5:           return SC_KEY_KP_5;
            case XK_KP_6:           return SC_KEY_KP_6;
            case XK_KP_7:           return SC_KEY_KP_7;
            case XK_KP_8:           return SC_KEY_KP_8;
            case XK_KP_9:           return SC_KEY_KP_9;
            case XK_KP_Separator:
            case XK_KP_Decimal:     return SC_KEY_KP_DECIMAL;
            case XK_KP_Equal:       return SC_KEY_KP_EQUAL;
            case XK_KP_Enter:       return SC_KEY_KP_ENTER;
            default:                break;
        }
    }

    switch (keysyms[0])
    {
        case XK_Escape:         return SC_KEY_ESCAPE;
        case XK_Tab:            return SC_KEY_TAB;
        case XK_Shift_L:        return SC_KEY_LEFT_SHIFT;
        case XK_Shift_R:        return SC_KEY_RIGHT_SHIFT;
        case XK_Control_L:      return SC_KEY_LEFT_CONTROL;
        case XK_Control_R:      return SC_KEY_RIGHT_CONTROL;
        case XK_Meta_L:
        case XK_Alt_L:          return SC_KEY_LEFT_ALT;
        case XK_Mode_switch: // Mapped to Alt_R on many keyboards
        case XK_ISO_Level3_Shift: // AltGr on at least some machines
        case XK_Meta_R:
        case XK_Alt_R:          return SC_KEY_RIGHT_ALT;
        case XK_Super_L:        return SC_KEY_LEFT_SUPER;
        case XK_Super_R:        return SC_KEY_RIGHT_SUPER;
        case XK_Menu:           return SC_KEY_MENU;
        case XK_Num_Lock:       return SC_KEY_NUM_LOCK;
        case XK_Caps_Lock:      return SC_KEY_CAPS_LOCK;
        case XK_Print:          return SC_KEY_PRINT_SCREEN;
        case XK_Scroll_Lock:    return SC_KEY_SCROLL_LOCK;
        case XK_Pause:          return SC_KEY_PAUSE;
        case XK_Delete:         return SC_KEY_DELETE;
        case XK_BackSpace:      return SC_KEY_BACKSPACE;
        case XK_Return:         return SC_KEY_ENTER;
        case XK_Home:           return SC_KEY_HOME;
        case XK_End:            return SC_KEY_END;
        case XK_Page_Up:        return SC_KEY_PAGE_UP;
        case XK_Page_Down:      return SC_KEY_PAGE_DOWN;
        case XK_Insert:         return SC_KEY_INSERT;
        case XK_Left:           return SC_KEY_LEFT;
        case XK_Right:          return SC_KEY_RIGHT;
        case XK_Down:           return SC_KEY_DOWN;
        case XK_Up:             return SC_KEY_UP;
        case XK_F1:             return SC_KEY_F1;
        case XK_F2:             return SC_KEY_F2;
        case XK_F3:             return SC_KEY_F3;
        case XK_F4:             return SC_KEY_F4;
        case XK_F5:             return SC_KEY_F5;
        case XK_F6:             return SC_KEY_F6;
        case XK_F7:             return SC_KEY_F7;
        case XK_F8:             return SC_KEY_F8;
        case XK_F9:             return SC_KEY_F9;
        case XK_F10:            return SC_KEY_F10;
        case XK_F11:            return SC_KEY_F11;
        case XK_F12:            return SC_KEY_F12;
        case XK_F13:            return SC_KEY_F13;
        case XK_F14:            return SC_KEY_F14;
        case XK_F15:            return SC_KEY_F15;
        case XK_F16:            return SC_KEY_F16;
        case XK_F17:            return SC_KEY_F17;
        case XK_F18:            return SC_KEY_F18;
        case XK_F19:            return SC_KEY_F19;
        case XK_F20:            return SC_KEY_F20;
        case XK_F21:            return SC_KEY_F21;
        case XK_F22:            return SC_KEY_F22;
        case XK_F23:            return SC_KEY_F23;
        case XK_F24:            return SC_KEY_F24;
        case XK_F25:            return SC_KEY_F25;

        // Numeric keypad
        case XK_KP_Divide:      return SC_KEY_KP_DIVIDE;
        case XK_KP_Multiply:    return SC_KEY_KP_MULTIPLY;
        case XK_KP_Subtract:    return SC_KEY_KP_SUBTRACT;
        case XK_KP_Add:         return SC_KEY_KP_ADD;

        // These should have been detected in secondary keysym test above!
        case XK_KP_Insert:      return SC_KEY_KP_0;
        case XK_KP_End:         return SC_KEY_KP_1;
        case XK_KP_Down:        return SC_KEY_KP_2;
        case XK_KP_Page_Down:   return SC_KEY_KP_3;
        case XK_KP_Left:        return SC_KEY_KP_4;
        case XK_KP_Right:       return SC_KEY_KP_6;
        case XK_KP_Home:        return SC_KEY_KP_7;
        case XK_KP_Up:          return SC_KEY_KP_8;
        case XK_KP_Page_Up:     return SC_KEY_KP_9;
        case XK_KP_Delete:      return SC_KEY_KP_DECIMAL;
        case XK_KP_Equal:       return SC_KEY_KP_EQUAL;
        case XK_KP_Enter:       return SC_KEY_KP_ENTER;

        // Last resort: Check for printable keys (should not happen if the XKB
        // extension is available). This will give a layout dependent mapping
        // (which is wrong, and we may miss some keys, especially on non-US
        // keyboards), but it's better than nothing...
        case XK_a:              return SC_KEY_A;
        case XK_b:              return SC_KEY_B;
        case XK_c:              return SC_KEY_C;
        case XK_d:              return SC_KEY_D;
        case XK_e:              return SC_KEY_E;
        case XK_f:              return SC_KEY_F;
        case XK_g:              return SC_KEY_G;
        case XK_h:              return SC_KEY_H;
        case XK_i:              return SC_KEY_I;
        case XK_j:              return SC_KEY_J;
        case XK_k:              return SC_KEY_K;
        case XK_l:              return SC_KEY_L;
        case XK_m:              return SC_KEY_M;
        case XK_n:              return SC_KEY_N;
        case XK_o:              return SC_KEY_O;
        case XK_p:              return SC_KEY_P;
        case XK_q:              return SC_KEY_Q;
        case XK_r:              return SC_KEY_R;
        case XK_s:              return SC_KEY_S;
        case XK_t:              return SC_KEY_T;
        case XK_u:              return SC_KEY_U;
        case XK_v:              return SC_KEY_V;
        case XK_w:              return SC_KEY_W;
        case XK_x:              return SC_KEY_X;
        case XK_y:              return SC_KEY_Y;
        case XK_z:              return SC_KEY_Z;
        case XK_1:              return SC_KEY_1;
        case XK_2:              return SC_KEY_2;
        case XK_3:              return SC_KEY_3;
        case XK_4:              return SC_KEY_4;
        case XK_5:              return SC_KEY_5;
        case XK_6:              return SC_KEY_6;
        case XK_7:              return SC_KEY_7;
        case XK_8:              return SC_KEY_8;
        case XK_9:              return SC_KEY_9;
        case XK_0:              return SC_KEY_0;
        case XK_space:          return SC_KEY_SPACE;
        case XK_minus:          return SC_KEY_MINUS;
        case XK_equal:          return SC_KEY_EQUAL;
        case XK_bracketleft:    return SC_KEY_LEFT_BRACKET;
        case XK_bracketright:   return SC_KEY_RIGHT_BRACKET;
        case XK_backslash:      return SC_KEY_BACKSLASH;
        case XK_semicolon:      return SC_KEY_SEMICOLON;
        case XK_apostrophe:     return SC_KEY_APOSTROPHE;
        case XK_grave:          return SC_KEY_GRAVE_ACCENT;
        case XK_comma:          return SC_KEY_COMMA;
        case XK_period:         return SC_KEY_PERIOD;
        case XK_slash:          return SC_KEY_SLASH;
        case XK_less:           return SC_KEY_WORLD_1; // At least in some layouts...
        default:                break;
    }

    // No matching translation was found
    return SC_KEY_UNKNOWN;
}

// Create key code translation tables
static void createKeyTables(void)
{
    int scancodeMin, scancodeMax;

    memset(g_wsi.x11.keycodes, -1, sizeof(g_wsi.x11.keycodes));
    memset(g_wsi.x11.scancodes, -1, sizeof(g_wsi.x11.scancodes));

    if (g_wsi.x11.xkb.available)
    {
        // Use XKB to determine physical key locations independently of the
        // current keyboard layout

        XkbDescPtr desc = XkbGetMap(g_wsi.x11.display, 0, XkbUseCoreKbd);
        XkbGetNames(g_wsi.x11.display, XkbKeyNamesMask | XkbKeyAliasesMask, desc);

        scancodeMin = desc->min_key_code;
        scancodeMax = desc->max_key_code;

        const struct
        {
            int key;
            char* name;
        } keymap[] =
        {
            { SC_KEY_GRAVE_ACCENT, "TLDE" },
            { SC_KEY_1, "AE01" },
            { SC_KEY_2, "AE02" },
            { SC_KEY_3, "AE03" },
            { SC_KEY_4, "AE04" },
            { SC_KEY_5, "AE05" },
            { SC_KEY_6, "AE06" },
            { SC_KEY_7, "AE07" },
            { SC_KEY_8, "AE08" },
            { SC_KEY_9, "AE09" },
            { SC_KEY_0, "AE10" },
            { SC_KEY_MINUS, "AE11" },
            { SC_KEY_EQUAL, "AE12" },
            { SC_KEY_Q, "AD01" },
            { SC_KEY_W, "AD02" },
            { SC_KEY_E, "AD03" },
            { SC_KEY_R, "AD04" },
            { SC_KEY_T, "AD05" },
            { SC_KEY_Y, "AD06" },
            { SC_KEY_U, "AD07" },
            { SC_KEY_I, "AD08" },
            { SC_KEY_O, "AD09" },
            { SC_KEY_P, "AD10" },
            { SC_KEY_LEFT_BRACKET, "AD11" },
            { SC_KEY_RIGHT_BRACKET, "AD12" },
            { SC_KEY_A, "AC01" },
            { SC_KEY_S, "AC02" },
            { SC_KEY_D, "AC03" },
            { SC_KEY_F, "AC04" },
            { SC_KEY_G, "AC05" },
            { SC_KEY_H, "AC06" },
            { SC_KEY_J, "AC07" },
            { SC_KEY_K, "AC08" },
            { SC_KEY_L, "AC09" },
            { SC_KEY_SEMICOLON, "AC10" },
            { SC_KEY_APOSTROPHE, "AC11" },
            { SC_KEY_Z, "AB01" },
            { SC_KEY_X, "AB02" },
            { SC_KEY_C, "AB03" },
            { SC_KEY_V, "AB04" },
            { SC_KEY_B, "AB05" },
            { SC_KEY_N, "AB06" },
            { SC_KEY_M, "AB07" },
            { SC_KEY_COMMA, "AB08" },
            { SC_KEY_PERIOD, "AB09" },
            { SC_KEY_SLASH, "AB10" },
            { SC_KEY_BACKSLASH, "BKSL" },
            { SC_KEY_WORLD_1, "LSGT" },
            { SC_KEY_SPACE, "SPCE" },
            { SC_KEY_ESCAPE, "ESC" },
            { SC_KEY_ENTER, "RTRN" },
            { SC_KEY_TAB, "TAB" },
            { SC_KEY_BACKSPACE, "BKSP" },
            { SC_KEY_INSERT, "INS" },
            { SC_KEY_DELETE, "DELE" },
            { SC_KEY_RIGHT, "RGHT" },
            { SC_KEY_LEFT, "LEFT" },
            { SC_KEY_DOWN, "DOWN" },
            { SC_KEY_UP, "UP" },
            { SC_KEY_PAGE_UP, "PGUP" },
            { SC_KEY_PAGE_DOWN, "PGDN" },
            { SC_KEY_HOME, "HOME" },
            { SC_KEY_END, "END" },
            { SC_KEY_CAPS_LOCK, "CAPS" },
            { SC_KEY_SCROLL_LOCK, "SCLK" },
            { SC_KEY_NUM_LOCK, "NMLK" },
            { SC_KEY_PRINT_SCREEN, "PRSC" },
            { SC_KEY_PAUSE, "PAUS" },
            { SC_KEY_F1, "FK01" },
            { SC_KEY_F2, "FK02" },
            { SC_KEY_F3, "FK03" },
            { SC_KEY_F4, "FK04" },
            { SC_KEY_F5, "FK05" },
            { SC_KEY_F6, "FK06" },
            { SC_KEY_F7, "FK07" },
            { SC_KEY_F8, "FK08" },
            { SC_KEY_F9, "FK09" },
            { SC_KEY_F10, "FK10" },
            { SC_KEY_F11, "FK11" },
            { SC_KEY_F12, "FK12" },
            { SC_KEY_F13, "FK13" },
            { SC_KEY_F14, "FK14" },
            { SC_KEY_F15, "FK15" },
            { SC_KEY_F16, "FK16" },
            { SC_KEY_F17, "FK17" },
            { SC_KEY_F18, "FK18" },
            { SC_KEY_F19, "FK19" },
            { SC_KEY_F20, "FK20" },
            { SC_KEY_F21, "FK21" },
            { SC_KEY_F22, "FK22" },
            { SC_KEY_F23, "FK23" },
            { SC_KEY_F24, "FK24" },
            { SC_KEY_F25, "FK25" },
            { SC_KEY_KP_0, "KP0" },
            { SC_KEY_KP_1, "KP1" },
            { SC_KEY_KP_2, "KP2" },
            { SC_KEY_KP_3, "KP3" },
            { SC_KEY_KP_4, "KP4" },
            { SC_KEY_KP_5, "KP5" },
            { SC_KEY_KP_6, "KP6" },
            { SC_KEY_KP_7, "KP7" },
            { SC_KEY_KP_8, "KP8" },
            { SC_KEY_KP_9, "KP9" },
            { SC_KEY_KP_DECIMAL, "KPDL" },
            { SC_KEY_KP_DIVIDE, "KPDV" },
            { SC_KEY_KP_MULTIPLY, "KPMU" },
            { SC_KEY_KP_SUBTRACT, "KPSU" },
            { SC_KEY_KP_ADD, "KPAD" },
            { SC_KEY_KP_ENTER, "KPEN" },
            { SC_KEY_KP_EQUAL, "KPEQ" },
            { SC_KEY_LEFT_SHIFT, "LFSH" },
            { SC_KEY_LEFT_CONTROL, "LCTL" },
            { SC_KEY_LEFT_ALT, "LALT" },
            { SC_KEY_LEFT_SUPER, "LWIN" },
            { SC_KEY_RIGHT_SHIFT, "RTSH" },
            { SC_KEY_RIGHT_CONTROL, "RCTL" },
            { SC_KEY_RIGHT_ALT, "RALT" },
            { SC_KEY_RIGHT_ALT, "LVL3" },
            { SC_KEY_RIGHT_ALT, "MDSW" },
            { SC_KEY_RIGHT_SUPER, "RWIN" },
            { SC_KEY_MENU, "MENU" }
        };

        // Find the X11 key code -> GLFW key code mapping
        for (int scancode = scancodeMin;  scancode <= scancodeMax;  scancode++)
        {
            int key = SC_KEY_UNKNOWN;

            // Map the key name to a GLFW key code. Note: We use the US
            // keyboard layout. Because function keys aren't mapped correctly
            // when using traditional KeySym translations, they are mapped
            // here instead.
            for (int i = 0;  i < sizeof(keymap) / sizeof(keymap[0]);  i++)
            {
                if (strncmp(desc->names->keys[scancode].name,
                            keymap[i].name,
                            XkbKeyNameLength) == 0)
                {
                    key = keymap[i].key;
                    break;
                }
            }

            // Fall back to key aliases in case the key name did not match
            for (int i = 0;  i < desc->names->num_key_aliases;  i++)
            {
                if (key != SC_KEY_UNKNOWN)
                    break;

                if (strncmp(desc->names->key_aliases[i].real,
                            desc->names->keys[scancode].name,
                            XkbKeyNameLength) != 0)
                {
                    continue;
                }

                for (int j = 0;  j < sizeof(keymap) / sizeof(keymap[0]);  j++)
                {
                    if (strncmp(desc->names->key_aliases[i].alias,
                                keymap[j].name,
                                XkbKeyNameLength) == 0)
                    {
                        key = keymap[j].key;
                        break;
                    }
                }
            }

            g_wsi.x11.keycodes[scancode] = key;
        }

        XkbFreeNames(desc, XkbKeyNamesMask, True);
        XkbFreeKeyboard(desc, 0, True);
    }
    else
        XDisplayKeycodes(g_wsi.x11.display, &scancodeMin, &scancodeMax);

    int width;
    KeySym* keysyms = XGetKeyboardMapping(g_wsi.x11.display,
                                          scancodeMin,
                                          scancodeMax - scancodeMin + 1,
                                          &width);

    // Some X servers (e.g. MobaXterm) may return a NULL mapping or zero
    // keysyms-per-keycode; skip the traditional fallback lookup in that case
    // to avoid dereferencing a NULL/short keysyms table.
    for (int scancode = scancodeMin;  keysyms && width > 0 && scancode <= scancodeMax;  scancode++)
    {
        // Translate the un-translated key codes using traditional X11 KeySym
        // lookups
        if (g_wsi.x11.keycodes[scancode] < 0)
        {
            const size_t base = (scancode - scancodeMin) * width;
            g_wsi.x11.keycodes[scancode] = translateKeySyms(&keysyms[base], width);
        }

        // Store the reverse translation for faster key name lookup
        if (g_wsi.x11.keycodes[scancode] > 0)
            g_wsi.x11.scancodes[g_wsi.x11.keycodes[scancode]] = scancode;
    }

    if (keysyms)
        XFree(keysyms);
}


static void inputMethodDestroyCallback(XIM im, XPointer clientData, XPointer callData)
{
    g_wsi.x11.im = NULL;
}

static void inputMethodInstantiateCallback(Display* display,
                                           XPointer clientData,
                                           XPointer callData)
{
    if (g_wsi.x11.im)
        return;

    g_wsi.x11.im = XOpenIM(g_wsi.x11.display, 0, NULL, NULL);
    if (g_wsi.x11.im)
    {
        if (!hasUsableInputMethodStyle())
        {
            XCloseIM(g_wsi.x11.im);
            g_wsi.x11.im = NULL;
        }
    }

    if (g_wsi.x11.im)
    {
        XIMCallback callback;
        callback.callback = (XIMProc) inputMethodDestroyCallback;
        callback.client_data = NULL;
        XSetIMValues(g_wsi.x11.im, XNDestroyCallback, &callback, NULL);

        for (window_st* window = g_wsi.windowListHead;  window;  window = window->next)
            x11_CreateInputContext(window);
    }
}

// Return the atom ID only if it is listed in the specified array
static Atom getAtomIfSupported(Atom* supportedAtoms,
                               unsigned long atomCount,
                               const char* atomName)
{
    const Atom atom = XInternAtom(g_wsi.x11.display, atomName, False);

    for (unsigned long i = 0;  i < atomCount;  i++)
    {
        if (supportedAtoms[i] == atom)
            return atom;
    }

    return None;
}

// Check whether the running window manager is EWMH-compliant
static void detectEWMH(void)
{
    // First we read the _NET_SUPPORTING_WM_CHECK property on the root window

    Window* windowFromRoot = NULL;
    if (!x11_GetWindowProperty(g_wsi.x11.root,
                                   g_wsi.x11.NET_SUPPORTING_WM_CHECK,
                                   XA_WINDOW,
                                   (unsigned char**) &windowFromRoot))
    {
        return;
    }

    x11_GrabErrorHandler();

    // If it exists, it should be the XID of a top-level window
    // Then we look for the same property on that window

    Window* windowFromChild = NULL;
    if (!x11_GetWindowProperty(*windowFromRoot,
                                   g_wsi.x11.NET_SUPPORTING_WM_CHECK,
                                   XA_WINDOW,
                                   (unsigned char**) &windowFromChild))
    {
        x11_ReleaseErrorHandler();
        XFree(windowFromRoot);
        return;
    }

    x11_ReleaseErrorHandler();

    // If the property exists, it should contain the XID of the window

    if (*windowFromRoot != *windowFromChild)
    {
        XFree(windowFromRoot);
        XFree(windowFromChild);
        return;
    }

    XFree(windowFromRoot);
    XFree(windowFromChild);

    // We are now fairly sure that an EWMH-compliant WM is currently running
    // We can now start querying the WM about what features it supports by
    // looking in the _NET_SUPPORTED property on the root window
    // It should contain a list of supported EWMH protocol and state atoms

    Atom* supportedAtoms = NULL;
    const unsigned long atomCount =
        x11_GetWindowProperty(g_wsi.x11.root,
                                  g_wsi.x11.NET_SUPPORTED,
                                  XA_ATOM,
                                  (unsigned char**) &supportedAtoms);

    // See which of the atoms we support that are supported by the WM

    g_wsi.x11.NET_WM_STATE =
        getAtomIfSupported(supportedAtoms, atomCount, "_NET_WM_STATE");
    g_wsi.x11.NET_WM_STATE_ABOVE =
        getAtomIfSupported(supportedAtoms, atomCount, "_NET_WM_STATE_ABOVE");
    g_wsi.x11.NET_WM_STATE_FULLSCREEN =
        getAtomIfSupported(supportedAtoms, atomCount, "_NET_WM_STATE_FULLSCREEN");
    g_wsi.x11.NET_WM_STATE_MAXIMIZED_VERT =
        getAtomIfSupported(supportedAtoms, atomCount, "_NET_WM_STATE_MAXIMIZED_VERT");
    g_wsi.x11.NET_WM_STATE_MAXIMIZED_HORZ =
        getAtomIfSupported(supportedAtoms, atomCount, "_NET_WM_STATE_MAXIMIZED_HORZ");
    g_wsi.x11.NET_WM_STATE_DEMANDS_ATTENTION =
        getAtomIfSupported(supportedAtoms, atomCount, "_NET_WM_STATE_DEMANDS_ATTENTION");
    g_wsi.x11.NET_WM_FULLSCREEN_MONITORS =
        getAtomIfSupported(supportedAtoms, atomCount, "_NET_WM_FULLSCREEN_MONITORS");
    g_wsi.x11.NET_WM_WINDOW_TYPE =
        getAtomIfSupported(supportedAtoms, atomCount, "_NET_WM_WINDOW_TYPE");
    g_wsi.x11.NET_WM_WINDOW_TYPE_NORMAL =
        getAtomIfSupported(supportedAtoms, atomCount, "_NET_WM_WINDOW_TYPE_NORMAL");
    g_wsi.x11.NET_WORKAREA =
        getAtomIfSupported(supportedAtoms, atomCount, "_NET_WORKAREA");
    g_wsi.x11.NET_CURRENT_DESKTOP =
        getAtomIfSupported(supportedAtoms, atomCount, "_NET_CURRENT_DESKTOP");
    g_wsi.x11.NET_ACTIVE_WINDOW =
        getAtomIfSupported(supportedAtoms, atomCount, "_NET_ACTIVE_WINDOW");
    g_wsi.x11.NET_FRAME_EXTENTS =
        getAtomIfSupported(supportedAtoms, atomCount, "_NET_FRAME_EXTENTS");
    g_wsi.x11.NET_REQUEST_FRAME_EXTENTS =
        getAtomIfSupported(supportedAtoms, atomCount, "_NET_REQUEST_FRAME_EXTENTS");

    if (supportedAtoms)
        XFree(supportedAtoms);
}

// Look for and initialize supported X11 extensions
static bool initExtensions(void)
{
#if defined(__OpenBSD__) || defined(__NetBSD__)
    g_wsi.x11.vidmode.handle = impl_platform_load_module("libXxf86vm.so");
#else
    g_wsi.x11.vidmode.handle = impl_platform_load_module("libXxf86vm.so.1");
#endif
    if (g_wsi.x11.vidmode.handle)
    {
        g_wsi.x11.vidmode.QueryExtension = (PFN_XF86VidModeQueryExtension)
            impl_platform_get_module_symbol(g_wsi.x11.vidmode.handle, "XF86VidModeQueryExtension");
        g_wsi.x11.vidmode.GetGammaRamp = (PFN_XF86VidModeGetGammaRamp)
            impl_platform_get_module_symbol(g_wsi.x11.vidmode.handle, "XF86VidModeGetGammaRamp");
        g_wsi.x11.vidmode.SetGammaRamp = (PFN_XF86VidModeSetGammaRamp)
            impl_platform_get_module_symbol(g_wsi.x11.vidmode.handle, "XF86VidModeSetGammaRamp");
        g_wsi.x11.vidmode.GetGammaRampSize = (PFN_XF86VidModeGetGammaRampSize)
            impl_platform_get_module_symbol(g_wsi.x11.vidmode.handle, "XF86VidModeGetGammaRampSize");

        g_wsi.x11.vidmode.available =
            XF86VidModeQueryExtension(g_wsi.x11.display,
                                      &g_wsi.x11.vidmode.eventBase,
                                      &g_wsi.x11.vidmode.errorBase);
    }

#if defined(__CYGWIN__)
    g_wsi.x11.xi.handle = impl_platform_load_module("libXi-6.so");
#elif defined(__OpenBSD__) || defined(__NetBSD__)
    g_wsi.x11.xi.handle = impl_platform_load_module("libXi.so");
#else
    g_wsi.x11.xi.handle = impl_platform_load_module("libXi.so.6");
#endif
    if (g_wsi.x11.xi.handle)
    {
        g_wsi.x11.xi.QueryVersion = (PFN_XIQueryVersion)
            impl_platform_get_module_symbol(g_wsi.x11.xi.handle, "XIQueryVersion");
        g_wsi.x11.xi.SelectEvents = (PFN_XISelectEvents)
            impl_platform_get_module_symbol(g_wsi.x11.xi.handle, "XISelectEvents");

        if (XQueryExtension(g_wsi.x11.display,
                            "XInputExtension",
                            &g_wsi.x11.xi.majorOpcode,
                            &g_wsi.x11.xi.eventBase,
                            &g_wsi.x11.xi.errorBase))
        {
            g_wsi.x11.xi.major = 2;
            g_wsi.x11.xi.minor = 0;

            if (XIQueryVersion(g_wsi.x11.display,
                               &g_wsi.x11.xi.major,
                               &g_wsi.x11.xi.minor) == Success)
            {
                g_wsi.x11.xi.available = true;
            }
        }
    }

#if defined(__CYGWIN__)
    g_wsi.x11.randr.handle = impl_platform_load_module("libXrandr-2.so");
#elif defined(__OpenBSD__) || defined(__NetBSD__)
    g_wsi.x11.randr.handle = impl_platform_load_module("libXrandr.so");
#else
    g_wsi.x11.randr.handle = impl_platform_load_module("libXrandr.so.2");
#endif
    if (g_wsi.x11.randr.handle)
    {
        g_wsi.x11.randr.AllocGamma = (PFN_XRRAllocGamma)
            impl_platform_get_module_symbol(g_wsi.x11.randr.handle, "XRRAllocGamma");
        g_wsi.x11.randr.FreeGamma = (PFN_XRRFreeGamma)
            impl_platform_get_module_symbol(g_wsi.x11.randr.handle, "XRRFreeGamma");
        g_wsi.x11.randr.FreeCrtcInfo = (PFN_XRRFreeCrtcInfo)
            impl_platform_get_module_symbol(g_wsi.x11.randr.handle, "XRRFreeCrtcInfo");
        g_wsi.x11.randr.FreeGamma = (PFN_XRRFreeGamma)
            impl_platform_get_module_symbol(g_wsi.x11.randr.handle, "XRRFreeGamma");
        g_wsi.x11.randr.FreeOutputInfo = (PFN_XRRFreeOutputInfo)
            impl_platform_get_module_symbol(g_wsi.x11.randr.handle, "XRRFreeOutputInfo");
        g_wsi.x11.randr.FreeScreenResources = (PFN_XRRFreeScreenResources)
            impl_platform_get_module_symbol(g_wsi.x11.randr.handle, "XRRFreeScreenResources");
        g_wsi.x11.randr.GetCrtcGamma = (PFN_XRRGetCrtcGamma)
            impl_platform_get_module_symbol(g_wsi.x11.randr.handle, "XRRGetCrtcGamma");
        g_wsi.x11.randr.GetCrtcGammaSize = (PFN_XRRGetCrtcGammaSize)
            impl_platform_get_module_symbol(g_wsi.x11.randr.handle, "XRRGetCrtcGammaSize");
        g_wsi.x11.randr.GetCrtcInfo = (PFN_XRRGetCrtcInfo)
            impl_platform_get_module_symbol(g_wsi.x11.randr.handle, "XRRGetCrtcInfo");
        g_wsi.x11.randr.GetOutputInfo = (PFN_XRRGetOutputInfo)
            impl_platform_get_module_symbol(g_wsi.x11.randr.handle, "XRRGetOutputInfo");
        g_wsi.x11.randr.GetOutputPrimary = (PFN_XRRGetOutputPrimary)
            impl_platform_get_module_symbol(g_wsi.x11.randr.handle, "XRRGetOutputPrimary");
        g_wsi.x11.randr.GetScreenResourcesCurrent = (PFN_XRRGetScreenResourcesCurrent)
            impl_platform_get_module_symbol(g_wsi.x11.randr.handle, "XRRGetScreenResourcesCurrent");
        g_wsi.x11.randr.QueryExtension = (PFN_XRRQueryExtension)
            impl_platform_get_module_symbol(g_wsi.x11.randr.handle, "XRRQueryExtension");
        g_wsi.x11.randr.QueryVersion = (PFN_XRRQueryVersion)
            impl_platform_get_module_symbol(g_wsi.x11.randr.handle, "XRRQueryVersion");
        g_wsi.x11.randr.SelectInput = (PFN_XRRSelectInput)
            impl_platform_get_module_symbol(g_wsi.x11.randr.handle, "XRRSelectInput");
        g_wsi.x11.randr.SetCrtcConfig = (PFN_XRRSetCrtcConfig)
            impl_platform_get_module_symbol(g_wsi.x11.randr.handle, "XRRSetCrtcConfig");
        g_wsi.x11.randr.SetCrtcGamma = (PFN_XRRSetCrtcGamma)
            impl_platform_get_module_symbol(g_wsi.x11.randr.handle, "XRRSetCrtcGamma");
        g_wsi.x11.randr.UpdateConfiguration = (PFN_XRRUpdateConfiguration)
            impl_platform_get_module_symbol(g_wsi.x11.randr.handle, "XRRUpdateConfiguration");

        if (XRRQueryExtension(g_wsi.x11.display,
                              &g_wsi.x11.randr.eventBase,
                              &g_wsi.x11.randr.errorBase))
        {
            if (XRRQueryVersion(g_wsi.x11.display,
                                &g_wsi.x11.randr.major,
                                &g_wsi.x11.randr.minor))
            {
                // The GLFW RandR path requires at least version 1.3
                if (g_wsi.x11.randr.major > 1 || g_wsi.x11.randr.minor >= 3)
                    g_wsi.x11.randr.available = true;
            }
            else
            {
                impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                                "X11: Failed to query RandR version");
            }
        }
    }

    if (g_wsi.x11.randr.available)
    {
        XRRScreenResources* sr = XRRGetScreenResourcesCurrent(g_wsi.x11.display,
                                                              g_wsi.x11.root);

        if (!sr->ncrtc || !XRRGetCrtcGammaSize(g_wsi.x11.display, sr->crtcs[0]))
        {
            // This is likely an older Nvidia driver with broken gamma support
            // Flag it as useless and fall back to xf86vm gamma, if available
            g_wsi.x11.randr.gammaBroken = true;
        }

        if (!sr->ncrtc)
        {
            // A system without CRTCs is likely a system with broken RandR
            // Disable the RandR monitor path and fall back to core functions
            g_wsi.x11.randr.monitorBroken = true;
        }

        XRRFreeScreenResources(sr);
    }

    if (g_wsi.x11.randr.available && !g_wsi.x11.randr.monitorBroken)
    {
        XRRSelectInput(g_wsi.x11.display, g_wsi.x11.root,
                       RROutputChangeNotifyMask);
    }

#if defined(__CYGWIN__)
    g_wsi.x11.xcursor.handle = impl_platform_load_module("libXcursor-1.so");
#elif defined(__OpenBSD__) || defined(__NetBSD__)
    g_wsi.x11.xcursor.handle = impl_platform_load_module("libXcursor.so");
#else
    g_wsi.x11.xcursor.handle = impl_platform_load_module("libXcursor.so.1");
#endif
    if (g_wsi.x11.xcursor.handle)
    {
        g_wsi.x11.xcursor.ImageCreate = (PFN_XcursorImageCreate)
            impl_platform_get_module_symbol(g_wsi.x11.xcursor.handle, "XcursorImageCreate");
        g_wsi.x11.xcursor.ImageDestroy = (PFN_XcursorImageDestroy)
            impl_platform_get_module_symbol(g_wsi.x11.xcursor.handle, "XcursorImageDestroy");
        g_wsi.x11.xcursor.ImageLoadCursor = (PFN_XcursorImageLoadCursor)
            impl_platform_get_module_symbol(g_wsi.x11.xcursor.handle, "XcursorImageLoadCursor");
        g_wsi.x11.xcursor.GetTheme = (PFN_XcursorGetTheme)
            impl_platform_get_module_symbol(g_wsi.x11.xcursor.handle, "XcursorGetTheme");
        g_wsi.x11.xcursor.GetDefaultSize = (PFN_XcursorGetDefaultSize)
            impl_platform_get_module_symbol(g_wsi.x11.xcursor.handle, "XcursorGetDefaultSize");
        g_wsi.x11.xcursor.LibraryLoadImage = (PFN_XcursorLibraryLoadImage)
            impl_platform_get_module_symbol(g_wsi.x11.xcursor.handle, "XcursorLibraryLoadImage");
    }

#if defined(__CYGWIN__)
    g_wsi.x11.xinerama.handle = impl_platform_load_module("libXinerama-1.so");
#elif defined(__OpenBSD__) || defined(__NetBSD__)
    g_wsi.x11.xinerama.handle = impl_platform_load_module("libXinerama.so");
#else
    g_wsi.x11.xinerama.handle = impl_platform_load_module("libXinerama.so.1");
#endif
    if (g_wsi.x11.xinerama.handle)
    {
        g_wsi.x11.xinerama.IsActive = (PFN_XineramaIsActive)
            impl_platform_get_module_symbol(g_wsi.x11.xinerama.handle, "XineramaIsActive");
        g_wsi.x11.xinerama.QueryExtension = (PFN_XineramaQueryExtension)
            impl_platform_get_module_symbol(g_wsi.x11.xinerama.handle, "XineramaQueryExtension");
        g_wsi.x11.xinerama.QueryScreens = (PFN_XineramaQueryScreens)
            impl_platform_get_module_symbol(g_wsi.x11.xinerama.handle, "XineramaQueryScreens");

        if (XineramaQueryExtension(g_wsi.x11.display,
                                   &g_wsi.x11.xinerama.major,
                                   &g_wsi.x11.xinerama.minor))
        {
            if (XineramaIsActive(g_wsi.x11.display))
                g_wsi.x11.xinerama.available = true;
        }
    }

    g_wsi.x11.xkb.major = 1;
    g_wsi.x11.xkb.minor = 0;
    g_wsi.x11.xkb.available =
        XkbQueryExtension(g_wsi.x11.display,
                          &g_wsi.x11.xkb.majorOpcode,
                          &g_wsi.x11.xkb.eventBase,
                          &g_wsi.x11.xkb.errorBase,
                          &g_wsi.x11.xkb.major,
                          &g_wsi.x11.xkb.minor);

    if (g_wsi.x11.xkb.available)
    {
        Bool supported;

        if (XkbSetDetectableAutoRepeat(g_wsi.x11.display, True, &supported))
        {
            if (supported)
                g_wsi.x11.xkb.detectable = true;
        }

        XkbStateRec state;
        if (XkbGetState(g_wsi.x11.display, XkbUseCoreKbd, &state) == Success)
            g_wsi.x11.xkb.group = (unsigned int)state.group;

        XkbSelectEventDetails(g_wsi.x11.display, XkbUseCoreKbd, XkbStateNotify,
                              XkbGroupStateMask, XkbGroupStateMask);
    }

#if defined(__CYGWIN__)
    g_wsi.x11.xrender.handle = impl_platform_load_module("libXrender-1.so");
#elif defined(__OpenBSD__) || defined(__NetBSD__)
    g_wsi.x11.xrender.handle = impl_platform_load_module("libXrender.so");
#else
    g_wsi.x11.xrender.handle = impl_platform_load_module("libXrender.so.1");
#endif
    if (g_wsi.x11.xrender.handle)
    {
        g_wsi.x11.xrender.QueryExtension = (PFN_XRenderQueryExtension)
            impl_platform_get_module_symbol(g_wsi.x11.xrender.handle, "XRenderQueryExtension");
        g_wsi.x11.xrender.QueryVersion = (PFN_XRenderQueryVersion)
            impl_platform_get_module_symbol(g_wsi.x11.xrender.handle, "XRenderQueryVersion");
        g_wsi.x11.xrender.FindVisualFormat = (PFN_XRenderFindVisualFormat)
            impl_platform_get_module_symbol(g_wsi.x11.xrender.handle, "XRenderFindVisualFormat");

        if (XRenderQueryExtension(g_wsi.x11.display,
                                  &g_wsi.x11.xrender.errorBase,
                                  &g_wsi.x11.xrender.eventBase))
        {
            if (XRenderQueryVersion(g_wsi.x11.display,
                                    &g_wsi.x11.xrender.major,
                                    &g_wsi.x11.xrender.minor))
            {
                g_wsi.x11.xrender.available = true;
            }
        }
    }

#if defined(__CYGWIN__)
    g_wsi.x11.xshape.handle = impl_platform_load_module("libXext-6.so");
#elif defined(__OpenBSD__) || defined(__NetBSD__)
    g_wsi.x11.xshape.handle = impl_platform_load_module("libXext.so");
#else
    g_wsi.x11.xshape.handle = impl_platform_load_module("libXext.so.6");
#endif
    if (g_wsi.x11.xshape.handle)
    {
        g_wsi.x11.xshape.QueryExtension = (PFN_XShapeQueryExtension)
            impl_platform_get_module_symbol(g_wsi.x11.xshape.handle, "XShapeQueryExtension");
        g_wsi.x11.xshape.ShapeCombineRegion = (PFN_XShapeCombineRegion)
            impl_platform_get_module_symbol(g_wsi.x11.xshape.handle, "XShapeCombineRegion");
        g_wsi.x11.xshape.QueryVersion = (PFN_XShapeQueryVersion)
            impl_platform_get_module_symbol(g_wsi.x11.xshape.handle, "XShapeQueryVersion");
        g_wsi.x11.xshape.ShapeCombineMask = (PFN_XShapeCombineMask)
            impl_platform_get_module_symbol(g_wsi.x11.xshape.handle, "XShapeCombineMask");

        if (XShapeQueryExtension(g_wsi.x11.display,
            &g_wsi.x11.xshape.errorBase,
            &g_wsi.x11.xshape.eventBase))
        {
            if (XShapeQueryVersion(g_wsi.x11.display,
                &g_wsi.x11.xshape.major,
                &g_wsi.x11.xshape.minor))
            {
                g_wsi.x11.xshape.available = true;
            }
        }
    }

    // Update the key code LUT
    // FIXME: We should listen to XkbMapNotify events to track changes to
    // the keyboard mapping.
    createKeyTables();

    // String format atoms
    g_wsi.x11.NULL_ = XInternAtom(g_wsi.x11.display, "NULL", False);
    g_wsi.x11.UTF8_STRING = XInternAtom(g_wsi.x11.display, "UTF8_STRING", False);
    g_wsi.x11.ATOM_PAIR = XInternAtom(g_wsi.x11.display, "ATOM_PAIR", False);

    // Custom selection property atom
    g_wsi.x11.GLFW_SELECTION =
        XInternAtom(g_wsi.x11.display, "GLFW_SELECTION", False);

    // ICCCM standard clipboard atoms
    g_wsi.x11.TARGETS = XInternAtom(g_wsi.x11.display, "TARGETS", False);
    g_wsi.x11.MULTIPLE = XInternAtom(g_wsi.x11.display, "MULTIPLE", False);
    g_wsi.x11.PRIMARY = XInternAtom(g_wsi.x11.display, "PRIMARY", False);
    g_wsi.x11.INCR = XInternAtom(g_wsi.x11.display, "INCR", False);
    g_wsi.x11.CLIPBOARD = XInternAtom(g_wsi.x11.display, "CLIPBOARD", False);

    // Clipboard manager atoms
    g_wsi.x11.CLIPBOARD_MANAGER =
        XInternAtom(g_wsi.x11.display, "CLIPBOARD_MANAGER", False);
    g_wsi.x11.SAVE_TARGETS =
        XInternAtom(g_wsi.x11.display, "SAVE_TARGETS", False);

    // Xdnd (drag and drop) atoms
    g_wsi.x11.XdndAware = XInternAtom(g_wsi.x11.display, "XdndAware", False);
    g_wsi.x11.XdndEnter = XInternAtom(g_wsi.x11.display, "XdndEnter", False);
    g_wsi.x11.XdndPosition = XInternAtom(g_wsi.x11.display, "XdndPosition", False);
    g_wsi.x11.XdndStatus = XInternAtom(g_wsi.x11.display, "XdndStatus", False);
    g_wsi.x11.XdndActionCopy = XInternAtom(g_wsi.x11.display, "XdndActionCopy", False);
    g_wsi.x11.XdndDrop = XInternAtom(g_wsi.x11.display, "XdndDrop", False);
    g_wsi.x11.XdndFinished = XInternAtom(g_wsi.x11.display, "XdndFinished", False);
    g_wsi.x11.XdndSelection = XInternAtom(g_wsi.x11.display, "XdndSelection", False);
    g_wsi.x11.XdndTypeList = XInternAtom(g_wsi.x11.display, "XdndTypeList", False);
    g_wsi.x11.text_uri_list = XInternAtom(g_wsi.x11.display, "text/uri-list", False);

    // ICCCM, EWMH and Motif window property atoms
    // These can be set safely even without WM support
    // The EWMH atoms that require WM support are handled in detectEWMH
    g_wsi.x11.WM_PROTOCOLS =
        XInternAtom(g_wsi.x11.display, "WM_PROTOCOLS", False);
    g_wsi.x11.WM_STATE =
        XInternAtom(g_wsi.x11.display, "WM_STATE", False);
    g_wsi.x11.WM_DELETE_WINDOW =
        XInternAtom(g_wsi.x11.display, "WM_DELETE_WINDOW", False);
    g_wsi.x11.NET_SUPPORTED =
        XInternAtom(g_wsi.x11.display, "_NET_SUPPORTED", False);
    g_wsi.x11.NET_SUPPORTING_WM_CHECK =
        XInternAtom(g_wsi.x11.display, "_NET_SUPPORTING_WM_CHECK", False);
    g_wsi.x11.NET_WM_ICON =
        XInternAtom(g_wsi.x11.display, "_NET_WM_ICON", False);
    g_wsi.x11.NET_WM_PING =
        XInternAtom(g_wsi.x11.display, "_NET_WM_PING", False);
    g_wsi.x11.NET_WM_PID =
        XInternAtom(g_wsi.x11.display, "_NET_WM_PID", False);
    g_wsi.x11.NET_WM_NAME =
        XInternAtom(g_wsi.x11.display, "_NET_WM_NAME", False);
    g_wsi.x11.NET_WM_ICON_NAME =
        XInternAtom(g_wsi.x11.display, "_NET_WM_ICON_NAME", False);
    g_wsi.x11.NET_WM_BYPASS_COMPOSITOR =
        XInternAtom(g_wsi.x11.display, "_NET_WM_BYPASS_COMPOSITOR", False);
    g_wsi.x11.NET_WM_WINDOW_OPACITY =
        XInternAtom(g_wsi.x11.display, "_NET_WM_WINDOW_OPACITY", False);
    g_wsi.x11.MOTIF_WM_HINTS =
        XInternAtom(g_wsi.x11.display, "_MOTIF_WM_HINTS", False);

    // The compositing manager selection name contains the screen number
    {
        char name[32];
        snprintf(name, sizeof(name), "_NET_WM_CM_S%u", g_wsi.x11.screen);
        g_wsi.x11.NET_WM_CM_Sx = XInternAtom(g_wsi.x11.display, name, False);
    }

    // Detect whether an EWMH-conformant window manager is running
    detectEWMH();

    return true;
}

// Create a blank cursor for hidden and disabled cursor modes
static Cursor createHiddenCursor(void)
{
    unsigned char pixels[16 * 16 * 4] = { 0 };
    GLFWimage image = { 16, 16, pixels };
    return x11_CreateNativeCursor(&image, 0, 0);
}

// Create a helper window for IPC
static Window createHelperWindow(void)
{
    XSetWindowAttributes wa;
    wa.event_mask = PropertyChangeMask;

    return XCreateWindow(g_wsi.x11.display, g_wsi.x11.root,
                         0, 0, 1, 1, 0, 0,
                         InputOnly,
                         DefaultVisual(g_wsi.x11.display, g_wsi.x11.screen),
                         CWEventMask, &wa);
}

// Create the pipe for empty events without assumuing the OS has pipe2(2)
static bool createEmptyEventPipe(void)
{
    if (pipe(g_wsi.x11.emptyEventPipe) != 0)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "X11: Failed to create empty event pipe: %s",
                        strerror(errno));
        return false;
    }

    for (int i = 0; i < 2; i++)
    {
        const int sf = fcntl(g_wsi.x11.emptyEventPipe[i], F_GETFL, 0);
        const int df = fcntl(g_wsi.x11.emptyEventPipe[i], F_GETFD, 0);

        if (sf == -1 || df == -1 ||
            fcntl(g_wsi.x11.emptyEventPipe[i], F_SETFL, sf | O_NONBLOCK) == -1 ||
            fcntl(g_wsi.x11.emptyEventPipe[i], F_SETFD, df | FD_CLOEXEC) == -1)
        {
            impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                            "X11: Failed to set flags for empty event pipe: %s",
                            strerror(errno));
            return false;
        }
    }

    return true;
}

//-----------------------------------------------------------------------------

static int x11_init(void)
{
    g_wsi.x11.xlib.AllocClassHint = (PFN_XAllocClassHint)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XAllocClassHint");
    g_wsi.x11.xlib.AllocSizeHints = (PFN_XAllocSizeHints)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XAllocSizeHints");
    g_wsi.x11.xlib.AllocWMHints = (PFN_XAllocWMHints)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XAllocWMHints");
    g_wsi.x11.xlib.ChangeProperty = (PFN_XChangeProperty)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XChangeProperty");
    g_wsi.x11.xlib.ChangeWindowAttributes = (PFN_XChangeWindowAttributes)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XChangeWindowAttributes");
    g_wsi.x11.xlib.CheckIfEvent = (PFN_XCheckIfEvent)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XCheckIfEvent");
    g_wsi.x11.xlib.CheckTypedWindowEvent = (PFN_XCheckTypedWindowEvent)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XCheckTypedWindowEvent");
    g_wsi.x11.xlib.CloseDisplay = (PFN_XCloseDisplay)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XCloseDisplay");
    g_wsi.x11.xlib.CloseIM = (PFN_XCloseIM)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XCloseIM");
    g_wsi.x11.xlib.ConvertSelection = (PFN_XConvertSelection)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XConvertSelection");
    g_wsi.x11.xlib.CreateColormap = (PFN_XCreateColormap)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XCreateColormap");
    g_wsi.x11.xlib.CreateFontCursor = (PFN_XCreateFontCursor)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XCreateFontCursor");
    g_wsi.x11.xlib.CreateIC = (PFN_XCreateIC)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XCreateIC");
    g_wsi.x11.xlib.CreateRegion = (PFN_XCreateRegion)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XCreateRegion");
    g_wsi.x11.xlib.CreateWindow = (PFN_XCreateWindow)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XCreateWindow");
    g_wsi.x11.xlib.DefineCursor = (PFN_XDefineCursor)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XDefineCursor");
    g_wsi.x11.xlib.DeleteContext = (PFN_XDeleteContext)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XDeleteContext");
    g_wsi.x11.xlib.DeleteProperty = (PFN_XDeleteProperty)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XDeleteProperty");
    g_wsi.x11.xlib.DestroyIC = (PFN_XDestroyIC)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XDestroyIC");
    g_wsi.x11.xlib.DestroyRegion = (PFN_XDestroyRegion)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XDestroyRegion");
    g_wsi.x11.xlib.DestroyWindow = (PFN_XDestroyWindow)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XDestroyWindow");
    g_wsi.x11.xlib.DisplayKeycodes = (PFN_XDisplayKeycodes)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XDisplayKeycodes");
    g_wsi.x11.xlib.EventsQueued = (PFN_XEventsQueued)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XEventsQueued");
    g_wsi.x11.xlib.FilterEvent = (PFN_XFilterEvent)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XFilterEvent");
    g_wsi.x11.xlib.FindContext = (PFN_XFindContext)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XFindContext");
    g_wsi.x11.xlib.Flush = (PFN_XFlush)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XFlush");
    g_wsi.x11.xlib.Free = (PFN_XFree)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XFree");
    g_wsi.x11.xlib.FreeColormap = (PFN_XFreeColormap)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XFreeColormap");
    g_wsi.x11.xlib.FreeCursor = (PFN_XFreeCursor)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XFreeCursor");
    g_wsi.x11.xlib.FreeEventData = (PFN_XFreeEventData)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XFreeEventData");
    g_wsi.x11.xlib.GetErrorText = (PFN_XGetErrorText)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XGetErrorText");
    g_wsi.x11.xlib.GetEventData = (PFN_XGetEventData)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XGetEventData");
    g_wsi.x11.xlib.GetICValues = (PFN_XGetICValues)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XGetICValues");
    g_wsi.x11.xlib.GetIMValues = (PFN_XGetIMValues)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XGetIMValues");
    g_wsi.x11.xlib.GetInputFocus = (PFN_XGetInputFocus)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XGetInputFocus");
    g_wsi.x11.xlib.GetKeyboardMapping = (PFN_XGetKeyboardMapping)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XGetKeyboardMapping");
    g_wsi.x11.xlib.GetScreenSaver = (PFN_XGetScreenSaver)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XGetScreenSaver");
    g_wsi.x11.xlib.GetSelectionOwner = (PFN_XGetSelectionOwner)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XGetSelectionOwner");
    g_wsi.x11.xlib.GetVisualInfo = (PFN_XGetVisualInfo)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XGetVisualInfo");
    g_wsi.x11.xlib.GetWMNormalHints = (PFN_XGetWMNormalHints)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XGetWMNormalHints");
    g_wsi.x11.xlib.GetWindowAttributes = (PFN_XGetWindowAttributes)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XGetWindowAttributes");
    g_wsi.x11.xlib.GetWindowProperty = (PFN_XGetWindowProperty)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XGetWindowProperty");
    g_wsi.x11.xlib.GrabPointer = (PFN_XGrabPointer)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XGrabPointer");
    g_wsi.x11.xlib.IconifyWindow = (PFN_XIconifyWindow)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XIconifyWindow");
    g_wsi.x11.xlib.InternAtom = (PFN_XInternAtom)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XInternAtom");
    g_wsi.x11.xlib.LookupString = (PFN_XLookupString)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XLookupString");
    g_wsi.x11.xlib.MapRaised = (PFN_XMapRaised)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XMapRaised");
    g_wsi.x11.xlib.MapWindow = (PFN_XMapWindow)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XMapWindow");
    g_wsi.x11.xlib.MoveResizeWindow = (PFN_XMoveResizeWindow)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XMoveResizeWindow");
    g_wsi.x11.xlib.MoveWindow = (PFN_XMoveWindow)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XMoveWindow");
    g_wsi.x11.xlib.NextEvent = (PFN_XNextEvent)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XNextEvent");
    g_wsi.x11.xlib.OpenIM = (PFN_XOpenIM)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XOpenIM");
    g_wsi.x11.xlib.PeekEvent = (PFN_XPeekEvent)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XPeekEvent");
    g_wsi.x11.xlib.Pending = (PFN_XPending)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XPending");
    g_wsi.x11.xlib.QueryExtension = (PFN_XQueryExtension)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XQueryExtension");
    g_wsi.x11.xlib.QueryPointer = (PFN_XQueryPointer)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XQueryPointer");
    g_wsi.x11.xlib.RaiseWindow = (PFN_XRaiseWindow)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XRaiseWindow");
    g_wsi.x11.xlib.RegisterIMInstantiateCallback = (PFN_XRegisterIMInstantiateCallback)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XRegisterIMInstantiateCallback");
    g_wsi.x11.xlib.ResizeWindow = (PFN_XResizeWindow)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XResizeWindow");
    g_wsi.x11.xlib.ResourceManagerString = (PFN_XResourceManagerString)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XResourceManagerString");
    g_wsi.x11.xlib.SaveContext = (PFN_XSaveContext)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XSaveContext");
    g_wsi.x11.xlib.SelectInput = (PFN_XSelectInput)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XSelectInput");
    g_wsi.x11.xlib.SendEvent = (PFN_XSendEvent)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XSendEvent");
    g_wsi.x11.xlib.SetClassHint = (PFN_XSetClassHint)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XSetClassHint");
    g_wsi.x11.xlib.SetErrorHandler = (PFN_XSetErrorHandler)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XSetErrorHandler");
    g_wsi.x11.xlib.SetICFocus = (PFN_XSetICFocus)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XSetICFocus");
    g_wsi.x11.xlib.SetIMValues = (PFN_XSetIMValues)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XSetIMValues");
    g_wsi.x11.xlib.SetInputFocus = (PFN_XSetInputFocus)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XSetInputFocus");
    g_wsi.x11.xlib.SetLocaleModifiers = (PFN_XSetLocaleModifiers)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XSetLocaleModifiers");
    g_wsi.x11.xlib.SetScreenSaver = (PFN_XSetScreenSaver)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XSetScreenSaver");
    g_wsi.x11.xlib.SetSelectionOwner = (PFN_XSetSelectionOwner)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XSetSelectionOwner");
    g_wsi.x11.xlib.SetWMHints = (PFN_XSetWMHints)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XSetWMHints");
    g_wsi.x11.xlib.SetWMNormalHints = (PFN_XSetWMNormalHints)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XSetWMNormalHints");
    g_wsi.x11.xlib.SetWMProtocols = (PFN_XSetWMProtocols)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XSetWMProtocols");
    g_wsi.x11.xlib.SupportsLocale = (PFN_XSupportsLocale)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XSupportsLocale");
    g_wsi.x11.xlib.Sync = (PFN_XSync)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XSync");
    g_wsi.x11.xlib.TranslateCoordinates = (PFN_XTranslateCoordinates)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XTranslateCoordinates");
    g_wsi.x11.xlib.UndefineCursor = (PFN_XUndefineCursor)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XUndefineCursor");
    g_wsi.x11.xlib.UngrabPointer = (PFN_XUngrabPointer)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XUngrabPointer");
    g_wsi.x11.xlib.UnmapWindow = (PFN_XUnmapWindow)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XUnmapWindow");
    g_wsi.x11.xlib.UnsetICFocus = (PFN_XUnsetICFocus)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XUnsetICFocus");
    g_wsi.x11.xlib.VisualIDFromVisual = (PFN_XVisualIDFromVisual)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XVisualIDFromVisual");
    g_wsi.x11.xlib.WarpPointer = (PFN_XWarpPointer)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XWarpPointer");
    g_wsi.x11.xkb.FreeKeyboard = (PFN_XkbFreeKeyboard)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XkbFreeKeyboard");
    g_wsi.x11.xkb.FreeNames = (PFN_XkbFreeNames)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XkbFreeNames");
    g_wsi.x11.xkb.GetMap = (PFN_XkbGetMap)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XkbGetMap");
    g_wsi.x11.xkb.GetNames = (PFN_XkbGetNames)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XkbGetNames");
    g_wsi.x11.xkb.GetState = (PFN_XkbGetState)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XkbGetState");
    g_wsi.x11.xkb.KeycodeToKeysym = (PFN_XkbKeycodeToKeysym)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XkbKeycodeToKeysym");
    g_wsi.x11.xkb.QueryExtension = (PFN_XkbQueryExtension)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XkbQueryExtension");
    g_wsi.x11.xkb.SelectEventDetails = (PFN_XkbSelectEventDetails)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XkbSelectEventDetails");
    g_wsi.x11.xkb.SetDetectableAutoRepeat = (PFN_XkbSetDetectableAutoRepeat)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XkbSetDetectableAutoRepeat");
    g_wsi.x11.xrm.DestroyDatabase = (PFN_XrmDestroyDatabase)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XrmDestroyDatabase");
    g_wsi.x11.xrm.GetResource = (PFN_XrmGetResource)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XrmGetResource");
    g_wsi.x11.xrm.GetStringDatabase = (PFN_XrmGetStringDatabase)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XrmGetStringDatabase");
    g_wsi.x11.xrm.UniqueQuark = (PFN_XrmUniqueQuark)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XrmUniqueQuark");
    g_wsi.x11.xlib.UnregisterIMInstantiateCallback = (PFN_XUnregisterIMInstantiateCallback)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "XUnregisterIMInstantiateCallback");
    g_wsi.x11.xlib.utf8LookupString = (PFN_Xutf8LookupString)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "Xutf8LookupString");
    g_wsi.x11.xlib.utf8SetWMProperties = (PFN_Xutf8SetWMProperties)
        impl_platform_get_module_symbol(g_wsi.x11.xlib.handle, "Xutf8SetWMProperties");

    if (g_wsi.x11.xlib.utf8LookupString && g_wsi.x11.xlib.utf8SetWMProperties)
        g_wsi.x11.xlib.utf8 = true;

    g_wsi.x11.screen = DefaultScreen(g_wsi.x11.display);
    g_wsi.x11.root = RootWindow(g_wsi.x11.display, g_wsi.x11.screen);
    g_wsi.x11.context = XUniqueContext();

    getSystemContentScale(&g_wsi.x11.contentScaleX, &g_wsi.x11.contentScaleY);

    if (!createEmptyEventPipe())
        return false;

    if (!initExtensions())
        return false;

    g_wsi.x11.helperWindowHandle = createHelperWindow();
    g_wsi.x11.hiddenCursorHandle = createHiddenCursor();

    if (XSupportsLocale() && g_wsi.x11.xlib.utf8)
    {
        XSetLocaleModifiers("");

        // If an IM is already present our callback will be called right away
        XRegisterIMInstantiateCallback(g_wsi.x11.display,
                                       NULL, NULL, NULL,
                                       inputMethodInstantiateCallback,
                                       NULL);
    }

    x11_poll_monitors();
    return true;
}

static void x11_terminate(void)
{
    if (g_wsi.x11.helperWindowHandle)
    {
        if (XGetSelectionOwner(g_wsi.x11.display, g_wsi.x11.CLIPBOARD) ==
            g_wsi.x11.helperWindowHandle)
        {
            x11_PushSelectionToManager();
        }

        XDestroyWindow(g_wsi.x11.display, g_wsi.x11.helperWindowHandle);
        g_wsi.x11.helperWindowHandle = None;
    }

    if (g_wsi.x11.hiddenCursorHandle)
    {
        XFreeCursor(g_wsi.x11.display, g_wsi.x11.hiddenCursorHandle);
        g_wsi.x11.hiddenCursorHandle = (Cursor) 0;
    }

    wsi_free(g_wsi.x11.primarySelectionString);
    wsi_free(g_wsi.x11.clipboardString);

    XUnregisterIMInstantiateCallback(g_wsi.x11.display,
                                     NULL, NULL, NULL,
                                     inputMethodInstantiateCallback,
                                     NULL);

    if (g_wsi.x11.im)
    {
        XCloseIM(g_wsi.x11.im);
        g_wsi.x11.im = NULL;
    }

    if (g_wsi.x11.display)
    {
        XCloseDisplay(g_wsi.x11.display);
        g_wsi.x11.display = NULL;
    }

    impl_platform_unload_module(g_wsi.x11.xcursor.handle);
    impl_platform_unload_module(g_wsi.x11.randr.handle);
    impl_platform_unload_module(g_wsi.x11.xinerama.handle);
    impl_platform_unload_module(g_wsi.x11.xrender.handle);
    impl_platform_unload_module(g_wsi.x11.xshape.handle);
    impl_platform_unload_module(g_wsi.x11.vidmode.handle);
    impl_platform_unload_module(g_wsi.x11.xi.handle);
    impl_platform_unload_module(g_wsi.x11.xlib.handle);

    if (g_wsi.x11.emptyEventPipe[0] || g_wsi.x11.emptyEventPipe[1])
    {
        close(g_wsi.x11.emptyEventPipe[0]);
        close(g_wsi.x11.emptyEventPipe[1]);
    }

    memset(&g_wsi.x11, 0, sizeof(g_wsi.x11));
}

///////////////////////////////////////////////////////////////////////////////
// Interface
///////////////////////////////////////////////////////////////////////////////

static void x11_set_window_title(window_st* window, const char* title)
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

static void x11_set_window_icon(window_st* window, int count, const GLFWimage* images)
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

static void x11_set_window_mouse_passthrough(window_st* window, bool enabled)
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


static bool x11_window_visible(window_st* window)
{
    XWindowAttributes wa;
    XGetWindowAttributes(g_wsi.x11.display, window->x11.handle, &wa);
    return wa.map_state == IsViewable;
}

static bool x11_window_maximized(window_st* window)
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
        x11_GetWindowProperty(window->x11.handle,
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

static bool x11_window_focused(window_st* window)
{
    Window focused;
    int state;

    XGetInputFocus(g_wsi.x11.display, &focused, &state);
    return window->x11.handle == focused;
}

static bool x11_window_hovered(window_st* window)
{
    Window w = g_wsi.x11.root;
    while (w)
    {
        Window root;
        int rootX, rootY, childX, childY;
        unsigned int mask;

        x11_GrabErrorHandler();

        const Bool result = XQueryPointer(g_wsi.x11.display, w,
                                          &root, &w, &rootX, &rootY,
                                          &childX, &childY, &mask);

        x11_ReleaseErrorHandler();

        if (g_wsi.x11.errorCode == BadWindow)
            w = g_wsi.x11.root;
        else if (!result)
            return false;
        else if (w == window->x11.handle)
            return true;
    }

    return false;
}

static bool x11_window_iconified(window_st* window)
{
    return getWindowState(window) == IconicState;
}


static void x11_set_window_decorated(window_st* window, bool enabled)
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

static void x11_set_window_floating(window_st* window, bool enabled)
{
    if (!g_wsi.x11.NET_WM_STATE || !g_wsi.x11.NET_WM_STATE_ABOVE)
        return;

    if (x11_window_visible(window))
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
            x11_GetWindowProperty(window->x11.handle,
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

static void x11_set_window_opacity(window_st* window, float opacity)
{
    const CARD32 value = (CARD32) (0xffffffffu * (double) opacity);
    XChangeProperty(g_wsi.x11.display, window->x11.handle,
                    g_wsi.x11.NET_WM_WINDOW_OPACITY, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char*) &value, 1);
}

static float x11_get_window_opacity(window_st* window)
{
    float opacity = 1.f;

    if (XGetSelectionOwner(g_wsi.x11.display, g_wsi.x11.NET_WM_CM_Sx))
    {
        CARD32* value = NULL;

        if (x11_GetWindowProperty(window->x11.handle,
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


static void x11_get_window_pos(window_st* window, int* xpos, int* ypos)
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

static void x11_set_window_pos(window_st* window, int xpos, int ypos)
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

static void x11_get_window_size(window_st* window, int* width, int* height)
{
    XWindowAttributes attribs;
    XGetWindowAttributes(g_wsi.x11.display, window->x11.handle, &attribs);

    if (width)
        *width = attribs.width;
    if (height)
        *height = attribs.height;
}

static void x11_get_framebuffer_size(window_st* window, int* width, int* height)
{
    // X11 无服务端缩放：窗口尺寸本身即像素，帧缓冲尺寸 == 窗口尺寸
    x11_get_window_size(window, width, height);
}

static void x11_set_window_size(window_st* window, int width, int height)
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

static void x11_get_window_frame_size(window_st* window,
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

static void x11_set_window_size_limits(window_st* window,
                                 int minwidth, int minheight,
                                 int maxwidth, int maxheight)
{
    int width, height;
    x11_get_window_size(window, &width, &height);
    updateNormalHints(window, width, height);
    XFlush(g_wsi.x11.display);
}

static void x11_get_window_content_scale(window_st* window, float* xscale, float* yscale)
{
    if (xscale)
        *xscale = g_wsi.x11.contentScaleX;
    if (yscale)
        *yscale = g_wsi.x11.contentScaleY;
}

static void x11_set_window_aspect_ratio(window_st* window, int numer, int denom)
{
    int width, height;
    x11_get_window_size(window, &width, &height);
    updateNormalHints(window, width, height);
    XFlush(g_wsi.x11.display);
}


static void x11_set_window_resizable(window_st* window, bool enabled)
{
    int width, height;
    x11_get_window_size(window, &width, &height);
    updateNormalHints(window, width, height);
}


static void x11_show_window(window_st* window)
{
    if (x11_window_visible(window))
        return;

    if (window->floating && g_wsi.x11.NET_WM_STATE && g_wsi.x11.NET_WM_STATE_ABOVE)
    {
        Atom* states = NULL;
        const unsigned long count =
            x11_GetWindowProperty(window->x11.handle,
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

static void x11_hide_window(window_st* window)
{
    XUnmapWindow(g_wsi.x11.display, window->x11.handle);
    XFlush(g_wsi.x11.display);
}

static void x11_maximize_window(window_st* window)
{
    if (!g_wsi.x11.NET_WM_STATE ||
        !g_wsi.x11.NET_WM_STATE_MAXIMIZED_VERT ||
        !g_wsi.x11.NET_WM_STATE_MAXIMIZED_HORZ)
    {
        return;
    }

    if (x11_window_visible(window))
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
            x11_GetWindowProperty(window->x11.handle,
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

static void x11_restore_window(window_st* window)
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
    else if (x11_window_visible(window))
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

static void x11_focus_window(window_st* window)
{
    if (g_wsi.x11.NET_ACTIVE_WINDOW)
        sendEventToWM(window, g_wsi.x11.NET_ACTIVE_WINDOW, 1, 0, 0, 0, 0);
    else if (x11_window_visible(window))
    {
        XRaiseWindow(g_wsi.x11.display, window->x11.handle);
        XSetInputFocus(g_wsi.x11.display, window->x11.handle,
                       RevertToParent, CurrentTime);
    }

    XFlush(g_wsi.x11.display);
}

static void x11_iconify_window(window_st* window)
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

static void x11_request_window_attention(window_st* window)
{
    if (!g_wsi.x11.NET_WM_STATE || !g_wsi.x11.NET_WM_STATE_DEMANDS_ATTENTION)
        return;

    sendEventToWM(window,
                  g_wsi.x11.NET_WM_STATE,
                  _NET_WM_STATE_ADD,
                  g_wsi.x11.NET_WM_STATE_DEMANDS_ATTENTION,
                  0, 1, 0);
}


static void x11_set_cursor(window_st* window, cursor_st* cursor)
{
    if (window->cursorMode == SC_CURSOR_NORMAL ||
        window->cursorMode == SC_CURSOR_CAPTURED)
    {
        updateCursorImage(window);
        XFlush(g_wsi.x11.display);
    }
}

static bool x11_create_standard_cursor(cursor_st* cursor, int shape)
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

static bool x11_create_cursor(cursor_st* cursor,
                              const GLFWimage* image,
                              int xhot, int yhot)
{
    cursor->x11.handle = x11_CreateNativeCursor(image, xhot, yhot);
    if (!cursor->x11.handle)
        return false;

    return true;
}

static void x11_destroy_cursor(cursor_st* cursor)
{
    if (cursor->x11.handle)
        XFreeCursor(g_wsi.x11.display, cursor->x11.handle);
}

static void x11_get_cursor_pos(window_st* window, double* xpos, double* ypos)
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

static void x11_set_cursor_pos(window_st* window, double x, double y)
{
    // Store the new position so it can be recognized later
    window->x11.warpCursorPosX = (int) x;
    window->x11.warpCursorPosY = (int) y;

    XWarpPointer(g_wsi.x11.display, None, window->x11.handle,
                 0,0,0,0, (int) x, (int) y);
    XFlush(g_wsi.x11.display);
}

static void x11_set_cursor_mode(window_st* window, int mode)
{
    if (x11_window_focused(window))
    {
        if (mode == SC_CURSOR_DISABLED)
        {
            x11_get_cursor_pos(window,
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
            x11_set_cursor_pos(window,
                                 g_wsi.x11.restoreCursorPosX,
                                 g_wsi.x11.restoreCursorPosY);
        }
    }

    updateCursorImage(window);
    XFlush(g_wsi.x11.display);
}

static void x11_set_mouse_raw_motion(window_st *window, bool enabled)
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

static bool x11_mouse_raw_motion_supported(void)
{
    return g_wsi.x11.xi.available;
}


static int x11_get_key_scancode(int key)
{
    return g_wsi.x11.scancodes[key];
}

static const char* x11_get_scancode_name(int scancode)
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

    const uint32_t codepoint = x11_KeySym2Unicode(keysym);
    if (codepoint == GLFW_INVALID_CODEPOINT)
        return NULL;

    const size_t count = wsi_encode_urf8(g_wsi.x11.keynames[key], codepoint);
    if (count == 0)
        return NULL;

    g_wsi.x11.keynames[key][count] = '\0';
    return g_wsi.x11.keynames[key];
}

static void x11_set_clipboard_string(const char* string)
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

static const char* x11_get_clipboard_string(void)
{
    return getSelectionString(g_wsi.x11.CLIPBOARD);
}

void x11_set_window_monitor(window_st* window,
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
        x11_set_window_decorated(window, window->decorated);
        x11_set_window_floating(window, window->floating);
        releaseMonitor(window);
    }

    impl_on_win_monitor(window, monitor);
    updateNormalHints(window, width, height);

    if (window->monitor)
    {
        if (!x11_window_visible(window))
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

///////////////////////////////////////////////////////////////////////////////
// Window 
///////////////////////////////////////////////////////////////////////////////

// Apply disabled cursor mode to a focused window
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

// Create the X11 window (and its colormap)
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

///////////////////////////////////////////////////////////////////////////////
// app loop
///////////////////////////////////////////////////////////////////////////////

void x11_poll_events(void)
{
    // Drains available data from the empty event pipe
    for (;;)
    {
        char dummy[64];
        const ssize_t result = read(g_wsi.x11.emptyEventPipe[0], dummy, sizeof(dummy));
        if (result == -1 && errno != EINTR)
            break;
    }

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
        x11_get_window_size(window, &width, &height);

        // NOTE: Re-center the cursor only if it has moved since the last call,
        //       to avoid breaking sc_wsi_wait_events with MotionNotify
        if (window->x11.lastCursorPosX != width / 2 ||
            window->x11.lastCursorPosY != height / 2)
        {
            x11_set_cursor_pos(window, width / 2, height / 2);
        }
    }

    XFlush(g_wsi.x11.display);
}

// Wait for event data to arrive on any event file descriptor
// This avoids blocking other threads via the per-display Xlib lock that also
// covers GLX functions
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
        if (!sc_poll_posix(fds, sizeof(fds) / sizeof(fds[0]), timeout))
            return false;

        for (int i = 1; i < sizeof(fds) / sizeof(fds[0]); i++)
        {
            if (fds[i].revents & POLLIN)
                return true;
        }
    }

    return true;
}

void x11_wait_events(void)
{
    waitForAnyEvent(NULL);
    x11_poll_events();
}

void x11_wait_events_timeout(double timeout)
{
    waitForAnyEvent(&timeout);
    x11_poll_events();
}

void x11_post_empty_event(void)
{
    for (;;)
    {
        const char byte = 0;
        const ssize_t result = write(g_wsi.x11.emptyEventPipe[1], &byte, 1);
        if (result == 1 || (result == -1 && errno != EINTR))
            break;
    }
}

///////////////////////////////////////////////////////////////////////////////
// 接口集成
///////////////////////////////////////////////////////////////////////////////

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

bool x11_connect(int platformID, platform_st* platform)
{
    const platform_st x11 =
    {
        .platformID = SC_PLATFORM_X11,
        .init = x11_init,
        .terminate = x11_terminate,

        .pollEvents = x11_poll_events,
        .waitEvents = x11_wait_events,
        .waitEventsTimeout = x11_wait_events_timeout,
        .postEmptyEvent = x11_post_empty_event,

        .createWindow = x11_create_window,
        .destroyWindow = x11_destroy_window,
        .setWindowTitle = x11_set_window_title,
        .setWindowIcon = x11_set_window_icon,
        .setWindowMonitor = x11_set_window_monitor,
        .setWindowMousePassthrough = x11_set_window_mouse_passthrough,

        .setWindowDecorated = x11_set_window_decorated,
        .setWindowResizable = x11_set_window_resizable,
        .setWindowFloating = x11_set_window_floating,
        .setWindowOpacity = x11_set_window_opacity,
        .getWindowOpacity = x11_get_window_opacity,

        .getWindowPos = x11_get_window_pos,
        .setWindowPos = x11_set_window_pos,
        .getWindowSize = x11_get_window_size,
        .getFramebufferSize = x11_get_framebuffer_size,
        .setWindowSize = x11_set_window_size,
        .getWindowFrameSize = x11_get_window_frame_size,
        .setWindowSizeLimits = x11_set_window_size_limits,
        .getWindowContentScale = x11_get_window_content_scale,
        .setWindowAspectRatio = x11_set_window_aspect_ratio,

        .showWindow = x11_show_window,
        .hideWindow = x11_hide_window,
        .maximizeWindow = x11_maximize_window,
        .restoreWindow = x11_restore_window,
        .focusWindow = x11_focus_window,
        .iconifyWindow = x11_iconify_window,
        .requestWindowAttention = x11_request_window_attention,

        .windowVisible = x11_window_visible,
        .windowMaximized = x11_window_maximized,
        .windowFocused = x11_window_focused,
        .windowHovered = x11_window_hovered,
        .windowIconified = x11_window_iconified,

        .setCursor = x11_set_cursor,
        .createStandardCursor = x11_create_standard_cursor,
        .createCursor = x11_create_cursor,
        .destroyCursor = x11_destroy_cursor,
        .getCursorPos = x11_get_cursor_pos,
        .setCursorPos = x11_set_cursor_pos,
        .setCursorMode = x11_set_cursor_mode,
        .setRawMouseMotion = x11_set_mouse_raw_motion,
        .rawMouseMotionSupported = x11_mouse_raw_motion_supported,

        .getKeyScancode = x11_get_key_scancode,
        .getScancodeName = x11_get_scancode_name,
        .getClipboardString = x11_get_clipboard_string,
        .setClipboardString = x11_set_clipboard_string,

        .freeMonitor = x11_free_monitor,
        .getMonitorPos = x11_get_monitor_pos,
        .getMonitorWorkarea = x11_get_monitor_work_area,
        .getMonitorContentScale = x11_get_monitor_content_scale,
        .getVideoModes = x11_get_video_modes,
        .getVideoMode = x11_get_video_mode,
        .getGammaRamp = x11_get_gamma_ramp,
        .setGammaRamp = x11_set_gamma_ramp,
    };

    // HACK: If the application has left the locale as "C" then both wide
    //       character text input and explicit UTF-8 input via XIM will break
    //       This sets the CTYPE part of the current locale from the environment
    //       in the hope that it is set to something more sane than "C"
    if (strcmp(setlocale(LC_CTYPE, NULL), "C") == 0)
        setlocale(LC_CTYPE, "");

#if defined(__CYGWIN__)
    void* module = impl_platform_load_module("libX11-6.so");
#elif defined(__OpenBSD__) || defined(__NetBSD__)
    void* module = impl_platform_load_module("libX11.so");
#else
    void* module = impl_platform_load_module("libX11.so.6");
#endif
    if (!module)
    {
        if (platformID == SC_PLATFORM_X11)
            impl_on_error(SC_WSI_ERR_PLATFORM_ERROR, "X11: Failed to load Xlib");

        return false;
    }

    PFN_XInitThreads XInitThreads = (PFN_XInitThreads)
        impl_platform_get_module_symbol(module, "XInitThreads");
    PFN_XrmInitialize XrmInitialize = (PFN_XrmInitialize)
        impl_platform_get_module_symbol(module, "XrmInitialize");
    PFN_XOpenDisplay XOpenDisplay = (PFN_XOpenDisplay)
        impl_platform_get_module_symbol(module, "XOpenDisplay");
    if (!XInitThreads || !XrmInitialize || !XOpenDisplay)
    {
        if (platformID == SC_PLATFORM_X11)
            impl_on_error(SC_WSI_ERR_PLATFORM_ERROR, "X11: Failed to load Xlib entry point");

        impl_platform_unload_module(module);
        return false;
    }

    XInitThreads();
    XrmInitialize();

    Display* display = XOpenDisplay(NULL);
    if (!display)
    {
        if (platformID == SC_PLATFORM_X11)
        {
            const char* name = getenv("DISPLAY");
            if (name)
            {
                impl_on_error(SC_WSI_ERR_PLATFORM_UNAVAILABLE,
                                "X11: Failed to open display %s", name);
            }
            else
            {
                impl_on_error(SC_WSI_ERR_PLATFORM_UNAVAILABLE,
                                "X11: The DISPLAY environment variable is missing");
            }
        }

        impl_platform_unload_module(module);
        return false;
    }

    g_wsi.x11.display = display;
    g_wsi.x11.xlib.handle = module;

    *platform = x11;
    return true;
}

///////////////////////////////////////////////////////////////////////////////

#endif // WSI_X11

