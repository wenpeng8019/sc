
#include "internal.h"

#if defined(WSI_WIN32)

#include <windowsx.h>
#include <shellapi.h>
#include <wchar.h>
#include <limits.h>

///////////////////////////////////////////////////////////////////////////////
// platform utils
///////////////////////////////////////////////////////////////////////////////

// Reports the specified error, appending information about the last Win32 error
static void win32_InputError(int error, const char* description)
{
    WCHAR buffer[WSI_MESSAGE_SIZE] = L"";
    char message[WSI_MESSAGE_SIZE] = "";

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

// Returns a UTF-8 string version of the specified wide string
static char* win32_CreateUTF8FromWideString(const WCHAR* source)
{
    char* target;
    int size;

    size = WideCharToMultiByte(CP_UTF8, 0, source, -1, NULL, 0, NULL, NULL);
    if (!size)
    {
        win32_InputError(SC_WSI_ERR_PLATFORM_ERROR,
                             "Win32: Failed to convert string to UTF-8");
        return NULL;
    }

    target = wsi_calloc(size, 1);

    if (!WideCharToMultiByte(CP_UTF8, 0, source, -1, target, size, NULL, NULL))
    {
        win32_InputError(SC_WSI_ERR_PLATFORM_ERROR,
                             "Win32: Failed to convert string to UTF-8");
        wsi_free(target);
        return NULL;
    }

    return target;
}

// Returns a wide string version of the specified UTF-8 string
static WCHAR* win32_CreateWideStringFromUTF8(const char* source)
{
    WCHAR* target;
    int count;

    count = MultiByteToWideChar(CP_UTF8, 0, source, -1, NULL, 0);
    if (!count)
    {
        win32_InputError(SC_WSI_ERR_PLATFORM_ERROR,
                             "Win32: Failed to convert string from UTF-8");
        return NULL;
    }

    target = wsi_calloc(count, sizeof(WCHAR));

    if (!MultiByteToWideChar(CP_UTF8, 0, source, -1, target, count))
    {
        win32_InputError(SC_WSI_ERR_PLATFORM_ERROR,
                             "Win32: Failed to convert string from UTF-8");
        wsi_free(target);
        return NULL;
    }

    return target;
}

// Replacement for IsWindowsVersionOrGreater, as we cannot rely on the
// application having a correct embedded manifest
static BOOL win32_IsWindowsVersionOrGreater(WORD major, WORD minor, WORD sp)
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
#define IsWindows8OrGreater()                                         \
    win32_IsWindowsVersionOrGreater(HIBYTE(_WIN32_WINNT_WIN8),    \
                                        LOBYTE(_WIN32_WINNT_WIN8), 0)
#define IsWindows8Point1OrGreater()                                   \
    win32_IsWindowsVersionOrGreater(HIBYTE(_WIN32_WINNT_WINBLUE), \
                                        LOBYTE(_WIN32_WINNT_WINBLUE), 0)


// Checks whether we are on at least the specified build of Windows 10
static BOOL win32_IsWindows10BuildOrGreater(WORD build)
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
// Windows 10 Anniversary Update
#define win32_IsWindows10Version1607OrGreater() \
    win32_IsWindows10BuildOrGreater(14393)
// Windows 10 Creators Update
#define win32_IsWindows10Version1703OrGreater() \
    win32_IsWindows10BuildOrGreater(15063)


// Retrieves and translates modifier keys
static int getKeyMods(void)
{
    int mods = 0;

    if (GetKeyState(VK_SHIFT) & 0x8000)
        mods |= SC_MOD_SHIFT;
    if (GetKeyState(VK_CONTROL) & 0x8000)
        mods |= SC_MOD_CONTROL;
    if (GetKeyState(VK_MENU) & 0x8000)
        mods |= SC_MOD_ALT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000)
        mods |= SC_MOD_SUPER;
    if (GetKeyState(VK_CAPITAL) & 1)
        mods |= SC_MOD_CAPS_LOCK;
    if (GetKeyState(VK_NUMLOCK) & 1)
        mods |= SC_MOD_NUM_LOCK;

    return mods;
}

// Returns the window style for the specified window
static DWORD getWindowStyle(const window_st* window)
{
    DWORD style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN;

    if (window->monitor)
        style |= WS_POPUP;
    else
    {
        style |= WS_SYSMENU | WS_MINIMIZEBOX;

        if (window->decorated)
        {
            style |= WS_CAPTION;

            if (window->resizable)
                style |= WS_MAXIMIZEBOX | WS_THICKFRAME;
        }
        else
            style |= WS_POPUP;
    }

    return style;
}

// Returns the extended window style for the specified window
static DWORD getWindowExStyle(const window_st* window)
{
    DWORD style = WS_EX_APPWINDOW;

    if (window->monitor || window->floating)
        style |= WS_EX_TOPMOST;

    return style;
}

// Creates an RGBA icon or cursor
static HICON createIcon(const GLFWimage* image, int xhot, int yhot, bool icon)
{
    int i;
    HDC dc;
    HICON handle;
    HBITMAP color, mask;
    BITMAPV5HEADER bi;
    ICONINFO ii;
    unsigned char* target = NULL;
    unsigned char* source = image->pixels;

    ZeroMemory(&bi, sizeof(bi));
    bi.bV5Size        = sizeof(bi);
    bi.bV5Width       = image->width;
    bi.bV5Height      = -image->height;
    bi.bV5Planes      = 1;
    bi.bV5BitCount    = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask     = 0x00ff0000;
    bi.bV5GreenMask   = 0x0000ff00;
    bi.bV5BlueMask    = 0x000000ff;
    bi.bV5AlphaMask   = 0xff000000;

    dc = GetDC(NULL);
    color = CreateDIBSection(dc,
                             (BITMAPINFO*) &bi,
                             DIB_RGB_COLORS,
                             (void**) &target,
                             NULL,
                             (DWORD) 0);
    ReleaseDC(NULL, dc);

    if (!color)
    {
        win32_InputError(SC_WSI_ERR_PLATFORM_ERROR,
                             "Win32: Failed to create RGBA bitmap");
        return NULL;
    }

    mask = CreateBitmap(image->width, image->height, 1, 1, NULL);
    if (!mask)
    {
        win32_InputError(SC_WSI_ERR_PLATFORM_ERROR,
                             "Win32: Failed to create mask bitmap");
        DeleteObject(color);
        return NULL;
    }

    for (i = 0;  i < image->width * image->height;  i++)
    {
        target[0] = source[2];
        target[1] = source[1];
        target[2] = source[0];
        target[3] = source[3];
        target += 4;
        source += 4;
    }

    ZeroMemory(&ii, sizeof(ii));
    ii.fIcon    = icon;
    ii.xHotspot = xhot;
    ii.yHotspot = yhot;
    ii.hbmMask  = mask;
    ii.hbmColor = color;

    handle = CreateIconIndirect(&ii);

    DeleteObject(color);
    DeleteObject(mask);

    if (!handle)
    {
        if (icon)
        {
            win32_InputError(SC_WSI_ERR_PLATFORM_ERROR,
                                 "Win32: Failed to create icon");
        }
        else
        {
            win32_InputError(SC_WSI_ERR_PLATFORM_ERROR,
                                 "Win32: Failed to create cursor");
        }
    }

    return handle;
}

// Returns the image whose area most closely matches the desired one
static const GLFWimage* chooseImage(int count, const GLFWimage* images,
                                    int width, int height)
{
    int i, leastDiff = INT_MAX;
    const GLFWimage* closest = NULL;

    for (i = 0;  i < count;  i++)
    {
        const int currDiff = abs(images[i].width * images[i].height -
                                 width * height);
        if (currDiff < leastDiff)
        {
            closest = images + i;
            leastDiff = currDiff;
        }
    }

    return closest;
}

// Enforce the content area aspect ratio based on which edge is being dragged
static void applyAspectRatio(window_st* window, int edge, RECT* area)
{
    RECT frame = {0};
    const float ratio = (float) window->numer / (float) window->denom;
    const DWORD style = getWindowStyle(window);
    const DWORD exStyle = getWindowExStyle(window);

    if (win32_IsWindows10Version1607OrGreater())
    {
        AdjustWindowRectExForDpi(&frame, style, FALSE, exStyle,
                                 GetDpiForWindow(window->win32.handle));
    }
    else
        AdjustWindowRectEx(&frame, style, FALSE, exStyle);

    if (edge == WMSZ_LEFT  || edge == WMSZ_BOTTOMLEFT ||
        edge == WMSZ_RIGHT || edge == WMSZ_BOTTOMRIGHT)
    {
        area->bottom = area->top + (frame.bottom - frame.top) +
            (int) (((area->right - area->left) - (frame.right - frame.left)) / ratio);
    }
    else if (edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT)
    {
        area->top = area->bottom - (frame.bottom - frame.top) -
            (int) (((area->right - area->left) - (frame.right - frame.left)) / ratio);
    }
    else if (edge == WMSZ_TOP || edge == WMSZ_BOTTOM)
    {
        area->right = area->left + (frame.right - frame.left) +
            (int) (((area->bottom - area->top) - (frame.bottom - frame.top)) * ratio);
    }
}

// Updates the cursor image according to its cursor mode
static void updateCursorImage(window_st* window)
{
    if (window->cursorMode == SC_CURSOR_NORMAL ||
        window->cursorMode == SC_CURSOR_CAPTURED)
    {
        if (window->cursor)
            SetCursor(window->cursor->win32.handle);
        else
            SetCursor(LoadCursorW(NULL, IDC_ARROW));
    }
    else
    {
        // NOTE: Via Remote Desktop, setting the cursor to NULL does not hide it.
        // HACK: When running locally, it is set to NULL, but when connected via Remote
        //       Desktop, this is a transparent cursor.
        SetCursor(g_wsi.win32.blankCursor);
    }
}

// Sets the cursor clip rect to the window content area
static void captureCursor(window_st* window)
{
    RECT clipRect;
    GetClientRect(window->win32.handle, &clipRect);
    ClientToScreen(window->win32.handle, (POINT*) &clipRect.left);
    ClientToScreen(window->win32.handle, (POINT*) &clipRect.right);
    ClipCursor(&clipRect);
    g_wsi.win32.capturedCursorWindow = window;
}

// Disabled clip cursor
static void releaseCursor(void)
{
    ClipCursor(NULL);
    g_wsi.win32.capturedCursorWindow = NULL;
}

// Enables WM_INPUT messages for the mouse for the specified window
static void enableRawMouseMotion(window_st* window)
{
    const RAWINPUTDEVICE rid = { 0x01, 0x02, 0, window->win32.handle };

    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid)))
    {
        win32_InputError(SC_WSI_ERR_PLATFORM_ERROR,
                             "Win32: Failed to register raw input device");
    }
}

// Disables WM_INPUT messages for the mouse
static void disableRawMouseMotion(window_st* window)
{
    const RAWINPUTDEVICE rid = { 0x01, 0x02, RIDEV_REMOVE, NULL };

    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid)))
    {
        win32_InputError(SC_WSI_ERR_PLATFORM_ERROR,
                             "Win32: Failed to remove raw input device");
    }
}

// Returns whether the cursor is in the content area of the specified window
static bool cursorInContentArea(window_st* window)
{
    RECT area;
    POINT pos;

    if (!GetCursorPos(&pos))
        return false;

    if (WindowFromPoint(pos) != window->win32.handle)
        return false;

    GetClientRect(window->win32.handle, &area);
    ClientToScreen(window->win32.handle, (POINT*) &area.left);
    ClientToScreen(window->win32.handle, (POINT*) &area.right);

    return PtInRect(&area, pos);
}

