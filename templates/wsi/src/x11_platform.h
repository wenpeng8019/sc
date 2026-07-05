
#include <unistd.h>
#include <signal.h>
#include <stdint.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xresource.h>
#include <X11/Xcursor/Xcursor.h>

// The XRandR extension provides mode setting and gamma control
#include <X11/extensions/Xrandr.h>

// The Xkb extension provides improved keyboard support
#include <X11/XKBlib.h>

// The Xinerama extension provides legacy monitor indices
#include <X11/extensions/Xinerama.h>

// The XInput extension provides raw mouse motion input
#include <X11/extensions/XInput2.h>

// The Shape extension provides custom window shapes
#include <X11/extensions/shape.h>

#define GLX_VENDOR 1
#define GLX_RGBA_BIT 0x00000001
#define GLX_WINDOW_BIT 0x00000001
#define GLX_DRAWABLE_TYPE 0x8010
#define GLX_RENDER_TYPE 0x8011
#define GLX_RGBA_TYPE 0x8014
#define GLX_DOUBLEBUFFER 5
#define GLX_STEREO 6
#define GLX_AUX_BUFFERS 7
#define GLX_RED_SIZE 8
#define GLX_GREEN_SIZE 9
#define GLX_BLUE_SIZE 10
#define GLX_ALPHA_SIZE 11
#define GLX_DEPTH_SIZE 12
#define GLX_STENCIL_SIZE 13
#define GLX_ACCUM_RED_SIZE 14
#define GLX_ACCUM_GREEN_SIZE 15
#define GLX_ACCUM_BLUE_SIZE 16
#define GLX_ACCUM_ALPHA_SIZE 17
#define GLX_SAMPLES 0x186a1
#define GLX_VISUAL_ID 0x800b

#define GLX_FRAMEBUFFER_SRGB_CAPABLE_ARB 0x20b2
#define GLX_CONTEXT_DEBUG_BIT_ARB 0x00000001
#define GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002
#define GLX_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#define GLX_CONTEXT_PROFILE_MASK_ARB 0x9126
#define GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x00000002
#define GLX_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define GLX_CONTEXT_MINOR_VERSION_ARB 0x2092
#define GLX_CONTEXT_FLAGS_ARB 0x2094
#define GLX_CONTEXT_ES2_PROFILE_BIT_EXT 0x00000004
#define GLX_CONTEXT_ROBUST_ACCESS_BIT_ARB 0x00000004
#define GLX_LOSE_CONTEXT_ON_RESET_ARB 0x8252
#define GLX_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB 0x8256
#define GLX_NO_RESET_NOTIFICATION_ARB 0x8261
#define GLX_CONTEXT_RELEASE_BEHAVIOR_ARB 0x2097
#define GLX_CONTEXT_RELEASE_BEHAVIOR_NONE_ARB 0
#define GLX_CONTEXT_RELEASE_BEHAVIOR_FLUSH_ARB 0x2098
#define GLX_CONTEXT_OPENGL_NO_ERROR_ARB 0x31b3

typedef XID GLXWindow;
typedef XID GLXDrawable;
typedef struct __GLXFBConfig* GLXFBConfig;
typedef struct __GLXcontext* GLXContext;
typedef void (*__GLXextproc)(void);

