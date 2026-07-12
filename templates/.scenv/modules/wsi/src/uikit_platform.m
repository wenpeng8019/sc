//
// uikit_platform.m — iOS (UIKit) 后端实现
//
// 采用 MRC（手动 retain/release，[ios] 段注入 -fno-objc-arc），与 cocoa 后端一致，
// 以便在 C 结构体中存放 id 句柄。
//
// 循环模型（path A 原型）：UIApplicationMain 拥有主循环，CADisplayLink 每帧回调
// frame_cb；生命周期委托推送 suspend/resume。桌面 pull 循环（poll+render）在此不适用，
// poll/wait 事件为退化 no-op。
//
#include "internal.h"

#if defined(WSI_IOS)

#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>

///////////////////////////////////////////////////////////////////////////////
// 前置声明
///////////////////////////////////////////////////////////////////////////////

@interface SC_iOSView : UIView
@end

@interface SC_iOSAppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow* keepWindow;   // MRC 下强引用保活系统窗口
@end

static void ios_start_display_link(void);
static void ios_stop_display_link(void);

// app 回调（在 uikit_run 中设置）。存于文件静态而非 g_wsi.ios：uikit_run 先存
// 回调再进 UIApplicationMain，而 didFinishLaunching 内的 sc_wsi_app_startup 会清零 g_wsi，
// 若存在 g_wsi.ios 会被抹掉。文件静态不随 g_wsi 重置而丢失。
static void (*s_ios_after_startup_cb)(void)          = NULL;
static void (*s_ios_main_window_created_cb)(sc_window*) = NULL;
static void (*s_ios_frame_cb)(void)                  = NULL;
static void (*s_ios_before_cleanup_cb)(void)         = NULL;

///////////////////////////////////////////////////////////////////////////////
// 显示器（iOS 仅主屏）
///////////////////////////////////////////////////////////////////////////////

static void uikit_poll_monitors(void)
{
    UIScreen* screen = [UIScreen mainScreen];
    CGRect bounds = screen.bounds;
    const float scale = (float) screen.nativeScale;
    // iOS 无物理毫米尺寸 API，按 ~163ppi（经典）粗略换算，仅用于占位
    const float ppi = 163.f * scale;
    monitor_st* monitor = wsi_alloc_monitor(
        "iOS Main Display",
        (int) (bounds.size.width  * scale * 25.4f / ppi),
        (int) (bounds.size.height * scale * 25.4f / ppi));
    monitor->ios.screen = [screen retain];
    impl_on_monitor(monitor, SC_CONNECTED, WSI_INSERT_FIRST);
}

void uikit_free_monitor(monitor_st* monitor)
{
    if (monitor->ios.screen)
    {
        [(UIScreen*) monitor->ios.screen release];
        monitor->ios.screen = nil;
    }
}

void uikit_get_monitor_pos(monitor_st* monitor, int* xpos, int* ypos)
{
    if (xpos) *xpos = 0;
    if (ypos) *ypos = 0;
}

void uikit_get_monitor_content_scale(monitor_st* monitor, float* xscale, float* yscale)
{
    const float s = (float) [UIScreen mainScreen].nativeScale;
    if (xscale) *xscale = s;
    if (yscale) *yscale = s;
}

void uikit_get_monitor_work_area(monitor_st* monitor, int* xpos, int* ypos, int* width, int* height)
{
    CGRect b = [UIScreen mainScreen].bounds;
    if (xpos)   *xpos = 0;
    if (ypos)   *ypos = 0;
    if (width)  *width  = (int) b.size.width;
    if (height) *height = (int) b.size.height;
}

static sc_wsi_video_mode uikit_video_mode(void)
{
    UIScreen* screen = [UIScreen mainScreen];
    CGRect b = screen.bounds;
    sc_wsi_video_mode mode;
    mode.width  = (int) b.size.width;
    mode.height = (int) b.size.height;
    mode.refreshRate = (int) screen.maximumFramesPerSecond;
    if (mode.refreshRate <= 0)
        mode.refreshRate = 60;
    return mode;
}