// Update native window styles to match attributes
static void updateWindowStyles(const window_st* window)
{
    RECT rect;
    DWORD style = GetWindowLongW(window->win32.handle, GWL_STYLE);
    style &= ~(WS_OVERLAPPEDWINDOW | WS_POPUP);
    style |= getWindowStyle(window);

    GetClientRect(window->win32.handle, &rect);

    if (win32_IsWindows10Version1607OrGreater())
    {
        AdjustWindowRectExForDpi(&rect, style, FALSE,
                                 getWindowExStyle(window),
                                 GetDpiForWindow(window->win32.handle));
    }
    else
        AdjustWindowRectEx(&rect, style, FALSE, getWindowExStyle(window));

    ClientToScreen(window->win32.handle, (POINT*) &rect.left);
    ClientToScreen(window->win32.handle, (POINT*) &rect.right);
    SetWindowLongW(window->win32.handle, GWL_STYLE, style);
    SetWindowPos(window->win32.handle, HWND_TOP,
                 rect.left, rect.top,
                 rect.right - rect.left, rect.bottom - rect.top,
                 SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER);
}

// Update window framebuffer transparency
static void updateFramebufferTransparency(const window_st* window)
{
    BOOL composition, opaque;
    DWORD color;

    if (FAILED(DwmIsCompositionEnabled(&composition)) || !composition)
       return;

    if (IsWindows8OrGreater() ||
        (SUCCEEDED(DwmGetColorizationColor(&color, &opaque)) && !opaque))
    {
        HRGN region = CreateRectRgn(0, 0, -1, -1);
        DWM_BLURBEHIND bb = {0};
        bb.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
        bb.hRgnBlur = region;
        bb.fEnable = TRUE;

        DwmEnableBlurBehindWindow(window->win32.handle, &bb);
        DeleteObject(region);
    }
    else
    {
        // HACK: Disable framebuffer transparency on Windows 7 when the
        //       colorization color is opaque, because otherwise the window
        //       contents is blended additively with the previous frame instead
        //       of replacing it
        DWM_BLURBEHIND bb = {0};
        bb.dwFlags = DWM_BB_ENABLE;
        DwmEnableBlurBehindWindow(window->win32.handle, &bb);
    }
}

static void fitToMonitor(window_st* window)
{
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(window->monitor->win32.handle, &mi);
    SetWindowPos(window->win32.handle, HWND_TOPMOST,
                 mi.rcMonitor.left,
                 mi.rcMonitor.top,
                 mi.rcMonitor.right - mi.rcMonitor.left,
                 mi.rcMonitor.bottom - mi.rcMonitor.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
}

// Manually maximize the window, for when SW_MAXIMIZE cannot be used
static void maximizeWindowManually(window_st* window)
{
    RECT rect;
    DWORD style;
    MONITORINFO mi = { sizeof(mi) };

    GetMonitorInfoW(MonitorFromWindow(window->win32.handle,
                                      MONITOR_DEFAULTTONEAREST), &mi);

    rect = mi.rcWork;

    if (window->maxwidth != SC_DONT_CARE && window->maxheight != SC_DONT_CARE)
    {
        rect.right = wsi_min(rect.right, rect.left + window->maxwidth);
        rect.bottom = wsi_min(rect.bottom, rect.top + window->maxheight);
    }

    style = GetWindowLongW(window->win32.handle, GWL_STYLE);
    style |= WS_MAXIMIZE;
    SetWindowLongW(window->win32.handle, GWL_STYLE, style);

    if (window->decorated)
    {
        const DWORD exStyle = GetWindowLongW(window->win32.handle, GWL_EXSTYLE);

        if (win32_IsWindows10Version1607OrGreater())
        {
            const UINT dpi = GetDpiForWindow(window->win32.handle);
            AdjustWindowRectExForDpi(&rect, style, FALSE, exStyle, dpi);
            OffsetRect(&rect, 0, GetSystemMetricsForDpi(SM_CYCAPTION, dpi));
        }
        else
        {
            AdjustWindowRectEx(&rect, style, FALSE, exStyle);
            OffsetRect(&rect, 0, GetSystemMetrics(SM_CYCAPTION));
        }

        rect.bottom = wsi_min(rect.bottom, mi.rcWork.bottom);
    }

    SetWindowPos(window->win32.handle, HWND_TOP,
                 rect.left,
                 rect.top,
                 rect.right - rect.left,
                 rect.bottom - rect.top,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

///////////////////////////////////////////////////////////////////////////////
// Monitor
///////////////////////////////////////////////////////////////////////////////

// Monitor 回调处理
static BOOL CALLBACK monitorCallback(HMONITOR handle, HDC dc, RECT* rect, LPARAM data)
{
    MONITORINFOEXW mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);

    if (GetMonitorInfoW(handle, (MONITORINFO*) &mi))
    {
        monitor_st* monitor = (monitor_st*) data;
        if (wcscmp(mi.szDevice, monitor->win32.adapterName) == 0)
            monitor->win32.handle = handle;
    }

    return TRUE;
}

// 创建 monitor 对象
static monitor_st* createMonitor(DISPLAY_DEVICEW* adapter, DISPLAY_DEVICEW* display)
{
    monitor_st* monitor;
    int widthMM, heightMM;
    char* name;
    HDC dc;
    DEVMODEW dm;
    RECT rect;

    if (display)
        name = win32_CreateUTF8FromWideString(display->DeviceString);
    else
        name = win32_CreateUTF8FromWideString(adapter->DeviceString);
    if (!name)
        return NULL;

    ZeroMemory(&dm, sizeof(dm));
    dm.dmSize = sizeof(dm);
    EnumDisplaySettingsW(adapter->DeviceName, ENUM_CURRENT_SETTINGS, &dm);

    dc = CreateDCW(L"DISPLAY", adapter->DeviceName, NULL, NULL);

    if (IsWindows8Point1OrGreater())
    {
        widthMM  = GetDeviceCaps(dc, HORZSIZE);
        heightMM = GetDeviceCaps(dc, VERTSIZE);
    }
    else
    {
        widthMM  = (int) (dm.dmPelsWidth * 25.4f / GetDeviceCaps(dc, LOGPIXELSX));
        heightMM = (int) (dm.dmPelsHeight * 25.4f / GetDeviceCaps(dc, LOGPIXELSY));
    }

    DeleteDC(dc);

    monitor = wsi_alloc_monitor(name, widthMM, heightMM);
    wsi_free(name);

    if (adapter->StateFlags & DISPLAY_DEVICE_MODESPRUNED)
        monitor->win32.modesPruned = true;

    wcscpy(monitor->win32.adapterName, adapter->DeviceName);
    WideCharToMultiByte(CP_UTF8, 0,
                        adapter->DeviceName, -1,
                        monitor->win32.publicAdapterName,
                        sizeof(monitor->win32.publicAdapterName),
                        NULL, NULL);

    if (display)
    {
        wcscpy(monitor->win32.displayName, display->DeviceName);
        WideCharToMultiByte(CP_UTF8, 0,
                            display->DeviceName, -1,
                            monitor->win32.publicDisplayName,
                            sizeof(monitor->win32.publicDisplayName),
                            NULL, NULL);
    }

    rect.left   = dm.dmPosition.x;
    rect.top    = dm.dmPosition.y;
    rect.right  = dm.dmPosition.x + dm.dmPelsWidth;
    rect.bottom = dm.dmPosition.y + dm.dmPelsHeight;

    EnumDisplayMonitors(NULL, &rect, monitorCallback, (LPARAM) monitor);
    return monitor;
}

//-----------------------------------------------------------------------------

// 获取更新当前连接的 monitor 列表
static void win32_poll_monitors(void)
{
    int i, disconnectedCount;
    monitor_st** disconnected = NULL;
    DWORD adapterIndex, displayIndex;
    DISPLAY_DEVICEW adapter, display;
    monitor_st* monitor;

    disconnectedCount = g_wsi.monitorCount;
    if (disconnectedCount)
    {
        disconnected = wsi_calloc(g_wsi.monitorCount, sizeof(monitor_st*));
        memcpy(disconnected,
               g_wsi.monitors,
               g_wsi.monitorCount * sizeof(monitor_st*));
    }

    for (adapterIndex = 0;  ;  adapterIndex++)
    {
        int type = WSI_INSERT_LAST;

        ZeroMemory(&adapter, sizeof(adapter));
        adapter.cb = sizeof(adapter);

        if (!EnumDisplayDevicesW(NULL, adapterIndex, &adapter, 0))
            break;

        if (!(adapter.StateFlags & DISPLAY_DEVICE_ACTIVE))
            continue;

        if (adapter.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
            type = WSI_INSERT_FIRST;

        for (displayIndex = 0;  ;  displayIndex++)
        {
            ZeroMemory(&display, sizeof(display));
            display.cb = sizeof(display);

            if (!EnumDisplayDevicesW(adapter.DeviceName, displayIndex, &display, 0))
                break;

            if (!(display.StateFlags & DISPLAY_DEVICE_ACTIVE))
                continue;

            for (i = 0;  i < disconnectedCount;  i++)
            {
                if (disconnected[i] &&
                    wcscmp(disconnected[i]->win32.displayName,
                           display.DeviceName) == 0)
                {
                    disconnected[i] = NULL;
                    // handle may have changed, update
                    EnumDisplayMonitors(NULL, NULL, monitorCallback, (LPARAM) g_wsi.monitors[i]);
                    break;
                }
            }

            if (i < disconnectedCount)
                continue;

            monitor = createMonitor(&adapter, &display);
            if (!monitor)
            {
                wsi_free(disconnected);
                return;
            }

            impl_on_monitor(monitor, SC_CONNECTED, type);

            type = WSI_INSERT_LAST;
        }

        // HACK: If an active adapter does not have any display devices
        //       (as sometimes happens), add it directly as a monitor
        if (displayIndex == 0)
        {
            for (i = 0;  i < disconnectedCount;  i++)
            {
                if (disconnected[i] &&
                    wcscmp(disconnected[i]->win32.adapterName,
                           adapter.DeviceName) == 0)
                {
                    disconnected[i] = NULL;
                    break;
                }
            }

            if (i < disconnectedCount)
                continue;

            monitor = createMonitor(&adapter, NULL);
            if (!monitor)
            {
                wsi_free(disconnected);
                return;
            }

            impl_on_monitor(monitor, SC_CONNECTED, type);
        }
    }

    for (i = 0;  i < disconnectedCount;  i++)
    {
        if (disconnected[i])
            impl_on_monitor(disconnected[i], SC_DISCONNECTED, 0);
    }

    wsi_free(disconnected);
}

// 恢复之前保存的（原始）视频模式
static void win32_RestoreVideoMode(monitor_st* monitor)
{
    if (monitor->win32.modeChanged)
    {
        ChangeDisplaySettingsExW(monitor->win32.adapterName,
                                 NULL, NULL, CDS_FULLSCREEN, NULL);
        monitor->win32.modeChanged = false;
    }
}

static void win32_GetHMONITORContentScale(HMONITOR handle, float* xscale, float* yscale)
{
    UINT xdpi, ydpi;

    if (xscale)
        *xscale = 0.f;
    if (yscale)
        *yscale = 0.f;

    if (IsWindows8Point1OrGreater())
    {
        if (GetDpiForMonitor(handle, MDT_EFFECTIVE_DPI, &xdpi, &ydpi) != S_OK)
        {
            impl_on_error(SC_WSI_ERR_PLATFORM_ERROR, "Win32: Failed to query monitor DPI");
            return;
        }
    }
    else
    {
        const HDC dc = GetDC(NULL);
        xdpi = GetDeviceCaps(dc, LOGPIXELSX);
        ydpi = GetDeviceCaps(dc, LOGPIXELSY);
        ReleaseDC(NULL, dc);
    }

    if (xscale)
        *xscale = xdpi / (float) USER_DEFAULT_SCREEN_DPI;
    if (yscale)
        *yscale = ydpi / (float) USER_DEFAULT_SCREEN_DPI;
}

///////////////////////////////////////////////////////////////////////////////

static void win32_free_monitor(monitor_st* monitor)
{
}

static void win32_get_monitor_pos(monitor_st* monitor, int* xpos, int* ypos)
{
    DEVMODEW dm;
    ZeroMemory(&dm, sizeof(dm));
    dm.dmSize = sizeof(dm);

    EnumDisplaySettingsExW(monitor->win32.adapterName,
                           ENUM_CURRENT_SETTINGS,
                           &dm,
                           EDS_ROTATEDMODE);

    if (xpos)
        *xpos = dm.dmPosition.x;
    if (ypos)
        *ypos = dm.dmPosition.y;
}

static void win32_get_monitor_content_scale(monitor_st* monitor, float* xscale, float* yscale)
{
    win32_GetHMONITORContentScale(monitor->win32.handle, xscale, yscale);
}

static void win32_get_monitor_work_area(monitor_st* monitor,
                                 int* xpos, int* ypos,
                                 int* width, int* height)
{
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(monitor->win32.handle, &mi);

    if (xpos)
        *xpos = mi.rcWork.left;
    if (ypos)
        *ypos = mi.rcWork.top;
    if (width)
        *width = mi.rcWork.right - mi.rcWork.left;
    if (height)
        *height = mi.rcWork.bottom - mi.rcWork.top;
}

static bool win32_get_video_mode(monitor_st* monitor, GLFWvidmode* mode)
{
    DEVMODEW dm;
    ZeroMemory(&dm, sizeof(dm));
    dm.dmSize = sizeof(dm);

    if (!EnumDisplaySettingsW(monitor->win32.adapterName, ENUM_CURRENT_SETTINGS, &dm))
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR, "Win32: Failed to query display settings");
        return false;
    }

    mode->width  = dm.dmPelsWidth;
    mode->height = dm.dmPelsHeight;
    mode->refreshRate = dm.dmDisplayFrequency;

    return true;
}

static GLFWvidmode* win32_get_video_modes(monitor_st* monitor, int* count)
{
    int modeIndex = 0, size = 0;
    GLFWvidmode* result = NULL;

    *count = 0;

    for (;;)
    {
        int i;
        GLFWvidmode mode;
        DEVMODEW dm;

        ZeroMemory(&dm, sizeof(dm));
        dm.dmSize = sizeof(dm);

        if (!EnumDisplaySettingsW(monitor->win32.adapterName, modeIndex, &dm))
            break;

        modeIndex++;

        // Skip modes with less than 15 BPP
        if (dm.dmBitsPerPel < 15)
            continue;

        mode.width  = dm.dmPelsWidth;
        mode.height = dm.dmPelsHeight;
        mode.refreshRate = dm.dmDisplayFrequency;

        for (i = 0;  i < *count;  i++)
        {
            if (wsi_compare_video_mode(result + i, &mode) == 0)
                break;
        }

        // Skip duplicate modes
        if (i < *count)
            continue;

        if (monitor->win32.modesPruned)
        {
            // Skip modes not supported by the connected displays
            if (ChangeDisplaySettingsExW(monitor->win32.adapterName,
                                         &dm,
                                         NULL,
                                         CDS_TEST,
                                         NULL) != DISP_CHANGE_SUCCESSFUL)
            {
                continue;
            }
        }

        if (*count == size)
        {
            size += 128;
            result = (GLFWvidmode*) wsi_realloc(result, size * sizeof(GLFWvidmode));
        }

        (*count)++;
        result[*count - 1] = mode;
    }

    if (!*count)
    {
        // HACK: Report the current mode if no valid modes were found
        result = wsi_calloc(1, sizeof(GLFWvidmode));
        win32_get_video_mode(monitor, result);
        *count = 1;
    }

    return result;
}

static void win32_set_video_mode(monitor_st* monitor, const GLFWvidmode* desired)
{
    GLFWvidmode current;
    const GLFWvidmode* best;
    DEVMODEW dm;
    LONG result;

    best = wsi_choose_video_mode(monitor, desired);
    win32_get_video_mode(monitor, &current);
    if (wsi_compare_video_mode(&current, best) == 0)
        return;

    ZeroMemory(&dm, sizeof(dm));
    dm.dmSize = sizeof(dm);
    dm.dmFields           = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL |
                            DM_DISPLAYFREQUENCY;
    dm.dmPelsWidth        = best->width;
    dm.dmPelsHeight       = best->height;
    dm.dmBitsPerPel       = 32;
    dm.dmDisplayFrequency = best->refreshRate;

    if (dm.dmBitsPerPel < 15 || dm.dmBitsPerPel >= 24)
        dm.dmBitsPerPel = 32;

    result = ChangeDisplaySettingsExW(monitor->win32.adapterName,
                                      &dm,
                                      NULL,
                                      CDS_FULLSCREEN,
                                      NULL);
    if (result == DISP_CHANGE_SUCCESSFUL)
        monitor->win32.modeChanged = true;
    else
    {
        const char* description = "Unknown error";

        if (result == DISP_CHANGE_BADDUALVIEW)
            description = "The system uses DualView";
        else if (result == DISP_CHANGE_BADFLAGS)
            description = "Invalid flags";
        else if (result == DISP_CHANGE_BADMODE)
            description = "Graphics mode not supported";
        else if (result == DISP_CHANGE_BADPARAM)
            description = "Invalid parameter";
        else if (result == DISP_CHANGE_FAILED)
            description = "Graphics mode failed";
        else if (result == DISP_CHANGE_NOTUPDATED)
            description = "Failed to write to registry";
        else if (result == DISP_CHANGE_RESTART)
            description = "Computer restart required";

        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Win32: Failed to set video mode: %s",
                        description);
    }
}

