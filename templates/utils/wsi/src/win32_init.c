
#include "internal.h"

#if defined(WSI_WIN32)

#include <stdlib.h>

static const GUID _glfw_GUID_DEVINTERFACE_HID =
    {0x4d1e55b2,0xf16f,0x11cf,{0x88,0xcb,0x00,0x11,0x11,0x00,0x00,0x30}};

#define GUID_DEVINTERFACE_HID _glfw_GUID_DEVINTERFACE_HID

#if defined(WSI_USE_HYBRID_HPG) || defined(WSI_USE_OPTIMUS_HPG)

#if defined(WSI_EXPORTS)
 #pragma message("These symbols must be exported by the executable and have no effect in a DLL")
#endif

// Executables (but not DLLs) exporting this symbol with this value will be
// automatically directed to the high-performance GPU on Nvidia Optimus systems
// with up-to-date drivers
//
__declspec(dllexport) DWORD NvOptimusEnablement = 1;

// Executables (but not DLLs) exporting this symbol with this value will be
// automatically directed to the high-performance GPU on AMD PowerXpress systems
// with up-to-date drivers
//
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;

#endif // WSI_USE_HYBRID_HPG

#if defined(WSI_EXPORTS)

// GLFW DLL entry point
//
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

#endif // WSI_EXPORTS

// Load necessary libraries (DLLs)
//
static bool loadLibraries(void)
{
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (const WCHAR*) &g_wsi,
                            (HMODULE*) &g_wsi.win32.instance))
    {
        _glfwInputErrorWin32(SC_WSI_ERR_PLATFORM_ERROR,
                             "Win32: Failed to retrieve own module handle");
        return false;
    }

    g_wsi.win32.user32.instance = impl_platform_load_module("user32.dll");
    if (!g_wsi.win32.user32.instance)
    {
        _glfwInputErrorWin32(SC_WSI_ERR_PLATFORM_ERROR,
                             "Win32: Failed to load user32.dll");
        return false;
    }

    g_wsi.win32.user32.EnableNonClientDpiScaling_ = (PFN_EnableNonClientDpiScaling)
        impl_platform_get_module_symbol(g_wsi.win32.user32.instance, "EnableNonClientDpiScaling");
    g_wsi.win32.user32.SetProcessDpiAwarenessContext_ = (PFN_SetProcessDpiAwarenessContext)
        impl_platform_get_module_symbol(g_wsi.win32.user32.instance, "SetProcessDpiAwarenessContext");
    g_wsi.win32.user32.GetDpiForWindow_ = (PFN_GetDpiForWindow)
        impl_platform_get_module_symbol(g_wsi.win32.user32.instance, "GetDpiForWindow");
    g_wsi.win32.user32.AdjustWindowRectExForDpi_ = (PFN_AdjustWindowRectExForDpi)
        impl_platform_get_module_symbol(g_wsi.win32.user32.instance, "AdjustWindowRectExForDpi");
    g_wsi.win32.user32.GetSystemMetricsForDpi_ = (PFN_GetSystemMetricsForDpi)
        impl_platform_get_module_symbol(g_wsi.win32.user32.instance, "GetSystemMetricsForDpi");

    g_wsi.win32.dinput8.instance = impl_platform_load_module("dinput8.dll");
    if (g_wsi.win32.dinput8.instance)
    {
        g_wsi.win32.dinput8.Create = (PFN_DirectInput8Create)
            impl_platform_get_module_symbol(g_wsi.win32.dinput8.instance, "DirectInput8Create");
    }

    {
        int i;
        const char* names[] =
        {
            "xinput1_4.dll",
            "xinput1_3.dll",
            "xinput9_1_0.dll",
            "xinput1_2.dll",
            "xinput1_1.dll",
            NULL
        };

        for (i = 0;  names[i];  i++)
        {
            g_wsi.win32.xinput.instance = impl_platform_load_module(names[i]);
            if (g_wsi.win32.xinput.instance)
            {
                g_wsi.win32.xinput.GetCapabilities = (PFN_XInputGetCapabilities)
                    impl_platform_get_module_symbol(g_wsi.win32.xinput.instance, "XInputGetCapabilities");
                g_wsi.win32.xinput.GetState = (PFN_XInputGetState)
                    impl_platform_get_module_symbol(g_wsi.win32.xinput.instance, "XInputGetState");

                break;
            }
        }
    }

    g_wsi.win32.dwmapi.instance = impl_platform_load_module("dwmapi.dll");
    if (g_wsi.win32.dwmapi.instance)
    {
        g_wsi.win32.dwmapi.IsCompositionEnabled = (PFN_DwmIsCompositionEnabled)
            impl_platform_get_module_symbol(g_wsi.win32.dwmapi.instance, "DwmIsCompositionEnabled");
        g_wsi.win32.dwmapi.Flush = (PFN_DwmFlush)
            impl_platform_get_module_symbol(g_wsi.win32.dwmapi.instance, "DwmFlush");
        g_wsi.win32.dwmapi.EnableBlurBehindWindow = (PFN_DwmEnableBlurBehindWindow)
            impl_platform_get_module_symbol(g_wsi.win32.dwmapi.instance, "DwmEnableBlurBehindWindow");
        g_wsi.win32.dwmapi.GetColorizationColor = (PFN_DwmGetColorizationColor)
            impl_platform_get_module_symbol(g_wsi.win32.dwmapi.instance, "DwmGetColorizationColor");
    }

    g_wsi.win32.shcore.instance = impl_platform_load_module("shcore.dll");
    if (g_wsi.win32.shcore.instance)
    {
        g_wsi.win32.shcore.SetProcessDpiAwareness_ = (PFN_SetProcessDpiAwareness)
            impl_platform_get_module_symbol(g_wsi.win32.shcore.instance, "SetProcessDpiAwareness");
        g_wsi.win32.shcore.GetDpiForMonitor_ = (PFN_GetDpiForMonitor)
            impl_platform_get_module_symbol(g_wsi.win32.shcore.instance, "GetDpiForMonitor");
    }

    g_wsi.win32.ntdll.instance = impl_platform_load_module("ntdll.dll");
    if (g_wsi.win32.ntdll.instance)
    {
        g_wsi.win32.ntdll.RtlVerifyVersionInfo_ = (PFN_RtlVerifyVersionInfo)
            impl_platform_get_module_symbol(g_wsi.win32.ntdll.instance, "RtlVerifyVersionInfo");
    }

    return true;
}