sc_wsi_video_mode* uikit_get_video_modes(monitor_st* monitor, int* found)
{
    sc_wsi_video_mode* mode = wsi_calloc(1, sizeof(sc_wsi_video_mode));
    *mode = uikit_video_mode();
    *found = 1;
    return mode;
}

bool uikit_get_video_mode(monitor_st* monitor, sc_wsi_video_mode* mode)
{
    *mode = uikit_video_mode();
    return true;
}

bool uikit_get_gamma_ramp(monitor_st* monitor, sc_wsi_gamma_ramp* ramp)
{
    // iOS 无伽马斜坡 API
    return false;
}

void uikit_set_gamma_ramp(monitor_st* monitor, const sc_wsi_gamma_ramp* ramp)
{
    // 不支持
}

///////////////////////////////////////////////////////////////////////////////
// 触摸转发
///////////////////////////////////////////////////////////////////////////////

static void ios_handle_touches(int phase, NSSet<UITouch*>* changed, UIEvent* event)
{
    window_st* window = g_wsi.ios.window;
    if (!window || !window->ios.view)
        return;

    UIView* view = (UIView*) window->ios.view;
    const float scale = window->ios.xscale;

    sc_wsi_touchpoint points[SC_MAX_TOUCHPOINTS];
    int count = 0;

    for (UITouch* t in event.allTouches)
    {
        if (count >= SC_MAX_TOUCHPOINTS)
            break;
        CGPoint p = [t locationInView:view];
        sc_wsi_touchpoint* dst = &points[count++];
        dst->identifier = (uint64_t) (uintptr_t) t;
        dst->x          = (float) p.x * scale;
        dst->y          = (float) p.y * scale;
        dst->tooltype   = SC_TOOLTYPE_FINGER;
        dst->changed    = [changed containsObject:t] ? true : false;
    }

    if (count > 0)
        impl_on_touch(window, phase, count, points);
}

///////////////////////////////////////////////////////////////////////////////
// 内容视图（layer = CAMetalLayer，供 gpu 建 Metal surface）
///////////////////////////////////////////////////////////////////////////////

@implementation SC_iOSView

+ (Class) layerClass
{
    return [CAMetalLayer class];
}

- (BOOL) canBecomeFirstResponder { return YES; }

- (void) touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    ios_handle_touches(SC_TOUCH_BEGAN, touches, event);
}
- (void) touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    ios_handle_touches(SC_TOUCH_MOVED, touches, event);
}
- (void) touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    ios_handle_touches(SC_TOUCH_ENDED, touches, event);
}
- (void) touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    ios_handle_touches(SC_TOUCH_CANCELLED, touches, event);
}

// 布局/旋转后尺寸变化：更新缓存并派发 size/scale 事件
- (void) layoutSubviews
{
    [super layoutSubviews];

    window_st* window = g_wsi.ios.window;
    if (!window || window->ios.view != self)
        return;

    const CGSize sz = self.bounds.size;
    const float scale = (float) self.contentScaleFactor;
    const int w = (int) sz.width;
    const int h = (int) sz.height;
    const int fbw = (int) (sz.width  * scale);
    const int fbh = (int) (sz.height * scale);

    // 保持 CAMetalLayer 的 drawableSize 与帧缓冲一致
    CAMetalLayer* layer = (CAMetalLayer*) self.layer;
    layer.contentsScale = scale;
    layer.drawableSize  = CGSizeMake(fbw, fbh);

    if (window->ios.xscale != scale)
    {
        window->ios.xscale = scale;
        window->ios.yscale = scale;
        impl_on_win_content_scale(window, scale, scale);
    }
    if (window->ios.width != w || window->ios.height != h ||
        window->ios.fbWidth != fbw || window->ios.fbHeight != fbh)
    {
        window->ios.width    = w;
        window->ios.height   = h;
        window->ios.fbWidth  = fbw;
        window->ios.fbHeight = fbh;
        impl_on_win_size(window, w, h);
        impl_on_win_damage(window);
    }
}

