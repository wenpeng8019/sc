//
// android_platform.h — Android (NativeActivity) 后端声明
//
// 与 uikit（iOS）对照：二者都是「系统拥有主循环、事件推送、全屏单窗口」的移动端
// 后端。差异在驱动模型——iOS 由 UIApplicationMain 在主线程拥有 runloop、CADisplayLink
// 推帧；Android 无「app 对象 native 入口」，改由框架的 Java 外壳 NativeActivity 经
// ANativeActivity_onCreate（本后端提供）拉起，惯例另起「渲染线程」跑帧循环，UI 线程
// 的窗口/输入/生命周期回调经自建管道 + ALooper 转到渲染线程消费（trimmed
// android_native_app_glue）。帧由 AChoreographer（vsync）驱动，对位 CADisplayLink。
//
// 三层生命周期分工：
//   * tier A（进程/库级 init）：Application.onCreate → android_jni.c 的 nativeOnCreate
//     → sc_wsi_app_startup（幂等）。纯 NativeActivity 形态下则由 android_run 首次触发。
//   * tier B/C（窗口/帧级）：本后端。ANativeActivity_onCreate 建渲染线程 → sc_app_main
//     → wsi_app_run → android_run（渲染线程事件循环）。
//
// 图形上下文仍归 gpu 模块：本后端只把 ANativeWindow* 作原生句柄交付
// （sc_wsi_win_get_native_window），gpu 侧据此建 EGL/Vulkan surface。
//
#ifndef ANDROID_PLATFORM_H
#define ANDROID_PLATFORM_H

#include "../wsi.h"

#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/input.h>
#include <android/looper.h>
#include <android/configuration.h>
#include <android/choreographer.h>

#include <pthread.h>

#define WSI_ANDROID_WINDOW_STATE          android_window_t  android;
#define WSI_ANDROID_LIBRARY_WINDOW_STATE  android_library_t android;
#define WSI_ANDROID_MONITOR_STATE         android_monitor_t android;
// Android 无光标概念
#define WSI_ANDROID_CURSOR_STATE

// 每窗口状态（移动平台单窗口，但保持与 glfw 逐窗口模型一致）
typedef struct android_window_t
{
    ANativeWindow*  window;         // 框架交付的原生窗口（gpu 建 EGL/Vulkan surface）

    // Android 无「点/像素」之分，逻辑与帧缓冲同为像素；保留两组字段与桌面模型一致
    int             width, height;      // 逻辑（= 像素）
    int             fbWidth, fbHeight;  // 帧缓冲像素
    float           xscale, yscale;     // = densityDpi / 160
} android_window_t;

// 触摸事件（shell 形态下 UI 线程 → 渲染线程转发环的元素）
#define ANDROID_TOUCH_RING 64
typedef struct android_touch_ev
{
    int                 phase;
    int                 count;
    sc_wsi_touchpoint   points[SC_MAX_TOUCHPOINTS];
} android_touch_ev;

// 库级（全局）状态。渲染线程 ⇄ UI 线程的全部运行时管线均在此，且在
// ANativeActivity_onCreate 内「先 sc_wsi_app_startup 后填字段」，故不会被 startup
// 的 memset 抹掉（startup 幂等，二次调用不 memset）。
typedef struct android_library_t
{
    ANativeActivity*  activity;         // 框架 activity（UI 线程属主）
    AConfiguration*   config;           // 设备配置（density 等）
    ALooper*          looper;           // 渲染线程 looper
    AChoreographer*   choreographer;    // vsync 帧驱动

    struct window_t*  window;           // 当前唯一 wsi 窗口（供委托/查询定位）

    // UI 线程 ⇄ 渲染线程：命令管道 + 握手（对齐 android_native_app_glue）
    int               msgread, msgwrite;
    pthread_t         thread;
    pthread_mutex_t   mutex;
    pthread_cond_t    cond;

    // 「已被渲染线程采纳」的原生对象（与 pending 比较，握手同步用）
    ANativeWindow*    nativeWindow;
    AInputQueue*      inputQueue;
    // 「UI 线程待交付」的原生对象
    ANativeWindow*    pendingWindow;
    AInputQueue*      pendingInputQueue;

    bool              running;          // 渲染线程 looper 已就绪
    bool              destroyRequested; // onDestroy 已请求退出
    bool              destroyed;        // 渲染线程已退出
    bool              suspended;        // 未 resume（后台/失活）
    bool              focused;          // 窗口是否有焦点
    bool              frameScheduled;   // 是否已向 choreographer 挂帧回调
    bool              firstFrameLogged; // 首帧生命周期日志只打一次

    float             scale;            // 内容缩放（densityDpi/160）

    // ── view-tree 外壳（ScActivity + FrameLayout(SurfaceView + 控件)）────────
    // 纯 NativeActivity（pure-gpu）形态下以下字段全为 0/NULL/false。
    jobject           shellActivity;      // ScActivity 对象（global ref）
    jobject           uiRoot;             // FrameLayout（ui 控件挂载根，global ref）
    bool              usesShell;          // true = view-tree 外壳；false = 纯 gpu
    ANativeWindow*    shellNativeWindow;  // ANativeWindow_fromSurface 取得，需 release

    // 触摸转发环（shell：UI 线程 nativeOnTouch 入队，渲染线程 CMD_TOUCH 消费）
    pthread_mutex_t   touchMutex;
    android_touch_ev  touchRing[ANDROID_TOUCH_RING];
    int               touchHead, touchTail;
} android_library_t;

// 显示器（Android 主屏占位；无物理尺寸/多屏 API）
typedef struct android_monitor_t
{
    int               _unused;
} android_monitor_t;

//////////////////////////////////////////////////////////////////////////

bool android_connect(int platformID, platform_st* platform);

int  android_init(void);
void android_terminate(void);

// 帧驱动入口（渲染线程）：由 sc_app_main → wsi_app_run 调用，进入 ALooper 事件循环；
// 子系统就绪回调 after_startup（可 NULL），wsi 自建全屏窗口后回调 main_window_created
// 交付句柄，AChoreographer 每帧回调 on_frame，退出前回调 before_cleanup（可 NULL）。
int  android_run(sc_wsi_app_cb after_startup, sc_wsi_window_cb main_window_created,
                 sc_wsi_app_cb on_frame, sc_wsi_app_cb before_cleanup);

// 内部：窗口销毁（vtable destroyWindow）
void android_destroy_window(window_st* window);

// view-tree 外壳：向 com/sc/wsi/ScActivity 注册 native 方法（JNI_OnLoad 调用；
// 若无该类——纯 gpu NativeActivity 形态——安全跳过）。
int  wsi_android_shell_register(JNIEnv* env);

#endif // ANDROID_PLATFORM_H