// Create key code translation tables
//
static void createKeyTables(void)
{
    int scancode;

    memset(g_wsi.win32.keycodes, -1, sizeof(g_wsi.win32.keycodes));
    memset(g_wsi.win32.scancodes, -1, sizeof(g_wsi.win32.scancodes));

    g_wsi.win32.keycodes[0x00B] = SC_KEY_0;
    g_wsi.win32.keycodes[0x002] = SC_KEY_1;
    g_wsi.win32.keycodes[0x003] = SC_KEY_2;
    g_wsi.win32.keycodes[0x004] = SC_KEY_3;
    g_wsi.win32.keycodes[0x005] = SC_KEY_4;
    g_wsi.win32.keycodes[0x006] = SC_KEY_5;
    g_wsi.win32.keycodes[0x007] = SC_KEY_6;
    g_wsi.win32.keycodes[0x008] = SC_KEY_7;
    g_wsi.win32.keycodes[0x009] = SC_KEY_8;
    g_wsi.win32.keycodes[0x00A] = SC_KEY_9;
    g_wsi.win32.keycodes[0x01E] = SC_KEY_A;
    g_wsi.win32.keycodes[0x030] = SC_KEY_B;
    g_wsi.win32.keycodes[0x02E] = SC_KEY_C;
    g_wsi.win32.keycodes[0x020] = SC_KEY_D;
    g_wsi.win32.keycodes[0x012] = SC_KEY_E;
    g_wsi.win32.keycodes[0x021] = SC_KEY_F;
    g_wsi.win32.keycodes[0x022] = SC_KEY_G;
    g_wsi.win32.keycodes[0x023] = SC_KEY_H;
    g_wsi.win32.keycodes[0x017] = SC_KEY_I;
    g_wsi.win32.keycodes[0x024] = SC_KEY_J;
    g_wsi.win32.keycodes[0x025] = SC_KEY_K;
    g_wsi.win32.keycodes[0x026] = SC_KEY_L;
    g_wsi.win32.keycodes[0x032] = SC_KEY_M;
    g_wsi.win32.keycodes[0x031] = SC_KEY_N;
    g_wsi.win32.keycodes[0x018] = SC_KEY_O;
    g_wsi.win32.keycodes[0x019] = SC_KEY_P;
    g_wsi.win32.keycodes[0x010] = SC_KEY_Q;
    g_wsi.win32.keycodes[0x013] = SC_KEY_R;
    g_wsi.win32.keycodes[0x01F] = SC_KEY_S;
    g_wsi.win32.keycodes[0x014] = SC_KEY_T;
    g_wsi.win32.keycodes[0x016] = SC_KEY_U;
    g_wsi.win32.keycodes[0x02F] = SC_KEY_V;
    g_wsi.win32.keycodes[0x011] = SC_KEY_W;
    g_wsi.win32.keycodes[0x02D] = SC_KEY_X;
    g_wsi.win32.keycodes[0x015] = SC_KEY_Y;
    g_wsi.win32.keycodes[0x02C] = SC_KEY_Z;

    g_wsi.win32.keycodes[0x028] = SC_KEY_APOSTROPHE;
    g_wsi.win32.keycodes[0x02B] = SC_KEY_BACKSLASH;
    g_wsi.win32.keycodes[0x033] = SC_KEY_COMMA;
    g_wsi.win32.keycodes[0x00D] = SC_KEY_EQUAL;
    g_wsi.win32.keycodes[0x029] = SC_KEY_GRAVE_ACCENT;
    g_wsi.win32.keycodes[0x01A] = SC_KEY_LEFT_BRACKET;
    g_wsi.win32.keycodes[0x00C] = SC_KEY_MINUS;
    g_wsi.win32.keycodes[0x034] = SC_KEY_PERIOD;
    g_wsi.win32.keycodes[0x01B] = SC_KEY_RIGHT_BRACKET;
    g_wsi.win32.keycodes[0x027] = SC_KEY_SEMICOLON;
    g_wsi.win32.keycodes[0x035] = SC_KEY_SLASH;
    g_wsi.win32.keycodes[0x056] = SC_KEY_WORLD_2;

    g_wsi.win32.keycodes[0x00E] = SC_KEY_BACKSPACE;
    g_wsi.win32.keycodes[0x153] = SC_KEY_DELETE;
    g_wsi.win32.keycodes[0x14F] = SC_KEY_END;
    g_wsi.win32.keycodes[0x01C] = SC_KEY_ENTER;
    g_wsi.win32.keycodes[0x001] = SC_KEY_ESCAPE;
    g_wsi.win32.keycodes[0x147] = SC_KEY_HOME;
    g_wsi.win32.keycodes[0x152] = SC_KEY_INSERT;
    g_wsi.win32.keycodes[0x15D] = SC_KEY_MENU;
    g_wsi.win32.keycodes[0x151] = SC_KEY_PAGE_DOWN;
    g_wsi.win32.keycodes[0x149] = SC_KEY_PAGE_UP;
    g_wsi.win32.keycodes[0x045] = SC_KEY_PAUSE;
    g_wsi.win32.keycodes[0x039] = SC_KEY_SPACE;
    g_wsi.win32.keycodes[0x00F] = SC_KEY_TAB;
    g_wsi.win32.keycodes[0x03A] = SC_KEY_CAPS_LOCK;
    g_wsi.win32.keycodes[0x145] = SC_KEY_NUM_LOCK;
    g_wsi.win32.keycodes[0x046] = SC_KEY_SCROLL_LOCK;
    g_wsi.win32.keycodes[0x03B] = SC_KEY_F1;
    g_wsi.win32.keycodes[0x03C] = SC_KEY_F2;
    g_wsi.win32.keycodes[0x03D] = SC_KEY_F3;
    g_wsi.win32.keycodes[0x03E] = SC_KEY_F4;
    g_wsi.win32.keycodes[0x03F] = SC_KEY_F5;
    g_wsi.win32.keycodes[0x040] = SC_KEY_F6;
    g_wsi.win32.keycodes[0x041] = SC_KEY_F7;
    g_wsi.win32.keycodes[0x042] = SC_KEY_F8;
    g_wsi.win32.keycodes[0x043] = SC_KEY_F9;
    g_wsi.win32.keycodes[0x044] = SC_KEY_F10;
    g_wsi.win32.keycodes[0x057] = SC_KEY_F11;
    g_wsi.win32.keycodes[0x058] = SC_KEY_F12;
    g_wsi.win32.keycodes[0x064] = SC_KEY_F13;
    g_wsi.win32.keycodes[0x065] = SC_KEY_F14;
    g_wsi.win32.keycodes[0x066] = SC_KEY_F15;
    g_wsi.win32.keycodes[0x067] = SC_KEY_F16;
    g_wsi.win32.keycodes[0x068] = SC_KEY_F17;
    g_wsi.win32.keycodes[0x069] = SC_KEY_F18;
    g_wsi.win32.keycodes[0x06A] = SC_KEY_F19;
    g_wsi.win32.keycodes[0x06B] = SC_KEY_F20;
    g_wsi.win32.keycodes[0x06C] = SC_KEY_F21;
    g_wsi.win32.keycodes[0x06D] = SC_KEY_F22;
    g_wsi.win32.keycodes[0x06E] = SC_KEY_F23;
    g_wsi.win32.keycodes[0x076] = SC_KEY_F24;
    g_wsi.win32.keycodes[0x038] = SC_KEY_LEFT_ALT;
    g_wsi.win32.keycodes[0x01D] = SC_KEY_LEFT_CONTROL;
    g_wsi.win32.keycodes[0x02A] = SC_KEY_LEFT_SHIFT;
    g_wsi.win32.keycodes[0x15B] = SC_KEY_LEFT_SUPER;
    g_wsi.win32.keycodes[0x137] = SC_KEY_PRINT_SCREEN;
    g_wsi.win32.keycodes[0x138] = SC_KEY_RIGHT_ALT;
    g_wsi.win32.keycodes[0x11D] = SC_KEY_RIGHT_CONTROL;
    g_wsi.win32.keycodes[0x036] = SC_KEY_RIGHT_SHIFT;
    g_wsi.win32.keycodes[0x15C] = SC_KEY_RIGHT_SUPER;
    g_wsi.win32.keycodes[0x150] = SC_KEY_DOWN;
    g_wsi.win32.keycodes[0x14B] = SC_KEY_LEFT;
    g_wsi.win32.keycodes[0x14D] = SC_KEY_RIGHT;
    g_wsi.win32.keycodes[0x148] = SC_KEY_UP;

    g_wsi.win32.keycodes[0x052] = SC_KEY_KP_0;
    g_wsi.win32.keycodes[0x04F] = SC_KEY_KP_1;
    g_wsi.win32.keycodes[0x050] = SC_KEY_KP_2;
    g_wsi.win32.keycodes[0x051] = SC_KEY_KP_3;
    g_wsi.win32.keycodes[0x04B] = SC_KEY_KP_4;
    g_wsi.win32.keycodes[0x04C] = SC_KEY_KP_5;
    g_wsi.win32.keycodes[0x04D] = SC_KEY_KP_6;
    g_wsi.win32.keycodes[0x047] = SC_KEY_KP_7;
    g_wsi.win32.keycodes[0x048] = SC_KEY_KP_8;
    g_wsi.win32.keycodes[0x049] = SC_KEY_KP_9;
    g_wsi.win32.keycodes[0x04E] = SC_KEY_KP_ADD;
    g_wsi.win32.keycodes[0x053] = SC_KEY_KP_DECIMAL;
    g_wsi.win32.keycodes[0x135] = SC_KEY_KP_DIVIDE;
    g_wsi.win32.keycodes[0x11C] = SC_KEY_KP_ENTER;
    g_wsi.win32.keycodes[0x059] = SC_KEY_KP_EQUAL;
    g_wsi.win32.keycodes[0x037] = SC_KEY_KP_MULTIPLY;
    g_wsi.win32.keycodes[0x04A] = SC_KEY_KP_SUBTRACT;

    for (scancode = 0;  scancode < 512;  scancode++)
    {
        if (g_wsi.win32.keycodes[scancode] > 0)
            g_wsi.win32.scancodes[g_wsi.win32.keycodes[scancode]] = scancode;
    }
}