@end

///////////////////////////////////////////////////////////////////////////////
// CADisplayLink 帧驱动
///////////////////////////////////////////////////////////////////////////////

@interface SC_iOSFrameTarget : NSObject
- (void) displayLinkFired:(CADisplayLink*)link;
@end

@implementation SC_iOSFrameTarget
- (void) displayLinkFired:(CADisplayLink*)link
{
    uikit_frame_tick();
}
@end

static SC_iOSFrameTarget* g_frame_target = nil;

static void ios_start_display_link(void)
{
    if (g_wsi.ios.display_link)
        return;
    if (!g_frame_target)
        g_frame_target = [[SC_iOSFrameTarget alloc] init];

    CADisplayLink* link = [CADisplayLink displayLinkWithTarget:g_frame_target
                                                      selector:@selector(displayLinkFired:)];
    [link addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSRunLoopCommonModes];
    g_wsi.ios.display_link = [link retain];
}

static void ios_stop_display_link(void)
{
    if (g_wsi.ios.display_link)
    {
        CADisplayLink* link = (CADisplayLink*) g_wsi.ios.display_link;
        [link invalidate];
        [link release];
        g_wsi.ios.display_link = nil;
    }
}

void uikit_frame_tick(void)
{
    if (g_wsi.ios.suspended)
        return;
    if (s_ios_frame_cb)
        s_ios_frame_cb();
}

///////////////////////////////////////////////////////////////////////////////
// 应用委托（生命周期）
///////////////////////////////////////////////////////////////////////////////

@implementation SC_iOSAppDelegate

- (BOOL) application:(UIApplication*)application
        didFinishLaunchingWithOptions:(NSDictionary*)launchOptions
{
    // 记录委托，供 g_wsi.ios 引用
    g_wsi.ios.app_delegate = self;

    // 初始化 wsi（选择 iOS 平台）。注意：这会清零 g_wsi，故 app 回调存于文件静态。
    if (!sc_wsi_app_startup())
        return NO;

    // 子系统就绪后、建窗前回调一次（可 NULL）。
    if (s_ios_after_startup_cb)
        s_ios_after_startup_cb();

    // 移动端窗口恒全屏、由 wsi 内部自建：不经公共 sc_wsi_win_create（iOS 不暴露建窗），
    // 直接分配窗口对象（登记入窗口表；尺寸对 iOS 无意义，传 1×1 占位过校验）后内联铺满。
    window_st* window = wsi_alloc_window(1, 1, "");
    if (window)
    {
        UIScreen* screen = [UIScreen mainScreen];
        CGRect bounds = screen.bounds;
        const float scale = (float) screen.nativeScale;

        UIWindow* uiwin = [[UIWindow alloc] initWithFrame:bounds];

        SC_iOSView* view = [[SC_iOSView alloc] initWithFrame:bounds];
        view.contentScaleFactor = scale;
        view.multipleTouchEnabled = YES;
        view.userInteractionEnabled = YES;

        CAMetalLayer* layer = (CAMetalLayer*) view.layer;
        layer.opaque = YES;
        layer.framebufferOnly = YES;
        layer.contentsScale = scale;
        layer.drawableSize = CGSizeMake(bounds.size.width * scale, bounds.size.height * scale);

        UIViewController* vc = [[UIViewController alloc] init];
        vc.view = view;
        uiwin.rootViewController = vc;

        window->ios.window    = uiwin;   // 已 +1 拥有
        window->ios.view      = view;    // 已 +1 拥有
        window->ios.view_ctrl = vc;      // 已 +1 拥有
        window->ios.width    = (int) bounds.size.width;
        window->ios.height   = (int) bounds.size.height;
        window->ios.fbWidth  = (int) (bounds.size.width  * scale);
        window->ios.fbHeight = (int) (bounds.size.height * scale);
        window->ios.xscale   = scale;
        window->ios.yscale   = scale;

        // 移动平台单窗口：登记为当前窗口
        g_wsi.ios.window = window;
        [uiwin makeKeyAndVisible];
    }

    // 交给应用：拿到主窗口 → 初始化 gpu/gfx + 注册回调
    sc_window* win = (sc_window*) window;
    if (win && s_ios_main_window_created_cb)
        s_ios_main_window_created_cb(win);

    // 保活系统窗口（MRC）
    if (g_wsi.ios.window)
        self.keepWindow = (UIWindow*) g_wsi.ios.window->ios.window;

    // 启动逐帧驱动
    ios_start_display_link();
    return YES;
}

