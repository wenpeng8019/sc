
#include "internal.h"

#if defined(WSI_COCOA)

#include <sys/param.h>      // For MAXPATHLEN
#include <crt_externs.h>    // Needed for _NSGetProgname

#include <IOKit/graphics/IOGraphicsLib.h>
#include <ApplicationServices/ApplicationServices.h>
#import <QuartzCore/CAMetalLayer.h>

///////////////////////////////////////////////////////////////////////////////
// platform utils
///////////////////////////////////////////////////////////////////////////////

// （如果存在）将当前工作目录更改为主捆绑包的资源目录
static void changeToResourcesDirectory(void)
{
    char resourcesPath[MAXPATHLEN];

    // 获取主捆绑包的资源目录
    CFBundleRef bundle = CFBundleGetMainBundle();
    if (!bundle)
        return;

    // 获取资源目录的 URL 路径
    CFURLRef resourcesURL = CFBundleCopyResourcesDirectoryURL(bundle);

    // 检查资源目录是否为 "Resources" 文件夹
    CFStringRef last = CFURLCopyLastPathComponent(resourcesURL);
    if (CFStringCompare(CFSTR("Resources"), last, 0) != kCFCompareEqualTo)
    {
        CFRelease(last);
        CFRelease(resourcesURL);
        return;
    }
    CFRelease(last);

    // 将资源目录的 URL 转换为文件系统路径
    if (!CFURLGetFileSystemRepresentation(resourcesURL,
                                          true,
                                          (UInt8*) resourcesPath,
                                          MAXPATHLEN))
    {
        CFRelease(resourcesURL);
        return;
    }

    CFRelease(resourcesURL);

    // 切换到资源目录
    chdir(resourcesPath);
}

// （手动）设置菜单栏
// + 注意，这是对官方未记录的半私有 API 的调用，所以这里应该（添加一个 nib）进行本地化处理，否则很多东西可能随时消失
static void createMenuBar(void)
{
    NSString* appName = nil;
    NSDictionary* bundleInfo = [[NSBundle mainBundle] infoDictionary];
    NSString* nameKeys[] =
    {
        @"CFBundleDisplayName",
        @"CFBundleName",
        @"CFBundleExecutable",
    };

    // 尝试查找调用应用程序的名称
    for (size_t i = 0;  i < sizeof(nameKeys) / sizeof(nameKeys[0]);  i++)
    {
        id name = bundleInfo[nameKeys[i]];
        if (name &&
            [name isKindOfClass:[NSString class]] &&
            ![name isEqualToString:@""])
        {
            appName = name;
            break;
        }
    }

    if (!appName)
    {
        char** progname = _NSGetProgname();
        if (progname && *progname)
            appName = @(*progname);
        else
            appName = @"WSI Application";
    }

    NSMenu* bar = [[NSMenu alloc] init];
    [NSApp setMainMenu:bar];

    NSMenuItem* appMenuItem =
        [bar addItemWithTitle:@"" action:NULL keyEquivalent:@""];
    NSMenu* appMenu = [[NSMenu alloc] init];
    [appMenuItem setSubmenu:appMenu];

    [appMenu addItemWithTitle:[NSString stringWithFormat:@"About %@", appName]
                       action:@selector(orderFrontStandardAboutPanel:)
                keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    NSMenu* servicesMenu = [[NSMenu alloc] init];
    [NSApp setServicesMenu:servicesMenu];
    [[appMenu addItemWithTitle:@"Services"
                       action:NULL
                keyEquivalent:@""] setSubmenu:servicesMenu];
    [servicesMenu release];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:[NSString stringWithFormat:@"Hide %@", appName]
                       action:@selector(hide:)
                keyEquivalent:@"h"];
    [[appMenu addItemWithTitle:@"Hide Others"
                       action:@selector(hideOtherApplications:)
                keyEquivalent:@"h"]
        setKeyEquivalentModifierMask:NSEventModifierFlagOption | NSEventModifierFlagCommand];
    [appMenu addItemWithTitle:@"Show All"
                       action:@selector(unhideAllApplications:)
                keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:[NSString stringWithFormat:@"Quit %@", appName]
                       action:@selector(terminate:)
                keyEquivalent:@"q"];

    NSMenuItem* windowMenuItem =
        [bar addItemWithTitle:@"" action:NULL keyEquivalent:@""];
    [bar release];
    NSMenu* windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
    [NSApp setWindowsMenu:windowMenu];
    [windowMenuItem setSubmenu:windowMenu];

    [windowMenu addItemWithTitle:@"Minimize"
                          action:@selector(performMiniaturize:)
                   keyEquivalent:@"m"];
    [windowMenu addItemWithTitle:@"Zoom"
                          action:@selector(performZoom:)
                   keyEquivalent:@""];
    [windowMenu addItem:[NSMenuItem separatorItem]];
    [windowMenu addItemWithTitle:@"Bring All to Front"
                          action:@selector(arrangeInFront:)
                   keyEquivalent:@""];

    // TODO: Make this appear at the bottom of the menu (for consistency)
    [windowMenu addItem:[NSMenuItem separatorItem]];
    [[windowMenu addItemWithTitle:@"Enter Full Screen"
                           action:@selector(toggleFullScreen:)
                    keyEquivalent:@"f"]
     setKeyEquivalentModifierMask:NSEventModifierFlagControl | NSEventModifierFlagCommand];

    // Prior to Snow Leopard, we need to use this oddly-named semi-private API
    // to get the application menu working properly.
    SEL setAppleMenuSelector = NSSelectorFromString(@"setAppleMenu:");
    [NSApp performSelector:setAppleMenuSelector withObject:appMenu];
}

// 获取当前键盘布局的 Unicode 数据
static bool updateUnicodeData(void)
{
    if (g_wsi.ns.inputSource)
    {
        CFRelease(g_wsi.ns.inputSource);
        g_wsi.ns.inputSource = NULL;
        g_wsi.ns.unicodeData = nil;
    }

    g_wsi.ns.inputSource = TISCopyCurrentKeyboardLayoutInputSource();
    if (!g_wsi.ns.inputSource)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Cocoa: Failed to retrieve keyboard layout input source");
        return false;
    }

    g_wsi.ns.unicodeData =
        TISGetInputSourceProperty(g_wsi.ns.inputSource,
                                  kTISPropertyUnicodeKeyLayoutData);
    if (!g_wsi.ns.unicodeData)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Cocoa: Failed to retrieve keyboard layout Unicode data");
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
// app loop
///////////////////////////////////////////////////////////////////////////////

static void cocoa_poll_events(void)
{
    @autoreleasepool {

    for (;;)
    {
        NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:[NSDate distantPast]
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];
        if (event == nil)
            break;

        [NSApp sendEvent:event];
    }

    } // autoreleasepool
}

static void cocoa_wait_events(void)
{
    @autoreleasepool {

    // I wanted to pass NO to dequeue:, and rely on PollEvents to
    // dequeue and send.  For reasons not at all clear to me, passing
    // NO to dequeue: causes this method never to return.
    NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                        untilDate:[NSDate distantFuture]
                                           inMode:NSDefaultRunLoopMode
                                          dequeue:YES];
    [NSApp sendEvent:event];

    cocoa_poll_events();

    } // autoreleasepool
}

static void cocoa_wait_events_timeout(double timeout)
{
    @autoreleasepool {

    NSDate* date = [NSDate dateWithTimeIntervalSinceNow:timeout];
    NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                        untilDate:date
                                           inMode:NSDefaultRunLoopMode
                                          dequeue:YES];
    if (event)
        [NSApp sendEvent:event];

    cocoa_poll_events();

    } // autoreleasepool
}

static void cocoa_post_empty_event(void)
{
    @autoreleasepool {

    NSEvent* event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                        location:NSMakePoint(0, 0)
                                   modifierFlags:0
                                       timestamp:0
                                    windowNumber:0
                                         context:nil
                                         subtype:0
                                           data1:0
                                           data2:0];
    [NSApp postEvent:event atStart:YES];

    } // autoreleasepool
}

///////////////////////////////////////////////////////////////////////////////
// MONITOR
///////////////////////////////////////////////////////////////////////////////

// Get the name of the specified display, or NULL
static char* getMonitorName(CGDirectDisplayID displayID, NSScreen* screen)
{
    // IOKit doesn't work on Apple Silicon anymore
    // Luckily, 10.15 introduced -[NSScreen localizedName].
    // Use it if available, and fall back to IOKit otherwise.
    if (screen)
    {
        if ([screen respondsToSelector:@selector(localizedName)])
        {
            NSString* name = [screen valueForKey:@"localizedName"];
            if (name)
                return wsi_strdup([name UTF8String]);
        }
    }

    io_iterator_t it;
    io_service_t service;
    CFDictionaryRef info;

    if (IOServiceGetMatchingServices(MACH_PORT_NULL,
                                     IOServiceMatching("IODisplayConnect"),
                                     &it) != 0)
    {
        // This may happen if a desktop Mac is running headless
        return wsi_strdup("Display");
    }

    while ((service = IOIteratorNext(it)) != 0)
    {
        info = IODisplayCreateInfoDictionary(service,
                                             kIODisplayOnlyPreferredName);

        CFNumberRef vendorIDRef =
            CFDictionaryGetValue(info, CFSTR(kDisplayVendorID));
        CFNumberRef productIDRef =
            CFDictionaryGetValue(info, CFSTR(kDisplayProductID));
        if (!vendorIDRef || !productIDRef)
        {
            CFRelease(info);
            continue;
        }

        unsigned int vendorID, productID;
        CFNumberGetValue(vendorIDRef, kCFNumberIntType, &vendorID);
        CFNumberGetValue(productIDRef, kCFNumberIntType, &productID);

        if (CGDisplayVendorNumber(displayID) == vendorID &&
            CGDisplayModelNumber(displayID) == productID)
        {
            // Info dictionary is used and freed below
            break;
        }

        CFRelease(info);
    }

    IOObjectRelease(it);

    if (!service)
        return wsi_strdup("Display");

    CFDictionaryRef names =
        CFDictionaryGetValue(info, CFSTR(kDisplayProductName));

    CFStringRef nameRef;

    if (!names || !CFDictionaryGetValueIfPresent(names, CFSTR("en_US"),
                                                 (const void**) &nameRef))
    {
        // This may happen if a desktop Mac is running headless
        CFRelease(info);
        return wsi_strdup("Display");
    }

    const CFIndex size =
        CFStringGetMaximumSizeForEncoding(CFStringGetLength(nameRef),
                                          kCFStringEncodingUTF8);
    char* name = wsi_calloc(size + 1, 1);
    CFStringGetCString(nameRef, name, size, kCFStringEncodingUTF8);

    CFRelease(info);
    return name;
}

// Check whether the display mode should be included in enumeration
static bool modeIsGood(CGDisplayModeRef mode)
{
    uint32_t flags = CGDisplayModeGetIOFlags(mode);

    if (!(flags & kDisplayModeValidFlag) || !(flags & kDisplayModeSafeFlag))
        return false;
    if (flags & kDisplayModeInterlacedFlag)
        return false;
    if (flags & kDisplayModeStretchedFlag)
        return false;

#if MAC_OS_X_VERSION_MAX_ALLOWED == 101100
    CFStringRef format = CGDisplayModeCopyPixelEncoding(mode);
    if (CFStringCompare(format, CFSTR(IO16BitDirectPixels), 0) &&
        CFStringCompare(format, CFSTR(IO32BitDirectPixels), 0))
    {
        CFRelease(format);
        return false;
    }

    CFRelease(format);
#endif /* MAC_OS_X_VERSION_MAX_ALLOWED */
    return true;
}