// Window procedure for the hidden helper window
//
static LRESULT CALLBACK helperWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_DISPLAYCHANGE:
            _glfwPollMonitorsWin32();
            break;

        case WM_DEVICECHANGE:
        {

            break;
        }
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

// Creates a dummy window for behind-the-scenes work
//
static bool createHelperWindow(void)
{
    MSG msg;
    WNDCLASSEXW wc = { sizeof(wc) };

    wc.style         = CS_OWNDC;
    wc.lpfnWndProc   = (WNDPROC) helperWindowProc;
    wc.hInstance     = g_wsi.win32.instance;
    wc.lpszClassName = L"GLFW3 Helper";

    g_wsi.win32.helperWindowClass = RegisterClassExW(&wc);
    if (!g_wsi.win32.helperWindowClass)
    {
        _glfwInputErrorWin32(SC_WSI_ERR_PLATFORM_ERROR,
                             "Win32: Failed to register helper window class");
        return false;
    }

    g_wsi.win32.helperWindowHandle =
        CreateWindowExW(WS_EX_OVERLAPPEDWINDOW,
                        MAKEINTATOM(g_wsi.win32.helperWindowClass),
                        L"GLFW message window",
                        WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                        0, 0, 1, 1,
                        NULL, NULL,
                        g_wsi.win32.instance,
                        NULL);

    if (!g_wsi.win32.helperWindowHandle)
    {
        _glfwInputErrorWin32(SC_WSI_ERR_PLATFORM_ERROR,
                             "Win32: Failed to create helper window");
        return false;
    }

    // HACK: The command to the first ShowWindow call is ignored if the parent
    //       process passed along a STARTUPINFO, so clear that with a no-op call
    ShowWindow(g_wsi.win32.helperWindowHandle, SW_HIDE);

    // Register for HID device notifications
    {
        DEV_BROADCAST_DEVICEINTERFACE_W dbi;
        ZeroMemory(&dbi, sizeof(dbi));
        dbi.dbcc_size = sizeof(dbi);
        dbi.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        dbi.dbcc_classguid = GUID_DEVINTERFACE_HID;

        g_wsi.win32.deviceNotificationHandle =
            RegisterDeviceNotificationW(g_wsi.win32.helperWindowHandle,
                                        (DEV_BROADCAST_HDR*) &dbi,
                                        DEVICE_NOTIFY_WINDOW_HANDLE);
    }

    while (PeekMessageW(&msg, g_wsi.win32.helperWindowHandle, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

   return true;
}

//////////////////////////////////////////////////////////////////////////
//////                       GLFW internal API                      //////
//////////////////////////////////////////////////////////////////////////

// Returns a wide string version of the specified UTF-8 string
//
WCHAR* _glfwCreateWideStringFromUTF8Win32(const char* source)
{
    WCHAR* target;
    int count;

    count = MultiByteToWideChar(CP_UTF8, 0, source, -1, NULL, 0);
    if (!count)
    {
        _glfwInputErrorWin32(SC_WSI_ERR_PLATFORM_ERROR,
                             "Win32: Failed to convert string from UTF-8");
        return NULL;
    }

    target = wsi_calloc(count, sizeof(WCHAR));

    if (!MultiByteToWideChar(CP_UTF8, 0, source, -1, target, count))
    {
        _glfwInputErrorWin32(SC_WSI_ERR_PLATFORM_ERROR,
                             "Win32: Failed to convert string from UTF-8");
        wsi_free(target);
        return NULL;
    }

    return target;
}

// Returns a UTF-8 string version of the specified wide string
//
char* _glfwCreateUTF8FromWideStringWin32(const WCHAR* source)
{
    char* target;
    int size;

    size = WideCharToMultiByte(CP_UTF8, 0, source, -1, NULL, 0, NULL, NULL);
    if (!size)
    {
        _glfwInputErrorWin32(SC_WSI_ERR_PLATFORM_ERROR,
                             "Win32: Failed to convert string to UTF-8");
        return NULL;
    }

    target = wsi_calloc(size, 1);

    if (!WideCharToMultiByte(CP_UTF8, 0, source, -1, target, size, NULL, NULL))
    {
        _glfwInputErrorWin32(SC_WSI_ERR_PLATFORM_ERROR,
                             "Win32: Failed to convert string to UTF-8");
        wsi_free(target);
        return NULL;
    }

    return target;
}

// Reports the specified error, appending information about the last Win32 error
//
void _glfwInputErrorWin32(int error, const char* description)
{
    WCHAR buffer[_SC_MESSAGE_SIZE] = L"";
    char message[_SC_MESSAGE_SIZE] = "";

    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS |
                       FORMAT_MESSAGE_MAX_WIDTH_MASK,
                   NULL,
                   GetLastError() & 0xffff,
                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   buffer,
                   sizeof(buffer) / sizeof(WCHAR),
                   NULL);
    WideCharToMultiByte(CP_UTF8, 0, buffer, -1, message, sizeof(message), NULL, NULL);

    impl_on_error(error, "%s: %s", description, message);
}

// Updates key names according to the current keyboard layout
//
void _glfwUpdateKeyNamesWin32(void)
{
    int key;
    BYTE state[256] = {0};

    memset(g_wsi.win32.keynames, 0, sizeof(g_wsi.win32.keynames));

    for (key = SC_KEY_SPACE;  key <= SC_KEY_LAST;  key++)
    {
        UINT vk;
        int scancode, length;
        WCHAR chars[16];

        scancode = g_wsi.win32.scancodes[key];
        if (scancode == -1)
            continue;

        if (key >= SC_KEY_KP_0 && key <= SC_KEY_KP_ADD)
        {
            const UINT vks[] =
            {
                VK_NUMPAD0,  VK_NUMPAD1,  VK_NUMPAD2, VK_NUMPAD3,
                VK_NUMPAD4,  VK_NUMPAD5,  VK_NUMPAD6, VK_NUMPAD7,
                VK_NUMPAD8,  VK_NUMPAD9,  VK_DECIMAL, VK_DIVIDE,
                VK_MULTIPLY, VK_SUBTRACT, VK_ADD
            };

            vk = vks[key - SC_KEY_KP_0];
        }
        else
            vk = MapVirtualKeyW(scancode, MAPVK_VSC_TO_VK);

        length = ToUnicode(vk, scancode, state,
                           chars, sizeof(chars) / sizeof(WCHAR),
                           0);

        if (length == -1)
        {
            // This is a dead key, so we need a second simulated key press
            // to make it output its own character (usually a diacritic)
            length = ToUnicode(vk, scancode, state,
                               chars, sizeof(chars) / sizeof(WCHAR),
                               0);
        }

        if (length < 1)
            continue;

        WideCharToMultiByte(CP_UTF8, 0, chars, 1,
                            g_wsi.win32.keynames[key],
                            sizeof(g_wsi.win32.keynames[key]),
                            NULL, NULL);
    }
}

// Replacement for IsWindowsVersionOrGreater, as we cannot rely on the
// application having a correct embedded manifest
//
BOOL _glfwIsWindowsVersionOrGreaterWin32(WORD major, WORD minor, WORD sp)
{
    OSVERSIONINFOEXW osvi = { sizeof(osvi), major, minor, 0, 0, {0}, sp };
    DWORD mask = VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR;
    ULONGLONG cond = VerSetConditionMask(0, VER_MAJORVERSION, VER_GREATER_EQUAL);
    cond = VerSetConditionMask(cond, VER_MINORVERSION, VER_GREATER_EQUAL);
    cond = VerSetConditionMask(cond, VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL);
    // HACK: Use RtlVerifyVersionInfo instead of VerifyVersionInfoW as the
    //       latter lies unless the user knew to embed a non-default manifest
    //       announcing support for Windows 10 via supportedOS GUID
    return RtlVerifyVersionInfo(&osvi, mask, cond) == 0;
}

// Checks whether we are on at least the specified build of Windows 10
//
BOOL _glfwIsWindows10BuildOrGreaterWin32(WORD build)
{
    OSVERSIONINFOEXW osvi = { sizeof(osvi), 10, 0, build };
    DWORD mask = VER_MAJORVERSION | VER_MINORVERSION | VER_BUILDNUMBER;
    ULONGLONG cond = VerSetConditionMask(0, VER_MAJORVERSION, VER_GREATER_EQUAL);
    cond = VerSetConditionMask(cond, VER_MINORVERSION, VER_GREATER_EQUAL);
    cond = VerSetConditionMask(cond, VER_BUILDNUMBER, VER_GREATER_EQUAL);
    // HACK: Use RtlVerifyVersionInfo instead of VerifyVersionInfoW as the
    //       latter lies unless the user knew to embed a non-default manifest
    //       announcing support for Windows 10 via supportedOS GUID
    return RtlVerifyVersionInfo(&osvi, mask, cond) == 0;
}

bool _glfwConnectWin32(int platformID, platform_st* platform)
{
    const platform_st win32 =
    {
        .platformID = SC_PLATFORM_WIN32,
        .init = _glfwInitWin32,
        .terminate = _glfwTerminateWin32,
        .getCursorPos = _glfwGetCursorPosWin32,
        .setCursorPos = _glfwSetCursorPosWin32,
        .setCursorMode = _glfwSetCursorModeWin32,
        .setRawMouseMotion = _glfwSetRawMouseMotionWin32,
        .rawMouseMotionSupported = _glfwRawMouseMotionSupportedWin32,
        .createCursor = _glfwCreateCursorWin32,
        .createStandardCursor = _glfwCreateStandardCursorWin32,
        .destroyCursor = _glfwDestroyCursorWin32,
        .setCursor = _glfwSetCursorWin32,
        .getScancodeName = _glfwGetScancodeNameWin32,
        .getKeyScancode = _glfwGetKeyScancodeWin32,
        .setClipboardString = _glfwSetClipboardStringWin32,
        .getClipboardString = _glfwGetClipboardStringWin32,
        .freeMonitor = wsi_free_monitorWin32,
        .getMonitorPos = _glfwGetMonitorPosWin32,
        .getMonitorContentScale = _glfwGetMonitorContentScaleWin32,
        .getMonitorWorkarea = _glfwGetMonitorWorkareaWin32,
        .getVideoModes = _glfwGetVideoModesWin32,
        .getVideoMode = _glfwGetVideoModeWin32,
        .getGammaRamp = _glfwGetGammaRampWin32,
        .setGammaRamp = _glfwSetGammaRampWin32,
        .createWindow = _glfwCreateWindowWin32,
        .destroyWindow = _glfwDestroyWindowWin32,
        .setWindowTitle = _glfwSetWindowTitleWin32,
        .setWindowIcon = _glfwSetWindowIconWin32,
        .getWindowPos = _glfwGetWindowPosWin32,
        .setWindowPos = _glfwSetWindowPosWin32,
        .getWindowSize = _glfwGetWindowSizeWin32,
        .setWindowSize = _glfwSetWindowSizeWin32,
        .setWindowSizeLimits = _glfwSetWindowSizeLimitsWin32,
        .setWindowAspectRatio = _glfwSetWindowAspectRatioWin32,
        .getWindowFrameSize = _glfwGetWindowFrameSizeWin32,
        .getWindowContentScale = _glfwGetWindowContentScaleWin32,
        .iconifyWindow = _glfwIconifyWindowWin32,
        .restoreWindow = _glfwRestoreWindowWin32,
        .maximizeWindow = _glfwMaximizeWindowWin32,
        .showWindow = _glfwShowWindowWin32,
        .hideWindow = _glfwHideWindowWin32,
        .requestWindowAttention = _glfwRequestWindowAttentionWin32,
        .focusWindow = _glfwFocusWindowWin32,
        .setWindowMonitor = _glfwSetWindowMonitorWin32,
        .windowFocused = _glfwWindowFocusedWin32,
        .windowIconified = _glfwWindowIconifiedWin32,
        .windowVisible = _glfwWindowVisibleWin32,
        .windowMaximized = _glfwWindowMaximizedWin32,
        .windowHovered = _glfwWindowHoveredWin32,
        .getWindowOpacity = _glfwGetWindowOpacityWin32,
        .setWindowResizable = _glfwSetWindowResizableWin32,
        .setWindowDecorated = _glfwSetWindowDecoratedWin32,
        .setWindowFloating = _glfwSetWindowFloatingWin32,
        .setWindowOpacity = _glfwSetWindowOpacityWin32,
        .setWindowMousePassthrough = _glfwSetWindowMousePassthroughWin32,
        .pollEvents = _glfwPollEventsWin32,
        .waitEvents = _glfwWaitEventsWin32,
        .waitEventsTimeout = _glfwWaitEventsTimeoutWin32,
        .postEmptyEvent = _glfwPostEmptyEventWin32,
    };

    *platform = win32;
    return true;
}

int _glfwInitWin32(void)
{
    if (!loadLibraries())
        return false;

    createKeyTables();
    _glfwUpdateKeyNamesWin32();

    if (_glfwIsWindows10Version1703OrGreaterWin32())
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    else if (IsWindows8Point1OrGreater())
        SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    else
        SetProcessDPIAware();

    if (!createHelperWindow())
        return false;

    _glfwPollMonitorsWin32();
    return true;
}

void _glfwTerminateWin32(void)
{
    if (g_wsi.win32.blankCursor)
        DestroyIcon((HICON) g_wsi.win32.blankCursor);

    if (g_wsi.win32.deviceNotificationHandle)
        UnregisterDeviceNotification(g_wsi.win32.deviceNotificationHandle);

    if (g_wsi.win32.helperWindowHandle)
        DestroyWindow(g_wsi.win32.helperWindowHandle);
    if (g_wsi.win32.helperWindowClass)
        UnregisterClassW(MAKEINTATOM(g_wsi.win32.helperWindowClass), g_wsi.win32.instance);
    if (g_wsi.win32.mainWindowClass)
        UnregisterClassW(MAKEINTATOM(g_wsi.win32.mainWindowClass), g_wsi.win32.instance);

    wsi_free(g_wsi.win32.clipboardString);
    wsi_free(g_wsi.win32.rawInput);

    impl_platform_unload_module(g_wsi.win32.xinput.instance);
    impl_platform_unload_module(g_wsi.win32.dinput8.instance);
    impl_platform_unload_module(g_wsi.win32.user32.instance);
    impl_platform_unload_module(g_wsi.win32.dwmapi.instance);
    impl_platform_unload_module(g_wsi.win32.shcore.instance);
    impl_platform_unload_module(g_wsi.win32.ntdll.instance);

    memset(&g_wsi.win32, 0, sizeof(g_wsi.win32));
}

#endif // WSI_WIN32