- (void) applicationWillResignActive:(UIApplication*)application
{
    g_wsi.ios.suspended = true;
    if (g_wsi.ios.window)
        impl_on_suspend(g_wsi.ios.window);
    if (g_wsi.ios.display_link)
        ((CADisplayLink*) g_wsi.ios.display_link).paused = YES;
}

- (void) applicationDidBecomeActive:(UIApplication*)application
{
    g_wsi.ios.suspended = false;
    if (g_wsi.ios.display_link)
        ((CADisplayLink*) g_wsi.ios.display_link).paused = NO;
    if (g_wsi.ios.window)
        impl_on_resume(g_wsi.ios.window);
}

- (void) applicationWillTerminate:(UIApplication*)application
{
    if (s_ios_before_cleanup_cb)
        s_ios_before_cleanup_cb();
    ios_stop_display_link();
}

@end

///////////////////////////////////////////////////////////////////////////////
// 平台初始化 / 终止
///////////////////////////////////////////////////////////////////////////////

int uikit_init(void)
{
    uikit_poll_monitors();
    return true;
}

void uikit_terminate(void)
{
    ios_stop_display_link();
    if (g_frame_target)
    {
        [g_frame_target release];
        g_frame_target = nil;
    }
    memset(&g_wsi.ios, 0, sizeof(g_wsi.ios));
}

///////////////////////////////////////////////////////////////////////////////
// 事件泵（iOS 由 UIKit 推送，退化实现）
///////////////////////////////////////////////////////////////////////////////

void uikit_poll_events(void)          { /* UIKit 拥有 runloop，无需手动泵 */ }
void uikit_wait_events(void)          { }
void uikit_wait_events_timeout(double timeout) { (void) timeout; }
void uikit_post_empty_event(void)     { }

///////////////////////////////////////////////////////////////////////////////
// 窗口销毁（创建为 wsi 内部行为，见 didFinishLaunching 内联，不经 vtable/公共 API）
///////////////////////////////////////////////////////////////////////////////

void uikit_destroy_window(window_st* window)
{
    if (g_wsi.ios.window == window)
        g_wsi.ios.window = NULL;

    if (window->ios.view_ctrl)
    {
        [(UIViewController*) window->ios.view_ctrl release];
        window->ios.view_ctrl = nil;
    }
    if (window->ios.view)
    {
        [(SC_iOSView*) window->ios.view release];
        window->ios.view = nil;
    }
    if (window->ios.window)
    {
        UIWindow* uiwin = (UIWindow*) window->ios.window;
        uiwin.hidden = YES;
        [uiwin release];
        window->ios.window = nil;
    }
}

///////////////////////////////////////////////////////////////////////////////
// 窗口属性查询（真实值） / 其余为 no-op（全屏、不可调、无装饰）
///////////////////////////////////////////////////////////////////////////////

void uikit_get_window_size(window_st* window, int* width, int* height)
{
    if (width)  *width  = window->ios.width;
    if (height) *height = window->ios.height;
}

void uikit_get_framebuffer_size(window_st* window, int* width, int* height)
{
    if (width)  *width  = window->ios.fbWidth;
    if (height) *height = window->ios.fbHeight;
}

void uikit_get_window_content_scale(window_st* window, float* xscale, float* yscale)
{
    if (xscale) *xscale = window->ios.xscale;
    if (yscale) *yscale = window->ios.yscale;
}

void uikit_get_window_pos(window_st* window, int* xpos, int* ypos)
{
    if (xpos) *xpos = 0;
    if (ypos) *ypos = 0;
}