typedef XClassHint* (* PFN_XAllocClassHint)(void);
typedef XSizeHints* (* PFN_XAllocSizeHints)(void);
typedef XWMHints* (* PFN_XAllocWMHints)(void);
typedef int (* PFN_XChangeProperty)(Display*,Window,Atom,Atom,int,int,const unsigned char*,int);
typedef int (* PFN_XChangeWindowAttributes)(Display*,Window,unsigned long,XSetWindowAttributes*);
typedef Bool (* PFN_XCheckIfEvent)(Display*,XEvent*,Bool(*)(Display*,XEvent*,XPointer),XPointer);
typedef Bool (* PFN_XCheckTypedWindowEvent)(Display*,Window,int,XEvent*);
typedef int (* PFN_XCloseDisplay)(Display*);
typedef Status (* PFN_XCloseIM)(XIM);
typedef int (* PFN_XConvertSelection)(Display*,Atom,Atom,Atom,Window,Time);
typedef Colormap (* PFN_XCreateColormap)(Display*,Window,Visual*,int);
typedef Cursor (* PFN_XCreateFontCursor)(Display*,unsigned int);
typedef XIC (* PFN_XCreateIC)(XIM,...);
typedef Region (* PFN_XCreateRegion)(void);
typedef Window (* PFN_XCreateWindow)(Display*,Window,int,int,unsigned int,unsigned int,unsigned int,int,unsigned int,Visual*,unsigned long,XSetWindowAttributes*);
typedef int (* PFN_XDefineCursor)(Display*,Window,Cursor);
typedef int (* PFN_XDeleteContext)(Display*,XID,XContext);
typedef int (* PFN_XDeleteProperty)(Display*,Window,Atom);
typedef void (* PFN_XDestroyIC)(XIC);
typedef int (* PFN_XDestroyRegion)(Region);
typedef int (* PFN_XDestroyWindow)(Display*,Window);
typedef int (* PFN_XDisplayKeycodes)(Display*,int*,int*);
typedef int (* PFN_XEventsQueued)(Display*,int);
typedef Bool (* PFN_XFilterEvent)(XEvent*,Window);
typedef int (* PFN_XFindContext)(Display*,XID,XContext,XPointer*);
typedef int (* PFN_XFlush)(Display*);
typedef int (* PFN_XFree)(void*);
typedef int (* PFN_XFreeColormap)(Display*,Colormap);
typedef int (* PFN_XFreeCursor)(Display*,Cursor);
typedef void (* PFN_XFreeEventData)(Display*,XGenericEventCookie*);
typedef int (* PFN_XGetErrorText)(Display*,int,char*,int);
typedef Bool (* PFN_XGetEventData)(Display*,XGenericEventCookie*);
typedef char* (* PFN_XGetICValues)(XIC,...);
typedef char* (* PFN_XGetIMValues)(XIM,...);
typedef int (* PFN_XGetInputFocus)(Display*,Window*,int*);
typedef KeySym* (* PFN_XGetKeyboardMapping)(Display*,KeyCode,int,int*);
typedef int (* PFN_XGetScreenSaver)(Display*,int*,int*,int*,int*);
typedef Window (* PFN_XGetSelectionOwner)(Display*,Atom);
typedef XVisualInfo* (* PFN_XGetVisualInfo)(Display*,long,XVisualInfo*,int*);
typedef Status (* PFN_XGetWMNormalHints)(Display*,Window,XSizeHints*,long*);
typedef Status (* PFN_XGetWindowAttributes)(Display*,Window,XWindowAttributes*);
typedef int (* PFN_XGetWindowProperty)(Display*,Window,Atom,long,long,Bool,Atom,Atom*,int*,unsigned long*,unsigned long*,unsigned char**);
typedef int (* PFN_XGrabPointer)(Display*,Window,Bool,unsigned int,int,int,Window,Cursor,Time);
typedef Status (* PFN_XIconifyWindow)(Display*,Window,int);
typedef Status (* PFN_XInitThreads)(void);
typedef Atom (* PFN_XInternAtom)(Display*,const char*,Bool);
typedef int (* PFN_XLookupString)(XKeyEvent*,char*,int,KeySym*,XComposeStatus*);
typedef int (* PFN_XMapRaised)(Display*,Window);
typedef int (* PFN_XMapWindow)(Display*,Window);
typedef int (* PFN_XMoveResizeWindow)(Display*,Window,int,int,unsigned int,unsigned int);
typedef int (* PFN_XMoveWindow)(Display*,Window,int,int);
typedef int (* PFN_XNextEvent)(Display*,XEvent*);
typedef Display* (* PFN_XOpenDisplay)(const char*);
typedef XIM (* PFN_XOpenIM)(Display*,XrmDatabase*,char*,char*);
typedef int (* PFN_XPeekEvent)(Display*,XEvent*);
typedef int (* PFN_XPending)(Display*);
typedef Bool (* PFN_XQueryExtension)(Display*,const char*,int*,int*,int*);
typedef Bool (* PFN_XQueryPointer)(Display*,Window,Window*,Window*,int*,int*,int*,int*,unsigned int*);
typedef int (* PFN_XRaiseWindow)(Display*,Window);
typedef Bool (* PFN_XRegisterIMInstantiateCallback)(Display*,void*,char*,char*,XIDProc,XPointer);
typedef int (* PFN_XResizeWindow)(Display*,Window,unsigned int,unsigned int);
typedef char* (* PFN_XResourceManagerString)(Display*);
typedef int (* PFN_XSaveContext)(Display*,XID,XContext,const char*);
typedef int (* PFN_XSelectInput)(Display*,Window,long);
typedef Status (* PFN_XSendEvent)(Display*,Window,Bool,long,XEvent*);
typedef int (* PFN_XSetClassHint)(Display*,Window,XClassHint*);
typedef XErrorHandler (* PFN_XSetErrorHandler)(XErrorHandler);
typedef void (* PFN_XSetICFocus)(XIC);
typedef char* (* PFN_XSetIMValues)(XIM,...);
typedef int (* PFN_XSetInputFocus)(Display*,Window,int,Time);
typedef char* (* PFN_XSetLocaleModifiers)(const char*);
typedef int (* PFN_XSetScreenSaver)(Display*,int,int,int,int);
typedef int (* PFN_XSetSelectionOwner)(Display*,Atom,Window,Time);
typedef int (* PFN_XSetWMHints)(Display*,Window,XWMHints*);
typedef void (* PFN_XSetWMNormalHints)(Display*,Window,XSizeHints*);
typedef Status (* PFN_XSetWMProtocols)(Display*,Window,Atom*,int);
typedef Bool (* PFN_XSupportsLocale)(void);
typedef int (* PFN_XSync)(Display*,Bool);
typedef Bool (* PFN_XTranslateCoordinates)(Display*,Window,Window,int,int,int*,int*,Window*);
typedef int (* PFN_XUndefineCursor)(Display*,Window);
typedef int (* PFN_XUngrabPointer)(Display*,Time);
typedef int (* PFN_XUnmapWindow)(Display*,Window);
typedef void (* PFN_XUnsetICFocus)(XIC);
typedef VisualID (* PFN_XVisualIDFromVisual)(Visual*);
typedef int (* PFN_XWarpPointer)(Display*,Window,Window,int,int,unsigned int,unsigned int,int,int);
typedef void (* PFN_XkbFreeKeyboard)(XkbDescPtr,unsigned int,Bool);
typedef void (* PFN_XkbFreeNames)(XkbDescPtr,unsigned int,Bool);
typedef XkbDescPtr (* PFN_XkbGetMap)(Display*,unsigned int,unsigned int);
typedef Status (* PFN_XkbGetNames)(Display*,unsigned int,XkbDescPtr);
typedef Status (* PFN_XkbGetState)(Display*,unsigned int,XkbStatePtr);
typedef KeySym (* PFN_XkbKeycodeToKeysym)(Display*,KeyCode,int,int);
typedef Bool (* PFN_XkbQueryExtension)(Display*,int*,int*,int*,int*,int*);
typedef Bool (* PFN_XkbSelectEventDetails)(Display*,unsigned int,unsigned int,unsigned long,unsigned long);
typedef Bool (* PFN_XkbSetDetectableAutoRepeat)(Display*,Bool,Bool*);
typedef void (* PFN_XrmDestroyDatabase)(XrmDatabase);
typedef Bool (* PFN_XrmGetResource)(XrmDatabase,const char*,const char*,char**,XrmValue*);
typedef XrmDatabase (* PFN_XrmGetStringDatabase)(const char*);
typedef void (* PFN_XrmInitialize)(void);
typedef XrmQuark (* PFN_XrmUniqueQuark)(void);
typedef Bool (* PFN_XUnregisterIMInstantiateCallback)(Display*,void*,char*,char*,XIDProc,XPointer);
typedef int (* PFN_Xutf8LookupString)(XIC,XKeyPressedEvent*,char*,int,KeySym*,Status*);
typedef void (* PFN_Xutf8SetWMProperties)(Display*,Window,const char*,const char*,char**,int,XSizeHints*,XWMHints*,XClassHint*);
#define XAllocClassHint g_wsi.x11.xlib.AllocClassHint
#define XAllocSizeHints g_wsi.x11.xlib.AllocSizeHints
#define XAllocWMHints g_wsi.x11.xlib.AllocWMHints
#define XChangeProperty g_wsi.x11.xlib.ChangeProperty
#define XChangeWindowAttributes g_wsi.x11.xlib.ChangeWindowAttributes
#define XCheckIfEvent g_wsi.x11.xlib.CheckIfEvent
#define XCheckTypedWindowEvent g_wsi.x11.xlib.CheckTypedWindowEvent
#define XCloseDisplay g_wsi.x11.xlib.CloseDisplay
#define XCloseIM g_wsi.x11.xlib.CloseIM
#define XConvertSelection g_wsi.x11.xlib.ConvertSelection
#define XCreateColormap g_wsi.x11.xlib.CreateColormap
#define XCreateFontCursor g_wsi.x11.xlib.CreateFontCursor
#define XCreateIC g_wsi.x11.xlib.CreateIC
#define XCreateRegion g_wsi.x11.xlib.CreateRegion
#define XCreateWindow g_wsi.x11.xlib.CreateWindow
#define XDefineCursor g_wsi.x11.xlib.DefineCursor
#define XDeleteContext g_wsi.x11.xlib.DeleteContext
#define XDeleteProperty g_wsi.x11.xlib.DeleteProperty
#define XDestroyIC g_wsi.x11.xlib.DestroyIC
#define XDestroyRegion g_wsi.x11.xlib.DestroyRegion
#define XDestroyWindow g_wsi.x11.xlib.DestroyWindow
#define XDisplayKeycodes g_wsi.x11.xlib.DisplayKeycodes
#define XEventsQueued g_wsi.x11.xlib.EventsQueued
#define XFilterEvent g_wsi.x11.xlib.FilterEvent
#define XFindContext g_wsi.x11.xlib.FindContext
#define XFlush g_wsi.x11.xlib.Flush
#define XFree g_wsi.x11.xlib.Free
#define XFreeColormap g_wsi.x11.xlib.FreeColormap
#define XFreeCursor g_wsi.x11.xlib.FreeCursor
#define XFreeEventData g_wsi.x11.xlib.FreeEventData
#define XGetErrorText g_wsi.x11.xlib.GetErrorText
#define XGetEventData g_wsi.x11.xlib.GetEventData
#define XGetICValues g_wsi.x11.xlib.GetICValues
#define XGetIMValues g_wsi.x11.xlib.GetIMValues
#define XGetInputFocus g_wsi.x11.xlib.GetInputFocus
#define XGetKeyboardMapping g_wsi.x11.xlib.GetKeyboardMapping
#define XGetScreenSaver g_wsi.x11.xlib.GetScreenSaver
#define XGetSelectionOwner g_wsi.x11.xlib.GetSelectionOwner
#define XGetVisualInfo g_wsi.x11.xlib.GetVisualInfo
#define XGetWMNormalHints g_wsi.x11.xlib.GetWMNormalHints
#define XGetWindowAttributes g_wsi.x11.xlib.GetWindowAttributes
#define XGetWindowProperty g_wsi.x11.xlib.GetWindowProperty
#define XGrabPointer g_wsi.x11.xlib.GrabPointer
#define XIconifyWindow g_wsi.x11.xlib.IconifyWindow
#define XInternAtom g_wsi.x11.xlib.InternAtom
#define XLookupString g_wsi.x11.xlib.LookupString
#define XMapRaised g_wsi.x11.xlib.MapRaised
#define XMapWindow g_wsi.x11.xlib.MapWindow
#define XMoveResizeWindow g_wsi.x11.xlib.MoveResizeWindow
#define XMoveWindow g_wsi.x11.xlib.MoveWindow
#define XNextEvent g_wsi.x11.xlib.NextEvent
#define XOpenIM g_wsi.x11.xlib.OpenIM
#define XPeekEvent g_wsi.x11.xlib.PeekEvent
#define XPending g_wsi.x11.xlib.Pending
#define XQueryExtension g_wsi.x11.xlib.QueryExtension
#define XQueryPointer g_wsi.x11.xlib.QueryPointer
#define XRaiseWindow g_wsi.x11.xlib.RaiseWindow
#define XRegisterIMInstantiateCallback g_wsi.x11.xlib.RegisterIMInstantiateCallback
#define XResizeWindow g_wsi.x11.xlib.ResizeWindow
#define XResourceManagerString g_wsi.x11.xlib.ResourceManagerString
#define XSaveContext g_wsi.x11.xlib.SaveContext
#define XSelectInput g_wsi.x11.xlib.SelectInput
#define XSendEvent g_wsi.x11.xlib.SendEvent
#define XSetClassHint g_wsi.x11.xlib.SetClassHint
#define XSetErrorHandler g_wsi.x11.xlib.SetErrorHandler
#define XSetICFocus g_wsi.x11.xlib.SetICFocus
#define XSetIMValues g_wsi.x11.xlib.SetIMValues
#define XSetInputFocus g_wsi.x11.xlib.SetInputFocus
#define XSetLocaleModifiers g_wsi.x11.xlib.SetLocaleModifiers
#define XSetScreenSaver g_wsi.x11.xlib.SetScreenSaver
#define XSetSelectionOwner g_wsi.x11.xlib.SetSelectionOwner
#define XSetWMHints g_wsi.x11.xlib.SetWMHints
#define XSetWMNormalHints g_wsi.x11.xlib.SetWMNormalHints
#define XSetWMProtocols g_wsi.x11.xlib.SetWMProtocols
#define XSupportsLocale g_wsi.x11.xlib.SupportsLocale
#define XSync g_wsi.x11.xlib.Sync
#define XTranslateCoordinates g_wsi.x11.xlib.TranslateCoordinates
#define XUndefineCursor g_wsi.x11.xlib.UndefineCursor
#define XUngrabPointer g_wsi.x11.xlib.UngrabPointer
#define XUnmapWindow g_wsi.x11.xlib.UnmapWindow
#define XUnsetICFocus g_wsi.x11.xlib.UnsetICFocus
#define XVisualIDFromVisual g_wsi.x11.xlib.VisualIDFromVisual
#define XWarpPointer g_wsi.x11.xlib.WarpPointer
#define XkbFreeKeyboard g_wsi.x11.xkb.FreeKeyboard
#define XkbFreeNames g_wsi.x11.xkb.FreeNames
#define XkbGetMap g_wsi.x11.xkb.GetMap
#define XkbGetNames g_wsi.x11.xkb.GetNames
#define XkbGetState g_wsi.x11.xkb.GetState
#define XkbKeycodeToKeysym g_wsi.x11.xkb.KeycodeToKeysym
#define XkbQueryExtension g_wsi.x11.xkb.QueryExtension
#define XkbSelectEventDetails g_wsi.x11.xkb.SelectEventDetails
#define XkbSetDetectableAutoRepeat g_wsi.x11.xkb.SetDetectableAutoRepeat
#define XrmDestroyDatabase g_wsi.x11.xrm.DestroyDatabase
#define XrmGetResource g_wsi.x11.xrm.GetResource
#define XrmGetStringDatabase g_wsi.x11.xrm.GetStringDatabase
#define XrmUniqueQuark g_wsi.x11.xrm.UniqueQuark
#define XUnregisterIMInstantiateCallback g_wsi.x11.xlib.UnregisterIMInstantiateCallback
#define Xutf8LookupString g_wsi.x11.xlib.utf8LookupString
#define Xutf8SetWMProperties g_wsi.x11.xlib.utf8SetWMProperties

