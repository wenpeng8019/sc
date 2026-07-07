
#include "internal.h"

#if defined(WSI_COCOA)

#include <sys/param.h> // For MAXPATHLEN

// Needed for _NSGetProgname
#include <crt_externs.h>

// Change to our application bundle's resources directory, if present
//
static void changeToResourcesDirectory(void)
{
    char resourcesPath[MAXPATHLEN];

    CFBundleRef bundle = CFBundleGetMainBundle();
    if (!bundle)
        return;

    CFURLRef resourcesURL = CFBundleCopyResourcesDirectoryURL(bundle);

    CFStringRef last = CFURLCopyLastPathComponent(resourcesURL);
    if (CFStringCompare(CFSTR("Resources"), last, 0) != kCFCompareEqualTo)
    {
        CFRelease(last);
        CFRelease(resourcesURL);
        return;
    }

    CFRelease(last);

    if (!CFURLGetFileSystemRepresentation(resourcesURL,
                                          true,
                                          (UInt8*) resourcesPath,
                                          MAXPATHLEN))
    {
        CFRelease(resourcesURL);
        return;
    }

    CFRelease(resourcesURL);

    chdir(resourcesPath);
}

// Set up the menu bar (manually)
// This is nasty, nasty stuff -- calls to undocumented semi-private APIs that
// could go away at any moment, lots of stuff that really should be
// localize(d|able), etc.  Add a nib to save us this horror.
//
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

    // Try to figure out what the calling application is called

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
            appName = @"GLFW Application";
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

// Create key code translation tables
//
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
        // Store the reverse translation for faster key name lookup
        if (g_wsi.ns.keycodes[scancode] >= 0)
            g_wsi.ns.scancodes[g_wsi.ns.keycodes[scancode]] = scancode;
    }
}

// Retrieve Unicode data for the current keyboard layout
//
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

// Load HIToolbox.framework and the TIS symbols we need from it
//
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

@interface GLFWHelper : NSObject
@end

@implementation GLFWHelper

- (void)selectedKeyboardInputSourceChanged:(NSObject* )object
{
    updateUnicodeData();
}

- (void)doNothing:(id)object
{
}

@end // GLFWHelper

@interface GLFWApplicationDelegate : NSObject <NSApplicationDelegate>
@end

@implementation GLFWApplicationDelegate

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    for (window_st* window = g_wsi.windowListHead;  window;  window = window->next)
        impl_on_win_close_req(window);

    return NSTerminateCancel;
}

- (void)applicationDidChangeScreenParameters:(NSNotification *) notification
{
    cocoa_poll_monitors();
}

- (void)applicationWillFinishLaunching:(NSNotification *)notification
{
    if (g_wsi.hints.init.ns.menubar)
    {
        // Menu bar setup must go between sharedApplication and finishLaunching
        // in order to properly emulate the behavior of NSApplicationMain

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
        cocoa_RestoreVideoMode(g_wsi.monitors[i]);
}

@end // GLFWApplicationDelegate

int cocoa_init(void)
{
    @autoreleasepool {

    g_wsi.ns.helper = [[GLFWHelper alloc] init];

    [NSThread detachNewThreadSelector:@selector(doNothing:)
                             toTarget:g_wsi.ns.helper
                           withObject:nil];

    [NSApplication sharedApplication];

    g_wsi.ns.delegate = [[GLFWApplicationDelegate alloc] init];
    if (g_wsi.ns.delegate == nil)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "Cocoa: Failed to create application delegate");
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

void cocoa_terminate(void)
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

#endif // WSI_COCOA