static bool win32_get_gamma_ramp(monitor_st* monitor, GLFWgammaramp* ramp)
{
    HDC dc;
    WORD values[3][256];

    dc = CreateDCW(L"DISPLAY", monitor->win32.adapterName, NULL, NULL);
    GetDeviceGammaRamp(dc, values);
    DeleteDC(dc);

    wsi_alloc_gamma_arrays(ramp, 256);

    memcpy(ramp->red,   values[0], sizeof(values[0]));
    memcpy(ramp->green, values[1], sizeof(values[1]));
    memcpy(ramp->blue,  values[2], sizeof(values[2]));

    return true;
}

static void win32_set_gamma_ramp(monitor_st* monitor, const GLFWgammaramp* ramp)
{
    HDC dc;
    WORD values[3][256];

    if (ramp->size != 256)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Win32: Gamma ramp size must be 256");
        return;
    }

    memcpy(values[0], ramp->red,   sizeof(values[0]));
    memcpy(values[1], ramp->green, sizeof(values[1]));
    memcpy(values[2], ramp->blue,  sizeof(values[2]));

    dc = CreateDCW(L"DISPLAY", monitor->win32.adapterName, NULL, NULL);
    SetDeviceGammaRamp(dc, values);
    DeleteDC(dc);
}

//-----------------------------------------------------------------------------

// Make the specified window and its video mode active on its monitor
static void acquireMonitor(window_st* window)
{
    if (!g_wsi.win32.acquiredMonitorCount)
    {
        SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED);

        // HACK: When mouse trails are enabled the cursor becomes invisible when
        //       the OpenGL ICD switches to page flipping
        SystemParametersInfoW(SPI_GETMOUSETRAILS, 0, &g_wsi.win32.mouseTrailSize, 0);
        SystemParametersInfoW(SPI_SETMOUSETRAILS, 0, 0, 0);
    }

    if (!window->monitor->window)
        g_wsi.win32.acquiredMonitorCount++;

    win32_set_video_mode(window->monitor, &window->videoMode);
    impl_on_monitor_window(window->monitor, window);
}

// Remove the window and restore the original video mode
static void releaseMonitor(window_st* window)
{
    if (window->monitor->window != window)
        return;

    g_wsi.win32.acquiredMonitorCount--;
    if (!g_wsi.win32.acquiredMonitorCount)
    {
        SetThreadExecutionState(ES_CONTINUOUS);

        // HACK: Restore mouse trail length saved in acquireMonitor
        SystemParametersInfoW(SPI_SETMOUSETRAILS, g_wsi.win32.mouseTrailSize, 0, 0);
    }

    impl_on_monitor_window(window->monitor, NULL);
    win32_RestoreVideoMode(window->monitor);
}

static void win32_set_window_monitor(window_st* window,
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
            {
                acquireMonitor(window);
                fitToMonitor(window);
            }
        }
        else
        {
            RECT rect = { xpos, ypos, xpos + width, ypos + height };

            if (win32_IsWindows10Version1607OrGreater())
            {
                AdjustWindowRectExForDpi(&rect, getWindowStyle(window),
                                         FALSE, getWindowExStyle(window),
                                         GetDpiForWindow(window->win32.handle));
            }
            else
            {
                AdjustWindowRectEx(&rect, getWindowStyle(window),
                                   FALSE, getWindowExStyle(window));
            }

            SetWindowPos(window->win32.handle, HWND_TOP,
                         rect.left, rect.top,
                         rect.right - rect.left, rect.bottom - rect.top,
                         SWP_NOCOPYBITS | SWP_NOACTIVATE | SWP_NOZORDER);
        }

        return;
    }

    if (window->monitor)
        releaseMonitor(window);

    impl_on_win_monitor(window, monitor);

    if (window->monitor)
    {
        MONITORINFO mi = { sizeof(mi) };
        UINT flags = SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOCOPYBITS;

        if (window->decorated)
        {
            DWORD style = GetWindowLongW(window->win32.handle, GWL_STYLE);
            style &= ~WS_OVERLAPPEDWINDOW;
            style |= getWindowStyle(window);
            SetWindowLongW(window->win32.handle, GWL_STYLE, style);
            flags |= SWP_FRAMECHANGED;
        }

        acquireMonitor(window);

        GetMonitorInfoW(window->monitor->win32.handle, &mi);
        SetWindowPos(window->win32.handle, HWND_TOPMOST,
                     mi.rcMonitor.left,
                     mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     flags);
    }
    else
    {
        HWND after;
        RECT rect = { xpos, ypos, xpos + width, ypos + height };
        DWORD style = GetWindowLongW(window->win32.handle, GWL_STYLE);
        UINT flags = SWP_NOACTIVATE | SWP_NOCOPYBITS;

        if (window->decorated)
        {
            style &= ~WS_POPUP;
            style |= getWindowStyle(window);
            SetWindowLongW(window->win32.handle, GWL_STYLE, style);

            flags |= SWP_FRAMECHANGED;
        }

        if (window->floating)
            after = HWND_TOPMOST;
        else
            after = HWND_NOTOPMOST;

        if (win32_IsWindows10Version1607OrGreater())
        {
            AdjustWindowRectExForDpi(&rect, getWindowStyle(window),
                                     FALSE, getWindowExStyle(window),
                                     GetDpiForWindow(window->win32.handle));
        }
        else
        {
            AdjustWindowRectEx(&rect, getWindowStyle(window),
                               FALSE, getWindowExStyle(window));
        }

        SetWindowPos(window->win32.handle, after,
                     rect.left, rect.top,
                     rect.right - rect.left, rect.bottom - rect.top,
                     flags);
    }
}

///////////////////////////////////////////////////////////////////////////////
// App Bebind
///////////////////////////////////////////////////////////////////////////////

#if defined(WSI_USE_HYBRID_HPG) || defined(WSI_USE_OPTIMUS_HPG)
#   if defined(WSI_EXPORTS)
#       pragma message("These symbols must be exported by the executable and have no effect in a DLL")
#   endif

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

// GLFW DLL entry point
#if defined(WSI_EXPORTS)
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}
#endif // WSI_EXPORTS


static const GUID g_GUID_DEVINTERFACE_HID =
    {0x4d1e55b2,0xf16f,0x11cf,{0x88,0xcb,0x00,0x11,0x11,0x00,0x00,0x30}};

// 创建隐藏（helper）窗口来监听显示器和设备的变化
static LRESULT CALLBACK helperWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_DISPLAYCHANGE:
            win32_poll_monitors();
            break;

        case WM_DEVICECHANGE:
        {

            break;
        }
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