typedef XRRCrtcGamma* (* PFN_XRRAllocGamma)(int);
typedef void (* PFN_XRRFreeCrtcInfo)(XRRCrtcInfo*);
typedef void (* PFN_XRRFreeGamma)(XRRCrtcGamma*);
typedef void (* PFN_XRRFreeOutputInfo)(XRROutputInfo*);
typedef void (* PFN_XRRFreeScreenResources)(XRRScreenResources*);
typedef XRRCrtcGamma* (* PFN_XRRGetCrtcGamma)(Display*,RRCrtc);
typedef int (* PFN_XRRGetCrtcGammaSize)(Display*,RRCrtc);
typedef XRRCrtcInfo* (* PFN_XRRGetCrtcInfo) (Display*,XRRScreenResources*,RRCrtc);
typedef XRROutputInfo* (* PFN_XRRGetOutputInfo)(Display*,XRRScreenResources*,RROutput);
typedef RROutput (* PFN_XRRGetOutputPrimary)(Display*,Window);
typedef XRRScreenResources* (* PFN_XRRGetScreenResourcesCurrent)(Display*,Window);
typedef Bool (* PFN_XRRQueryExtension)(Display*,int*,int*);
typedef Status (* PFN_XRRQueryVersion)(Display*,int*,int*);
typedef void (* PFN_XRRSelectInput)(Display*,Window,int);
typedef Status (* PFN_XRRSetCrtcConfig)(Display*,XRRScreenResources*,RRCrtc,Time,int,int,RRMode,Rotation,RROutput*,int);
typedef void (* PFN_XRRSetCrtcGamma)(Display*,RRCrtc,XRRCrtcGamma*);
typedef int (* PFN_XRRUpdateConfiguration)(XEvent*);
#define XRRAllocGamma g_wsi.x11.randr.AllocGamma
#define XRRFreeCrtcInfo g_wsi.x11.randr.FreeCrtcInfo
#define XRRFreeGamma g_wsi.x11.randr.FreeGamma
#define XRRFreeOutputInfo g_wsi.x11.randr.FreeOutputInfo
#define XRRFreeScreenResources g_wsi.x11.randr.FreeScreenResources
#define XRRGetCrtcGamma g_wsi.x11.randr.GetCrtcGamma
#define XRRGetCrtcGammaSize g_wsi.x11.randr.GetCrtcGammaSize
#define XRRGetCrtcInfo g_wsi.x11.randr.GetCrtcInfo
#define XRRGetOutputInfo g_wsi.x11.randr.GetOutputInfo
#define XRRGetOutputPrimary g_wsi.x11.randr.GetOutputPrimary
#define XRRGetScreenResourcesCurrent g_wsi.x11.randr.GetScreenResourcesCurrent
#define XRRQueryExtension g_wsi.x11.randr.QueryExtension
#define XRRQueryVersion g_wsi.x11.randr.QueryVersion
#define XRRSelectInput g_wsi.x11.randr.SelectInput
#define XRRSetCrtcConfig g_wsi.x11.randr.SetCrtcConfig
#define XRRSetCrtcGamma g_wsi.x11.randr.SetCrtcGamma
#define XRRUpdateConfiguration g_wsi.x11.randr.UpdateConfiguration