// Convert Core Graphics display mode to WSI video mode
static sc_wsi_video_mode vidmodeFromCGDisplayMode(CGDisplayModeRef mode,
                                            double fallbackRefreshRate)
{
    sc_wsi_video_mode result;
    result.width = (int) CGDisplayModeGetWidth(mode);
    result.height = (int) CGDisplayModeGetHeight(mode);
    result.refreshRate = (int) round(CGDisplayModeGetRefreshRate(mode));

    if (result.refreshRate == 0)
        result.refreshRate = (int) round(fallbackRefreshRate);

#if MAC_OS_X_VERSION_MAX_ALLOWED == 101100
    CFRelease(format);
#endif /* MAC_OS_X_VERSION_MAX_ALLOWED */
    return result;
}

// Starts reservation for display fading
static CGDisplayFadeReservationToken beginFadeReservation(void)
{
    CGDisplayFadeReservationToken token = kCGDisplayFadeReservationInvalidToken;

    if (CGAcquireDisplayFadeReservation(5, &token) == kCGErrorSuccess)
    {
        CGDisplayFade(token, 0.3,
                      kCGDisplayBlendNormal,
                      kCGDisplayBlendSolidColor,
                      0.0, 0.0, 0.0,
                      TRUE);
    }

    return token;
}

// Ends reservation for display fading
//
static void endFadeReservation(CGDisplayFadeReservationToken token)
{
    if (token != kCGDisplayFadeReservationInvalidToken)
    {
        CGDisplayFade(token, 0.5,
                      kCGDisplayBlendSolidColor,
                      kCGDisplayBlendNormal,
                      0.0, 0.0, 0.0,
                      FALSE);
        CGReleaseDisplayFadeReservation(token);
    }
}

// Returns the display refresh rate queried from the I/O registry
//
static double getFallbackRefreshRate(CGDirectDisplayID displayID)
{
    double refreshRate = 60.0;

    io_iterator_t it;
    io_service_t service;

    if (IOServiceGetMatchingServices(MACH_PORT_NULL,
                                     IOServiceMatching("IOFramebuffer"),
                                     &it) != 0)
    {
        return refreshRate;
    }

    while ((service = IOIteratorNext(it)) != 0)
    {
        const CFNumberRef indexRef =
            IORegistryEntryCreateCFProperty(service,
                                            CFSTR("IOFramebufferOpenGLIndex"),
                                            kCFAllocatorDefault,
                                            kNilOptions);
        if (!indexRef)
            continue;

        uint32_t index = 0;
        CFNumberGetValue(indexRef, kCFNumberIntType, &index);
        CFRelease(indexRef);

        if (CGOpenGLDisplayMaskToDisplayID(1 << index) != displayID)
            continue;

        const CFNumberRef clockRef =
            IORegistryEntryCreateCFProperty(service,
                                            CFSTR("IOFBCurrentPixelClock"),
                                            kCFAllocatorDefault,
                                            kNilOptions);
        const CFNumberRef countRef =
            IORegistryEntryCreateCFProperty(service,
                                            CFSTR("IOFBCurrentPixelCount"),
                                            kCFAllocatorDefault,
                                            kNilOptions);

        uint32_t clock = 0, count = 0;

        if (clockRef)
        {
            CFNumberGetValue(clockRef, kCFNumberIntType, &clock);
            CFRelease(clockRef);
        }

        if (countRef)
        {
            CFNumberGetValue(countRef, kCFNumberIntType, &count);
            CFRelease(countRef);
        }

        if (clock > 0 && count > 0)
            refreshRate = clock / (double) count;

        break;
    }

    IOObjectRelease(it);
    return refreshRate;
}

// Transforms a y-coordinate between the CG display and NS screen spaces
//
static inline float cocoa_TransformY(float y)
{
    return CGDisplayBounds(CGMainDisplayID()).size.height - y - 1;
}

//-----------------------------------------------------------------------------

// Poll for changes in the set of connected monitors
//
static void cocoa_poll_monitors(void)
{
    uint32_t displayCount;
    CGGetOnlineDisplayList(0, NULL, &displayCount);
    CGDirectDisplayID* displays = wsi_calloc(displayCount, sizeof(CGDirectDisplayID));
    CGGetOnlineDisplayList(displayCount, displays, &displayCount);

    for (int i = 0;  i < g_wsi.monitorCount;  i++)
        g_wsi.monitors[i]->ns.screen = nil;

    monitor_st** disconnected = NULL;
    uint32_t disconnectedCount = g_wsi.monitorCount;
    if (disconnectedCount)
    {
        disconnected = wsi_calloc(g_wsi.monitorCount, sizeof(monitor_st*));
        memcpy(disconnected,
               g_wsi.monitors,
               g_wsi.monitorCount * sizeof(monitor_st*));
    }

    for (uint32_t i = 0;  i < displayCount;  i++)
    {
        if (CGDisplayIsAsleep(displays[i]))
            continue;

        const uint32_t unitNumber = CGDisplayUnitNumber(displays[i]);
        NSScreen* screen = nil;

        for (screen in [NSScreen screens])
        {
            NSNumber* screenNumber = [screen deviceDescription][@"NSScreenNumber"];

            // HACK: Compare unit numbers instead of display IDs to work around
            //       display replacement on machines with automatic graphics
            //       switching
            if (CGDisplayUnitNumber([screenNumber unsignedIntValue]) == unitNumber)
                break;
        }

        // HACK: Compare unit numbers instead of display IDs to work around
        //       display replacement on machines with automatic graphics
        //       switching
        uint32_t j;
        for (j = 0;  j < disconnectedCount;  j++)
        {
            if (disconnected[j] && disconnected[j]->ns.unitNumber == unitNumber)
            {
                disconnected[j]->ns.screen = screen;
                disconnected[j] = NULL;
                break;
            }
        }

        if (j < disconnectedCount)
            continue;

        const CGSize size = CGDisplayScreenSize(displays[i]);
        char* name = getMonitorName(displays[i], screen);
        if (!name)
            continue;

        monitor_st* monitor = wsi_alloc_monitor(name, size.width, size.height);
        monitor->ns.displayID  = displays[i];
        monitor->ns.unitNumber = unitNumber;
        monitor->ns.screen     = screen;

        wsi_free(name);

        CGDisplayModeRef mode = CGDisplayCopyDisplayMode(displays[i]);
        if (CGDisplayModeGetRefreshRate(mode) == 0.0)
            monitor->ns.fallbackRefreshRate = getFallbackRefreshRate(displays[i]);
        CGDisplayModeRelease(mode);

        impl_on_monitor(monitor, SC_CONNECTED, WSI_INSERT_LAST);
    }

    for (uint32_t i = 0;  i < disconnectedCount;  i++)
    {
        if (disconnected[i])
            impl_on_monitor(disconnected[i], SC_DISCONNECTED, 0);
    }

    wsi_free(disconnected);
    wsi_free(displays);
}

// 恢复之前保存的（原始）视频模式
static void cocoa_restore_video_mode(monitor_st* monitor)
{
    if (monitor->ns.previousMode)
    {
        CGDisplayFadeReservationToken token = beginFadeReservation();
        CGDisplaySetDisplayMode(monitor->ns.displayID,
                                monitor->ns.previousMode, NULL);
        endFadeReservation(token);

        CGDisplayModeRelease(monitor->ns.previousMode);
        monitor->ns.previousMode = NULL;
    }
}

//-----------------------------------------------------------------------------

static void cocoa_free_monitor(monitor_st* monitor)
{
}

static void cocoa_get_monitor_pos(monitor_st* monitor, int* xpos, int* ypos)
{
    @autoreleasepool {

    const CGRect bounds = CGDisplayBounds(monitor->ns.displayID);

    if (xpos)
        *xpos = (int) bounds.origin.x;
    if (ypos)
        *ypos = (int) bounds.origin.y;

    } // autoreleasepool
}

static void cocoa_get_monitor_content_scale(monitor_st* monitor, float* xscale, float* yscale)
{
    @autoreleasepool {

    if (!monitor->ns.screen)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Cocoa: Cannot query content scale without screen");
    }

    const NSRect points = [monitor->ns.screen frame];
    const NSRect pixels = [monitor->ns.screen convertRectToBacking:points];

    if (xscale)
        *xscale = (float) (pixels.size.width / points.size.width);
    if (yscale)
        *yscale = (float) (pixels.size.height / points.size.height);

    } // autoreleasepool
}

static void cocoa_get_monitor_work_area(monitor_st* monitor,
                                  int* xpos, int* ypos,
                                  int* width, int* height)
{
    @autoreleasepool {

    if (!monitor->ns.screen)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Cocoa: Cannot query workarea without screen");
    }

    const NSRect frameRect = [monitor->ns.screen visibleFrame];

    if (xpos)
        *xpos = frameRect.origin.x;
    if (ypos)
        *ypos = cocoa_TransformY(frameRect.origin.y + frameRect.size.height - 1);
    if (width)
        *width = frameRect.size.width;
    if (height)
        *height = frameRect.size.height;

    } // autoreleasepool
}

static sc_wsi_video_mode* cocoa_get_video_modes(monitor_st* monitor, int* count)
{
    @autoreleasepool {

    *count = 0;

    CFArrayRef modes = CGDisplayCopyAllDisplayModes(monitor->ns.displayID, NULL);
    const CFIndex found = CFArrayGetCount(modes);
    sc_wsi_video_mode* result = wsi_calloc(found, sizeof(sc_wsi_video_mode));

    for (CFIndex i = 0;  i < found;  i++)
    {
        CGDisplayModeRef dm = (CGDisplayModeRef) CFArrayGetValueAtIndex(modes, i);
        if (!modeIsGood(dm))
            continue;

        const sc_wsi_video_mode mode =
            vidmodeFromCGDisplayMode(dm, monitor->ns.fallbackRefreshRate);
        CFIndex j;

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

    CFRelease(modes);
    return result;

    } // autoreleasepool
}

static bool cocoa_get_video_mode(monitor_st* monitor, sc_wsi_video_mode *mode)
{
    @autoreleasepool {

    CGDisplayModeRef native = CGDisplayCopyDisplayMode(monitor->ns.displayID);
    if (!native)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR, "Cocoa: Failed to query display mode");
        return false;
    }

    *mode = vidmodeFromCGDisplayMode(native, monitor->ns.fallbackRefreshRate);
    CGDisplayModeRelease(native);
    return true;

    } // autoreleasepool
}

// Change the current video mode
static void cocoa_set_video_mode(monitor_st* monitor, const sc_wsi_video_mode* desired)
{
    sc_wsi_video_mode current;
    cocoa_get_video_mode(monitor, &current);

    const sc_wsi_video_mode* best = wsi_choose_video_mode(monitor, desired);
    if (wsi_compare_video_mode(&current, best) == 0)
        return;

    CFArrayRef modes = CGDisplayCopyAllDisplayModes(monitor->ns.displayID, NULL);
    const CFIndex count = CFArrayGetCount(modes);
    CGDisplayModeRef native = NULL;

    for (CFIndex i = 0;  i < count;  i++)
    {
        CGDisplayModeRef dm = (CGDisplayModeRef) CFArrayGetValueAtIndex(modes, i);
        if (!modeIsGood(dm))
            continue;

        const sc_wsi_video_mode mode =
            vidmodeFromCGDisplayMode(dm, monitor->ns.fallbackRefreshRate);
        if (wsi_compare_video_mode(best, &mode) == 0)
        {
            native = dm;
            break;
        }
    }

    if (native)
    {
        if (monitor->ns.previousMode == NULL)
            monitor->ns.previousMode = CGDisplayCopyDisplayMode(monitor->ns.displayID);

        CGDisplayFadeReservationToken token = beginFadeReservation();
        CGDisplaySetDisplayMode(monitor->ns.displayID, native, NULL);
        endFadeReservation(token);
    }

    CFRelease(modes);
}

