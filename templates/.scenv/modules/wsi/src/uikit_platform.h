//
// uikit_platform.h — iOS (UIKit) 后端声明
//
// 移动平台特性（相对桌面 Cocoa 后端）：
//   * 窗口恒为全屏、不可调整大小、无装饰、无光标 —— 绝大多数 window/cursor/monitor
//     vtable 项退化为 no-op 或固定值（参考 null_platform）。
//   * 生命周期由系统驱动（UIApplicationMain 拥有主循环），通过 CADisplayLink 推送帧、
//     通过 Scene/App 委托推送挂起/恢复 —— 对应 impl_on_suspend/impl_on_resume。
//   * 触摸输入经 UIView 的 touchesBegan/Moved/Ended/Cancelled 转 impl_on_touch。
//   * 图形上下文仍由 gpu 模块负责：本后端只把 UIView（其 layerClass = CAMetalLayer）
//     作为原生句柄交付（sc_wsi_win_get_native_window）。
//
#ifndef UIKIT_PLATFORM_H
#define UIKIT_PLATFORM_H

#include "../wsi.h"

#if defined(__OBJC__)
#import <UIKit/UIKit.h>
#else
typedef void* id;
#endif

#define WSI_IOS_WINDOW_STATE          ios_window_t  ios;
#define WSI_IOS_LIBRARY_WINDOW_STATE  ios_library_t ios;
#define WSI_IOS_MONITOR_STATE         ios_monitor_t ios;
// iOS 无光标概念
#define WSI_IOS_CURSOR_STATE

// 每窗口状态（移动平台单窗口，但保持与 glfw 逐窗口模型一致）
typedef struct ios_window_t
{
    id          window;         // UIWindow*
    id          view;           // SC_iOSView*（layerClass = CAMetalLayer）
    id          view_ctrl;      // UIViewController*

    // 缓存的尺寸，用于过滤重复的 size/scale 事件
    int         width, height;      // 逻辑点
    int         fbWidth, fbHeight;  // 帧缓冲像素
    float       xscale, yscale;     // = nativeScale
} ios_window_t;

// 库级（全局）状态
typedef struct ios_library_t
{
    id          app_delegate;   // SC_iOSAppDelegate*
    id          display_link;   // CADisplayLink*
    id          text_field;     // 隐藏 UITextField*（软键盘文本输入，可选）
    id          text_field_dlg; // UITextFieldDelegate*

    struct window_t*  window;         // 当前唯一窗口（供委托回调定位）

    bool        suspended;      // 应用是否处于后台/失活
    bool        started;        // 首个 scene 是否已连接

    // run-loop 帧回调（path A：库拥有循环，CADisplayLink 每帧调用）
    void        (*frame_cb)(void);
    void        (*init_cb)(void);
    void        (*cleanup_cb)(void);
} ios_library_t;

// 显示器（iOS 只有主屏，信息取自 UIScreen.mainScreen）
typedef struct ios_monitor_t
{
    id          screen;         // UIScreen*
} ios_monitor_t;

//////////////////////////////////////////////////////////////////////////

bool uikit_connect(int platformID, platform_st* platform);

int  uikit_init(void);
void uikit_terminate(void);

// 事件泵：iOS 事件由系统推送，poll/wait 为退化实现（run-loop 抽干由 UIKit 负责）
void uikit_poll_events(void);
void uikit_wait_events(void);
void uikit_wait_events_timeout(double timeout);
void uikit_post_empty_event(void);

// 帧驱动入口（path A 原型）：启动 UIApplicationMain，永不返回；
// 子系统就绪后回调 after_startup（可 NULL），wsi 自建全屏窗口后回调 main_window_created
// 交付句柄，CADisplayLink 每帧回调 on_frame，应用终止前回调 before_cleanup（可 NULL）。
int  uikit_run(void (*after_startup)(void), void (*main_window_created)(sc_window*),
               void (*on_frame)(void), void (*before_cleanup)(void));

// 内部：CADisplayLink 帧回调派发（.m 中定义）
void uikit_frame_tick(void);

#endif // UIKIT_PLATFORM_H