typedef XcursorImage* (* PFN_XcursorImageCreate)(int,int);
typedef void (* PFN_XcursorImageDestroy)(XcursorImage*);
typedef Cursor (* PFN_XcursorImageLoadCursor)(Display*,const XcursorImage*);
typedef char* (* PFN_XcursorGetTheme)(Display*);
typedef int (* PFN_XcursorGetDefaultSize)(Display*);
typedef XcursorImage* (* PFN_XcursorLibraryLoadImage)(const char*,const char*,int);
#define XcursorImageCreate g_wsi.x11.xcursor.ImageCreate
#define XcursorImageDestroy g_wsi.x11.xcursor.ImageDestroy
#define XcursorImageLoadCursor g_wsi.x11.xcursor.ImageLoadCursor
#define XcursorGetTheme g_wsi.x11.xcursor.GetTheme
#define XcursorGetDefaultSize g_wsi.x11.xcursor.GetDefaultSize
#define XcursorLibraryLoadImage g_wsi.x11.xcursor.LibraryLoadImage

typedef Bool (* PFN_XineramaIsActive)(Display*);
typedef Bool (* PFN_XineramaQueryExtension)(Display*,int*,int*);
typedef XineramaScreenInfo* (* PFN_XineramaQueryScreens)(Display*,int*);
#define XineramaIsActive g_wsi.x11.xinerama.IsActive
#define XineramaQueryExtension g_wsi.x11.xinerama.QueryExtension
#define XineramaQueryScreens g_wsi.x11.xinerama.QueryScreens

typedef XID xcb_window_t;
typedef XID xcb_visualid_t;
typedef struct xcb_connection_t xcb_connection_t;
typedef xcb_connection_t* (* PFN_XGetXCBConnection)(Display*);
#define XGetXCBConnection g_wsi.x11.x11xcb.GetXCBConnection

typedef Bool (* PFN_XF86VidModeQueryExtension)(Display*,int*,int*);
typedef Bool (* PFN_XF86VidModeGetGammaRamp)(Display*,int,int,unsigned short*,unsigned short*,unsigned short*);
typedef Bool (* PFN_XF86VidModeSetGammaRamp)(Display*,int,int,unsigned short*,unsigned short*,unsigned short*);
typedef Bool (* PFN_XF86VidModeGetGammaRampSize)(Display*,int,int*);
#define XF86VidModeQueryExtension g_wsi.x11.vidmode.QueryExtension
#define XF86VidModeGetGammaRamp g_wsi.x11.vidmode.GetGammaRamp
#define XF86VidModeSetGammaRamp g_wsi.x11.vidmode.SetGammaRamp
#define XF86VidModeGetGammaRampSize g_wsi.x11.vidmode.GetGammaRampSize

typedef Status (* PFN_XIQueryVersion)(Display*,int*,int*);
typedef int (* PFN_XISelectEvents)(Display*,Window,XIEventMask*,int);
#define XIQueryVersion g_wsi.x11.xi.QueryVersion
#define XISelectEvents g_wsi.x11.xi.SelectEvents

typedef Bool (* PFN_XRenderQueryExtension)(Display*,int*,int*);
typedef Status (* PFN_XRenderQueryVersion)(Display*dpy,int*,int*);
typedef XRenderPictFormat* (* PFN_XRenderFindVisualFormat)(Display*,Visual const*);
#define XRenderQueryExtension g_wsi.x11.xrender.QueryExtension
#define XRenderQueryVersion g_wsi.x11.xrender.QueryVersion
#define XRenderFindVisualFormat g_wsi.x11.xrender.FindVisualFormat

typedef Bool (* PFN_XShapeQueryExtension)(Display*,int*,int*);
typedef Status (* PFN_XShapeQueryVersion)(Display*dpy,int*,int*);
typedef void (* PFN_XShapeCombineRegion)(Display*,Window,int,int,int,Region,int);
typedef void (* PFN_XShapeCombineMask)(Display*,Window,int,int,int,Pixmap,int);

#define XShapeQueryExtension g_wsi.x11.xshape.QueryExtension
#define XShapeQueryVersion g_wsi.x11.xshape.QueryVersion
#define XShapeCombineRegion g_wsi.x11.xshape.ShapeCombineRegion
#define XShapeCombineMask g_wsi.x11.xshape.ShapeCombineMask