static bool cocoa_get_gamma_ramp(monitor_st* monitor, sc_wsi_gamma_ramp* ramp)
{
    @autoreleasepool {

    uint32_t size = CGDisplayGammaTableCapacity(monitor->ns.displayID);
    CGGammaValue* values = wsi_calloc(size * 3, sizeof(CGGammaValue));

    CGGetDisplayTransferByTable(monitor->ns.displayID,
                                size,
                                values,
                                values + size,
                                values + size * 2,
                                &size);

    wsi_alloc_gamma_arrays(ramp, size);

    for (uint32_t i = 0; i < size; i++)
    {
        ramp->red[i]   = (unsigned short) (values[i] * 65535);
        ramp->green[i] = (unsigned short) (values[i + size] * 65535);
        ramp->blue[i]  = (unsigned short) (values[i + size * 2] * 65535);
    }

    wsi_free(values);
    return true;

    } // autoreleasepool
}

static void cocoa_set_gamma_ramp(monitor_st* monitor, const sc_wsi_gamma_ramp* ramp)
{
    @autoreleasepool {

    CGGammaValue* values = wsi_calloc(ramp->size * 3, sizeof(CGGammaValue));

    for (unsigned int i = 0;  i < ramp->size;  i++)
    {
        values[i]                  = ramp->red[i] / 65535.f;
        values[i + ramp->size]     = ramp->green[i] / 65535.f;
        values[i + ramp->size * 2] = ramp->blue[i] / 65535.f;
    }

    CGSetDisplayTransferByTable(monitor->ns.displayID,
                                ramp->size,
                                values,
                                values + ramp->size,
                                values + ramp->size * 2);

    wsi_free(values);

    } // autoreleasepool
}

//-----------------------------------------------------------------------------

// Make the specified window and its video mode active on its monitor
static void acquireMonitor(window_st* window)
{
    cocoa_set_video_mode(window->monitor, &window->videoMode);
    const CGRect bounds = CGDisplayBounds(window->monitor->ns.displayID);
    const NSRect frame = NSMakeRect(bounds.origin.x,
                                    cocoa_TransformY(bounds.origin.y + bounds.size.height - 1),
                                    bounds.size.width,
                                    bounds.size.height);

    [window->ns.object setFrame:frame display:YES];
    impl_on_monitor_window(window->monitor, window);
}

// Remove the window and restore the original video mode
static void releaseMonitor(window_st* window)
{
    if (window->monitor->window != window)
        return;

    impl_on_monitor_window(window->monitor, NULL);
    cocoa_restore_video_mode(window->monitor);
}

static void cocoa_set_window_monitor(window_st* window,
                                monitor_st* monitor,
                                int xpos, int ypos,
                                int width, int height,
                                int refreshRate)
{
    @autoreleasepool {

    if (window->monitor == monitor)
    {
        if (monitor)
        {
            if (monitor->window == window)
                acquireMonitor(window);
        }
        else
        {
            const NSRect contentRect =
                NSMakeRect(xpos, cocoa_TransformY(ypos + height - 1), width, height);
            const NSUInteger styleMask = [window->ns.object styleMask];
            const NSRect frameRect =
                [window->ns.object frameRectForContentRect:contentRect
                                                 styleMask:styleMask];

            [window->ns.object setFrame:frameRect display:YES];
        }

        return;
    }

    if (window->monitor)
        releaseMonitor(window);

    impl_on_win_monitor(window, monitor);

    // HACK: Allow the state cached in Cocoa to catch up to reality
    // TODO: Solve this in a less terrible way
    cocoa_poll_events();

    NSUInteger styleMask = [window->ns.object styleMask];

    if (window->monitor)
    {
        styleMask &= ~(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable);
        styleMask |= NSWindowStyleMaskBorderless;
    }
    else
    {
        if (window->decorated)
        {
            styleMask &= ~NSWindowStyleMaskBorderless;
            styleMask |= (NSWindowStyleMaskTitled | NSWindowStyleMaskClosable);
        }

        if (window->resizable)
            styleMask |= NSWindowStyleMaskResizable;
        else
            styleMask &= ~NSWindowStyleMaskResizable;
    }

    [window->ns.object setStyleMask:styleMask];
    // HACK: Changing the style mask can cause the first responder to be cleared
    [window->ns.object makeFirstResponder:window->ns.view];

    if (window->monitor)
    {
        [window->ns.object setLevel:NSMainMenuWindowLevel + 1];
        [window->ns.object setHasShadow:NO];

        acquireMonitor(window);
    }
    else
    {
        NSRect contentRect = NSMakeRect(xpos, cocoa_TransformY(ypos + height - 1),
                                        width, height);
        NSRect frameRect = [window->ns.object frameRectForContentRect:contentRect
                                                            styleMask:styleMask];
        [window->ns.object setFrame:frameRect display:YES];

        if (window->numer != SC_DONT_CARE &&
            window->denom != SC_DONT_CARE)
        {
            [window->ns.object setContentAspectRatio:NSMakeSize(window->numer,
                                                                window->denom)];
        }

        if (window->minwidth != SC_DONT_CARE &&
            window->minheight != SC_DONT_CARE)
        {
            [window->ns.object setContentMinSize:NSMakeSize(window->minwidth,
                                                            window->minheight)];
        }

        if (window->maxwidth != SC_DONT_CARE &&
            window->maxheight != SC_DONT_CARE)
        {
            [window->ns.object setContentMaxSize:NSMakeSize(window->maxwidth,
                                                            window->maxheight)];
        }

        if (window->floating)
            [window->ns.object setLevel:NSFloatingWindowLevel];
        else
            [window->ns.object setLevel:NSNormalWindowLevel];

        if (window->resizable)
        {
            const NSWindowCollectionBehavior behavior =
                NSWindowCollectionBehaviorFullScreenPrimary |
                NSWindowCollectionBehaviorManaged;
            [window->ns.object setCollectionBehavior:behavior];
        }
        else
        {
            const NSWindowCollectionBehavior behavior =
                NSWindowCollectionBehaviorFullScreenNone;
            [window->ns.object setCollectionBehavior:behavior];
        }

        [window->ns.object setHasShadow:YES];
        // HACK: Clearing NSWindowStyleMaskTitled resets and disables the window
        //       title property but the miniwindow title property is unaffected
        [window->ns.object setTitle:[window->ns.object miniwindowTitle]];
    }

    } // autoreleasepool
}

///////////////////////////////////////////////////////////////////////////////
// APP
///////////////////////////////////////////////////////////////////////////////

@interface SC_Helper : NSObject
@end

@implementation SC_Helper

- (void)selectedKeyboardInputSourceChanged:(NSObject* )object
{
    updateUnicodeData();
}

- (void)doNothing:(id)object
{
}

@end // SC_Helper

@interface SC_ApplicationDelegate : NSObject <NSApplicationDelegate>
@end

@implementation SC_ApplicationDelegate

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    for (window_st* window = g_wsi.windowListHead;  window;  window = window->next)
        impl_on_win_close_req(window);
    return NSTerminateCancel;
}

- (void)applicationWillFinishLaunching:(NSNotification *)notification
{
    if (g_wsi.hints.init.ns.menubar)
    {
        // 菜单栏设置必须在 sharedApplication 和 finishLaunching 之间进行，以便正确模拟 NSApplicationMain 的行为
        if ([[NSBundle mainBundle] pathForResource:@"MainMenu" ofType:@"nib"])
        {
            [[NSBundle mainBundle] loadNibNamed:@"MainMenu"
                                          owner:NSApp
                                topLevelObjects:&g_wsi.ns.nibObjects];
        }
        else
            createMenuBar();
    }
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    cocoa_post_empty_event();
    [NSApp stop:nil];
}

- (void)applicationDidHide:(NSNotification *)notification
{
    for (int i = 0;  i < g_wsi.monitorCount;  i++)
        cocoa_restore_video_mode(g_wsi.monitors[i]);
}

- (void)applicationDidChangeScreenParameters:(NSNotification *) notification
{
    cocoa_poll_monitors();
}

@end // SC_ApplicationDelegate

///////////////////////////////////////////////////////////////////////////////
// lib
///////////////////////////////////////////////////////////////////////////////

// 初始化加载 HIToolbox.framework 和所需的 TIS 符号
static bool initializeTIS(void)
{
    // This works only because Cocoa has already loaded it properly
    g_wsi.ns.tis.bundle =
        CFBundleGetBundleWithIdentifier(CFSTR("com.apple.HIToolbox"));
    if (!g_wsi.ns.tis.bundle)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Cocoa: Failed to load HIToolbox.framework");
        return false;
    }

    CFStringRef* kPropertyUnicodeKeyLayoutData =
        CFBundleGetDataPointerForName(g_wsi.ns.tis.bundle,
                                      CFSTR("kTISPropertyUnicodeKeyLayoutData"));
    g_wsi.ns.tis.CopyCurrentKeyboardLayoutInputSource =
        CFBundleGetFunctionPointerForName(g_wsi.ns.tis.bundle,
                                          CFSTR("TISCopyCurrentKeyboardLayoutInputSource"));
    g_wsi.ns.tis.GetInputSourceProperty =
        CFBundleGetFunctionPointerForName(g_wsi.ns.tis.bundle,
                                          CFSTR("TISGetInputSourceProperty"));
    g_wsi.ns.tis.GetKbdType =
        CFBundleGetFunctionPointerForName(g_wsi.ns.tis.bundle,
                                          CFSTR("LMGetKbdType"));

    if (!kPropertyUnicodeKeyLayoutData ||
        !TISCopyCurrentKeyboardLayoutInputSource ||
        !TISGetInputSourceProperty ||
        !LMGetKbdType)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Cocoa: Failed to load TIS API symbols");
        return false;
    }

    g_wsi.ns.tis.kPropertyUnicodeKeyLayoutData =
        *kPropertyUnicodeKeyLayoutData;

    return updateUnicodeData();
}

