
#include "internal.h"

#if defined(WSI_X11)

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
#include <locale.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>


// Translate the X11 KeySyms for a key to a GLFW key code
// NOTE: This is only used as a fallback, in case the XKB method fails
//       It is layout-dependent and will fail partially on most non-US layouts
//
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
//
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

    for (int scancode = scancodeMin;  scancode <= scancodeMax;  scancode++)
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

    XFree(keysyms);
}

// Check whether the IM has a usable style
//
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
            _glfwCreateInputContextX11(window);
    }
}

// Return the atom ID only if it is listed in the specified array
//
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
//
static void detectEWMH(void)
{
    // First we read the _NET_SUPPORTING_WM_CHECK property on the root window

    Window* windowFromRoot = NULL;
    if (!_glfwGetWindowPropertyX11(g_wsi.x11.root,
                                   g_wsi.x11.NET_SUPPORTING_WM_CHECK,
                                   XA_WINDOW,
                                   (unsigned char**) &windowFromRoot))
    {
        return;
    }

    _glfwGrabErrorHandlerX11();

    // If it exists, it should be the XID of a top-level window
    // Then we look for the same property on that window

    Window* windowFromChild = NULL;
    if (!_glfwGetWindowPropertyX11(*windowFromRoot,
                                   g_wsi.x11.NET_SUPPORTING_WM_CHECK,
                                   XA_WINDOW,
                                   (unsigned char**) &windowFromChild))
    {
        _glfwReleaseErrorHandlerX11();
        XFree(windowFromRoot);
        return;
    }

    _glfwReleaseErrorHandlerX11();

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
        _glfwGetWindowPropertyX11(g_wsi.x11.root,
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
//
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

// Retrieve system content scale via folklore heuristics
//
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

// Create a blank cursor for hidden and disabled cursor modes
//
static Cursor createHiddenCursor(void)
{
    unsigned char pixels[16 * 16 * 4] = { 0 };
    GLFWimage image = { 16, 16, pixels };
    return _glfwCreateNativeCursorX11(&image, 0, 0);
}

// Create a helper window for IPC
//
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
//
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

// X error handler
//
static int errorHandler(Display *display, XErrorEvent* event)
{
    if (g_wsi.x11.display != display)
        return 0;

    g_wsi.x11.errorCode = event->error_code;
    return 0;
}


//////////////////////////////////////////////////////////////////////////
//////                       GLFW internal API                      //////
//////////////////////////////////////////////////////////////////////////

// Sets the X error handler callback
//
void _glfwGrabErrorHandlerX11(void)
{
    assert(g_wsi.x11.errorHandler == NULL);
    g_wsi.x11.errorCode = Success;
    g_wsi.x11.errorHandler = XSetErrorHandler(errorHandler);
}

// Clears the X error handler callback
//
void _glfwReleaseErrorHandlerX11(void)
{
    // Synchronize to make sure all commands are processed
    XSync(g_wsi.x11.display, False);
    XSetErrorHandler(g_wsi.x11.errorHandler);
    g_wsi.x11.errorHandler = NULL;
}

// Reports the specified error, appending information about the last X error
//
void _glfwInputErrorX11(int error, const char* message)
{
    char buffer[_SC_MESSAGE_SIZE];
    XGetErrorText(g_wsi.x11.display, g_wsi.x11.errorCode,
                  buffer, sizeof(buffer));

    impl_on_error(error, "%s: %s", message, buffer);
}

// Creates a native cursor object from the specified image and hotspot
//
Cursor _glfwCreateNativeCursorX11(const GLFWimage* image, int xhot, int yhot)
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


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

bool _glfwConnectX11(int platformID, platform_st* platform)
{
    const platform_st x11 =
    {
        .platformID = SC_PLATFORM_X11,
        .init = _glfwInitX11,
        .terminate = _glfwTerminateX11,
        .getCursorPos = _glfwGetCursorPosX11,
        .setCursorPos = _glfwSetCursorPosX11,
        .setCursorMode = _glfwSetCursorModeX11,
        .setRawMouseMotion = _glfwSetRawMouseMotionX11,
        .rawMouseMotionSupported = _glfwRawMouseMotionSupportedX11,
        .createCursor = _glfwCreateCursorX11,
        .createStandardCursor = _glfwCreateStandardCursorX11,
        .destroyCursor = _glfwDestroyCursorX11,
        .setCursor = _glfwSetCursorX11,
        .getScancodeName = _glfwGetScancodeNameX11,
        .getKeyScancode = _glfwGetKeyScancodeX11,
        .setClipboardString = _glfwSetClipboardStringX11,
        .getClipboardString = _glfwGetClipboardStringX11,
        .freeMonitor = wsi_free_monitorX11,
        .getMonitorPos = _glfwGetMonitorPosX11,
        .getMonitorContentScale = _glfwGetMonitorContentScaleX11,
        .getMonitorWorkarea = _glfwGetMonitorWorkareaX11,
        .getVideoModes = _glfwGetVideoModesX11,
        .getVideoMode = _glfwGetVideoModeX11,
        .getGammaRamp = _glfwGetGammaRampX11,
        .setGammaRamp = _glfwSetGammaRampX11,
        .createWindow = _glfwCreateWindowX11,
        .destroyWindow = _glfwDestroyWindowX11,
        .setWindowTitle = _glfwSetWindowTitleX11,
        .setWindowIcon = _glfwSetWindowIconX11,
        .getWindowPos = _glfwGetWindowPosX11,
        .setWindowPos = _glfwSetWindowPosX11,
        .getWindowSize = _glfwGetWindowSizeX11,
        .setWindowSize = _glfwSetWindowSizeX11,
        .setWindowSizeLimits = _glfwSetWindowSizeLimitsX11,
        .setWindowAspectRatio = _glfwSetWindowAspectRatioX11,
        .getWindowFrameSize = _glfwGetWindowFrameSizeX11,
        .getWindowContentScale = _glfwGetWindowContentScaleX11,
        .iconifyWindow = _glfwIconifyWindowX11,
        .restoreWindow = _glfwRestoreWindowX11,
        .maximizeWindow = _glfwMaximizeWindowX11,
        .showWindow = _glfwShowWindowX11,
        .hideWindow = _glfwHideWindowX11,
        .requestWindowAttention = _glfwRequestWindowAttentionX11,
        .focusWindow = _glfwFocusWindowX11,
        .setWindowMonitor = _glfwSetWindowMonitorX11,
        .windowFocused = _glfwWindowFocusedX11,
        .windowIconified = _glfwWindowIconifiedX11,
        .windowVisible = _glfwWindowVisibleX11,
        .windowMaximized = _glfwWindowMaximizedX11,
        .windowHovered = _glfwWindowHoveredX11,
        .getWindowOpacity = _glfwGetWindowOpacityX11,
        .setWindowResizable = _glfwSetWindowResizableX11,
        .setWindowDecorated = _glfwSetWindowDecoratedX11,
        .setWindowFloating = _glfwSetWindowFloatingX11,
        .setWindowOpacity = _glfwSetWindowOpacityX11,
        .setWindowMousePassthrough = _glfwSetWindowMousePassthroughX11,
        .pollEvents = _glfwPollEventsX11,
        .waitEvents = _glfwWaitEventsX11,
        .waitEventsTimeout = _glfwWaitEventsTimeoutX11,
        .postEmptyEvent = _glfwPostEmptyEventX11
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

int _glfwInitX11(void)
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

    _glfwPollMonitorsX11();
    return true;
}

void _glfwTerminateX11(void)
{
    if (g_wsi.x11.helperWindowHandle)
    {
        if (XGetSelectionOwner(g_wsi.x11.display, g_wsi.x11.CLIPBOARD) ==
            g_wsi.x11.helperWindowHandle)
        {
            _glfwPushSelectionToManagerX11();
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

#endif // WSI_X11