typedef int (*PFNGLXGETFBCONFIGATTRIBPROC)(Display*,GLXFBConfig,int,int*);
typedef const char* (*PFNGLXGETCLIENTSTRINGPROC)(Display*,int);
typedef Bool (*PFNGLXQUERYEXTENSIONPROC)(Display*,int*,int*);
typedef Bool (*PFNGLXQUERYVERSIONPROC)(Display*,int*,int*);
typedef void (*PFNGLXDESTROYCONTEXTPROC)(Display*,GLXContext);
typedef Bool (*PFNGLXMAKECURRENTPROC)(Display*,GLXDrawable,GLXContext);
typedef void (*PFNGLXSWAPBUFFERSPROC)(Display*,GLXDrawable);
typedef const char* (*PFNGLXQUERYEXTENSIONSSTRINGPROC)(Display*,int);
typedef GLXFBConfig* (*PFNGLXGETFBCONFIGSPROC)(Display*,int,int*);
typedef GLXContext (*PFNGLXCREATENEWCONTEXTPROC)(Display*,GLXFBConfig,int,GLXContext,Bool);
typedef __GLXextproc (* PFNGLXGETPROCADDRESSPROC)(const GLubyte *procName);
typedef void (*PFNGLXSWAPINTERVALEXTPROC)(Display*,GLXDrawable,int);
typedef XVisualInfo* (*PFNGLXGETVISUALFROMFBCONFIGPROC)(Display*,GLXFBConfig);
typedef GLXWindow (*PFNGLXCREATEWINDOWPROC)(Display*,GLXFBConfig,Window,const int*);
typedef void (*PFNGLXDESTROYWINDOWPROC)(Display*,GLXWindow);

typedef int (*PFNGLXSWAPINTERVALMESAPROC)(int);
typedef int (*PFNGLXSWAPINTERVALSGIPROC)(int);
typedef GLXContext (*PFNGLXCREATECONTEXTATTRIBSARBPROC)(Display*,GLXFBConfig,GLXContext,Bool,const int*);

// libGL.so function pointer typedefs
#define glXGetFBConfigs g_wsi.glx.GetFBConfigs
#define glXGetFBConfigAttrib g_wsi.glx.GetFBConfigAttrib
#define glXGetClientString g_wsi.glx.GetClientString
#define glXQueryExtension g_wsi.glx.QueryExtension
#define glXQueryVersion g_wsi.glx.QueryVersion
#define glXDestroyContext g_wsi.glx.DestroyContext
#define glXMakeCurrent g_wsi.glx.MakeCurrent
#define glXSwapBuffers g_wsi.glx.SwapBuffers
#define glXQueryExtensionsString g_wsi.glx.QueryExtensionsString
#define glXCreateNewContext g_wsi.glx.CreateNewContext
#define glXGetVisualFromFBConfig g_wsi.glx.GetVisualFromFBConfig
#define glXCreateWindow g_wsi.glx.CreateWindow
#define glXDestroyWindow g_wsi.glx.DestroyWindow

typedef VkFlags VkXlibSurfaceCreateFlagsKHR;
typedef VkFlags VkXcbSurfaceCreateFlagsKHR;

typedef struct VkXlibSurfaceCreateInfoKHR
{
    VkStructureType             sType;
    const void*                 pNext;
    VkXlibSurfaceCreateFlagsKHR flags;
    Display*                    dpy;
    Window                      window;
} VkXlibSurfaceCreateInfoKHR;

typedef struct VkXcbSurfaceCreateInfoKHR
{
    VkStructureType             sType;
    const void*                 pNext;
    VkXcbSurfaceCreateFlagsKHR  flags;
    xcb_connection_t*           connection;
    xcb_window_t                window;
} VkXcbSurfaceCreateInfoKHR;

typedef VkResult (APIENTRY *PFN_vkCreateXlibSurfaceKHR)(VkInstance,const VkXlibSurfaceCreateInfoKHR*,const VkAllocationCallbacks*,VkSurfaceKHR*);
typedef VkBool32 (APIENTRY *PFN_vkGetPhysicalDeviceXlibPresentationSupportKHR)(VkPhysicalDevice,uint32_t,Display*,VisualID);
typedef VkResult (APIENTRY *PFN_vkCreateXcbSurfaceKHR)(VkInstance,const VkXcbSurfaceCreateInfoKHR*,const VkAllocationCallbacks*,VkSurfaceKHR*);
typedef VkBool32 (APIENTRY *PFN_vkGetPhysicalDeviceXcbPresentationSupportKHR)(VkPhysicalDevice,uint32_t,xcb_connection_t*,xcb_visualid_t);

#define GLFW_X11_WINDOW_STATE           _sc_windowX11 x11;
#define GLFW_X11_LIBRARY_WINDOW_STATE   _GLFWlibraryX11 x11;
#define GLFW_X11_MONITOR_STATE          _sc_monitorX11 x11;
#define GLFW_X11_CURSOR_STATE           _sc_cursorX11 x11;

#define GLFW_GLX_CONTEXT_STATE          _GLFWcontextGLX glx;
#define GLFW_GLX_LIBRARY_CONTEXT_STATE  _GLFWlibraryGLX glx;

#define GLFW_INVALID_CODEPOINT 0xffffffffu

// GLX-specific per-context data
//
typedef struct _GLFWcontextGLX
{
    GLXContext      handle;
    GLXWindow       window;
    GLXFBConfig     fbconfig;
} _GLFWcontextGLX;

// GLX-specific global data
//
typedef struct _GLFWlibraryGLX
{
    int             major, minor;
    int             eventBase;
    int             errorBase;

    void*           handle;

    // GLX 1.3 functions
    PFNGLXGETFBCONFIGSPROC              GetFBConfigs;
    PFNGLXGETFBCONFIGATTRIBPROC         GetFBConfigAttrib;
    PFNGLXGETCLIENTSTRINGPROC           GetClientString;
    PFNGLXQUERYEXTENSIONPROC            QueryExtension;
    PFNGLXQUERYVERSIONPROC              QueryVersion;
    PFNGLXDESTROYCONTEXTPROC            DestroyContext;
    PFNGLXMAKECURRENTPROC               MakeCurrent;
    PFNGLXSWAPBUFFERSPROC               SwapBuffers;
    PFNGLXQUERYEXTENSIONSSTRINGPROC     QueryExtensionsString;
    PFNGLXCREATENEWCONTEXTPROC          CreateNewContext;
    PFNGLXGETVISUALFROMFBCONFIGPROC     GetVisualFromFBConfig;
    PFNGLXCREATEWINDOWPROC              CreateWindow;
    PFNGLXDESTROYWINDOWPROC             DestroyWindow;

    // GLX 1.4 and extension functions
    PFNGLXGETPROCADDRESSPROC            GetProcAddress;
    PFNGLXGETPROCADDRESSPROC            GetProcAddressARB;
    PFNGLXSWAPINTERVALSGIPROC           SwapIntervalSGI;
    PFNGLXSWAPINTERVALEXTPROC           SwapIntervalEXT;
    PFNGLXSWAPINTERVALMESAPROC          SwapIntervalMESA;
    PFNGLXCREATECONTEXTATTRIBSARBPROC   CreateContextAttribsARB;
    bool            SGI_swap_control;
    bool            EXT_swap_control;
    bool            MESA_swap_control;
    bool            ARB_multisample;
    bool            ARB_framebuffer_sRGB;
    bool            EXT_framebuffer_sRGB;
    bool            ARB_create_context;
    bool            ARB_create_context_profile;
    bool            ARB_create_context_robustness;
    bool            EXT_create_context_es2_profile;
    bool            ARB_create_context_no_error;
    bool            ARB_context_flush_control;
} _GLFWlibraryGLX;