// 创建键盘键码翻译表
static void createKeyTables(void)
{
    memset(g_wsi.ns.keycodes, -1, sizeof(g_wsi.ns.keycodes));
    memset(g_wsi.ns.scancodes, -1, sizeof(g_wsi.ns.scancodes));

    g_wsi.ns.keycodes[0x1D] = SC_KEY_0;
    g_wsi.ns.keycodes[0x12] = SC_KEY_1;
    g_wsi.ns.keycodes[0x13] = SC_KEY_2;
    g_wsi.ns.keycodes[0x14] = SC_KEY_3;
    g_wsi.ns.keycodes[0x15] = SC_KEY_4;
    g_wsi.ns.keycodes[0x17] = SC_KEY_5;
    g_wsi.ns.keycodes[0x16] = SC_KEY_6;
    g_wsi.ns.keycodes[0x1A] = SC_KEY_7;
    g_wsi.ns.keycodes[0x1C] = SC_KEY_8;
    g_wsi.ns.keycodes[0x19] = SC_KEY_9;
    g_wsi.ns.keycodes[0x00] = SC_KEY_A;
    g_wsi.ns.keycodes[0x0B] = SC_KEY_B;
    g_wsi.ns.keycodes[0x08] = SC_KEY_C;
    g_wsi.ns.keycodes[0x02] = SC_KEY_D;
    g_wsi.ns.keycodes[0x0E] = SC_KEY_E;
    g_wsi.ns.keycodes[0x03] = SC_KEY_F;
    g_wsi.ns.keycodes[0x05] = SC_KEY_G;
    g_wsi.ns.keycodes[0x04] = SC_KEY_H;
    g_wsi.ns.keycodes[0x22] = SC_KEY_I;
    g_wsi.ns.keycodes[0x26] = SC_KEY_J;
    g_wsi.ns.keycodes[0x28] = SC_KEY_K;
    g_wsi.ns.keycodes[0x25] = SC_KEY_L;
    g_wsi.ns.keycodes[0x2E] = SC_KEY_M;
    g_wsi.ns.keycodes[0x2D] = SC_KEY_N;
    g_wsi.ns.keycodes[0x1F] = SC_KEY_O;
    g_wsi.ns.keycodes[0x23] = SC_KEY_P;
    g_wsi.ns.keycodes[0x0C] = SC_KEY_Q;
    g_wsi.ns.keycodes[0x0F] = SC_KEY_R;
    g_wsi.ns.keycodes[0x01] = SC_KEY_S;
    g_wsi.ns.keycodes[0x11] = SC_KEY_T;
    g_wsi.ns.keycodes[0x20] = SC_KEY_U;
    g_wsi.ns.keycodes[0x09] = SC_KEY_V;
    g_wsi.ns.keycodes[0x0D] = SC_KEY_W;
    g_wsi.ns.keycodes[0x07] = SC_KEY_X;
    g_wsi.ns.keycodes[0x10] = SC_KEY_Y;
    g_wsi.ns.keycodes[0x06] = SC_KEY_Z;

    g_wsi.ns.keycodes[0x27] = SC_KEY_APOSTROPHE;
    g_wsi.ns.keycodes[0x2A] = SC_KEY_BACKSLASH;
    g_wsi.ns.keycodes[0x2B] = SC_KEY_COMMA;
    g_wsi.ns.keycodes[0x18] = SC_KEY_EQUAL;
    g_wsi.ns.keycodes[0x32] = SC_KEY_GRAVE_ACCENT;
    g_wsi.ns.keycodes[0x21] = SC_KEY_LEFT_BRACKET;
    g_wsi.ns.keycodes[0x1B] = SC_KEY_MINUS;
    g_wsi.ns.keycodes[0x2F] = SC_KEY_PERIOD;
    g_wsi.ns.keycodes[0x1E] = SC_KEY_RIGHT_BRACKET;
    g_wsi.ns.keycodes[0x29] = SC_KEY_SEMICOLON;
    g_wsi.ns.keycodes[0x2C] = SC_KEY_SLASH;
    g_wsi.ns.keycodes[0x0A] = SC_KEY_WORLD_1;

    g_wsi.ns.keycodes[0x33] = SC_KEY_BACKSPACE;
    g_wsi.ns.keycodes[0x39] = SC_KEY_CAPS_LOCK;
    g_wsi.ns.keycodes[0x75] = SC_KEY_DELETE;
    g_wsi.ns.keycodes[0x7D] = SC_KEY_DOWN;
    g_wsi.ns.keycodes[0x77] = SC_KEY_END;
    g_wsi.ns.keycodes[0x24] = SC_KEY_ENTER;
    g_wsi.ns.keycodes[0x35] = SC_KEY_ESCAPE;
    g_wsi.ns.keycodes[0x7A] = SC_KEY_F1;
    g_wsi.ns.keycodes[0x78] = SC_KEY_F2;
    g_wsi.ns.keycodes[0x63] = SC_KEY_F3;
    g_wsi.ns.keycodes[0x76] = SC_KEY_F4;
    g_wsi.ns.keycodes[0x60] = SC_KEY_F5;
    g_wsi.ns.keycodes[0x61] = SC_KEY_F6;
    g_wsi.ns.keycodes[0x62] = SC_KEY_F7;
    g_wsi.ns.keycodes[0x64] = SC_KEY_F8;
    g_wsi.ns.keycodes[0x65] = SC_KEY_F9;
    g_wsi.ns.keycodes[0x6D] = SC_KEY_F10;
    g_wsi.ns.keycodes[0x67] = SC_KEY_F11;
    g_wsi.ns.keycodes[0x6F] = SC_KEY_F12;
    g_wsi.ns.keycodes[0x69] = SC_KEY_PRINT_SCREEN;
    g_wsi.ns.keycodes[0x6B] = SC_KEY_F14;
    g_wsi.ns.keycodes[0x71] = SC_KEY_F15;
    g_wsi.ns.keycodes[0x6A] = SC_KEY_F16;
    g_wsi.ns.keycodes[0x40] = SC_KEY_F17;
    g_wsi.ns.keycodes[0x4F] = SC_KEY_F18;
    g_wsi.ns.keycodes[0x50] = SC_KEY_F19;
    g_wsi.ns.keycodes[0x5A] = SC_KEY_F20;
    g_wsi.ns.keycodes[0x73] = SC_KEY_HOME;
    g_wsi.ns.keycodes[0x72] = SC_KEY_INSERT;
    g_wsi.ns.keycodes[0x7B] = SC_KEY_LEFT;
    g_wsi.ns.keycodes[0x3A] = SC_KEY_LEFT_ALT;
    g_wsi.ns.keycodes[0x3B] = SC_KEY_LEFT_CONTROL;
    g_wsi.ns.keycodes[0x38] = SC_KEY_LEFT_SHIFT;
    g_wsi.ns.keycodes[0x37] = SC_KEY_LEFT_SUPER;
    g_wsi.ns.keycodes[0x6E] = SC_KEY_MENU;
    g_wsi.ns.keycodes[0x47] = SC_KEY_NUM_LOCK;
    g_wsi.ns.keycodes[0x79] = SC_KEY_PAGE_DOWN;
    g_wsi.ns.keycodes[0x74] = SC_KEY_PAGE_UP;
    g_wsi.ns.keycodes[0x7C] = SC_KEY_RIGHT;
    g_wsi.ns.keycodes[0x3D] = SC_KEY_RIGHT_ALT;
    g_wsi.ns.keycodes[0x3E] = SC_KEY_RIGHT_CONTROL;
    g_wsi.ns.keycodes[0x3C] = SC_KEY_RIGHT_SHIFT;
    g_wsi.ns.keycodes[0x36] = SC_KEY_RIGHT_SUPER;
    g_wsi.ns.keycodes[0x31] = SC_KEY_SPACE;
    g_wsi.ns.keycodes[0x30] = SC_KEY_TAB;
    g_wsi.ns.keycodes[0x7E] = SC_KEY_UP;

    g_wsi.ns.keycodes[0x52] = SC_KEY_KP_0;
    g_wsi.ns.keycodes[0x53] = SC_KEY_KP_1;
    g_wsi.ns.keycodes[0x54] = SC_KEY_KP_2;
    g_wsi.ns.keycodes[0x55] = SC_KEY_KP_3;
    g_wsi.ns.keycodes[0x56] = SC_KEY_KP_4;
    g_wsi.ns.keycodes[0x57] = SC_KEY_KP_5;
    g_wsi.ns.keycodes[0x58] = SC_KEY_KP_6;
    g_wsi.ns.keycodes[0x59] = SC_KEY_KP_7;
    g_wsi.ns.keycodes[0x5B] = SC_KEY_KP_8;
    g_wsi.ns.keycodes[0x5C] = SC_KEY_KP_9;
    g_wsi.ns.keycodes[0x45] = SC_KEY_KP_ADD;
    g_wsi.ns.keycodes[0x41] = SC_KEY_KP_DECIMAL;
    g_wsi.ns.keycodes[0x4B] = SC_KEY_KP_DIVIDE;
    g_wsi.ns.keycodes[0x4C] = SC_KEY_KP_ENTER;
    g_wsi.ns.keycodes[0x51] = SC_KEY_KP_EQUAL;
    g_wsi.ns.keycodes[0x43] = SC_KEY_KP_MULTIPLY;
    g_wsi.ns.keycodes[0x4E] = SC_KEY_KP_SUBTRACT;

    for (int scancode = 0;  scancode < 256;  scancode++)
    {
        // 恢复键码到扫描码的映射
        if (g_wsi.ns.keycodes[scancode] >= 0)
            g_wsi.ns.scancodes[g_wsi.ns.keycodes[scancode]] = scancode;
    }
}

static int cocoa_init(void)
{
    @autoreleasepool {

    g_wsi.ns.helper = [[SC_Helper alloc] init];

    [NSThread detachNewThreadSelector:@selector(doNothing:)
                             toTarget:g_wsi.ns.helper
                           withObject:nil];

    [NSApplication sharedApplication];

    g_wsi.ns.delegate = [[SC_ApplicationDelegate alloc] init];
    if (g_wsi.ns.delegate == nil)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR, "Cocoa: Failed to create application delegate");
        return false;
    }

    [NSApp setDelegate:g_wsi.ns.delegate];

    NSEvent* (^block)(NSEvent*) = ^ NSEvent* (NSEvent* event)
    {
        if ([event modifierFlags] & NSEventModifierFlagCommand)
            [[NSApp keyWindow] sendEvent:event];

        return event;
    };

    g_wsi.ns.keyUpMonitor =
        [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyUp
                                              handler:block];

    // 将当前工作目录更改为主捆绑包的资源目录
    if (g_wsi.hints.init.ns.chdir)
        changeToResourcesDirectory();

    // Press and Hold prevents some keys from emitting repeated characters
    NSDictionary* defaults = @{@"ApplePressAndHoldEnabled":@NO};
    [[NSUserDefaults standardUserDefaults] registerDefaults:defaults];

    [[NSNotificationCenter defaultCenter]
        addObserver:g_wsi.ns.helper
           selector:@selector(selectedKeyboardInputSourceChanged:)
               name:NSTextInputContextKeyboardSelectionDidChangeNotification
             object:nil];

    createKeyTables();

    g_wsi.ns.eventSource = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    if (!g_wsi.ns.eventSource)
        return false;

    CGEventSourceSetLocalEventsSuppressionInterval(g_wsi.ns.eventSource, 0.0);

    if (!initializeTIS())
        return false;

    cocoa_poll_monitors();

    if (![[NSRunningApplication currentApplication] isFinishedLaunching])
        [NSApp run];

    // In case we are unbundled, make us a proper UI application
    if (g_wsi.hints.init.ns.menubar)
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    return true;

    } // autoreleasepool
}

static void cocoa_terminate(void)
{
    @autoreleasepool {

    if (g_wsi.ns.inputSource)
    {
        CFRelease(g_wsi.ns.inputSource);
        g_wsi.ns.inputSource = NULL;
        g_wsi.ns.unicodeData = nil;
    }

    if (g_wsi.ns.eventSource)
    {
        CFRelease(g_wsi.ns.eventSource);
        g_wsi.ns.eventSource = NULL;
    }

    if (g_wsi.ns.delegate)
    {
        [NSApp setDelegate:nil];
        [g_wsi.ns.delegate release];
        g_wsi.ns.delegate = nil;
    }

    if (g_wsi.ns.helper)
    {
        [[NSNotificationCenter defaultCenter]
            removeObserver:g_wsi.ns.helper
                      name:NSTextInputContextKeyboardSelectionDidChangeNotification
                    object:nil];
        [[NSNotificationCenter defaultCenter]
            removeObserver:g_wsi.ns.helper];
        [g_wsi.ns.helper release];
        g_wsi.ns.helper = nil;
    }

    if (g_wsi.ns.keyUpMonitor)
        [NSEvent removeMonitor:g_wsi.ns.keyUpMonitor];

    wsi_free(g_wsi.ns.clipboardString);

    memset(&g_wsi.ns, 0, sizeof(g_wsi.ns));

    } // autoreleasepool
}

///////////////////////////////////////////////////////////////////////////////
// common API
///////////////////////////////////////////////////////////////////////////////

static void updateCursorImage(window_st* window);
static void updateCursorMode(window_st* window);
static bool cursorInContentArea(window_st* window);

static void cocoa_set_window_title(window_st* window, const char* title)
{
    @autoreleasepool {
    NSString* string = @(title);
    [window->ns.object setTitle:string];
    // HACK: Set the miniwindow title explicitly as setTitle: doesn't update it
    //       if the window lacks NSWindowStyleMaskTitled
    [window->ns.object setMiniwindowTitle:string];
    } // autoreleasepool
}

static void cocoa_set_window_icon(window_st* window, int count, const sc_wsi_img* images)
{
    impl_on_error(SC_WSI_ERR_FEATURE_UNAVAILABLE,
                    "Cocoa: Regular windows do not have icons on macOS");
}

static void cocoa_set_window_mouse_passthrough(window_st* window, bool enabled)
{
    @autoreleasepool {
    [window->ns.object setIgnoresMouseEvents:enabled];
    }
}