static bool createHelperWindow(void)
{
    MSG msg;
    WNDCLASSEXW wc = { sizeof(wc) };

    wc.style         = CS_OWNDC;
    wc.lpfnWndProc   = (WNDPROC) helperWindowProc;
    wc.hInstance     = g_wsi.win32.instance;
    wc.lpszClassName = L"SC Helper";

    g_wsi.win32.helperWindowClass = RegisterClassExW(&wc);
    if (!g_wsi.win32.helperWindowClass)
    {
        win32_InputError(SC_WSI_ERR_PLATFORM_ERROR,
                         "Win32: Failed to register helper window class");
        return false;
    }

    g_wsi.win32.helperWindowHandle =
        CreateWindowExW(WS_EX_OVERLAPPEDWINDOW,
                        MAKEINTATOM(g_wsi.win32.helperWindowClass),
                        L"SC Helper Window",
                        WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                        0, 0, 1, 1,
                        NULL, NULL,
                        g_wsi.win32.instance,
                        NULL);

    if (!g_wsi.win32.helperWindowHandle)
    {
        win32_InputError(SC_WSI_ERR_PLATFORM_ERROR,
                             "Win32: Failed to create helper window");
        return false;
    }

    // HACK: 如果父进程传递了 STARTUPINFO，则第一次 ShowWindow 调用的命令会被忽略，所以用一个空操作调用来清除它
    ShowWindow(g_wsi.win32.helperWindowHandle, SW_HIDE);

    // 注册设备通知
    {
        DEV_BROADCAST_DEVICEINTERFACE_W dbi;
        ZeroMemory(&dbi, sizeof(dbi));
        dbi.dbcc_size = sizeof(dbi);
        dbi.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        dbi.dbcc_classguid = g_GUID_DEVINTERFACE_HID;

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

///////////////////////////////////////////////////////////////////////////////
// lib
///////////////////////////////////////////////////////////////////////////////

// Updates key names according to the current keyboard layout
static void win32_UpdateKeyNames(void)
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

// Load necessary libraries (DLLs)
static bool loadLibraries(void)
{
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (const WCHAR*) &g_wsi,
                            (HMODULE*) &g_wsi.win32.instance))
    {
        win32_InputError(SC_WSI_ERR_PLATFORM_ERROR,
                             "Win32: Failed to retrieve own module handle");
        return false;
    }

    g_wsi.win32.user32.instance = impl_platform_load_module("user32.dll");
    if (!g_wsi.win32.user32.instance)
    {
        win32_InputError(SC_WSI_ERR_PLATFORM_ERROR,
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

static int win32_init(void)
{
    if (!loadLibraries())
        return false;

    createKeyTables();
    win32_UpdateKeyNames();

    if (win32_IsWindows10Version1703OrGreater())
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    else if (IsWindows8Point1OrGreater())
        SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    else
        SetProcessDPIAware();

    if (!createHelperWindow())
        return false;

    win32_poll_monitors();
    return true;
}

static void win32_terminate(void)
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

///////////////////////////////////////////////////////////////////////////////
// Interface
///////////////////////////////////////////////////////////////////////////////

static void win32_set_window_title(window_st* window, const char* title)
{
    WCHAR* wideTitle = win32_CreateWideStringFromUTF8(title);
    if (!wideTitle)
        return;

    SetWindowTextW(window->win32.handle, wideTitle);
    wsi_free(wideTitle);
}

static void win32_set_window_icon(window_st* window, int count, const GLFWimage* images)
{
    HICON bigIcon = NULL, smallIcon = NULL;

    if (count)
    {
        const GLFWimage* bigImage = chooseImage(count, images,
                                                GetSystemMetrics(SM_CXICON),
                                                GetSystemMetrics(SM_CYICON));
        const GLFWimage* smallImage = chooseImage(count, images,
                                                  GetSystemMetrics(SM_CXSMICON),
                                                  GetSystemMetrics(SM_CYSMICON));

        bigIcon = createIcon(bigImage, 0, 0, true);
        smallIcon = createIcon(smallImage, 0, 0, true);
    }
    else
    {
        bigIcon = (HICON) GetClassLongPtrW(window->win32.handle, GCLP_HICON);
        smallIcon = (HICON) GetClassLongPtrW(window->win32.handle, GCLP_HICONSM);
    }

    SendMessageW(window->win32.handle, WM_SETICON, ICON_BIG, (LPARAM) bigIcon);
    SendMessageW(window->win32.handle, WM_SETICON, ICON_SMALL, (LPARAM) smallIcon);

    if (window->win32.bigIcon)
        DestroyIcon(window->win32.bigIcon);

    if (window->win32.smallIcon)
        DestroyIcon(window->win32.smallIcon);

    if (count)
    {
        window->win32.bigIcon = bigIcon;
        window->win32.smallIcon = smallIcon;
    }
}

static void win32_set_window_mouse_passthrough(window_st* window, bool enabled)
{
    COLORREF key = 0;
    BYTE alpha = 0;
    DWORD flags = 0;
    DWORD exStyle = GetWindowLongW(window->win32.handle, GWL_EXSTYLE);

    if (exStyle & WS_EX_LAYERED)
        GetLayeredWindowAttributes(window->win32.handle, &key, &alpha, &flags);

    if (enabled)
        exStyle |= (WS_EX_TRANSPARENT | WS_EX_LAYERED);
    else
    {
        exStyle &= ~WS_EX_TRANSPARENT;
        // NOTE: Window opacity also needs the layered window style so do not
        //       remove it if the window is alpha blended
        if (exStyle & WS_EX_LAYERED)
        {
            if (!(flags & LWA_ALPHA))
                exStyle &= ~WS_EX_LAYERED;
        }
    }

    SetWindowLongW(window->win32.handle, GWL_EXSTYLE, exStyle);

    if (enabled)
        SetLayeredWindowAttributes(window->win32.handle, key, alpha, flags);
}


static void win32_set_window_decorated(window_st* window, bool enabled)
{
    updateWindowStyles(window);
}

static void win32_set_window_resizable(window_st* window, bool enabled)
{
    updateWindowStyles(window);
}

static void win32_set_window_floating(window_st* window, bool enabled)
{
    const HWND after = enabled ? HWND_TOPMOST : HWND_NOTOPMOST;
    SetWindowPos(window->win32.handle, after, 0, 0, 0, 0,
                 SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
}

static void win32_set_window_opacity(window_st* window, float opacity)
{
    LONG exStyle = GetWindowLongW(window->win32.handle, GWL_EXSTYLE);
    if (opacity < 1.f || (exStyle & WS_EX_TRANSPARENT))
    {
        const BYTE alpha = (BYTE) (255 * opacity);
        exStyle |= WS_EX_LAYERED;
        SetWindowLongW(window->win32.handle, GWL_EXSTYLE, exStyle);
        SetLayeredWindowAttributes(window->win32.handle, 0, alpha, LWA_ALPHA);
    }
    else if (exStyle & WS_EX_TRANSPARENT)
    {
        SetLayeredWindowAttributes(window->win32.handle, 0, 0, 0);
    }
    else
    {
        exStyle &= ~WS_EX_LAYERED;
        SetWindowLongW(window->win32.handle, GWL_EXSTYLE, exStyle);
    }
}

static float win32_get_window_opacity(window_st* window)
{
    BYTE alpha;
    DWORD flags;

    if ((GetWindowLongW(window->win32.handle, GWL_EXSTYLE) & WS_EX_LAYERED) &&
        GetLayeredWindowAttributes(window->win32.handle, NULL, &alpha, &flags))
    {
        if (flags & LWA_ALPHA)
            return alpha / 255.f;
    }

    return 1.f;
}


static void win32_get_window_pos(window_st* window, int* xpos, int* ypos)
{
    POINT pos = { 0, 0 };
    ClientToScreen(window->win32.handle, &pos);

    if (xpos)
        *xpos = pos.x;
    if (ypos)
        *ypos = pos.y;
}

static void win32_set_window_pos(window_st* window, int xpos, int ypos)
{
    RECT rect = { xpos, ypos, xpos, ypos };

    if (win32_IsWindows10Version1607OrGreater())
    {
        AdjustWindowRectExForDpi(&rect, getWindowStyle(window),
                                 FALSE, getWindowExStyle(window),
                                 GetDpiForWindow(window->win32.handle));
    }
    else
    {
        AdjustWindowRectEx(&rect, getWindowStyle(window),
                           FALSE, getWindowExStyle(window));
    }

    SetWindowPos(window->win32.handle, NULL, rect.left, rect.top, 0, 0,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);
}

static void win32_get_window_size(window_st* window, int* width, int* height)
{
    RECT area;
    GetClientRect(window->win32.handle, &area);

    if (width)
        *width = area.right;
    if (height)
        *height = area.bottom;
}

static void win32_get_framebuffer_size(window_st* window, int* width, int* height)
{
    // Win32 客户区即像素，无服务端缩放：帧缓冲尺寸 == 窗口尺寸
    win32_get_window_size(window, width, height);
}

static void win32_set_window_size(window_st* window, int width, int height)
{
    if (window->monitor)
    {
        if (window->monitor->window == window)
        {
            acquireMonitor(window);
            fitToMonitor(window);
        }
    }
    else
    {
        RECT rect = { 0, 0, width, height };

        if (win32_IsWindows10Version1607OrGreater())
        {
            AdjustWindowRectExForDpi(&rect, getWindowStyle(window),
                                     FALSE, getWindowExStyle(window),
                                     GetDpiForWindow(window->win32.handle));
        }
        else
        {
            AdjustWindowRectEx(&rect, getWindowStyle(window),
                               FALSE, getWindowExStyle(window));
        }

        SetWindowPos(window->win32.handle, HWND_TOP,
                     0, 0, rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOMOVE | SWP_NOZORDER);
    }
}

static void win32_get_window_frame_size(window_st* window,
                                  int* left, int* top,
                                  int* right, int* bottom)
{
    RECT rect;
    int width, height;

    win32_get_window_size(window, &width, &height);
    SetRect(&rect, 0, 0, width, height);

    if (win32_IsWindows10Version1607OrGreater())
    {
        AdjustWindowRectExForDpi(&rect, getWindowStyle(window),
                                 FALSE, getWindowExStyle(window),
                                 GetDpiForWindow(window->win32.handle));
    }
    else
    {
        AdjustWindowRectEx(&rect, getWindowStyle(window),
                           FALSE, getWindowExStyle(window));
    }

    if (left)
        *left = -rect.left;
    if (top)
        *top = -rect.top;
    if (right)
        *right = rect.right - width;
    if (bottom)
        *bottom = rect.bottom - height;
}

static void win32_set_window_size_limits(window_st* window,
                                   int minwidth, int minheight,
                                   int maxwidth, int maxheight)
{
    RECT area;

    if ((minwidth == SC_DONT_CARE || minheight == SC_DONT_CARE) &&
        (maxwidth == SC_DONT_CARE || maxheight == SC_DONT_CARE))
    {
        return;
    }

    GetWindowRect(window->win32.handle, &area);
    MoveWindow(window->win32.handle,
               area.left, area.top,
               area.right - area.left,
               area.bottom - area.top, TRUE);
}

static void win32_get_window_content_scale(window_st* window, float* xscale, float* yscale)
{
    const HANDLE handle = MonitorFromWindow(window->win32.handle,
                                            MONITOR_DEFAULTTONEAREST);
    win32_GetHMONITORContentScale(handle, xscale, yscale);
}

static void win32_set_window_aspect_ratio(window_st* window, int numer, int denom)
{
    RECT area;

    if (numer == SC_DONT_CARE || denom == SC_DONT_CARE)
        return;

    GetWindowRect(window->win32.handle, &area);
    applyAspectRatio(window, WMSZ_BOTTOMRIGHT, &area);
    MoveWindow(window->win32.handle,
               area.left, area.top,
               area.right - area.left,
               area.bottom - area.top, TRUE);
}


static void win32_show_window(window_st* window)
{
    int showCommand = SW_SHOWNA;

    if (window->win32.showDefault)
    {
        // NOTE: GLFW windows currently do not seem to match the Windows 10 definition of
        //       a main window, so even SW_SHOWDEFAULT does nothing
        //       This definition is undocumented and can change (source: Raymond Chen)
        // HACK: Apply the STARTUPINFO show command manually if available
        STARTUPINFOW si = { sizeof(si) };
        GetStartupInfoW(&si);
        if (si.dwFlags & STARTF_USESHOWWINDOW)
            showCommand = si.wShowWindow;

        window->win32.showDefault = false;
    }

    ShowWindow(window->win32.handle, showCommand);
}

static void win32_hide_window(window_st* window)
{
    ShowWindow(window->win32.handle, SW_HIDE);
}

static void win32_maximize_window(window_st* window)
{
    if (IsWindowVisible(window->win32.handle))
        ShowWindow(window->win32.handle, SW_MAXIMIZE);
    else
        maximizeWindowManually(window);
}

static void win32_restore_window(window_st* window)
{
    ShowWindow(window->win32.handle, SW_RESTORE);
}

static void win32_focus_window(window_st* window)
{
    BringWindowToTop(window->win32.handle);
    SetForegroundWindow(window->win32.handle);
    SetFocus(window->win32.handle);
}

static void win32_iconify_window(window_st* window)
{
    ShowWindow(window->win32.handle, SW_MINIMIZE);
}

static void win32_request_window_attention(window_st* window)
{
    FlashWindow(window->win32.handle, TRUE);
}


static bool win32_window_visible(window_st* window)
{
    return IsWindowVisible(window->win32.handle);
}

static bool win32_window_maximized(window_st* window)
{
    return IsZoomed(window->win32.handle);
}

static bool win32_window_focused(window_st* window)
{
    return window->win32.handle == GetActiveWindow();
}

static bool win32_window_hovered(window_st* window)
{
    return cursorInContentArea(window);
}

static bool win32_window_iconified(window_st* window)
{
    return IsIconic(window->win32.handle);
}


static void win32_set_cursor(window_st* window, cursor_st* cursor)
{
    if (cursorInContentArea(window))
        updateCursorImage(window);
}

static bool win32_create_standard_cursor(cursor_st* cursor, int shape)
{
    int id = 0;

    switch (shape)
    {
        case SC_ARROW_CURSOR:
            id = OCR_NORMAL;
            break;
        case SC_IBEAM_CURSOR:
            id = OCR_IBEAM;
            break;
        case SC_CROSSHAIR_CURSOR:
            id = OCR_CROSS;
            break;
        case SC_POINTING_HAND_CURSOR:
            id = OCR_HAND;
            break;
        case SC_RESIZE_EW_CURSOR:
            id = OCR_SIZEWE;
            break;
        case SC_RESIZE_NS_CURSOR:
            id = OCR_SIZENS;
            break;
        case SC_RESIZE_NWSE_CURSOR:
            id = OCR_SIZENWSE;
            break;
        case SC_RESIZE_NESW_CURSOR:
            id = OCR_SIZENESW;
            break;
        case SC_RESIZE_ALL_CURSOR:
            id = OCR_SIZEALL;
            break;
        case SC_NOT_ALLOWED_CURSOR:
            id = OCR_NO;
            break;
        default:
            impl_on_error(SC_WSI_ERR_PLATFORM_ERROR, "Win32: Unknown standard cursor");
            return false;
    }

    cursor->win32.handle = LoadImageW(NULL,
                                      MAKEINTRESOURCEW(id), IMAGE_CURSOR, 0, 0,
                                      LR_DEFAULTSIZE | LR_SHARED);
    if (!cursor->win32.handle)
    {
        win32_InputError(SC_WSI_ERR_PLATFORM_ERROR,
                             "Win32: Failed to create standard cursor");
        return false;
    }

    return true;
}

static bool win32_create_cursor(cursor_st* cursor,
                                const GLFWimage* image,
                                int xhot, int yhot)
{
    cursor->win32.handle = (HCURSOR) createIcon(image, xhot, yhot, false);
    if (!cursor->win32.handle)
        return false;

    return true;
}

static void win32_destroy_cursor(cursor_st* cursor)
{
    if (cursor->win32.handle)
        DestroyIcon((HICON) cursor->win32.handle);
}

static void win32_get_cursor_pos(window_st* window, double* xpos, double* ypos)
{
    POINT pos;

    if (GetCursorPos(&pos))
    {
        ScreenToClient(window->win32.handle, &pos);

        if (xpos)
            *xpos = pos.x;
        if (ypos)
            *ypos = pos.y;
    }
}

static void win32_set_cursor_pos(window_st* window, double xpos, double ypos)
{
    POINT pos = { (int) xpos, (int) ypos };

    // Store the new position so it can be recognized later
    window->win32.lastCursorPosX = pos.x;
    window->win32.lastCursorPosY = pos.y;

    ClientToScreen(window->win32.handle, &pos);
    SetCursorPos(pos.x, pos.y);
}

static void win32_set_cursor_mode(window_st* window, int mode)
{
    if (win32_window_focused(window))
    {
        if (mode == SC_CURSOR_DISABLED)
        {
            win32_get_cursor_pos(window,
                                   &g_wsi.win32.restoreCursorPosX,
                                   &g_wsi.win32.restoreCursorPosY);
            wsi_center_cursor_in_content_area(window);
            if (window->rawMouseMotion)
                enableRawMouseMotion(window);
        }
        else if (g_wsi.win32.disabledCursorWindow == window)
        {
            if (window->rawMouseMotion)
                disableRawMouseMotion(window);
        }

        if (mode == SC_CURSOR_DISABLED || mode == SC_CURSOR_CAPTURED)
            captureCursor(window);
        else
            releaseCursor();

        if (mode == SC_CURSOR_DISABLED)
            g_wsi.win32.disabledCursorWindow = window;
        else if (g_wsi.win32.disabledCursorWindow == window)
        {
            g_wsi.win32.disabledCursorWindow = NULL;
            win32_set_cursor_pos(window,
                                   g_wsi.win32.restoreCursorPosX,
                                   g_wsi.win32.restoreCursorPosY);
        }
    }

    if (cursorInContentArea(window))
        updateCursorImage(window);
}

static void win32_set_mouse_raw_motion(window_st *window, bool enabled)
{
    if (g_wsi.win32.disabledCursorWindow != window)
        return;

    if (enabled)
        enableRawMouseMotion(window);
    else
        disableRawMouseMotion(window);
}

static bool win32_mouse_raw_motion_supported(void)
{
    return true;
}


static int win32_get_key_scancode(int key)
{
    return g_wsi.win32.scancodes[key];
}

static const char* win32_get_scancode_name(int scancode)
{
    if (scancode < 0 || scancode > (KF_EXTENDED | 0xff))
    {
        impl_on_error(SC_WSI_ERR_INVALID_VALUE, "Invalid scancode %i", scancode);
        return NULL;
    }

    const int key = g_wsi.win32.keycodes[scancode];
    if (key == SC_KEY_UNKNOWN)
        return NULL;

    return g_wsi.win32.keynames[key];
}

static void win32_set_clipboard_string(const char* string)
{
    int characterCount, tries = 0;
    HANDLE object;
    WCHAR* buffer;

    characterCount = MultiByteToWideChar(CP_UTF8, 0, string, -1, NULL, 0);
    if (!characterCount)
        return;

    object = GlobalAlloc(GMEM_MOVEABLE, characterCount * sizeof(WCHAR));
    if (!object)
    {
        win32_InputError(SC_WSI_ERR_PLATFORM_ERROR,
                             "Win32: Failed to allocate global handle for clipboard");
        return;
    }

    buffer = GlobalLock(object);
    if (!buffer)
    {
        win32_InputError(SC_WSI_ERR_PLATFORM_ERROR,
                             "Win32: Failed to lock global handle");
        GlobalFree(object);
        return;
    }

    MultiByteToWideChar(CP_UTF8, 0, string, -1, buffer, characterCount);
    GlobalUnlock(object);

    // NOTE: Retry clipboard opening a few times as some other application may have it
    //       open and also the Windows Clipboard History reads it after each update
    while (!OpenClipboard(g_wsi.win32.helperWindowHandle))
    {
        Sleep(1);
        tries++;

        if (tries == 3)
        {
            win32_InputError(SC_WSI_ERR_PLATFORM_ERROR,
                                 "Win32: Failed to open clipboard");
            GlobalFree(object);
            return;
        }
    }

    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, object);
    CloseClipboard();
}

static const char* win32_get_clipboard_string(void)
{
    HANDLE object;
    WCHAR* buffer;
    int tries = 0;

    // NOTE: Retry clipboard opening a few times as some other application may have it
    //       open and also the Windows Clipboard History reads it after each update
    while (!OpenClipboard(g_wsi.win32.helperWindowHandle))
    {
        Sleep(1);
        tries++;

        if (tries == 3)
        {
            win32_InputError(SC_WSI_ERR_PLATFORM_ERROR,
                                 "Win32: Failed to open clipboard");
            return NULL;
        }
    }

    object = GetClipboardData(CF_UNICODETEXT);
    if (!object)
    {
        win32_InputError(SC_WSI_ERR_FORMAT_UNAVAILABLE,
                             "Win32: Failed to convert clipboard to string");
        CloseClipboard();
        return NULL;
    }

    buffer = GlobalLock(object);
    if (!buffer)
    {
        win32_InputError(SC_WSI_ERR_PLATFORM_ERROR,
                             "Win32: Failed to lock global handle");
        CloseClipboard();
        return NULL;
    }

    wsi_free(g_wsi.win32.clipboardString);
    g_wsi.win32.clipboardString = win32_CreateUTF8FromWideString(buffer);

    GlobalUnlock(object);
    CloseClipboard();

    return g_wsi.win32.clipboardString;
}

///////////////////////////////////////////////////////////////////////////////
// Window 
///////////////////////////////////////////////////////////////////////////////

// Apply disabled cursor mode to a focused window
static void disableCursor(window_st* window)
{
    g_wsi.win32.disabledCursorWindow = window;
    win32_get_cursor_pos(window,
                           &g_wsi.win32.restoreCursorPosX,
                           &g_wsi.win32.restoreCursorPosY);
    updateCursorImage(window);
    wsi_center_cursor_in_content_area(window);
    captureCursor(window);

    if (window->rawMouseMotion)
        enableRawMouseMotion(window);
}

// Exit disabled cursor mode for the specified window
static void enableCursor(window_st* window)
{
    if (window->rawMouseMotion)
        disableRawMouseMotion(window);

    g_wsi.win32.disabledCursorWindow = NULL;
    releaseCursor();
    win32_set_cursor_pos(window,
                           g_wsi.win32.restoreCursorPosX,
                           g_wsi.win32.restoreCursorPosY);
    updateCursorImage(window);
}

// 窗口过程
static LRESULT CALLBACK windowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    window_st* window = GetPropW(hWnd, L"GLFW");
    if (!window)
    {
        if (uMsg == WM_NCCREATE)
        {
            if (win32_IsWindows10Version1607OrGreater())
            {
                const CREATESTRUCTW* cs = (const CREATESTRUCTW*) lParam;
                const wnd_config_st* wndconfig = cs->lpCreateParams;

                // On per-monitor DPI aware V1 systems, only enable
                // non-client scaling for windows that scale the client area
                // We need WM_GETDPISCALEDSIZE from V2 to keep the client
                // area static when the non-client area is scaled
                if (wndconfig && wndconfig->scaleToMonitor)
                    EnableNonClientDpiScaling(hWnd);
            }
        }

        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }

    switch (uMsg)
    {
        case WM_MOUSEACTIVATE:
        {
            // HACK: Postpone cursor disabling when the window was activated by
            //       clicking a caption button
            if (HIWORD(lParam) == WM_LBUTTONDOWN)
            {
                if (LOWORD(lParam) != HTCLIENT)
                    window->win32.frameAction = true;
            }

            break;
        }

        case WM_CAPTURECHANGED:
        {
            // HACK: Disable the cursor once the caption button action has been
            //       completed or cancelled
            if (lParam == 0 && window->win32.frameAction)
            {
                if (window->cursorMode == SC_CURSOR_DISABLED)
                    disableCursor(window);
                else if (window->cursorMode == SC_CURSOR_CAPTURED)
                    captureCursor(window);

                window->win32.frameAction = false;
            }

            break;
        }

        case WM_SETFOCUS:
        {
            impl_on_win_focus(window, true);

            // HACK: Do not disable cursor while the user is interacting with
            //       a caption button
            if (window->win32.frameAction)
                break;

            if (window->cursorMode == SC_CURSOR_DISABLED)
                disableCursor(window);
            else if (window->cursorMode == SC_CURSOR_CAPTURED)
                captureCursor(window);

            return 0;
        }

        case WM_KILLFOCUS:
        {
            if (window->cursorMode == SC_CURSOR_DISABLED)
                enableCursor(window);
            else if (window->cursorMode == SC_CURSOR_CAPTURED)
                releaseCursor();

            if (window->monitor && window->autoIconify)
                win32_iconify_window(window);

            impl_on_win_focus(window, false);
            return 0;
        }

        case WM_SYSCOMMAND:
        {
            switch (wParam & 0xfff0)
            {
                case SC_SCREENSAVE:
                case SC_MONITORPOWER:
                {
                    if (window->monitor)
                    {
                        // We are running in full screen mode, so disallow
                        // screen saver and screen blanking
                        return 0;
                    }
                    else
                        break;
                }

                // User trying to access application menu using ALT?
                case SC_KEYMENU:
                {
                    if (!window->win32.keymenu)
                        return 0;

                    break;
                }
            }
            break;
        }

        case WM_CLOSE:
        {
            impl_on_win_close_req(window);
            return 0;
        }

        case WM_INPUTLANGCHANGE:
        {
            win32_UpdateKeyNames();
            break;
        }

        case WM_CHAR:
        case WM_SYSCHAR:
        {
            if (wParam >= 0xd800 && wParam <= 0xdbff)
                window->win32.highSurrogate = (WCHAR) wParam;
            else
            {
                uint32_t codepoint = 0;

                if (wParam >= 0xdc00 && wParam <= 0xdfff)
                {
                    if (window->win32.highSurrogate)
                    {
                        codepoint += (window->win32.highSurrogate - 0xd800) << 10;
                        codepoint += (WCHAR) wParam - 0xdc00;
                        codepoint += 0x10000;
                    }
                }
                else
                    codepoint = (WCHAR) wParam;

                window->win32.highSurrogate = 0;
                impl_on_chr(window, codepoint, getKeyMods(), uMsg != WM_SYSCHAR);
            }

            if (uMsg == WM_SYSCHAR && window->win32.keymenu)
                break;

            return 0;
        }

        case WM_UNICHAR:
        {
            if (wParam == UNICODE_NOCHAR)
            {
                // WM_UNICHAR is not sent by Windows, but is sent by some
                // third-party input method engine
                // Returning TRUE here announces support for this message
                return TRUE;
            }

            impl_on_chr(window, (uint32_t) wParam, getKeyMods(), true);
            return 0;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP:
        {
            int key, scancode;
            const int action = (HIWORD(lParam) & KF_UP) ? SC_RELEASE : SC_PRESS;
            const int mods = getKeyMods();

            scancode = (HIWORD(lParam) & (KF_EXTENDED | 0xff));
            if (!scancode)
            {
                // NOTE: Some synthetic key messages have a scancode of zero
                // HACK: Map the virtual key back to a usable scancode
                scancode = MapVirtualKeyW((UINT) wParam, MAPVK_VK_TO_VSC);
            }

            // HACK: Alt+PrtSc has a different scancode than just PrtSc
            if (scancode == 0x54)
                scancode = 0x137;

            // HACK: Ctrl+Pause has a different scancode than just Pause
            if (scancode == 0x146)
                scancode = 0x45;

            // HACK: CJK IME sets the extended bit for right Shift
            if (scancode == 0x136)
                scancode = 0x36;

            key = g_wsi.win32.keycodes[scancode];

            // The Ctrl keys require special handling
            if (wParam == VK_CONTROL)
            {
                if (HIWORD(lParam) & KF_EXTENDED)
                {
                    // Right side keys have the extended key bit set
                    key = SC_KEY_RIGHT_CONTROL;
                }
                else
                {
                    // NOTE: Alt Gr sends Left Ctrl followed by Right Alt
                    // HACK: We only want one event for Alt Gr, so if we detect
                    //       this sequence we discard this Left Ctrl message now
                    //       and later report Right Alt normally
                    MSG next;
                    const DWORD time = GetMessageTime();

                    if (PeekMessageW(&next, NULL, 0, 0, PM_NOREMOVE))
                    {
                        if (next.message == WM_KEYDOWN ||
                            next.message == WM_SYSKEYDOWN ||
                            next.message == WM_KEYUP ||
                            next.message == WM_SYSKEYUP)
                        {
                            if (next.wParam == VK_MENU &&
                                (HIWORD(next.lParam) & KF_EXTENDED) &&
                                next.time == time)
                            {
                                // Next message is Right Alt down so discard this
                                break;
                            }
                        }
                    }

                    // This is a regular Left Ctrl message
                    key = SC_KEY_LEFT_CONTROL;
                }
            }
            else if (wParam == VK_PROCESSKEY)
            {
                // IME notifies that keys have been filtered by setting the
                // virtual key-code to VK_PROCESSKEY
                break;
            }

            if (action == SC_RELEASE && wParam == VK_SHIFT)
            {
                // HACK: Release both Shift keys on Shift up event, as when both
                //       are pressed the first release does not emit any event
                // NOTE: The other half of this is in win32_poll_events
                impl_on_key(window, SC_KEY_LEFT_SHIFT, scancode, action, mods);
                impl_on_key(window, SC_KEY_RIGHT_SHIFT, scancode, action, mods);
            }
            else if (wParam == VK_SNAPSHOT)
            {
                // HACK: Key down is not reported for the Print Screen key
                impl_on_key(window, key, scancode, SC_PRESS, mods);
                impl_on_key(window, key, scancode, SC_RELEASE, mods);
            }
            else
                impl_on_key(window, key, scancode, action, mods);

            break;
        }

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_XBUTTONUP:
        {
            int i, button, action;

            if (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONUP)
                button = SC_MOUSE_BUTTON_LEFT;
            else if (uMsg == WM_RBUTTONDOWN || uMsg == WM_RBUTTONUP)
                button = SC_MOUSE_BUTTON_RIGHT;
            else if (uMsg == WM_MBUTTONDOWN || uMsg == WM_MBUTTONUP)
                button = SC_MOUSE_BUTTON_MIDDLE;
            else if (GET_XBUTTON_WPARAM(wParam) == XBUTTON1)
                button = SC_MOUSE_BUTTON_4;
            else
                button = SC_MOUSE_BUTTON_5;

            if (uMsg == WM_LBUTTONDOWN || uMsg == WM_RBUTTONDOWN ||
                uMsg == WM_MBUTTONDOWN || uMsg == WM_XBUTTONDOWN)
            {
                action = SC_PRESS;
            }
            else
                action = SC_RELEASE;

            for (i = 0;  i <= SC_MOUSE_BUTTON_LAST;  i++)
            {
                if (window->mouseButtons[i] == SC_PRESS)
                    break;
            }

            if (i > SC_MOUSE_BUTTON_LAST)
                SetCapture(hWnd);

            impl_on_mouse_click(window, button, action, getKeyMods());

            for (i = 0;  i <= SC_MOUSE_BUTTON_LAST;  i++)
            {
                if (window->mouseButtons[i] == SC_PRESS)
                    break;
            }

            if (i > SC_MOUSE_BUTTON_LAST)
                ReleaseCapture();

            if (uMsg == WM_XBUTTONDOWN || uMsg == WM_XBUTTONUP)
                return TRUE;

            return 0;
        }

        case WM_MOUSEMOVE:
        {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);

            if (!window->win32.cursorTracked)
            {
                TRACKMOUSEEVENT tme;
                ZeroMemory(&tme, sizeof(tme));
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = window->win32.handle;
                TrackMouseEvent(&tme);

                window->win32.cursorTracked = true;
                impl_on_cursor_enter(window, true);
            }

            if (window->cursorMode == SC_CURSOR_DISABLED)
            {
                const int dx = x - window->win32.lastCursorPosX;
                const int dy = y - window->win32.lastCursorPosY;

                if (g_wsi.win32.disabledCursorWindow != window)
                    break;
                if (window->rawMouseMotion)
                    break;

                impl_on_cursor_pos(window,
                                    window->virtualCursorPosX + dx,
                                    window->virtualCursorPosY + dy);
            }
            else
                impl_on_cursor_pos(window, x, y);

            window->win32.lastCursorPosX = x;
            window->win32.lastCursorPosY = y;

            return 0;
        }

        case WM_INPUT:
        {
            UINT size = 0;
            HRAWINPUT ri = (HRAWINPUT) lParam;
            RAWINPUT* data = NULL;
            int dx, dy;

            if (g_wsi.win32.disabledCursorWindow != window)
                break;
            if (!window->rawMouseMotion)
                break;

            GetRawInputData(ri, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER));
            if (size > (UINT) g_wsi.win32.rawInputSize)
            {
                wsi_free(g_wsi.win32.rawInput);
                g_wsi.win32.rawInput = wsi_calloc(size, 1);
                g_wsi.win32.rawInputSize = size;
            }

            size = g_wsi.win32.rawInputSize;
            if (GetRawInputData(ri, RID_INPUT,
                                g_wsi.win32.rawInput, &size,
                                sizeof(RAWINPUTHEADER)) == (UINT) -1)
            {
                impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                                "Win32: Failed to retrieve raw input data");
                break;
            }

            data = g_wsi.win32.rawInput;
            if (data->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)
            {
                POINT pos = {0};
                int width, height;

                if (data->data.mouse.usFlags & MOUSE_VIRTUAL_DESKTOP)
                {
                    pos.x += GetSystemMetrics(SM_XVIRTUALSCREEN);
                    pos.y += GetSystemMetrics(SM_YVIRTUALSCREEN);
                    width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
                    height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
                }
                else
                {
                    width = GetSystemMetrics(SM_CXSCREEN);
                    height = GetSystemMetrics(SM_CYSCREEN);
                }

                pos.x += (int) ((data->data.mouse.lLastX / 65535.f) * width);
                pos.y += (int) ((data->data.mouse.lLastY / 65535.f) * height);
                ScreenToClient(window->win32.handle, &pos);

                dx = pos.x - window->win32.lastCursorPosX;
                dy = pos.y - window->win32.lastCursorPosY;
            }
            else
            {
                dx = data->data.mouse.lLastX;
                dy = data->data.mouse.lLastY;
            }

            impl_on_cursor_pos(window,
                                window->virtualCursorPosX + dx,
                                window->virtualCursorPosY + dy);

            window->win32.lastCursorPosX += dx;
            window->win32.lastCursorPosY += dy;
            break;
        }

        case WM_MOUSELEAVE:
        {
            window->win32.cursorTracked = false;
            impl_on_cursor_enter(window, false);
            return 0;
        }

        case WM_MOUSEWHEEL:
        {
            impl_on_scroll(window, 0.0, (SHORT) HIWORD(wParam) / (double) WHEEL_DELTA);
            return 0;
        }

        case WM_MOUSEHWHEEL:
        {
            // NOTE: The X-axis is inverted for consistency with macOS and X11
            impl_on_scroll(window, -((SHORT) HIWORD(wParam) / (double) WHEEL_DELTA), 0.0);
            return 0;
        }

        case WM_ENTERSIZEMOVE:
        case WM_ENTERMENULOOP:
        {
            if (window->win32.frameAction)
                break;

            // HACK: Enable the cursor while the user is moving or
            //       resizing the window or using the window menu
            if (window->cursorMode == SC_CURSOR_DISABLED)
                enableCursor(window);
            else if (window->cursorMode == SC_CURSOR_CAPTURED)
                releaseCursor();

            break;
        }

        case WM_EXITSIZEMOVE:
        case WM_EXITMENULOOP:
        {
            if (window->win32.frameAction)
                break;

            // HACK: Disable the cursor once the user is done moving or
            //       resizing the window or using the menu
            if (window->cursorMode == SC_CURSOR_DISABLED)
                disableCursor(window);
            else if (window->cursorMode == SC_CURSOR_CAPTURED)
                captureCursor(window);

            break;
        }

        case WM_SIZE:
        {
            const int width = LOWORD(lParam);
            const int height = HIWORD(lParam);
            const bool iconified = wParam == SIZE_MINIMIZED;
            const bool maximized = wParam == SIZE_MAXIMIZED ||
                                       (window->win32.maximized &&
                                        wParam != SIZE_RESTORED);

            if (g_wsi.win32.capturedCursorWindow == window)
                captureCursor(window);

            if (window->win32.iconified != iconified)
                impl_on_win_iconify(window, iconified);

            if (window->win32.maximized != maximized)
                impl_on_win_maximize(window, maximized);

            if (width != window->win32.width || height != window->win32.height)
            {
                window->win32.width = width;
                window->win32.height = height;

                impl_on_win_size(window, width, height);
            }

            if (window->monitor && window->win32.iconified != iconified)
            {
                if (iconified)
                    releaseMonitor(window);
                else
                {
                    acquireMonitor(window);
                    fitToMonitor(window);
                }
            }

            window->win32.iconified = iconified;
            window->win32.maximized = maximized;
            return 0;
        }

        case WM_MOVE:
        {
            if (g_wsi.win32.capturedCursorWindow == window)
                captureCursor(window);

            // NOTE: This cannot use LOWORD/HIWORD recommended by MSDN, as
            // those macros do not handle negative window positions correctly
            impl_on_win_pos(window,
                                GET_X_LPARAM(lParam),
                                GET_Y_LPARAM(lParam));
            return 0;
        }

        case WM_SIZING:
        {
            if (window->numer == SC_DONT_CARE ||
                window->denom == SC_DONT_CARE)
            {
                break;
            }

            applyAspectRatio(window, (int) wParam, (RECT*) lParam);
            return TRUE;
        }

        case WM_GETMINMAXINFO:
        {
            RECT frame = {0};
            MINMAXINFO* mmi = (MINMAXINFO*) lParam;
            const DWORD style = getWindowStyle(window);
            const DWORD exStyle = getWindowExStyle(window);

            if (window->monitor)
                break;

            if (win32_IsWindows10Version1607OrGreater())
            {
                AdjustWindowRectExForDpi(&frame, style, FALSE, exStyle,
                                         GetDpiForWindow(window->win32.handle));
            }
            else
                AdjustWindowRectEx(&frame, style, FALSE, exStyle);

            if (window->minwidth != SC_DONT_CARE &&
                window->minheight != SC_DONT_CARE)
            {
                mmi->ptMinTrackSize.x = window->minwidth + frame.right - frame.left;
                mmi->ptMinTrackSize.y = window->minheight + frame.bottom - frame.top;
            }

            if (window->maxwidth != SC_DONT_CARE &&
                window->maxheight != SC_DONT_CARE)
            {
                mmi->ptMaxTrackSize.x = window->maxwidth + frame.right - frame.left;
                mmi->ptMaxTrackSize.y = window->maxheight + frame.bottom - frame.top;
            }

            if (!window->decorated)
            {
                MONITORINFO mi;
                const HMONITOR mh = MonitorFromWindow(window->win32.handle,
                                                      MONITOR_DEFAULTTONEAREST);

                ZeroMemory(&mi, sizeof(mi));
                mi.cbSize = sizeof(mi);
                GetMonitorInfoW(mh, &mi);

                mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
                mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
                mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
                mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
            }

            return 0;
        }

        case WM_PAINT:
        {
            impl_on_win_damage(window);
            break;
        }

        case WM_ERASEBKGND:
        {
            return TRUE;
        }

        case WM_NCACTIVATE:
        case WM_NCPAINT:
        {
            // Prevent title bar from being drawn after restoring a minimized
            // undecorated window
            if (!window->decorated)
                return TRUE;

            break;
        }

        case WM_DWMCOMPOSITIONCHANGED:
        case WM_DWMCOLORIZATIONCOLORCHANGED:
        {
            if (window->win32.transparent)
                updateFramebufferTransparency(window);
            return 0;
        }

        case WM_GETDPISCALEDSIZE:
        {
            if (window->win32.scaleToMonitor)
                break;

            // Adjust the window size to keep the content area size constant
            if (win32_IsWindows10Version1703OrGreater())
            {
                RECT source = {0}, target = {0};
                SIZE* size = (SIZE*) lParam;

                AdjustWindowRectExForDpi(&source, getWindowStyle(window),
                                         FALSE, getWindowExStyle(window),
                                         GetDpiForWindow(window->win32.handle));
                AdjustWindowRectExForDpi(&target, getWindowStyle(window),
                                         FALSE, getWindowExStyle(window),
                                         LOWORD(wParam));

                size->cx += (target.right - target.left) -
                            (source.right - source.left);
                size->cy += (target.bottom - target.top) -
                            (source.bottom - source.top);
                return TRUE;
            }

            break;
        }

        case WM_DPICHANGED:
        {
            const float xscale = HIWORD(wParam) / (float) USER_DEFAULT_SCREEN_DPI;
            const float yscale = LOWORD(wParam) / (float) USER_DEFAULT_SCREEN_DPI;

            // Resize windowed mode windows that either permit rescaling or that
            // need it to compensate for non-client area scaling
            if (!window->monitor &&
                (window->win32.scaleToMonitor ||
                 win32_IsWindows10Version1703OrGreater()))
            {
                RECT* suggested = (RECT*) lParam;
                SetWindowPos(window->win32.handle, HWND_TOP,
                             suggested->left,
                             suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOACTIVATE | SWP_NOZORDER);
            }

            impl_on_win_content_scale(window, xscale, yscale);
            break;
        }

        case WM_SETCURSOR:
        {
            if (LOWORD(lParam) == HTCLIENT)
            {
                updateCursorImage(window);
                return TRUE;
            }

            break;
        }

        case WM_DROPFILES:
        {
            HDROP drop = (HDROP) wParam;
            POINT pt;
            int i;

            const int count = DragQueryFileW(drop, 0xffffffff, NULL, 0);
            char** paths = wsi_calloc(count, sizeof(char*));

            // Move the mouse to the position of the drop
            DragQueryPoint(drop, &pt);
            impl_on_cursor_pos(window, pt.x, pt.y);

            for (i = 0;  i < count;  i++)
            {
                const UINT length = DragQueryFileW(drop, i, NULL, 0);
                WCHAR* buffer = wsi_calloc((size_t) length + 1, sizeof(WCHAR));

                DragQueryFileW(drop, i, buffer, length + 1);
                paths[i] = win32_CreateUTF8FromWideString(buffer);

                wsi_free(buffer);
            }

            impl_on_drop(window, count, (const char**) paths);

            for (i = 0;  i < count;  i++)
                wsi_free(paths[i]);
            wsi_free(paths);

            DragFinish(drop);
            return 0;
        }
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

// Creates the GLFW window
//
static int createNativeWindow(window_st* window,
                              const wnd_config_st* wndconfig)
{
    int frameX, frameY, frameWidth, frameHeight;
    WCHAR* wideTitle;
    DWORD style = getWindowStyle(window);
    DWORD exStyle = getWindowExStyle(window);

    if (!g_wsi.win32.mainWindowClass)
    {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc   = windowProc;
        wc.hInstance     = g_wsi.win32.instance;
        wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
#if defined(_GLFW_WNDCLASSNAME)
        wc.lpszClassName = _GLFW_WNDCLASSNAME;
#else
        wc.lpszClassName = L"GLFW30";
#endif
        // Load user-provided icon if available
        wc.hIcon = LoadImageW(GetModuleHandleW(NULL),
                              L"GLFW_ICON", IMAGE_ICON,
                              0, 0, LR_DEFAULTSIZE | LR_SHARED);
        if (!wc.hIcon)
        {
            // No user-provided icon found, load default icon
            wc.hIcon = LoadImageW(NULL,
                                  IDI_APPLICATION, IMAGE_ICON,
                                  0, 0, LR_DEFAULTSIZE | LR_SHARED);
        }

        g_wsi.win32.mainWindowClass = RegisterClassExW(&wc);
        if (!g_wsi.win32.mainWindowClass)
        {
            win32_InputError(SC_WSI_ERR_PLATFORM_ERROR,
                                 "Win32: Failed to register window class");
            return false;
        }
    }

    if (GetSystemMetrics(SM_REMOTESESSION))
    {
        // NOTE: On Remote Desktop, setting the cursor to NULL does not hide it
        // HACK: Create a transparent cursor and always set that instead of NULL
        //       When not on Remote Desktop, this handle is NULL and normal hiding is used
        if (!g_wsi.win32.blankCursor)
        {
            const int cursorWidth = GetSystemMetrics(SM_CXCURSOR);
            const int cursorHeight = GetSystemMetrics(SM_CYCURSOR);

            unsigned char* cursorPixels = wsi_calloc(cursorWidth * cursorHeight, 4);
            if (!cursorPixels)
                return false;

            // NOTE: Windows checks whether the image is fully transparent and if so
            //       just ignores the alpha channel and makes the whole cursor opaque
            // HACK: Make one pixel slightly less transparent
            cursorPixels[3] = 1;

            const GLFWimage cursorImage = { cursorWidth, cursorHeight, cursorPixels };
            g_wsi.win32.blankCursor = createIcon(&cursorImage, 0, 0, FALSE);
            wsi_free(cursorPixels);

            if (!g_wsi.win32.blankCursor)
                return false;
        }
    }

    if (window->monitor)
    {
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(window->monitor->win32.handle, &mi);

        // NOTE: This window placement is temporary and approximate, as the
        //       correct position and size cannot be known until the monitor
        //       video mode has been picked in win32_set_video_mode
        frameX = mi.rcMonitor.left;
        frameY = mi.rcMonitor.top;
        frameWidth  = mi.rcMonitor.right - mi.rcMonitor.left;
        frameHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;
    }
    else
    {
        RECT rect = { 0, 0, wndconfig->width, wndconfig->height };

        window->win32.maximized = wndconfig->maximized;
        if (wndconfig->maximized)
            style |= WS_MAXIMIZE;

        AdjustWindowRectEx(&rect, style, FALSE, exStyle);

        if (wndconfig->xpos == SC_ANY_POSITION && wndconfig->ypos == SC_ANY_POSITION)
        {
            frameX = CW_USEDEFAULT;
            frameY = CW_USEDEFAULT;
        }
        else
        {
            frameX = wndconfig->xpos + rect.left;
            frameY = wndconfig->ypos + rect.top;
        }

        frameWidth  = rect.right - rect.left;
        frameHeight = rect.bottom - rect.top;
    }

    wideTitle = win32_CreateWideStringFromUTF8(window->title);
    if (!wideTitle)
        return false;

    window->win32.handle = CreateWindowExW(exStyle,
                                           MAKEINTATOM(g_wsi.win32.mainWindowClass),
                                           wideTitle,
                                           style,
                                           frameX, frameY,
                                           frameWidth, frameHeight,
                                           NULL, // No parent window
                                           NULL, // No window menu
                                           g_wsi.win32.instance,
                                           (LPVOID) wndconfig);

    wsi_free(wideTitle);

    if (!window->win32.handle)
    {
        win32_InputError(SC_WSI_ERR_PLATFORM_ERROR,
                             "Win32: Failed to create window");
        return false;
    }

    SetPropW(window->win32.handle, L"GLFW", window);

    ChangeWindowMessageFilterEx(window->win32.handle, WM_DROPFILES, MSGFLT_ALLOW, NULL);
    ChangeWindowMessageFilterEx(window->win32.handle, WM_COPYDATA, MSGFLT_ALLOW, NULL);
    ChangeWindowMessageFilterEx(window->win32.handle, WM_COPYGLOBALDATA, MSGFLT_ALLOW, NULL);

    window->win32.scaleToMonitor = wndconfig->scaleToMonitor;
    window->win32.keymenu = wndconfig->win32.keymenu;
    window->win32.showDefault = wndconfig->win32.showDefault;

    if (!window->monitor)
    {
        RECT rect = { 0, 0, wndconfig->width, wndconfig->height };
        WINDOWPLACEMENT wp = { sizeof(wp) };
        const HMONITOR mh = MonitorFromWindow(window->win32.handle,
                                              MONITOR_DEFAULTTONEAREST);

        // Adjust window rect to account for DPI scaling of the window frame and
        // (if enabled) DPI scaling of the content area
        // This cannot be done until we know what monitor the window was placed on
        // Only update the restored window rect as the window may be maximized

        if (wndconfig->scaleToMonitor)
        {
            float xscale, yscale;
            win32_GetHMONITORContentScale(mh, &xscale, &yscale);

            if (xscale > 0.f && yscale > 0.f)
            {
                rect.right = (int) (rect.right * xscale);
                rect.bottom = (int) (rect.bottom * yscale);
            }
        }

        if (win32_IsWindows10Version1607OrGreater())
        {
            AdjustWindowRectExForDpi(&rect, style, FALSE, exStyle,
                                     GetDpiForWindow(window->win32.handle));
        }
        else
            AdjustWindowRectEx(&rect, style, FALSE, exStyle);

        GetWindowPlacement(window->win32.handle, &wp);
        OffsetRect(&rect,
                   wp.rcNormalPosition.left - rect.left,
                   wp.rcNormalPosition.top - rect.top);

        wp.rcNormalPosition = rect;
        wp.showCmd = SW_HIDE;
        SetWindowPlacement(window->win32.handle, &wp);

        // Adjust rect of maximized undecorated window, because by default Windows will
        // make such a window cover the whole monitor instead of its workarea

        if (wndconfig->maximized && !wndconfig->decorated)
        {
            MONITORINFO mi = { sizeof(mi) };
            GetMonitorInfoW(mh, &mi);

            SetWindowPos(window->win32.handle, HWND_TOP,
                         mi.rcWork.left,
                         mi.rcWork.top,
                         mi.rcWork.right - mi.rcWork.left,
                         mi.rcWork.bottom - mi.rcWork.top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
        }
    }

    DragAcceptFiles(window->win32.handle, TRUE);

    win32_get_window_size(window, &window->win32.width, &window->win32.height);

    return true;
}

bool win32_create_window(window_st* window,
                                const wnd_config_st* wndconfig)
{
    if (!createNativeWindow(window, wndconfig))
        return false;

    if (wndconfig->mousePassthrough)
        win32_set_window_mouse_passthrough(window, true);

    if (window->monitor)
    {
        win32_show_window(window);
        win32_focus_window(window);
        acquireMonitor(window);
        fitToMonitor(window);

        if (wndconfig->centerCursor)
            wsi_center_cursor_in_content_area(window);
    }
    else
    {
        if (wndconfig->visible)
        {
            win32_show_window(window);
            if (wndconfig->focused)
                win32_focus_window(window);
        }
    }

    return true;
}

void win32_destroy_window(window_st* window)
{
    if (window->monitor)
        releaseMonitor(window);

    if (g_wsi.win32.disabledCursorWindow == window)
        enableCursor(window);

    if (g_wsi.win32.capturedCursorWindow == window)
        releaseCursor();

    if (window->win32.handle)
    {
        RemovePropW(window->win32.handle, L"GLFW");
        DestroyWindow(window->win32.handle);
        window->win32.handle = NULL;
    }

    if (window->win32.bigIcon)
        DestroyIcon(window->win32.bigIcon);

    if (window->win32.smallIcon)
        DestroyIcon(window->win32.smallIcon);
}

///////////////////////////////////////////////////////////////////////////////
// app loop
///////////////////////////////////////////////////////////////////////////////

static void win32_poll_events(void)
{
    MSG msg;
    HWND handle;
    window_st* window;

    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
        {
            // NOTE: While GLFW does not itself post WM_QUIT, other processes
            //       may post it to this one, for example Task Manager
            // HACK: Treat WM_QUIT as a close on all windows

            window = g_wsi.windowListHead;
            while (window)
            {
                impl_on_win_close_req(window);
                window = window->next;
            }
        }
        else
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    // HACK: Release modifier keys that the system did not emit KEYUP for
    // NOTE: Shift keys on Windows tend to "stick" when both are pressed as
    //       no key up message is generated by the first key release
    // NOTE: Windows key is not reported as released by the Win+V hotkey
    //       Other Win hotkeys are handled implicitly by impl_on_win_focus
    //       because they change the input focus
    // NOTE: The other half of this is in the WM_*KEY* handler in windowProc
    handle = GetActiveWindow();
    if (handle)
    {
        window = GetPropW(handle, L"GLFW");
        if (window)
        {
            int i;
            const int keys[4][2] =
            {
                { VK_LSHIFT, SC_KEY_LEFT_SHIFT },
                { VK_RSHIFT, SC_KEY_RIGHT_SHIFT },
                { VK_LWIN, SC_KEY_LEFT_SUPER },
                { VK_RWIN, SC_KEY_RIGHT_SUPER }
            };

            for (i = 0;  i < 4;  i++)
            {
                const int vk = keys[i][0];
                const int key = keys[i][1];
                const int scancode = g_wsi.win32.scancodes[key];

                if ((GetKeyState(vk) & 0x8000))
                    continue;
                if (window->keys[key] != SC_PRESS)
                    continue;

                impl_on_key(window, key, scancode, SC_RELEASE, getKeyMods());
            }
        }
    }

    window = g_wsi.win32.disabledCursorWindow;
    if (window)
    {
        int width, height;
        win32_get_window_size(window, &width, &height);

        // NOTE: Re-center the cursor only if it has moved since the last call,
        //       to avoid breaking sc_wsi_wait_events with WM_MOUSEMOVE
        // The re-center is required in order to prevent the mouse cursor stopping at the edges of the screen.
        if (window->win32.lastCursorPosX != width / 2 ||
            window->win32.lastCursorPosY != height / 2)
        {
            win32_set_cursor_pos(window, width / 2, height / 2);
        }
    }
}

static void win32_wait_events(void)
{
    WaitMessage();

    win32_poll_events();
}

static void win32_wait_eventsTimeout(double timeout)
{
    MsgWaitForMultipleObjects(0, NULL, FALSE, (DWORD) (timeout * 1e3), QS_ALLINPUT);

    win32_poll_events();
}

static void win32_post_empty_event(void)
{
    PostMessageW(g_wsi.win32.helperWindowHandle, WM_NULL, 0, 0);
}

///////////////////////////////////////////////////////////////////////////////
// 接口集成
///////////////////////////////////////////////////////////////////////////////

WSI_API HWND wsi_get_win32_window(sc_window* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }

    if (g_wsi.platform.platformID != SC_PLATFORM_WIN32)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_UNAVAILABLE,
                        "Win32: Platform not initialized");
        return NULL;
    }

    window_st* window = (window_st*) handle;
    assert(window != NULL);

    return window->win32.handle;
}