// X11-specific per-window data
//
typedef struct _sc_windowX11
{
    Colormap        colormap;
    Window          handle;
    Window          parent;
    XIC             ic;

    bool        overrideRedirect;
    bool        iconified;
    bool        maximized;

    // Whether the visual supports framebuffer transparency
    bool        transparent;

    // Cached position and size used to filter out duplicate events
    int             width, height;
    int             xpos, ypos;

    // The last received cursor position, regardless of source
    int             lastCursorPosX, lastCursorPosY;
    // The last position the cursor was warped to by GLFW
    int             warpCursorPosX, warpCursorPosY;

    // The time of the last KeyPress event per keycode, for discarding
    // duplicate key events generated for some keys by ibus
    Time            keyPressTimes[256];
} _sc_windowX11;

// X11-specific global data
//
typedef struct _GLFWlibraryX11
{
    Display*        display;
    int             screen;
    Window          root;

    // System content scale
    float           contentScaleX, contentScaleY;
    // Helper window for IPC
    Window          helperWindowHandle;
    // Invisible cursor for hidden cursor mode
    Cursor          hiddenCursorHandle;
    // Context for mapping window XIDs to window_st pointers
    XContext        context;
    // XIM input method
    XIM             im;
    // The previous X error handler, to be restored later
    XErrorHandler   errorHandler;
    // Most recent error code received by X error handler
    int             errorCode;
    // Primary selection string (while the primary selection is owned)
    char*           primarySelectionString;
    // Clipboard string (while the selection is owned)
    char*           clipboardString;
    // Key name string
    char            keynames[SC_KEY_LAST + 1][5];
    // X11 keycode to GLFW key LUT
    short int       keycodes[256];
    // GLFW key to X11 keycode LUT
    short int       scancodes[SC_KEY_LAST + 1];
    // Where to place the cursor when re-enabled
    double          restoreCursorPosX, restoreCursorPosY;
    // The window whose disabled cursor mode is active
    window_st*    disabledCursorWindow;
    int             emptyEventPipe[2];

    // Window manager atoms
    Atom            NET_SUPPORTED;
    Atom            NET_SUPPORTING_WM_CHECK;
    Atom            WM_PROTOCOLS;
    Atom            WM_STATE;
    Atom            WM_DELETE_WINDOW;
    Atom            NET_WM_NAME;
    Atom            NET_WM_ICON_NAME;
    Atom            NET_WM_ICON;
    Atom            NET_WM_PID;
    Atom            NET_WM_PING;
    Atom            NET_WM_WINDOW_TYPE;
    Atom            NET_WM_WINDOW_TYPE_NORMAL;
    Atom            NET_WM_STATE;
    Atom            NET_WM_STATE_ABOVE;
    Atom            NET_WM_STATE_FULLSCREEN;
    Atom            NET_WM_STATE_MAXIMIZED_VERT;
    Atom            NET_WM_STATE_MAXIMIZED_HORZ;
    Atom            NET_WM_STATE_DEMANDS_ATTENTION;
    Atom            NET_WM_BYPASS_COMPOSITOR;
    Atom            NET_WM_FULLSCREEN_MONITORS;
    Atom            NET_WM_WINDOW_OPACITY;
    Atom            NET_WM_CM_Sx;
    Atom            NET_WORKAREA;
    Atom            NET_CURRENT_DESKTOP;
    Atom            NET_ACTIVE_WINDOW;
    Atom            NET_FRAME_EXTENTS;
    Atom            NET_REQUEST_FRAME_EXTENTS;
    Atom            MOTIF_WM_HINTS;

    // Xdnd (drag and drop) atoms
    Atom            XdndAware;
    Atom            XdndEnter;
    Atom            XdndPosition;
    Atom            XdndStatus;
    Atom            XdndActionCopy;
    Atom            XdndDrop;
    Atom            XdndFinished;
    Atom            XdndSelection;
    Atom            XdndTypeList;
    Atom            text_uri_list;

    // Selection (clipboard) atoms
    Atom            TARGETS;
    Atom            MULTIPLE;
    Atom            INCR;
    Atom            CLIPBOARD;
    Atom            PRIMARY;
    Atom            CLIPBOARD_MANAGER;
    Atom            SAVE_TARGETS;
    Atom            NULL_;
    Atom            UTF8_STRING;
    Atom            COMPOUND_STRING;
    Atom            ATOM_PAIR;
    Atom            GLFW_SELECTION;

    struct {
        void*       handle;
        bool    utf8;
        PFN_XAllocClassHint AllocClassHint;
        PFN_XAllocSizeHints AllocSizeHints;
        PFN_XAllocWMHints AllocWMHints;
        PFN_XChangeProperty ChangeProperty;
        PFN_XChangeWindowAttributes ChangeWindowAttributes;
        PFN_XCheckIfEvent CheckIfEvent;
        PFN_XCheckTypedWindowEvent CheckTypedWindowEvent;
        PFN_XCloseDisplay CloseDisplay;
        PFN_XCloseIM CloseIM;
        PFN_XConvertSelection ConvertSelection;
        PFN_XCreateColormap CreateColormap;
        PFN_XCreateFontCursor CreateFontCursor;
        PFN_XCreateIC CreateIC;
        PFN_XCreateRegion CreateRegion;
        PFN_XCreateWindow CreateWindow;
        PFN_XDefineCursor DefineCursor;
        PFN_XDeleteContext DeleteContext;
        PFN_XDeleteProperty DeleteProperty;
        PFN_XDestroyIC DestroyIC;
        PFN_XDestroyRegion DestroyRegion;
        PFN_XDestroyWindow DestroyWindow;
        PFN_XDisplayKeycodes DisplayKeycodes;
        PFN_XEventsQueued EventsQueued;
        PFN_XFilterEvent FilterEvent;
        PFN_XFindContext FindContext;
        PFN_XFlush Flush;
        PFN_XFree Free;
        PFN_XFreeColormap FreeColormap;
        PFN_XFreeCursor FreeCursor;
        PFN_XFreeEventData FreeEventData;
        PFN_XGetErrorText GetErrorText;
        PFN_XGetEventData GetEventData;
        PFN_XGetICValues GetICValues;
        PFN_XGetIMValues GetIMValues;
        PFN_XGetInputFocus GetInputFocus;
        PFN_XGetKeyboardMapping GetKeyboardMapping;
        PFN_XGetScreenSaver GetScreenSaver;
        PFN_XGetSelectionOwner GetSelectionOwner;
        PFN_XGetVisualInfo GetVisualInfo;
        PFN_XGetWMNormalHints GetWMNormalHints;
        PFN_XGetWindowAttributes GetWindowAttributes;
        PFN_XGetWindowProperty GetWindowProperty;
        PFN_XGrabPointer GrabPointer;
        PFN_XIconifyWindow IconifyWindow;
        PFN_XInternAtom InternAtom;
        PFN_XLookupString LookupString;
        PFN_XMapRaised MapRaised;
        PFN_XMapWindow MapWindow;
        PFN_XMoveResizeWindow MoveResizeWindow;
        PFN_XMoveWindow MoveWindow;
        PFN_XNextEvent NextEvent;
        PFN_XOpenIM OpenIM;
        PFN_XPeekEvent PeekEvent;
        PFN_XPending Pending;
        PFN_XQueryExtension QueryExtension;
        PFN_XQueryPointer QueryPointer;
        PFN_XRaiseWindow RaiseWindow;
        PFN_XRegisterIMInstantiateCallback RegisterIMInstantiateCallback;
        PFN_XResizeWindow ResizeWindow;
        PFN_XResourceManagerString ResourceManagerString;
        PFN_XSaveContext SaveContext;
        PFN_XSelectInput SelectInput;
        PFN_XSendEvent SendEvent;
        PFN_XSetClassHint SetClassHint;
        PFN_XSetErrorHandler SetErrorHandler;
        PFN_XSetICFocus SetICFocus;
        PFN_XSetIMValues SetIMValues;
        PFN_XSetInputFocus SetInputFocus;
        PFN_XSetLocaleModifiers SetLocaleModifiers;
        PFN_XSetScreenSaver SetScreenSaver;
        PFN_XSetSelectionOwner SetSelectionOwner;
        PFN_XSetWMHints SetWMHints;
        PFN_XSetWMNormalHints SetWMNormalHints;
        PFN_XSetWMProtocols SetWMProtocols;
        PFN_XSupportsLocale SupportsLocale;
        PFN_XSync Sync;
        PFN_XTranslateCoordinates TranslateCoordinates;
        PFN_XUndefineCursor UndefineCursor;
        PFN_XUngrabPointer UngrabPointer;
        PFN_XUnmapWindow UnmapWindow;
        PFN_XUnsetICFocus UnsetICFocus;
        PFN_XVisualIDFromVisual VisualIDFromVisual;
        PFN_XWarpPointer WarpPointer;
        PFN_XUnregisterIMInstantiateCallback UnregisterIMInstantiateCallback;
        PFN_Xutf8LookupString utf8LookupString;
        PFN_Xutf8SetWMProperties utf8SetWMProperties;
    } xlib;

    struct {
        PFN_XrmDestroyDatabase DestroyDatabase;
        PFN_XrmGetResource GetResource;
        PFN_XrmGetStringDatabase GetStringDatabase;
        PFN_XrmUniqueQuark UniqueQuark;
    } xrm;

    struct {
        bool    available;
        void*       handle;
        int         eventBase;
        int         errorBase;
        int         major;
        int         minor;
        bool    gammaBroken;
        bool    monitorBroken;
        PFN_XRRAllocGamma AllocGamma;
        PFN_XRRFreeCrtcInfo FreeCrtcInfo;
        PFN_XRRFreeGamma FreeGamma;
        PFN_XRRFreeOutputInfo FreeOutputInfo;
        PFN_XRRFreeScreenResources FreeScreenResources;
        PFN_XRRGetCrtcGamma GetCrtcGamma;
        PFN_XRRGetCrtcGammaSize GetCrtcGammaSize;
        PFN_XRRGetCrtcInfo GetCrtcInfo;
        PFN_XRRGetOutputInfo GetOutputInfo;
        PFN_XRRGetOutputPrimary GetOutputPrimary;
        PFN_XRRGetScreenResourcesCurrent GetScreenResourcesCurrent;
        PFN_XRRQueryExtension QueryExtension;
        PFN_XRRQueryVersion QueryVersion;
        PFN_XRRSelectInput SelectInput;
        PFN_XRRSetCrtcConfig SetCrtcConfig;
        PFN_XRRSetCrtcGamma SetCrtcGamma;
        PFN_XRRUpdateConfiguration UpdateConfiguration;
    } randr;

    struct {
        bool     available;
        bool     detectable;
        int          majorOpcode;
        int          eventBase;
        int          errorBase;
        int          major;
        int          minor;
        unsigned int group;
        PFN_XkbFreeKeyboard FreeKeyboard;
        PFN_XkbFreeNames FreeNames;
        PFN_XkbGetMap GetMap;
        PFN_XkbGetNames GetNames;
        PFN_XkbGetState GetState;
        PFN_XkbKeycodeToKeysym KeycodeToKeysym;
        PFN_XkbQueryExtension QueryExtension;
        PFN_XkbSelectEventDetails SelectEventDetails;
        PFN_XkbSetDetectableAutoRepeat SetDetectableAutoRepeat;
    } xkb;

    struct {
        int         count;
        int         timeout;
        int         interval;
        int         blanking;
        int         exposure;
    } saver;

    struct {
        int         version;
        Window      source;
        Atom        format;
    } xdnd;

    struct {
        void*       handle;
        PFN_XcursorImageCreate ImageCreate;
        PFN_XcursorImageDestroy ImageDestroy;
        PFN_XcursorImageLoadCursor ImageLoadCursor;
        PFN_XcursorGetTheme GetTheme;
        PFN_XcursorGetDefaultSize GetDefaultSize;
        PFN_XcursorLibraryLoadImage LibraryLoadImage;
    } xcursor;

    struct {
        bool    available;
        void*       handle;
        int         major;
        int         minor;
        PFN_XineramaIsActive IsActive;
        PFN_XineramaQueryExtension QueryExtension;
        PFN_XineramaQueryScreens QueryScreens;
    } xinerama;

    struct {
        void*       handle;
        PFN_XGetXCBConnection GetXCBConnection;
    } x11xcb;

    struct {
        bool    available;
        void*       handle;
        int         eventBase;
        int         errorBase;
        PFN_XF86VidModeQueryExtension QueryExtension;
        PFN_XF86VidModeGetGammaRamp GetGammaRamp;
        PFN_XF86VidModeSetGammaRamp SetGammaRamp;
        PFN_XF86VidModeGetGammaRampSize GetGammaRampSize;
    } vidmode;

    struct {
        bool    available;
        void*       handle;
        int         majorOpcode;
        int         eventBase;
        int         errorBase;
        int         major;
        int         minor;
        PFN_XIQueryVersion QueryVersion;
        PFN_XISelectEvents SelectEvents;
    } xi;

    struct {
        bool    available;
        void*       handle;
        int         major;
        int         minor;
        int         eventBase;
        int         errorBase;
        PFN_XRenderQueryExtension QueryExtension;
        PFN_XRenderQueryVersion QueryVersion;
        PFN_XRenderFindVisualFormat FindVisualFormat;
    } xrender;

    struct {
        bool    available;
        void*       handle;
        int         major;
        int         minor;
        int         eventBase;
        int         errorBase;
        PFN_XShapeQueryExtension QueryExtension;
        PFN_XShapeCombineRegion ShapeCombineRegion;
        PFN_XShapeQueryVersion QueryVersion;
        PFN_XShapeCombineMask ShapeCombineMask;
    } xshape;
} _GLFWlibraryX11;