static void cocoa_set_window_decorated(window_st* window, bool enabled)
{
    @autoreleasepool {

    NSUInteger styleMask = [window->ns.object styleMask];
    if (enabled)
    {
        styleMask |= (NSWindowStyleMaskTitled | NSWindowStyleMaskClosable);
        styleMask &= ~NSWindowStyleMaskBorderless;
    }
    else
    {
        styleMask |= NSWindowStyleMaskBorderless;
        styleMask &= ~(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable);
    }

    [window->ns.object setStyleMask:styleMask];
    [window->ns.object makeFirstResponder:window->ns.view];

    } // autoreleasepool
}

static void cocoa_set_window_resizable(window_st* window, bool enabled)
{
    @autoreleasepool {

    const NSUInteger styleMask = [window->ns.object styleMask];
    if (enabled)
    {
        [window->ns.object setStyleMask:(styleMask | NSWindowStyleMaskResizable)];
        const NSWindowCollectionBehavior behavior =
            NSWindowCollectionBehaviorFullScreenPrimary |
            NSWindowCollectionBehaviorManaged;
        [window->ns.object setCollectionBehavior:behavior];
    }
    else
    {
        [window->ns.object setStyleMask:(styleMask & ~NSWindowStyleMaskResizable)];
        const NSWindowCollectionBehavior behavior =
            NSWindowCollectionBehaviorFullScreenNone;
        [window->ns.object setCollectionBehavior:behavior];
    }

    } // autoreleasepool
}

static void cocoa_set_window_floating(window_st* window, bool enabled)
{
    @autoreleasepool {
    if (enabled)
        [window->ns.object setLevel:NSFloatingWindowLevel];
    else
        [window->ns.object setLevel:NSNormalWindowLevel];
    } // autoreleasepool
}

static float cocoa_get_window_opacity(window_st* window)
{
    @autoreleasepool {
    return (float) [window->ns.object alphaValue];
    } // autoreleasepool
}

static void cocoa_set_window_opacity(window_st* window, float opacity)
{
    @autoreleasepool {
    [window->ns.object setAlphaValue:opacity];
    } // autoreleasepool
}


static void cocoa_get_window_pos(window_st* window, int* xpos, int* ypos)
{
    @autoreleasepool {

    const NSRect contentRect =
        [window->ns.object contentRectForFrameRect:[window->ns.object frame]];

    if (xpos)
        *xpos = contentRect.origin.x;
    if (ypos)
        *ypos = cocoa_TransformY(contentRect.origin.y + contentRect.size.height - 1);

    } // autoreleasepool
}

static void cocoa_set_window_pos(window_st* window, int x, int y)
{
    @autoreleasepool {

    const NSRect contentRect = [window->ns.view frame];
    const NSRect dummyRect = NSMakeRect(x, cocoa_TransformY(y + contentRect.size.height - 1), 0, 0);
    const NSRect frameRect = [window->ns.object frameRectForContentRect:dummyRect];
    [window->ns.object setFrameOrigin:frameRect.origin];

    } // autoreleasepool
}

static void cocoa_get_window_size(window_st* window, int* width, int* height)
{
    @autoreleasepool {

    const NSRect contentRect = [window->ns.view frame];

    if (width)
        *width = contentRect.size.width;
    if (height)
        *height = contentRect.size.height;

    } // autoreleasepool
}

static void cocoa_get_framebuffer_size(window_st* window, int* width, int* height)
{
    @autoreleasepool {

    // Retina：帧缓冲像素 = 内容区 points 经 backing store 缩放
    const NSRect points = [window->ns.view frame];
    const NSRect fbRect = [window->ns.view convertRectToBacking:points];

    if (width)
        *width = (int) fbRect.size.width;
    if (height)
        *height = (int) fbRect.size.height;

    } // autoreleasepool
}

static void cocoa_set_window_size(window_st* window, int width, int height)
{
    @autoreleasepool {

    if (window->monitor)
    {
        if (window->monitor->window == window)
            acquireMonitor(window);
    }
    else
    {
        NSRect contentRect =
            [window->ns.object contentRectForFrameRect:[window->ns.object frame]];
        contentRect.origin.y += contentRect.size.height - height;
        contentRect.size = NSMakeSize(width, height);
        [window->ns.object setFrame:[window->ns.object frameRectForContentRect:contentRect]
                            display:YES];
    }

    } // autoreleasepool
}

static void cocoa_get_window_frame_size(window_st* window,
                                  int* left, int* top,
                                  int* right, int* bottom)
{
    @autoreleasepool {

    const NSRect contentRect = [window->ns.view frame];
    const NSRect frameRect = [window->ns.object frameRectForContentRect:contentRect];

    if (left)
        *left = contentRect.origin.x - frameRect.origin.x;
    if (top)
        *top = frameRect.origin.y + frameRect.size.height -
               contentRect.origin.y - contentRect.size.height;
    if (right)
        *right = frameRect.origin.x + frameRect.size.width -
                 contentRect.origin.x - contentRect.size.width;
    if (bottom)
        *bottom = contentRect.origin.y - frameRect.origin.y;

    } // autoreleasepool
}

static void cocoa_set_window_size_limits(window_st* window,
                                   int minwidth, int minheight,
                                   int maxwidth, int maxheight)
{
    @autoreleasepool {

    if (minwidth == SC_DONT_CARE || minheight == SC_DONT_CARE)
        [window->ns.object setContentMinSize:NSMakeSize(0, 0)];
    else
        [window->ns.object setContentMinSize:NSMakeSize(minwidth, minheight)];

    if (maxwidth == SC_DONT_CARE || maxheight == SC_DONT_CARE)
        [window->ns.object setContentMaxSize:NSMakeSize(DBL_MAX, DBL_MAX)];
    else
        [window->ns.object setContentMaxSize:NSMakeSize(maxwidth, maxheight)];

    } // autoreleasepool
}

static void cocoa_get_window_content_scale(window_st* window,
                                     float* xscale, float* yscale)
{
    @autoreleasepool {

    const NSRect points = [window->ns.view frame];
    const NSRect pixels = [window->ns.view convertRectToBacking:points];

    if (xscale)
        *xscale = (float) (pixels.size.width / points.size.width);
    if (yscale)
        *yscale = (float) (pixels.size.height / points.size.height);

    } // autoreleasepool
}

static void cocoa_set_window_aspect_ratio(window_st* window, int numer, int denom)
{
    @autoreleasepool {
    if (numer == SC_DONT_CARE || denom == SC_DONT_CARE)
        [window->ns.object setResizeIncrements:NSMakeSize(1.0, 1.0)];
    else
        [window->ns.object setContentAspectRatio:NSMakeSize(numer, denom)];
    } // autoreleasepool
}


static void cocoa_show_window(window_st* window)
{
    @autoreleasepool {
    [window->ns.object orderFront:nil];
    } // autoreleasepool
}

static void cocoa_hide_window(window_st* window)
{
    @autoreleasepool {
    [window->ns.object orderOut:nil];
    } // autoreleasepool
}

static void cocoa_maximize_window(window_st* window)
{
    @autoreleasepool {
    if (![window->ns.object isZoomed])
        [window->ns.object zoom:nil];
    } // autoreleasepool
}

static void cocoa_restore_window(window_st* window)
{
    @autoreleasepool {
    if ([window->ns.object isMiniaturized])
        [window->ns.object deminiaturize:nil];
    else if ([window->ns.object isZoomed])
        [window->ns.object zoom:nil];
    } // autoreleasepool
}

static void cocoa_focus_window(window_st* window)
{
    @autoreleasepool {
    // Make us the active application
    // HACK: This is here to prevent applications using only hidden windows from
    //       being activated, but should probably not be done every time any
    //       window is shown
    [NSApp activateIgnoringOtherApps:YES];
    [window->ns.object makeKeyAndOrderFront:nil];
    } // autoreleasepool
}

static void cocoa_iconify_window(window_st* window)
{
    @autoreleasepool {
    [window->ns.object miniaturize:nil];
    } // autoreleasepool
}

static void cocoa_request_window_attention(window_st* window)
{
    @autoreleasepool {
    [NSApp requestUserAttention:NSInformationalRequest];
    } // autoreleasepool
}


static bool cocoa_window_visible(window_st* window)
{
    @autoreleasepool {
    return [window->ns.object isVisible];
    } // autoreleasepool
}

static bool cocoa_window_maximized(window_st* window)
{
    @autoreleasepool {

    if (window->resizable)
        return [window->ns.object isZoomed];
    else
        return false;

    } // autoreleasepool
}

static bool cocoa_window_focused(window_st* window)
{
    @autoreleasepool {
    return [window->ns.object isKeyWindow];
    } // autoreleasepool
}

static bool cocoa_window_hovered(window_st* window)
{
    @autoreleasepool {

    const NSPoint point = [NSEvent mouseLocation];

    if ([NSWindow windowNumberAtPoint:point belowWindowWithWindowNumber:0] !=
        [window->ns.object windowNumber])
    {
        return false;
    }

    return NSMouseInRect(point,
        [window->ns.object convertRectToScreen:[window->ns.view frame]], NO);

    } // autoreleasepool
}

static bool cocoa_window_iconified(window_st* window)
{
    @autoreleasepool {
    return [window->ns.object isMiniaturized];
    } // autoreleasepool
}


static void cocoa_set_cursor(window_st* window, cursor_st* cursor)
{
    @autoreleasepool {
    if (cursorInContentArea(window))
        updateCursorImage(window);
    } // autoreleasepool
}

static bool cocoa_create_standard_cursor(cursor_st* cursor, int shape)
{
    @autoreleasepool {

    SEL cursorSelector = NULL;

    // HACK: Try to use a private message
    switch (shape)
    {
        case SC_RESIZE_EW_CURSOR:
            cursorSelector = NSSelectorFromString(@"_windowResizeEastWestCursor");
            break;
        case SC_RESIZE_NS_CURSOR:
            cursorSelector = NSSelectorFromString(@"_windowResizeNorthSouthCursor");
            break;
        case SC_RESIZE_NWSE_CURSOR:
            cursorSelector = NSSelectorFromString(@"_windowResizeNorthWestSouthEastCursor");
            break;
        case SC_RESIZE_NESW_CURSOR:
            cursorSelector = NSSelectorFromString(@"_windowResizeNorthEastSouthWestCursor");
            break;
    }

    if (cursorSelector && [NSCursor respondsToSelector:cursorSelector])
    {
        id object = [NSCursor performSelector:cursorSelector];
        if ([object isKindOfClass:[NSCursor class]])
            cursor->ns.object = object;
    }

    if (!cursor->ns.object)
    {
        switch (shape)
        {
            case SC_ARROW_CURSOR:
                cursor->ns.object = [NSCursor arrowCursor];
                break;
            case SC_IBEAM_CURSOR:
                cursor->ns.object = [NSCursor IBeamCursor];
                break;
            case SC_CROSSHAIR_CURSOR:
                cursor->ns.object = [NSCursor crosshairCursor];
                break;
            case SC_POINTING_HAND_CURSOR:
                cursor->ns.object = [NSCursor pointingHandCursor];
                break;
            case SC_RESIZE_EW_CURSOR:
                cursor->ns.object = [NSCursor resizeLeftRightCursor];
                break;
            case SC_RESIZE_NS_CURSOR:
                cursor->ns.object = [NSCursor resizeUpDownCursor];
                break;
            case SC_RESIZE_ALL_CURSOR:
                cursor->ns.object = [NSCursor closedHandCursor];
                break;
            case SC_NOT_ALLOWED_CURSOR:
                cursor->ns.object = [NSCursor operationNotAllowedCursor];
                break;
        }
    }

    if (!cursor->ns.object)
    {
        impl_on_error(SC_WSI_ERR_CURSOR_UNAVAILABLE,
                        "Cocoa: Standard cursor shape unavailable");
        return false;
    }

    [cursor->ns.object retain];
    return true;

    } // autoreleasepool
}