WSI_API const char* wsi_get_win32_adapter(sc_monitor* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }

    if (g_wsi.platform.platformID != SC_PLATFORM_WIN32)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_UNAVAILABLE, "Win32: Platform not initialized");
        return NULL;
    }

    monitor_st* monitor = (monitor_st*) handle;
    assert(monitor != NULL);

    return monitor->win32.publicAdapterName;
}

WSI_API const char* wsi_get_win32_monitor(sc_monitor* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return NULL;
    }

    if (g_wsi.platform.platformID != SC_PLATFORM_WIN32)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_UNAVAILABLE, "Win32: Platform not initialized");
        return NULL;
    }

    monitor_st* monitor = (monitor_st*) handle;
    assert(monitor != NULL);

    return monitor->win32.publicDisplayName;
}

bool win32_connect(int platformID, platform_st* platform)
{
    const platform_st win32 =
    {
        .platformID = SC_PLATFORM_WIN32,
        .init = win32_init,
        .terminate = win32_terminate,

        .pollEvents = win32_poll_events,
        .waitEvents = win32_wait_events,
        .waitEventsTimeout = win32_wait_eventsTimeout,
        .postEmptyEvent = win32_post_empty_event,

        .createWindow = win32_create_window,
        .destroyWindow = win32_destroy_window,
        .setWindowTitle = win32_set_window_title,
        .setWindowIcon = win32_set_window_icon,
        .setWindowMonitor = win32_set_window_monitor,
        .setWindowMousePassthrough = win32_set_window_mouse_passthrough,

        .setWindowDecorated = win32_set_window_decorated,
        .setWindowResizable = win32_set_window_resizable,
        .setWindowFloating = win32_set_window_floating,
        .setWindowOpacity = win32_set_window_opacity,
        .getWindowOpacity = win32_get_window_opacity,

        .getWindowPos = win32_get_window_pos,
        .setWindowPos = win32_set_window_pos,
        .getWindowSize = win32_get_window_size,
        .getFramebufferSize = win32_get_framebuffer_size,
        .setWindowSize = win32_set_window_size,
        .getWindowFrameSize = win32_get_window_frame_size,
        .setWindowSizeLimits = win32_set_window_size_limits,
        .getWindowContentScale = win32_get_window_content_scale,
        .setWindowAspectRatio = win32_set_window_aspect_ratio,

        .showWindow = win32_show_window,
        .hideWindow = win32_hide_window,
        .maximizeWindow = win32_maximize_window,
        .restoreWindow = win32_restore_window,
        .focusWindow = win32_focus_window,
        .iconifyWindow = win32_iconify_window,
        .requestWindowAttention = win32_request_window_attention,

        .windowVisible = win32_window_visible,
        .windowMaximized = win32_window_maximized,
        .windowFocused = win32_window_focused,
        .windowHovered = win32_window_hovered,
        .windowIconified = win32_window_iconified,

        .setCursor = win32_set_cursor,
        .createStandardCursor = win32_create_standard_cursor,
        .createCursor = win32_create_cursor,
        .destroyCursor = win32_destroy_cursor,
        .setCursorMode = win32_set_cursor_mode,
        .setCursorPos = win32_set_cursor_pos,
        .getCursorPos = win32_get_cursor_pos,
        .setRawMouseMotion = win32_set_mouse_raw_motion,
        .rawMouseMotionSupported = win32_mouse_raw_motion_supported,

        .getKeyScancode = win32_get_key_scancode,
        .getScancodeName = win32_get_scancode_name,
        .getClipboardString = win32_get_clipboard_string,
        .setClipboardString = win32_set_clipboard_string,

        .freeMonitor = win32_free_monitor,
        .getMonitorPos = win32_get_monitor_pos,
        .getMonitorWorkarea = win32_get_monitor_work_area,
        .getMonitorContentScale = win32_get_monitor_content_scale,
        .getVideoModes = win32_get_video_modes,
        .getVideoMode = win32_get_video_mode,
        .getGammaRamp = win32_get_gamma_ramp,
        .setGammaRamp = win32_set_gamma_ramp,
    };

    *platform = win32;
    return true;
}

///////////////////////////////////////////////////////////////////////////////

#endif // WSI_WIN32