// X11-specific per-monitor data
//
typedef struct _sc_monitorX11
{
    RROutput        output;
    RRCrtc          crtc;
    RRMode          oldMode;

    // Index of corresponding Xinerama screen,
    // for EWMH full screen window placement
    int             index;
} _sc_monitorX11;

// X11-specific per-cursor data
//
typedef struct _sc_cursorX11
{
    Cursor handle;
} _sc_cursorX11;


bool _glfwConnectX11(int platformID, platform_st* platform);
int _glfwInitX11(void);
void _glfwTerminateX11(void);

bool _glfwCreateWindowX11(window_st* window, const wnd_config_st* wndconfig, const _GLFWctxconfig* ctxconfig, const _GLFWfbconfig* fbconfig);
void _glfwDestroyWindowX11(window_st* window);
void _glfwSetWindowTitleX11(window_st* window, const char* title);
void _glfwSetWindowIconX11(window_st* window, int count, const GLFWimage* images);
void _glfwGetWindowPosX11(window_st* window, int* xpos, int* ypos);
void _glfwSetWindowPosX11(window_st* window, int xpos, int ypos);
void _glfwGetWindowSizeX11(window_st* window, int* width, int* height);
void _glfwSetWindowSizeX11(window_st* window, int width, int height);
void _glfwSetWindowSizeLimitsX11(window_st* window, int minwidth, int minheight, int maxwidth, int maxheight);
void _glfwSetWindowAspectRatioX11(window_st* window, int numer, int denom);
void _glfwGetFramebufferSizeX11(window_st* window, int* width, int* height);
void _glfwGetWindowFrameSizeX11(window_st* window, int* left, int* top, int* right, int* bottom);
void _glfwGetWindowContentScaleX11(window_st* window, float* xscale, float* yscale);
void _glfwIconifyWindowX11(window_st* window);
void _glfwRestoreWindowX11(window_st* window);
void _glfwMaximizeWindowX11(window_st* window);
void _glfwShowWindowX11(window_st* window);
void _glfwHideWindowX11(window_st* window);
void _glfwRequestWindowAttentionX11(window_st* window);
void _glfwFocusWindowX11(window_st* window);
void _glfwSetWindowMonitorX11(window_st* window, monitor_st* monitor, int xpos, int ypos, int width, int height, int refreshRate);
bool _glfwWindowFocusedX11(window_st* window);
bool _glfwWindowIconifiedX11(window_st* window);
bool _glfwWindowVisibleX11(window_st* window);
bool _glfwWindowMaximizedX11(window_st* window);
bool _glfwWindowHoveredX11(window_st* window);
bool _glfwFramebufferTransparentX11(window_st* window);
void _glfwSetWindowResizableX11(window_st* window, bool enabled);
void _glfwSetWindowDecoratedX11(window_st* window, bool enabled);
void _glfwSetWindowFloatingX11(window_st* window, bool enabled);
float _glfwGetWindowOpacityX11(window_st* window);
void _glfwSetWindowOpacityX11(window_st* window, float opacity);
void _glfwSetWindowMousePassthroughX11(window_st* window, bool enabled);