void uikit_get_window_frame_size(window_st* window, int* left, int* top, int* right, int* bottom)
{
    if (left)   *left = 0;
    if (top)    *top = 0;
    if (right)  *right = 0;
    if (bottom) *bottom = 0;
}

bool uikit_window_focused(window_st* window)   { return !g_wsi.ios.suspended; }
bool uikit_window_iconified(window_st* window) { return g_wsi.ios.suspended; }
bool uikit_window_visible(window_st* window)   { return true; }
bool uikit_window_maximized(window_st* window) { return true; }
bool uikit_window_hovered(window_st* window)   { return false; }
float uikit_get_window_opacity(window_st* window) { return 1.f; }

// 全屏不可变：以下窗口操作在 iOS 上均为 no-op
void uikit_set_window_title(window_st* window, const char* title) { (void) title; }
void uikit_set_window_icon(window_st* window, int count, const sc_wsi_img* images) { }
void uikit_set_window_pos(window_st* window, int xpos, int ypos) { }
void uikit_set_window_size(window_st* window, int width, int height) { }
void uikit_set_window_size_limits(window_st* window, int minw, int minh, int maxw, int maxh) { }
void uikit_set_window_aspect_ratio(window_st* window, int numer, int denom) { }
void uikit_set_window_monitor(window_st* window, monitor_st* monitor, int xpos, int ypos, int width, int height, int refreshRate) { }
void uikit_set_window_resizable(window_st* window, bool enabled) { }
void uikit_set_window_decorated(window_st* window, bool enabled) { }
void uikit_set_window_floating(window_st* window, bool enabled) { }
void uikit_set_window_opacity(window_st* window, float opacity) { }
void uikit_set_window_mouse_passthrough(window_st* window, bool enabled) { }
void uikit_iconify_window(window_st* window) { }
void uikit_restore_window(window_st* window) { }
void uikit_maximize_window(window_st* window) { }
void uikit_show_window(window_st* window) { }
void uikit_hide_window(window_st* window) { }
void uikit_request_window_attention(window_st* window) { }
void uikit_focus_window(window_st* window) { }

///////////////////////////////////////////////////////////////////////////////
// 光标 / 剪贴板（iOS 无光标；剪贴板暂不接入）
///////////////////////////////////////////////////////////////////////////////

void uikit_get_cursor_pos(window_st* window, double* xpos, double* ypos)
{
    if (xpos) *xpos = 0.0;
    if (ypos) *ypos = 0.0;
}
void uikit_set_cursor_pos(window_st* window, double xpos, double ypos) { }
void uikit_set_cursor_mode(window_st* window, int mode) { }
void uikit_set_cursor(window_st* window, cursor_st* cursor) { }
bool uikit_create_cursor(cursor_st* cursor, const sc_wsi_img* image, int xhot, int yhot) { return false; }
bool uikit_create_standard_cursor(cursor_st* cursor, int shape) { return false; }
void uikit_destroy_cursor(cursor_st* cursor) { }
void uikit_set_raw_mouse_motion(window_st* window, bool enabled) { }
bool uikit_raw_mouse_motion_supported(void) { return false; }
const char* uikit_get_scancode_name(int scancode) { return ""; }
int uikit_get_key_scancode(int key) { return -1; }
void uikit_set_clipboard_string(const char* string) { }
const char* uikit_get_clipboard_string(void) { return ""; }

///////////////////////////////////////////////////////////////////////////////
// 帧驱动入口（path A 原型）
///////////////////////////////////////////////////////////////////////////////

int uikit_run(void (*after_startup)(void), void (*main_window_created)(sc_window*),
              void (*on_frame)(void), void (*before_cleanup)(void))
{
    s_ios_after_startup_cb       = after_startup;
    s_ios_main_window_created_cb = main_window_created;
    s_ios_frame_cb               = on_frame;
    s_ios_before_cleanup_cb      = before_cleanup;

    @autoreleasepool {
        // UIApplicationMain 的 argv 标注为 _Nonnull；提供占位数组避免传 NULL 告警。
        char  arg0[] = "";
        char* argv[] = { arg0, NULL };
        return UIApplicationMain(1, argv, nil, NSStringFromClass([SC_iOSAppDelegate class]));
    }
}