static bool cocoa_create_cursor(cursor_st* cursor, const sc_wsi_img* image, int xhot, int yhot)
{
    @autoreleasepool {

    NSImage* native;
    NSBitmapImageRep* rep;

    rep = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:NULL
                      pixelsWide:image->width
                      pixelsHigh:image->height
                   bitsPerSample:8
                 samplesPerPixel:4
                        hasAlpha:YES
                        isPlanar:NO
                  colorSpaceName:NSCalibratedRGBColorSpace
                    bitmapFormat:NSBitmapFormatAlphaNonpremultiplied
                     bytesPerRow:image->width * 4
                    bitsPerPixel:32];

    if (rep == nil)
        return false;

    memcpy([rep bitmapData], image->pixels, image->width * image->height * 4);

    native = [[NSImage alloc] initWithSize:NSMakeSize(image->width, image->height)];
    [native addRepresentation:rep];

    cursor->ns.object = [[NSCursor alloc] initWithImage:native
                                                hotSpot:NSMakePoint(xhot, yhot)];

    [native release];
    [rep release];

    if (cursor->ns.object == nil)
        return false;

    return true;

    } // autoreleasepool
}

static void cocoa_destroy_cursor(cursor_st* cursor)
{
    @autoreleasepool {
    if (cursor->ns.object)
        [(NSCursor*) cursor->ns.object release];
    } // autoreleasepool
}

static void cocoa_get_cursor_pos(window_st* window, double* xpos, double* ypos)
{
    @autoreleasepool {

    const NSRect contentRect = [window->ns.view frame];
    // NOTE: The returned location uses base 0,1 not 0,0
    const NSPoint pos = [window->ns.object mouseLocationOutsideOfEventStream];

    if (xpos)
        *xpos = pos.x;
    if (ypos)
        *ypos = contentRect.size.height - pos.y;

    } // autoreleasepool
}

static void cocoa_set_cursor_pos(window_st* window, double x, double y)
{
    @autoreleasepool {

    updateCursorImage(window);

    const NSRect contentRect = [window->ns.view frame];
    // NOTE: The returned location uses base 0,1 not 0,0
    const NSPoint pos = [window->ns.object mouseLocationOutsideOfEventStream];

    window->ns.cursorWarpDeltaX += x - pos.x;
    window->ns.cursorWarpDeltaY += y - contentRect.size.height + pos.y;

    if (window->monitor)
    {
        CGDisplayMoveCursorToPoint(window->monitor->ns.displayID,
                                   CGPointMake(x, y));
    }
    else
    {
        const NSRect localRect = NSMakeRect(x, contentRect.size.height - y - 1, 0, 0);
        const NSRect globalRect = [window->ns.object convertRectToScreen:localRect];
        const NSPoint globalPoint = globalRect.origin;

        CGWarpMouseCursorPosition(CGPointMake(globalPoint.x,
                                              cocoa_TransformY(globalPoint.y)));
    }

    // HACK: Calling this right after setting the cursor position prevents macOS
    //       from freezing the cursor for a fraction of a second afterwards
    if (window->cursorMode != SC_CURSOR_DISABLED)
        CGAssociateMouseAndMouseCursorPosition(true);

    } // autoreleasepool
}

static void cocoa_set_cursor_mode(window_st* window, int mode)
{
    @autoreleasepool {

    if (mode == SC_CURSOR_CAPTURED)
    {
        impl_on_error(SC_WSI_ERR_FEATURE_UNIMPLEMENTED,
                        "Cocoa: Captured cursor mode not yet implemented");
    }

    if (cocoa_window_focused(window))
        updateCursorMode(window);

    } // autoreleasepool
}

static void cocoa_set_mouse_raw_motion(window_st *window, bool enabled)
{
    impl_on_error(SC_WSI_ERR_FEATURE_UNIMPLEMENTED,
                    "Cocoa: Raw mouse motion not yet implemented");
}

static bool cocoa_mouse_raw_motion_supported(void)
{
    return false;
}


static int cocoa_get_key_scancode(int key)
{
    return g_wsi.ns.scancodes[key];
}

static const char* cocoa_get_scancode_name(int scancode)
{
    @autoreleasepool {

    if (scancode < 0 || scancode > 0xff)
    {
        impl_on_error(SC_WSI_ERR_INVALID_VALUE, "Invalid scancode %i", scancode);
        return NULL;
    }

    const int key = g_wsi.ns.keycodes[scancode];
    if (key == SC_KEY_UNKNOWN)
        return NULL;

    UInt32 deadKeyState = 0;
    UniChar characters[4];
    UniCharCount characterCount = 0;

    if (UCKeyTranslate([(NSData*) g_wsi.ns.unicodeData bytes],
                       scancode,
                       kUCKeyActionDisplay,
                       0,
                       LMGetKbdType(),
                       kUCKeyTranslateNoDeadKeysBit,
                       &deadKeyState,
                       sizeof(characters) / sizeof(characters[0]),
                       &characterCount,
                       characters) != noErr)
    {
        return NULL;
    }

    if (!characterCount)
        return NULL;

    CFStringRef string = CFStringCreateWithCharactersNoCopy(kCFAllocatorDefault,
                                                            characters,
                                                            characterCount,
                                                            kCFAllocatorNull);
    CFStringGetCString(string,
                       g_wsi.ns.keynames[key],
                       sizeof(g_wsi.ns.keynames[key]),
                       kCFStringEncodingUTF8);
    CFRelease(string);

    return g_wsi.ns.keynames[key];

    } // autoreleasepool
}

static void cocoa_set_clipboard_string(const char* string)
{
    @autoreleasepool {
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    [pasteboard declareTypes:@[NSPasteboardTypeString] owner:nil];
    [pasteboard setString:@(string) forType:NSPasteboardTypeString];
    } // autoreleasepool
}

static const char* cocoa_get_clipboard_string(void)
{
    @autoreleasepool {

    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];

    if (![[pasteboard types] containsObject:NSPasteboardTypeString])
    {
        impl_on_error(SC_WSI_ERR_FORMAT_UNAVAILABLE,
                        "Cocoa: Failed to retrieve string from pasteboard");
        return NULL;
    }

    NSString* object = [pasteboard stringForType:NSPasteboardTypeString];
    if (!object)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Cocoa: Failed to retrieve object from pasteboard");
        return NULL;
    }

    wsi_free(g_wsi.ns.clipboardString);
    g_wsi.ns.clipboardString = wsi_strdup([object UTF8String]);

    return g_wsi.ns.clipboardString;

    } // autoreleasepool
}

///////////////////////////////////////////////////////////////////////////////
// Window 
///////////////////////////////////////////////////////////////////////////////

// HACK: This enum value is missing from framework headers on OS X 10.11 despite
//       having been (according to documentation) added in Mac OS X 10.7
#define NSWindowCollectionBehaviorFullScreenNone (1 << 9)

// Returns whether the cursor is in the content area of the specified window
static bool cursorInContentArea(window_st* window)
{
    const NSPoint pos = [window->ns.object mouseLocationOutsideOfEventStream];
    return [window->ns.view mouse:pos inRect:[window->ns.view frame]];
}

// Hides the cursor if not already hidden
static void hideCursor(window_st* window)
{
    if (!g_wsi.ns.cursorHidden)
    {
        [NSCursor hide];
        g_wsi.ns.cursorHidden = true;
    }
}

// Shows the cursor if not already shown
static void showCursor(window_st* window)
{
    if (g_wsi.ns.cursorHidden)
    {
        [NSCursor unhide];
        g_wsi.ns.cursorHidden = false;
    }
}

// Updates the cursor image according to its cursor mode
static void updateCursorImage(window_st* window)
{
    if (window->cursorMode == SC_CURSOR_NORMAL)
    {
        showCursor(window);

        if (window->cursor)
            [(NSCursor*) window->cursor->ns.object set];
        else
            [[NSCursor arrowCursor] set];
    }
    else
        hideCursor(window);
}

// Apply chosen cursor mode to a focused window
static void updateCursorMode(window_st* window)
{
    if (window->cursorMode == SC_CURSOR_DISABLED)
    {
        g_wsi.ns.disabledCursorWindow = window;
        cocoa_get_cursor_pos(window,
                               &g_wsi.ns.restoreCursorPosX,
                               &g_wsi.ns.restoreCursorPosY);
        wsi_center_cursor_in_content_area(window);
        CGAssociateMouseAndMouseCursorPosition(false);
    }
    else if (g_wsi.ns.disabledCursorWindow == window)
    {
        g_wsi.ns.disabledCursorWindow = NULL;
        cocoa_set_cursor_pos(window,
                               g_wsi.ns.restoreCursorPosX,
                               g_wsi.ns.restoreCursorPosY);
        // NOTE: The matching CGAssociateMouseAndMouseCursorPosition call is
        //       made in cocoa_set_cursor_pos as part of a workaround
    }

    if (cursorInContentArea(window))
        updateCursorImage(window);
}

// Translates macOS key modifiers into WSI ones
static int translateFlags(NSUInteger flags)
{
    int mods = 0;

    if (flags & NSEventModifierFlagShift)
        mods |= SC_MOD_SHIFT;
    if (flags & NSEventModifierFlagControl)
        mods |= SC_MOD_CONTROL;
    if (flags & NSEventModifierFlagOption)
        mods |= SC_MOD_ALT;
    if (flags & NSEventModifierFlagCommand)
        mods |= SC_MOD_SUPER;
    if (flags & NSEventModifierFlagCapsLock)
        mods |= SC_MOD_CAPS_LOCK;

    return mods;
}

// Translates a macOS keycode to a WSI keycode
static int translateKey(unsigned int key)
{
    if (key >= sizeof(g_wsi.ns.keycodes) / sizeof(g_wsi.ns.keycodes[0]))
        return SC_KEY_UNKNOWN;

    return g_wsi.ns.keycodes[key];
}

// Translate a WSI keycode to a Cocoa modifier flag
static NSUInteger translateKeyToModifierFlag(int key)
{
    switch (key)
    {
        case SC_KEY_LEFT_SHIFT:
        case SC_KEY_RIGHT_SHIFT:
            return NSEventModifierFlagShift;
        case SC_KEY_LEFT_CONTROL:
        case SC_KEY_RIGHT_CONTROL:
            return NSEventModifierFlagControl;
        case SC_KEY_LEFT_ALT:
        case SC_KEY_RIGHT_ALT:
            return NSEventModifierFlagOption;
        case SC_KEY_LEFT_SUPER:
        case SC_KEY_RIGHT_SUPER:
            return NSEventModifierFlagCommand;
        case SC_KEY_CAPS_LOCK:
            return NSEventModifierFlagCapsLock;
    }

    return 0;
}

// Defines a constant for empty ranges in NSTextInputClient
static const NSRange kEmptyRange = { NSNotFound, 0 };

//------------------------------------------------------------------------
// Content view class
//------------------------------------------------------------------------

@interface SC_ContentView : NSView <NSTextInputClient>
{
    window_st* window;
    NSTrackingArea* trackingArea;
    NSMutableAttributedString* markedText;
}
- (instancetype)initWithGlfwWindow:(window_st *)initWindow;
@end