void _glfwSetRawMouseMotionX11(window_st *window, bool enabled);
bool _glfwRawMouseMotionSupportedX11(void);

void _glfwPollEventsX11(void);
void _glfwWaitEventsX11(void);
void _glfwWaitEventsTimeoutX11(double timeout);
void _glfwPostEmptyEventX11(void);

void _glfwGetCursorPosX11(window_st* window, double* xpos, double* ypos);
void _glfwSetCursorPosX11(window_st* window, double xpos, double ypos);
void _glfwSetCursorModeX11(window_st* window, int mode);
const char* _glfwGetScancodeNameX11(int scancode);
int _glfwGetKeyScancodeX11(int key);
bool _glfwCreateCursorX11(cursor_st* cursor, const GLFWimage* image, int xhot, int yhot);
bool _glfwCreateStandardCursorX11(cursor_st* cursor, int shape);
void _glfwDestroyCursorX11(cursor_st* cursor);
void _glfwSetCursorX11(window_st* window, cursor_st* cursor);
void _glfwSetClipboardStringX11(const char* string);
const char* _glfwGetClipboardStringX11(void);

EGLenum _glfwGetEGLPlatformX11(EGLint** attribs);
EGLNativeDisplayType _glfwGetEGLNativeDisplayX11(void);
EGLNativeWindowType _glfwGetEGLNativeWindowX11(window_st* window);

void _glfwGetRequiredInstanceExtensionsX11(char** extensions);
bool _glfwGetPhysicalDevicePresentationSupportX11(VkInstance instance, VkPhysicalDevice device, uint32_t queuefamily);
VkResult _glfwCreateWindowSurfaceX11(VkInstance instance, window_st* window, const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface);

void wsi_free_monitorX11(monitor_st* monitor);
void _glfwGetMonitorPosX11(monitor_st* monitor, int* xpos, int* ypos);
void _glfwGetMonitorContentScaleX11(monitor_st* monitor, float* xscale, float* yscale);
void _glfwGetMonitorWorkareaX11(monitor_st* monitor, int* xpos, int* ypos, int* width, int* height);
GLFWvidmode* _glfwGetVideoModesX11(monitor_st* monitor, int* count);
bool _glfwGetVideoModeX11(monitor_st* monitor, GLFWvidmode* mode);
bool _glfwGetGammaRampX11(monitor_st* monitor, GLFWgammaramp* ramp);
void _glfwSetGammaRampX11(monitor_st* monitor, const GLFWgammaramp* ramp);

void _glfwPollMonitorsX11(void);
void _glfwSetVideoModeX11(monitor_st* monitor, const GLFWvidmode* desired);
void _glfwRestoreVideoModeX11(monitor_st* monitor);

Cursor _glfwCreateNativeCursorX11(const GLFWimage* image, int xhot, int yhot);

unsigned long _glfwGetWindowPropertyX11(Window window,
                                        Atom property,
                                        Atom type,
                                        unsigned char** value);
bool _glfwIsVisualTransparentX11(Visual* visual);

uint32_t _glfwKeySym2UnicodeX11(unsigned int keysym);

void _glfwGrabErrorHandlerX11(void);
void _glfwReleaseErrorHandlerX11(void);
void _glfwInputErrorX11(int error, const char* message);

void _glfwPushSelectionToManagerX11(void);
void _glfwCreateInputContextX11(window_st* window);

bool _glfwInitGLX(void);
void _glfwTerminateGLX(void);
bool _glfwCreateContextGLX(window_st* window,
                               const _GLFWctxconfig* ctxconfig,
                               const _GLFWfbconfig* fbconfig);
void _glfwDestroyContextGLX(window_st* window);
bool _glfwChooseVisualGLX(const wnd_config_st* wndconfig,
                              const _GLFWctxconfig* ctxconfig,
                              const _GLFWfbconfig* fbconfig,
                              Visual** visual, int* depth);