///////////////////////////////////////////////////////////////////////////////
// vtable 装配
///////////////////////////////////////////////////////////////////////////////

bool uikit_connect(int platformID, platform_st* platform)
{
    const platform_st uikit =
    {
        .platformID = SC_PLATFORM_IOS,
        .init = uikit_init,
        .terminate = uikit_terminate,

        .pollEvents                 = uikit_poll_events,
        .waitEvents                 = uikit_wait_events,
        .waitEventsTimeout          = uikit_wait_events_timeout,
        .postEmptyEvent             = uikit_post_empty_event,

        .destroyWindow              = uikit_destroy_window,
        .setWindowTitle             = uikit_set_window_title,
        .setWindowIcon              = uikit_set_window_icon,
        .setWindowMonitor           = uikit_set_window_monitor,
        .setWindowMousePassthrough  = uikit_set_window_mouse_passthrough,

        .setWindowDecorated         = uikit_set_window_decorated,
        .setWindowResizable         = uikit_set_window_resizable,
        .setWindowFloating          = uikit_set_window_floating,
        .setWindowOpacity           = uikit_set_window_opacity,
        .getWindowOpacity           = uikit_get_window_opacity,

        .getWindowPos               = uikit_get_window_pos,
        .setWindowPos               = uikit_set_window_pos,
        .getWindowSize              = uikit_get_window_size,
        .getFramebufferSize         = uikit_get_framebuffer_size,
        .setWindowSize              = uikit_set_window_size,
        .getWindowFrameSize         = uikit_get_window_frame_size,
        .setWindowSizeLimits        = uikit_set_window_size_limits,
        .getWindowContentScale      = uikit_get_window_content_scale,
        .setWindowAspectRatio       = uikit_set_window_aspect_ratio,

        .showWindow                 = uikit_show_window,
        .hideWindow                 = uikit_hide_window,
        .maximizeWindow             = uikit_maximize_window,
        .restoreWindow              = uikit_restore_window,
        .focusWindow                = uikit_focus_window,
        .iconifyWindow              = uikit_iconify_window,
        .requestWindowAttention     = uikit_request_window_attention,

        .windowVisible              = uikit_window_visible,
        .windowMaximized            = uikit_window_maximized,
        .windowFocused              = uikit_window_focused,
        .windowHovered              = uikit_window_hovered,
        .windowIconified            = uikit_window_iconified,

        .setCursor                  = uikit_set_cursor,
        .createStandardCursor       = uikit_create_standard_cursor,
        .createCursor               = uikit_create_cursor,
        .destroyCursor              = uikit_destroy_cursor,
        .setCursorMode              = uikit_set_cursor_mode,
        .setCursorPos               = uikit_set_cursor_pos,
        .getCursorPos               = uikit_get_cursor_pos,
        .setRawMouseMotion          = uikit_set_raw_mouse_motion,
        .rawMouseMotionSupported    = uikit_raw_mouse_motion_supported,

        .getKeyScancode             = uikit_get_key_scancode,
        .getScancodeName            = uikit_get_scancode_name,
        .getClipboardString         = uikit_get_clipboard_string,
        .setClipboardString         = uikit_set_clipboard_string,

        .freeMonitor                = uikit_free_monitor,
        .getMonitorPos              = uikit_get_monitor_pos,
        .getMonitorWorkarea         = uikit_get_monitor_work_area,
        .getMonitorContentScale     = uikit_get_monitor_content_scale,
        .getVideoModes              = uikit_get_video_modes,
        .getVideoMode               = uikit_get_video_mode,
        .getGammaRamp               = uikit_get_gamma_ramp,
        .setGammaRamp               = uikit_set_gamma_ramp,
    };

    *platform = uikit;
    return true;
}

#endif // WSI_IOS