@implementation SC_ContentView
- (instancetype)initWithGlfwWindow:(window_st *)initWindow
{
    self = [super init];
    if (self != nil)
    {
        window = initWindow;
        trackingArea = nil;
        markedText = [[NSMutableAttributedString alloc] init];

        [self updateTrackingAreas];
        [self registerForDraggedTypes:@[NSPasteboardTypeURL]];
    }

    return self;
}
- (void)dealloc
{
    [trackingArea release];
    [markedText release];
    [super dealloc];
}
- (BOOL)isOpaque
{
    return [window->ns.object isOpaque];
}
- (BOOL)canBecomeKeyView
{
    return YES;
}
- (BOOL)acceptsFirstResponder
{
    return YES;
}
- (BOOL)wantsUpdateLayer
{
    return YES;
}
- (void)updateLayer
{
    impl_on_win_damage(window);
}
- (void)cursorUpdate:(NSEvent *)event
{
    updateCursorImage(window);
}
- (BOOL)acceptsFirstMouse:(NSEvent *)event
{
    return YES;
}
- (void)mouseDown:(NSEvent *)event
{
    impl_on_mouse_click(window,
                         SC_MOUSE_BUTTON_LEFT,
                         SC_PRESS,
                         translateFlags([event modifierFlags]));
}
- (void)mouseDragged:(NSEvent *)event
{
    [self mouseMoved:event];
}
- (void)mouseUp:(NSEvent *)event
{
    impl_on_mouse_click(window,
                         SC_MOUSE_BUTTON_LEFT,
                         SC_RELEASE,
                         translateFlags([event modifierFlags]));
}
- (void)mouseMoved:(NSEvent *)event
{
    if (window->cursorMode == SC_CURSOR_DISABLED)
    {
        const double dx = [event deltaX] - window->ns.cursorWarpDeltaX;
        const double dy = [event deltaY] - window->ns.cursorWarpDeltaY;

        impl_on_cursor_pos(window,
                            window->virtualCursorPosX + dx,
                            window->virtualCursorPosY + dy);
    }
    else
    {
        const NSRect contentRect = [window->ns.view frame];
        // NOTE: The returned location uses base 0,1 not 0,0
        const NSPoint pos = [event locationInWindow];

        impl_on_cursor_pos(window, pos.x, contentRect.size.height - pos.y);
    }

    window->ns.cursorWarpDeltaX = 0;
    window->ns.cursorWarpDeltaY = 0;
}
- (void)rightMouseDown:(NSEvent *)event
{
    impl_on_mouse_click(window,
                         SC_MOUSE_BUTTON_RIGHT,
                         SC_PRESS,
                         translateFlags([event modifierFlags]));
}
- (void)rightMouseDragged:(NSEvent *)event
{
    [self mouseMoved:event];
}
- (void)rightMouseUp:(NSEvent *)event
{
    impl_on_mouse_click(window,
                         SC_MOUSE_BUTTON_RIGHT,
                         SC_RELEASE,
                         translateFlags([event modifierFlags]));
}

- (void)otherMouseDown:(NSEvent *)event
{
    impl_on_mouse_click(window,
                         (int) [event buttonNumber],
                         SC_PRESS,
                         translateFlags([event modifierFlags]));
}
- (void)otherMouseDragged:(NSEvent *)event
{
    [self mouseMoved:event];
}
- (void)otherMouseUp:(NSEvent *)event
{
    impl_on_mouse_click(window,
                         (int) [event buttonNumber],
                         SC_RELEASE,
                         translateFlags([event modifierFlags]));
}
- (void)mouseExited:(NSEvent *)event
{
    if (window->cursorMode == SC_CURSOR_HIDDEN)
        showCursor(window);

    impl_on_cursor_enter(window, false);
}
- (void)mouseEntered:(NSEvent *)event
{
    if (window->cursorMode == SC_CURSOR_HIDDEN)
        hideCursor(window);

    impl_on_cursor_enter(window, true);
}
- (void)viewDidChangeBackingProperties
{
    const NSRect contentRect = [window->ns.view frame];
    const NSRect fbRect = [window->ns.view convertRectToBacking:contentRect];
    const float xscale = fbRect.size.width / contentRect.size.width;
    const float yscale = fbRect.size.height / contentRect.size.height;

    if (xscale != window->ns.xscale || yscale != window->ns.yscale)
    {
        if (window->ns.scaleFramebuffer && window->ns.layer)
            [window->ns.layer setContentsScale:[window->ns.object backingScaleFactor]];

        window->ns.xscale = xscale;
        window->ns.yscale = yscale;
        impl_on_win_content_scale(window, xscale, yscale);
    }

    if (fbRect.size.width != window->ns.fbWidth ||
        fbRect.size.height != window->ns.fbHeight)
    {
        window->ns.fbWidth  = fbRect.size.width;
        window->ns.fbHeight = fbRect.size.height;
    }
}
- (void)drawRect:(NSRect)rect
{
    impl_on_win_damage(window);
}
- (void)updateTrackingAreas
{
    if (trackingArea != nil)
    {
        [self removeTrackingArea:trackingArea];
        [trackingArea release];
    }

    const NSTrackingAreaOptions options = NSTrackingMouseEnteredAndExited |
                                          NSTrackingActiveInKeyWindow |
                                          NSTrackingEnabledDuringMouseDrag |
                                          NSTrackingCursorUpdate |
                                          NSTrackingInVisibleRect |
                                          NSTrackingAssumeInside;

    trackingArea = [[NSTrackingArea alloc] initWithRect:[self bounds]
                                                options:options
                                                  owner:self
                                               userInfo:nil];

    [self addTrackingArea:trackingArea];
    [super updateTrackingAreas];
}
- (void)keyDown:(NSEvent *)event
{
    const int key = translateKey([event keyCode]);
    const int mods = translateFlags([event modifierFlags]);

    impl_on_key(window, key, [event keyCode], SC_PRESS, mods);

    [self interpretKeyEvents:@[event]];
}
- (void)flagsChanged:(NSEvent *)event
{
    int action;
    const unsigned int modifierFlags =
        [event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask;
    const int key = translateKey([event keyCode]);
    const int mods = translateFlags(modifierFlags);
    const NSUInteger keyFlag = translateKeyToModifierFlag(key);

    if (keyFlag & modifierFlags)
    {
        if (window->keys[key] == SC_PRESS)
            action = SC_RELEASE;
        else
            action = SC_PRESS;
    }
    else
        action = SC_RELEASE;

    impl_on_key(window, key, [event keyCode], action, mods);
}
- (void)keyUp:(NSEvent *)event
{
    const int key = translateKey([event keyCode]);
    const int mods = translateFlags([event modifierFlags]);
    impl_on_key(window, key, [event keyCode], SC_RELEASE, mods);
}
- (BOOL)performKeyEquivalent:(NSEvent *)event
{
    // HACK: Some key combinations are consumed before reaching keyDown:
    //       so we claim those events and emit them here
    const int key = translateKey([event keyCode]);
    const int mods = translateFlags([event modifierFlags]);

    if (mods & SC_MOD_CONTROL)
    {
        if (key == SC_KEY_TAB || key == SC_KEY_ESCAPE)
        {
            impl_on_key(window, key, [event keyCode], SC_PRESS, mods);
            return YES;
        }
    }

    if (mods & SC_MOD_SUPER)
    {
        if (key == SC_KEY_PERIOD)
        {
            impl_on_key(window, key, [event keyCode], SC_PRESS, mods);
            return YES;
        }
    }

    return [super performKeyEquivalent:event];
}
- (void)scrollWheel:(NSEvent *)event
{
    double deltaX = [event scrollingDeltaX];
    double deltaY = [event scrollingDeltaY];

    if ([event hasPreciseScrollingDeltas])
    {
        deltaX *= 0.1;
        deltaY *= 0.1;
    }

    if (fabs(deltaX) > 0.0 || fabs(deltaY) > 0.0)
        impl_on_scroll(window, deltaX, deltaY);
}
- (NSDragOperation)draggingEntered:(id <NSDraggingInfo>)sender
{
    // HACK: We don't know what to say here because we don't know what the
    //       application wants to do with the paths
    return NSDragOperationGeneric;
}
- (BOOL)performDragOperation:(id <NSDraggingInfo>)sender
{
    const NSRect contentRect = [window->ns.view frame];
    // NOTE: The returned location uses base 0,1 not 0,0
    const NSPoint pos = [sender draggingLocation];
    impl_on_cursor_pos(window, pos.x, contentRect.size.height - pos.y);

    NSPasteboard* pasteboard = [sender draggingPasteboard];
    NSDictionary* options = @{NSPasteboardURLReadingFileURLsOnlyKey:@YES};
    NSArray* urls = [pasteboard readObjectsForClasses:@[[NSURL class]]
                                              options:options];
    const NSUInteger count = [urls count];
    if (count)
    {
        char** paths = wsi_calloc(count, sizeof(char*));

        for (NSUInteger i = 0;  i < count;  i++)
            paths[i] = wsi_strdup([urls[i] fileSystemRepresentation]);

        impl_on_drop(window, (int) count, (const char**) paths);

        for (NSUInteger i = 0;  i < count;  i++)
            wsi_free(paths[i]);
        wsi_free(paths);
    }

    return YES;
}
- (BOOL)hasMarkedText
{
    return [markedText length] > 0;
}
- (NSRange)markedRange
{
    if ([markedText length] > 0)
        return NSMakeRange(0, [markedText length] - 1);
    else
        return kEmptyRange;
}
- (NSRange)selectedRange
{
    return kEmptyRange;
}
- (void)setMarkedText:(id)string
        selectedRange:(NSRange)selectedRange
     replacementRange:(NSRange)replacementRange
{
    [markedText release];
    if ([string isKindOfClass:[NSAttributedString class]])
        markedText = [[NSMutableAttributedString alloc] initWithAttributedString:string];
    else
        markedText = [[NSMutableAttributedString alloc] initWithString:string];
}
- (void)unmarkText
{
    [[markedText mutableString] setString:@""];
}
- (NSArray*)validAttributesForMarkedText
{
    return [NSArray array];
}
- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range
                                               actualRange:(NSRangePointer)actualRange
{
    return nil;
}
- (NSUInteger)characterIndexForPoint:(NSPoint)point
{
    return 0;
}
- (NSRect)firstRectForCharacterRange:(NSRange)range
                         actualRange:(NSRangePointer)actualRange
{
    const NSRect frame = [window->ns.view frame];
    return NSMakeRect(frame.origin.x, frame.origin.y, 0.0, 0.0);
}
- (void)insertText:(id)string replacementRange:(NSRange)replacementRange
{
    NSString* characters;
    NSEvent* event = [NSApp currentEvent];
    const int mods = translateFlags([event modifierFlags]);
    const int plain = !(mods & SC_MOD_SUPER);

    if ([string isKindOfClass:[NSAttributedString class]])
        characters = [string string];
    else
        characters = (NSString*) string;

    NSRange range = NSMakeRange(0, [characters length]);
    while (range.length)
    {
        uint32_t codepoint = 0;

        if ([characters getBytes:&codepoint
                       maxLength:sizeof(codepoint)
                      usedLength:NULL
                        encoding:NSUTF32StringEncoding
                         options:0
                           range:range
                  remainingRange:&range])
        {
            if (codepoint >= 0xf700 && codepoint <= 0xf7ff)
                continue;

            impl_on_chr(window, codepoint, mods, plain);
        }
    }
}
- (void)doCommandBySelector:(SEL)selector
{
}
@end

//------------------------------------------------------------------------
// window class
//------------------------------------------------------------------------

@interface SC_WindowDelegate : NSObject
{
    window_st* window;
}
- (instancetype)initWithGlfwWindow:(window_st *)initWindow;
@end

@implementation SC_WindowDelegate
- (instancetype)initWithGlfwWindow:(window_st *)initWindow
{
    self = [super init];
    if (self != nil)
        window = initWindow;

    return self;
}
- (BOOL)windowShouldClose:(id)sender
{
    impl_on_win_close_req(window);
    return NO;
}
- (void)windowDidResize:(NSNotification *)notification
{
    if (g_wsi.ns.disabledCursorWindow == window)
        wsi_center_cursor_in_content_area(window);

    const int maximized = [window->ns.object isZoomed];
    if (window->ns.maximized != maximized)
    {
        window->ns.maximized = maximized;
        impl_on_win_maximize(window, maximized);
    }

    const NSRect contentRect = [window->ns.view frame];
    const NSRect fbRect = [window->ns.view convertRectToBacking:contentRect];

    if (fbRect.size.width != window->ns.fbWidth ||
        fbRect.size.height != window->ns.fbHeight)
    {
        window->ns.fbWidth  = fbRect.size.width;
        window->ns.fbHeight = fbRect.size.height;
    }

    if (contentRect.size.width != window->ns.width ||
        contentRect.size.height != window->ns.height)
    {
        window->ns.width  = contentRect.size.width;
        window->ns.height = contentRect.size.height;
        impl_on_win_size(window, contentRect.size.width, contentRect.size.height);
    }
}
- (void)windowDidMove:(NSNotification *)notification
{
    if (g_wsi.ns.disabledCursorWindow == window)
        wsi_center_cursor_in_content_area(window);

    int x, y;
    cocoa_get_window_pos(window, &x, &y);
    impl_on_win_pos(window, x, y);
}
- (void)windowDidMiniaturize:(NSNotification *)notification
{
    if (window->monitor)
        releaseMonitor(window);

    impl_on_win_iconify(window, true);
}
- (void)windowDidDeminiaturize:(NSNotification *)notification
{
    if (window->monitor)
        acquireMonitor(window);

    impl_on_win_iconify(window, false);
}
- (void)windowDidBecomeKey:(NSNotification *)notification
{
    if (g_wsi.ns.disabledCursorWindow == window)
        wsi_center_cursor_in_content_area(window);

    impl_on_win_focus(window, true);
    updateCursorMode(window);
}
- (void)windowDidResignKey:(NSNotification *)notification
{
    if (window->monitor && window->autoIconify)
        cocoa_iconify_window(window);

    impl_on_win_focus(window, false);
}
- (void)windowDidChangeOcclusionState:(NSNotification* )notification
{
    if ([window->ns.object respondsToSelector:@selector(occlusionState)])
    {
        if ([window->ns.object occlusionState] & NSWindowOcclusionStateVisible)
            window->ns.occluded = false;
        else
            window->ns.occluded = true;
    }
}
@end


@interface WSIWindow : NSWindow {}
@end

@implementation WSIWindow
- (BOOL)canBecomeKeyWindow
{
    // Required for NSWindowStyleMaskBorderless windows
    return YES;
}
- (BOOL)canBecomeMainWindow
{
    return YES;
}
@end

//------------------------------------------------------------------------

// Create the Cocoa window
static bool createNativeWindow(window_st* window, const wnd_config_st* wndconfig)
{
    window->ns.delegate = [[SC_WindowDelegate alloc] initWithGlfwWindow:window];
    if (window->ns.delegate == nil)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Cocoa: Failed to create window delegate");
        return false;
    }

    NSRect contentRect;

    if (window->monitor)
    {
        sc_wsi_video_mode mode;
        int xpos, ypos;

        cocoa_get_video_mode(window->monitor, &mode);
        cocoa_get_monitor_pos(window->monitor, &xpos, &ypos);

        contentRect = NSMakeRect(xpos, ypos, mode.width, mode.height);
    }
    else
    {
        if (wndconfig->xpos == SC_ANY_POSITION ||
            wndconfig->ypos == SC_ANY_POSITION)
        {
            contentRect = NSMakeRect(0, 0, wndconfig->width, wndconfig->height);
        }
        else
        {
            const int xpos = wndconfig->xpos;
            const int ypos = cocoa_TransformY(wndconfig->ypos + wndconfig->height - 1);
            contentRect = NSMakeRect(xpos, ypos, wndconfig->width, wndconfig->height);
        }
    }

    NSUInteger styleMask = NSWindowStyleMaskMiniaturizable;

    if (window->monitor || !window->decorated)
        styleMask |= NSWindowStyleMaskBorderless;
    else
    {
        styleMask |= (NSWindowStyleMaskTitled | NSWindowStyleMaskClosable);

        if (window->resizable)
            styleMask |= NSWindowStyleMaskResizable;
    }

    window->ns.object = [[WSIWindow alloc]
        initWithContentRect:contentRect
                  styleMask:styleMask
                    backing:NSBackingStoreBuffered
                      defer:NO];

    if (window->ns.object == nil)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR, "Cocoa: Failed to create window");
        return false;
    }

    if (window->monitor)
        [window->ns.object setLevel:NSMainMenuWindowLevel + 1];
    else
    {
        if (wndconfig->xpos == SC_ANY_POSITION ||
            wndconfig->ypos == SC_ANY_POSITION)
        {
            [(NSWindow*) window->ns.object center];
            g_wsi.ns.cascadePoint =
                NSPointToCGPoint([window->ns.object cascadeTopLeftFromPoint:
                                NSPointFromCGPoint(g_wsi.ns.cascadePoint)]);
        }

        if (wndconfig->resizable)
        {
            const NSWindowCollectionBehavior behavior =
                NSWindowCollectionBehaviorFullScreenPrimary |
                NSWindowCollectionBehaviorManaged;
            [window->ns.object setCollectionBehavior:behavior];
        }
        else
        {
            const NSWindowCollectionBehavior behavior =
                NSWindowCollectionBehaviorFullScreenNone;
            [window->ns.object setCollectionBehavior:behavior];
        }

        if (wndconfig->floating)
            [window->ns.object setLevel:NSFloatingWindowLevel];

        if (wndconfig->maximized)
            [window->ns.object zoom:nil];
    }

    if (strlen(wndconfig->ns.frameName))
        [window->ns.object setFrameAutosaveName:@(wndconfig->ns.frameName)];

    window->ns.view = [[SC_ContentView alloc] initWithGlfwWindow:window];
    window->ns.scaleFramebuffer = wndconfig->scaleToMonitor;

    [window->ns.object setContentView:window->ns.view];
    [window->ns.object makeFirstResponder:window->ns.view];
    [window->ns.object setTitle:@(window->title)];
    [window->ns.object setDelegate:window->ns.delegate];
    [window->ns.object setAcceptsMouseMovedEvents:YES];
    [window->ns.object setRestorable:NO];

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101200
    if ([window->ns.object respondsToSelector:@selector(setTabbingMode:)])
        [window->ns.object setTabbingMode:NSWindowTabbingModeDisallowed];
#endif

    cocoa_get_window_size(window, &window->ns.width, &window->ns.height);
    {
        const NSRect contentRect = [window->ns.view frame];
        const NSRect fbRect = [window->ns.view convertRectToBacking:contentRect];
        window->ns.fbWidth  = fbRect.size.width;
        window->ns.fbHeight = fbRect.size.height;
    }

    return true;
}

static bool cocoa_create_window(window_st* window, const wnd_config_st* wndconfig)
{
    @autoreleasepool {

    if (!createNativeWindow(window, wndconfig))
        return false;

    if (wndconfig->mousePassthrough)
        cocoa_set_window_mouse_passthrough(window, true);

    if (window->monitor)
    {
        cocoa_show_window(window);
        cocoa_focus_window(window);
        acquireMonitor(window);

        if (wndconfig->centerCursor)
            wsi_center_cursor_in_content_area(window);
    }
    else
    {
        if (wndconfig->visible)
        {
            cocoa_show_window(window);
            if (wndconfig->focused)
                cocoa_focus_window(window);
        }
    }

    return true;

    } // autoreleasepool
}

static void cocoa_destroy_window(window_st* window)
{
    @autoreleasepool {

    if (g_wsi.ns.disabledCursorWindow == window)
        g_wsi.ns.disabledCursorWindow = NULL;

    [window->ns.object orderOut:nil];

    if (window->monitor)
        releaseMonitor(window);

    [window->ns.object setDelegate:nil];
    [window->ns.delegate release];
    window->ns.delegate = nil;

    [window->ns.view release];
    window->ns.view = nil;

    [window->ns.object close];
    window->ns.object = nil;

    // HACK: Allow Cocoa to catch up before returning
    cocoa_poll_events();

    } // autoreleasepool
}

///////////////////////////////////////////////////////////////////////////////
// 接口集成
///////////////////////////////////////////////////////////////////////////////

WSI_API CGDirectDisplayID wsi_get_cocoa_monitor(sc_monitor* handle)
{
    if (!g_wsi.initialized) {
        impl_on_error(SC_WSI_ERR_NOT_INITIALIZED, NULL);
        return kCGNullDirectDisplay;
    }

    if (g_wsi.platform.platformID != SC_PLATFORM_COCOA)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_UNAVAILABLE, "Cocoa: Platform not initialized");
        return kCGNullDirectDisplay;
    }

    monitor_st* monitor = (monitor_st*) handle;
    assert(monitor != NULL);

    return monitor->ns.displayID;
}

bool cocoa_connect(int platformID, platform_st* platform)
{
    const platform_st cocoa =
    {
        .platformID                 = SC_PLATFORM_COCOA,
        .init                       = cocoa_init,
        .terminate                  = cocoa_terminate,

        .pollEvents                 = cocoa_poll_events,
        .waitEvents                 = cocoa_wait_events,
        .waitEventsTimeout          = cocoa_wait_events_timeout,
        .postEmptyEvent             = cocoa_post_empty_event,

        .createWindow               = cocoa_create_window,
        .destroyWindow              = cocoa_destroy_window,
        .setWindowTitle             = cocoa_set_window_title,
        .setWindowIcon              = cocoa_set_window_icon,
        .setWindowMonitor           = cocoa_set_window_monitor,
        .setWindowMousePassthrough  = cocoa_set_window_mouse_passthrough,

        .setWindowDecorated         = cocoa_set_window_decorated,
        .setWindowResizable         = cocoa_set_window_resizable,
        .setWindowFloating          = cocoa_set_window_floating,
        .setWindowOpacity           = cocoa_set_window_opacity,
        .getWindowOpacity           = cocoa_get_window_opacity,

        .getWindowPos               = cocoa_get_window_pos,
        .setWindowPos               = cocoa_set_window_pos,
        .getWindowSize              = cocoa_get_window_size,
        .getFramebufferSize         = cocoa_get_framebuffer_size,
        .setWindowSize              = cocoa_set_window_size,
        .getWindowFrameSize         = cocoa_get_window_frame_size,
        .setWindowSizeLimits        = cocoa_set_window_size_limits,
        .getWindowContentScale      = cocoa_get_window_content_scale,
        .setWindowAspectRatio       = cocoa_set_window_aspect_ratio,

        .showWindow                 = cocoa_show_window,
        .hideWindow                 = cocoa_hide_window,
        .maximizeWindow             = cocoa_maximize_window,
        .restoreWindow              = cocoa_restore_window,
        .focusWindow                = cocoa_focus_window,
        .iconifyWindow              = cocoa_iconify_window,
        .requestWindowAttention     = cocoa_request_window_attention,

        .windowVisible              = cocoa_window_visible,
        .windowMaximized            = cocoa_window_maximized,
        .windowFocused              = cocoa_window_focused,
        .windowHovered              = cocoa_window_hovered,
        .windowIconified            = cocoa_window_iconified,

        .setCursor                  = cocoa_set_cursor,
        .createStandardCursor       = cocoa_create_standard_cursor,
        .createCursor               = cocoa_create_cursor,
        .destroyCursor              = cocoa_destroy_cursor,
        .setCursorMode              = cocoa_set_cursor_mode,
        .setCursorPos               = cocoa_set_cursor_pos,
        .getCursorPos               = cocoa_get_cursor_pos,
        .setRawMouseMotion          = cocoa_set_mouse_raw_motion,
        .rawMouseMotionSupported    = cocoa_mouse_raw_motion_supported,

        .getKeyScancode             = cocoa_get_key_scancode,
        .getScancodeName            = cocoa_get_scancode_name,
        .getClipboardString         = cocoa_get_clipboard_string,
        .setClipboardString         = cocoa_set_clipboard_string,

        .freeMonitor                = cocoa_free_monitor,
        .getMonitorPos              = cocoa_get_monitor_pos,
        .getMonitorWorkarea         = cocoa_get_monitor_work_area,
        .getMonitorContentScale     = cocoa_get_monitor_content_scale,
        .getVideoModes              = cocoa_get_video_modes,
        .getVideoMode               = cocoa_get_video_mode,
        .getGammaRamp               = cocoa_get_gamma_ramp,
        .setGammaRamp               = cocoa_set_gamma_ramp,
    };

    *platform = cocoa;
    return true;
}

///////////////////////////////////////////////////////////////////////////////

#endif // WSI_COCOA

