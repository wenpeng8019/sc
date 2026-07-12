//
// android_platform.c — Android (NativeActivity) 后端实现
//
// 循环模型（对照 uikit 的 UIApplicationMain）：框架 Java 外壳 NativeActivity 经 JNI
// 调 ANativeActivity_onCreate（本文件导出）；onCreate 在 UI 线程建「渲染线程」并挂上
// ANativeActivity 各回调，随后返回。渲染线程跑 app 逻辑入口 sc_app_main → wsi_app_run
// → android_run（本文件的 ALooper 事件循环）。UI 线程的窗口/输入/生命周期回调经自建
// 管道（msgread/msgwrite）+ 握手（mutex/cond）转到渲染线程消费——这是 trimmed 版
// android_native_app_glue。帧由 AChoreographer（vsync）驱动，对位 iOS 的 CADisplayLink。
//
// 与 android_jni.c（tier A 进程/库级 JNI 桥）的关系：二者共处同一 .so。JNI_OnLoad
// 只能有一个——本文件不定义 JNI_OnLoad（NDK 的 ANativeActivity/ALooper/AInputQueue/
// AChoreographer 均为纯 C API，无需 JNI）；进程级 init 由 android_jni.c 触发，本文件
// 的 android_run 再幂等调 sc_wsi_app_startup（纯 NativeActivity 无 Application 形态下
// 由它首次触发）。
//
// 整文件由 WSI_ANDROID 守卫，非 android 目标编译为空翻译单元（可安全随全量 src/*
// 一起编）。
//
#include "internal.h"

#if defined(WSI_ANDROID)

#include <android/log.h>

#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#define WSI_ATAG "sc.wsi"
#define WSI_ALOGI(...) __android_log_print(ANDROID_LOG_INFO, WSI_ATAG, __VA_ARGS__)
#define WSI_ALOGW(...) __android_log_print(ANDROID_LOG_WARN, WSI_ATAG, __VA_ARGS__)

// ALooper fd 标识（渲染线程）
enum {
    ANDROID_LOOPER_ID_MAIN  = 1,   // UI→渲染 命令管道
    ANDROID_LOOPER_ID_INPUT = 2,   // AInputQueue
};

// UI 线程 → 渲染线程命令
enum {
    ANDROID_CMD_INIT_WINDOW = 1,   // pendingWindow 交付（新 ANativeWindow）
    ANDROID_CMD_TERM_WINDOW,       // pendingWindow 撤回（ANativeWindow 即将失效）
    ANDROID_CMD_WINDOW_RESIZED,    // 尺寸/朝向变化
    ANDROID_CMD_WINDOW_REDRAW,     // 需重绘
    ANDROID_CMD_INPUT_CHANGED,     // pendingInputQueue 变化
    ANDROID_CMD_START,
    ANDROID_CMD_RESUME,
    ANDROID_CMD_PAUSE,
    ANDROID_CMD_STOP,
    ANDROID_CMD_FOCUS_GAINED,
    ANDROID_CMD_FOCUS_LOST,
    ANDROID_CMD_CONFIG_CHANGED,
    ANDROID_CMD_DESTROY,
};

// app 4 回调（存文件静态：sc_wsi_app_startup 会 memset g_wsi，回调不放 g_wsi.android）
static sc_wsi_app_cb    s_after_startup       = NULL;
static sc_wsi_window_cb s_main_window_created = NULL;
static sc_wsi_app_cb    s_on_frame            = NULL;
static sc_wsi_app_cb    s_before_cleanup      = NULL;

// app 逻辑入口（fnc app_main → C 符号 sc_app_main）；由渲染线程调用
extern int sc_app_main(void);

// 前置声明
static void android_setup_window(void);
static void android_teardown_window(void);
static void android_schedule_frame(void);

///////////////////////////////////////////////////////////////////////////////
// UI 线程侧：命令写入 + 握手
///////////////////////////////////////////////////////////////////////////////

static void android_write_cmd(int8_t cmd)
{
    if (write(g_wsi.android.msgwrite, &cmd, sizeof(cmd)) != sizeof(cmd))
        WSI_ALOGW("写命令失败: %s", strerror(errno));
}

// 交付/撤回原生窗口（UI 线程调用，阻塞至渲染线程采纳——ANativeWindow 撤回后立即失效，
// 必须等渲染线程停用后再返回，见 glue 的 window 握手）。
static void android_set_window(ANativeWindow* window)
{
    pthread_mutex_lock(&g_wsi.android.mutex);
    g_wsi.android.pendingWindow = window;
    android_write_cmd(window ? ANDROID_CMD_INIT_WINDOW : ANDROID_CMD_TERM_WINDOW);
    while (g_wsi.android.nativeWindow != g_wsi.android.pendingWindow)
        pthread_cond_wait(&g_wsi.android.cond, &g_wsi.android.mutex);
    pthread_mutex_unlock(&g_wsi.android.mutex);
}

// 交付/撤回输入队列（同上握手：撤回须等渲染线程 detach 后再返回）。
static void android_set_input(AInputQueue* queue)
{
    pthread_mutex_lock(&g_wsi.android.mutex);
    g_wsi.android.pendingInputQueue = queue;
    android_write_cmd(ANDROID_CMD_INPUT_CHANGED);
    while (g_wsi.android.inputQueue != g_wsi.android.pendingInputQueue)
        pthread_cond_wait(&g_wsi.android.cond, &g_wsi.android.mutex);
    pthread_mutex_unlock(&g_wsi.android.mutex);
}

// 请求销毁（UI 线程 onDestroy 调用，阻塞至渲染线程退出——ANativeActivity 随后被框架
// 释放，渲染线程须先停用其一切资源）。
static void android_set_destroy(void)
{
    pthread_mutex_lock(&g_wsi.android.mutex);
    g_wsi.android.destroyRequested = true;
    android_write_cmd(ANDROID_CMD_DESTROY);
    while (!g_wsi.android.destroyed)
        pthread_cond_wait(&g_wsi.android.cond, &g_wsi.android.mutex);
    pthread_mutex_unlock(&g_wsi.android.mutex);
}

///////////////////////////////////////////////////////////////////////////////
// 显示器（Android 主屏占位；无物理尺寸/多屏/伽马 API）
///////////////////////////////////////////////////////////////////////////////

static void android_poll_monitors(void)
{
    monitor_st* monitor = wsi_alloc_monitor("Android Display", 0, 0);
    impl_on_monitor(monitor, SC_CONNECTED, WSI_INSERT_FIRST);
}

void android_free_monitor(monitor_st* monitor) { (void) monitor; }

void android_get_monitor_pos(monitor_st* monitor, int* xpos, int* ypos)
{
    (void) monitor;
    if (xpos) *xpos = 0;
    if (ypos) *ypos = 0;
}

void android_get_monitor_content_scale(monitor_st* monitor, float* xscale, float* yscale)
{
    (void) monitor;
    const float s = g_wsi.android.scale > 0.f ? g_wsi.android.scale : 1.f;
    if (xscale) *xscale = s;
    if (yscale) *yscale = s;
}

void android_get_monitor_work_area(monitor_st* monitor, int* xpos, int* ypos, int* width, int* height)
{
    (void) monitor;
    int w = 0, h = 0;
    if (g_wsi.android.nativeWindow)
    {
        w = ANativeWindow_getWidth(g_wsi.android.nativeWindow);
        h = ANativeWindow_getHeight(g_wsi.android.nativeWindow);
    }
    if (xpos)   *xpos = 0;
    if (ypos)   *ypos = 0;
    if (width)  *width  = w;
    if (height) *height = h;
}

static sc_wsi_video_mode android_video_mode(void)
{
    sc_wsi_video_mode mode;
    memset(&mode, 0, sizeof(mode));
    if (g_wsi.android.nativeWindow)
    {
        mode.width  = ANativeWindow_getWidth(g_wsi.android.nativeWindow);
        mode.height = ANativeWindow_getHeight(g_wsi.android.nativeWindow);
    }
    mode.refreshRate = 60;
    return mode;
}

sc_wsi_video_mode* android_get_video_modes(monitor_st* monitor, int* found)
{
    (void) monitor;
    sc_wsi_video_mode* mode = wsi_calloc(1, sizeof(sc_wsi_video_mode));
    *mode = android_video_mode();
    *found = 1;
    return mode;
}

bool android_get_video_mode(monitor_st* monitor, sc_wsi_video_mode* mode)
{
    (void) monitor;
    *mode = android_video_mode();
    return true;
}

bool android_get_gamma_ramp(monitor_st* monitor, sc_wsi_gamma_ramp* ramp)
{
    (void) monitor; (void) ramp;
    return false;   // Android 无伽马斜坡 API
}

void android_set_gamma_ramp(monitor_st* monitor, const sc_wsi_gamma_ramp* ramp)
{
    (void) monitor; (void) ramp;   // 不支持
}

///////////////////////////////////////////////////////////////////////////////
// 触摸输入转发（AInputQueue → impl_on_touch）
///////////////////////////////////////////////////////////////////////////////

static int android_handle_motion(const AInputEvent* event)
{
    window_st* window = g_wsi.android.window;
    if (!window)
        return 0;

    const int32_t action = AMotionEvent_getAction(event);
    const int32_t masked = action & AMOTION_EVENT_ACTION_MASK;

    int phase;
    switch (masked)
    {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN: phase = SC_TOUCH_BEGAN;     break;
        case AMOTION_EVENT_ACTION_MOVE:         phase = SC_TOUCH_MOVED;     break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:   phase = SC_TOUCH_ENDED;     break;
        case AMOTION_EVENT_ACTION_CANCEL:       phase = SC_TOUCH_CANCELLED; break;
        default: return 0;
    }

    // DOWN/UP 只有一个触点「改变」（由 action 的 pointer index 指出）；MOVE 全部改变。
    const int changedIdx =
        (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
            >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

    const size_t total = AMotionEvent_getPointerCount(event);
    sc_wsi_touchpoint points[SC_MAX_TOUCHPOINTS];
    int count = 0;

    for (size_t i = 0; i < total && count < SC_MAX_TOUCHPOINTS; i++)
    {
        sc_wsi_touchpoint* dst = &points[count];
        dst->identifier = (uint64_t) AMotionEvent_getPointerId(event, i);
        dst->x          = AMotionEvent_getX(event, i);   // 已是帧缓冲像素
        dst->y          = AMotionEvent_getY(event, i);
        dst->tooltype   = SC_TOOLTYPE_FINGER;
        dst->changed    = (phase == SC_TOUCH_MOVED) ? true : ((int) i == changedIdx);
        count++;
    }

    if (count > 0)
        impl_on_touch(window, phase, count, points);
    return 1;
}

// 返回是否消费该事件（1 = 已处理；0 = 交回系统，如 BACK 键）。
static int android_handle_input(const AInputEvent* event)
{
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION)
        return android_handle_motion(event);
    // 键盘/系统按键暂不接入——交回系统（保证 BACK/HOME 正常）
    return 0;
}

static void android_process_input(void)
{
    AInputQueue* queue = g_wsi.android.inputQueue;
    if (!queue)
        return;

    AInputEvent* event = NULL;
    while (AInputQueue_getEvent(queue, &event) >= 0)
    {
        // 让系统先行处理（IME 等）；被吞则跳过
        if (AInputQueue_preDispatchEvent(queue, event))
            continue;
        const int handled = android_handle_input(event);
        AInputQueue_finishEvent(queue, event, handled);
    }
}

///////////////////////////////////////////////////////////////////////////////
// 帧驱动（AChoreographer vsync；对位 iOS 的 CADisplayLink）
///////////////////////////////////////////////////////////////////////////////

static void android_frame_cb(long frameTimeNanos, void* data)
{
    (void) frameTimeNanos; (void) data;
    g_wsi.android.frameScheduled = false;
    if (g_wsi.android.suspended || !g_wsi.android.window)
        return;
    if (!g_wsi.android.firstFrameLogged) {
        g_wsi.android.firstFrameLogged = true;
        WSI_ALOGI("首帧呈现（AChoreographer 帧循环已运行）");
    }
    if (s_on_frame)
        s_on_frame();
    android_schedule_frame();   // 续下一帧
}

static void android_schedule_frame(void)
{
    if (g_wsi.android.frameScheduled)      return;
    if (g_wsi.android.suspended)           return;
    if (!g_wsi.android.window)             return;
    if (!g_wsi.android.choreographer)      return;
    g_wsi.android.frameScheduled = true;
    AChoreographer_postFrameCallback(g_wsi.android.choreographer, android_frame_cb, NULL);
}

///////////////////////////////////////////////////////////////////////////////
// 窗口建立 / 拆除（移动端全屏单窗口，wsi 内部自建，不经公共 sc_wsi_win_create）
///////////////////////////////////////////////////////////////////////////////

static void android_setup_window(void)
{
    ANativeWindow* nw = g_wsi.android.nativeWindow;
    if (!nw || g_wsi.android.window)
        return;

    // 分配并登记窗口对象（尺寸对 Android 无意义，传 1×1 占位过校验后内联铺满）
    window_st* window = wsi_alloc_window(1, 1, "");
    if (!window)
        return;

    const int w = ANativeWindow_getWidth(nw);
    const int h = ANativeWindow_getHeight(nw);
    const float scale = g_wsi.android.scale > 0.f ? g_wsi.android.scale : 1.f;

    window->android.window   = nw;
    window->android.width    = w;
    window->android.height   = h;
    window->android.fbWidth  = w;
    window->android.fbHeight = h;
    window->android.xscale   = scale;
    window->android.yscale   = scale;

    g_wsi.android.window = window;

    WSI_ALOGI("主窗口就绪 · %dx%d px（scale %.2f）", w, h, (double) scale);

    // 交给应用：拿到主窗口 → 初始化 gpu/gfx（EGL/Vulkan surface 取自 ANativeWindow）
    // + 经 sc_wsi_win_set_callback 注册 touch/suspend/resume/destroy 委托。
    if (s_main_window_created)
        s_main_window_created((sc_window*) window);

    // 启动逐帧驱动
    android_schedule_frame();
}

static void android_teardown_window(void)
{
    window_st* window = g_wsi.android.window;
    if (!window)
        return;

    // 通过公共销毁路径：vtable android_destroy_window + 出表 + 释放对象。
    // 先置空 g_wsi.android.window 防重入（destroy 内亦有同样置空的兜底）。
    g_wsi.android.window = NULL;
    sc_wsi_win_destroy((sc_window*) window);
}

///////////////////////////////////////////////////////////////////////////////
// 命令处理（渲染线程消费 UI 线程投递的命令）
///////////////////////////////////////////////////////////////////////////////

static void android_process_cmd(void)
{
    int8_t cmd = 0;
    if (read(g_wsi.android.msgread, &cmd, sizeof(cmd)) != sizeof(cmd))
        return;

    switch (cmd)
    {
        case ANDROID_CMD_INIT_WINDOW:
            // 采纳新窗口并唤醒 UI 线程（新窗口保持有效，可安全先解锁再建 surface）
            pthread_mutex_lock(&g_wsi.android.mutex);
            g_wsi.android.nativeWindow = g_wsi.android.pendingWindow;
            pthread_cond_broadcast(&g_wsi.android.cond);
            pthread_mutex_unlock(&g_wsi.android.mutex);
            android_setup_window();
            break;

        case ANDROID_CMD_TERM_WINDOW:
            // 先拆窗（停用 ANativeWindow）——务必在唤醒 UI 线程「之前」，否则窗口会
            // 被框架提前失效。拆完再置空 nativeWindow 并 broadcast。
            android_teardown_window();
            pthread_mutex_lock(&g_wsi.android.mutex);
            g_wsi.android.nativeWindow = g_wsi.android.pendingWindow;   // == NULL
            pthread_cond_broadcast(&g_wsi.android.cond);
            pthread_mutex_unlock(&g_wsi.android.mutex);
            break;

        case ANDROID_CMD_INPUT_CHANGED:
            pthread_mutex_lock(&g_wsi.android.mutex);
            if (g_wsi.android.inputQueue)
                AInputQueue_detachLooper(g_wsi.android.inputQueue);
            g_wsi.android.inputQueue = g_wsi.android.pendingInputQueue;
            if (g_wsi.android.inputQueue)
                AInputQueue_attachLooper(g_wsi.android.inputQueue, g_wsi.android.looper,
                                         ANDROID_LOOPER_ID_INPUT, NULL, NULL);
            pthread_cond_broadcast(&g_wsi.android.cond);
            pthread_mutex_unlock(&g_wsi.android.mutex);
            break;

        case ANDROID_CMD_WINDOW_RESIZED:
        case ANDROID_CMD_WINDOW_REDRAW:
        {
            window_st* window = g_wsi.android.window;
            if (window && g_wsi.android.nativeWindow)
            {
                const int w = ANativeWindow_getWidth(g_wsi.android.nativeWindow);
                const int h = ANativeWindow_getHeight(g_wsi.android.nativeWindow);
                if (w != window->android.fbWidth || h != window->android.fbHeight)
                {
                    window->android.width    = w;
                    window->android.height   = h;
                    window->android.fbWidth  = w;
                    window->android.fbHeight = h;
                    impl_on_win_size(window, w, h);
                }
                impl_on_win_damage(window);
            }
            break;
        }

        case ANDROID_CMD_RESUME:
            g_wsi.android.suspended = false;
            if (g_wsi.android.window)
                impl_on_resume(g_wsi.android.window);
            android_schedule_frame();
            break;

        case ANDROID_CMD_PAUSE:
            g_wsi.android.suspended = true;
            if (g_wsi.android.window)
                impl_on_suspend(g_wsi.android.window);
            break;

        case ANDROID_CMD_FOCUS_GAINED:
            g_wsi.android.focused = true;
            if (g_wsi.android.window)
                impl_on_win_focus(g_wsi.android.window, true);
            break;

        case ANDROID_CMD_FOCUS_LOST:
            g_wsi.android.focused = false;
            if (g_wsi.android.window)
                impl_on_win_focus(g_wsi.android.window, false);
            break;

        case ANDROID_CMD_CONFIG_CHANGED:
            if (g_wsi.android.config && g_wsi.android.activity)
                AConfiguration_fromAssetManager(g_wsi.android.config,
                                                g_wsi.android.activity->assetManager);
            break;

        case ANDROID_CMD_START:
        case ANDROID_CMD_STOP:
            break;   // 目前无额外动作（可见性由 RESUME/PAUSE 驱动）

        case ANDROID_CMD_DESTROY:
            g_wsi.android.destroyRequested = true;
            break;

        default:
            break;
    }
}

///////////////////////////////////////////////////////////////////////////////
// ANativeActivity 回调（全部在 UI 线程触发，经命令管道转渲染线程）
///////////////////////////////////////////////////////////////////////////////

static void cb_onStart(ANativeActivity* a)  { (void) a; android_write_cmd(ANDROID_CMD_START); }
static void cb_onResume(ANativeActivity* a) { (void) a; android_write_cmd(ANDROID_CMD_RESUME); }
static void cb_onPause(ANativeActivity* a)  { (void) a; android_write_cmd(ANDROID_CMD_PAUSE); }
static void cb_onStop(ANativeActivity* a)   { (void) a; android_write_cmd(ANDROID_CMD_STOP); }

static void cb_onDestroy(ANativeActivity* a) { (void) a; android_set_destroy(); }

static void cb_onWindowFocusChanged(ANativeActivity* a, int hasFocus)
{
    (void) a;
    android_write_cmd(hasFocus ? ANDROID_CMD_FOCUS_GAINED : ANDROID_CMD_FOCUS_LOST);
}

static void cb_onNativeWindowCreated(ANativeActivity* a, ANativeWindow* w)   { (void) a; android_set_window(w); }
static void cb_onNativeWindowDestroyed(ANativeActivity* a, ANativeWindow* w) { (void) a; (void) w; android_set_window(NULL); }
static void cb_onNativeWindowResized(ANativeActivity* a, ANativeWindow* w)   { (void) a; (void) w; android_write_cmd(ANDROID_CMD_WINDOW_RESIZED); }
static void cb_onNativeWindowRedrawNeeded(ANativeActivity* a, ANativeWindow* w) { (void) a; (void) w; android_write_cmd(ANDROID_CMD_WINDOW_REDRAW); }

static void cb_onInputQueueCreated(ANativeActivity* a, AInputQueue* q)   { (void) a; android_set_input(q); }
static void cb_onInputQueueDestroyed(ANativeActivity* a, AInputQueue* q) { (void) a; (void) q; android_set_input(NULL); }

static void cb_onConfigurationChanged(ANativeActivity* a) { (void) a; android_write_cmd(ANDROID_CMD_CONFIG_CHANGED); }
static void cb_onLowMemory(ANativeActivity* a) { (void) a; }

static void* cb_onSaveInstanceState(ANativeActivity* a, size_t* outLen)
{
    (void) a;
    if (outLen) *outLen = 0;
    return NULL;   // 状态保存交由 tier A（ScApplication.onTrimMemory）
}

///////////////////////////////////////////////////////////////////////////////
// 渲染线程入口：跑 app 逻辑（sc_app_main → wsi_app_run → android_run 本线程循环）
///////////////////////////////////////////////////////////////////////////////

static void* android_thread_entry(void* arg)
{
    (void) arg;

    sc_app_main();   // 进 android_run 事件循环，destroy 后返回

    // 循环退出：标记并唤醒 UI 线程 onDestroy 等待者
    pthread_mutex_lock(&g_wsi.android.mutex);
    g_wsi.android.destroyed = true;
    pthread_cond_broadcast(&g_wsi.android.cond);
    pthread_mutex_unlock(&g_wsi.android.mutex);
    return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// NativeActivity 入口：框架经 JNI 调用；在 UI 线程建管道 + 渲染线程
///////////////////////////////////////////////////////////////////////////////

JNIEXPORT void ANativeActivity_onCreate(ANativeActivity* activity,
                                        void* savedState, size_t savedStateSize)
{
    (void) savedState; (void) savedStateSize;

    // 幂等确保平台已选、android_init 已跑（memset g_wsi 只在这次或更早的 tier A 发生；
    // 之后填的 g_wsi.android.* 不会再被抹）。
    if (!sc_wsi_app_startup())
    {
        WSI_ALOGW("ANativeActivity_onCreate: sc_wsi_app_startup 失败");
        return;
    }

    g_wsi.android.activity = activity;

    // 读设备配置（density → 内容缩放）
    g_wsi.android.config = AConfiguration_new();
    AConfiguration_fromAssetManager(g_wsi.android.config, activity->assetManager);
    const int density = AConfiguration_getDensity(g_wsi.android.config);
    g_wsi.android.scale =
        (density > 0 && density != ACONFIGURATION_DENSITY_NONE) ? (density / 160.0f) : 1.0f;

    // 挂 ANativeActivity 回调
    activity->callbacks->onStart               = cb_onStart;
    activity->callbacks->onResume              = cb_onResume;
    activity->callbacks->onPause               = cb_onPause;
    activity->callbacks->onStop                = cb_onStop;
    activity->callbacks->onDestroy             = cb_onDestroy;
    activity->callbacks->onWindowFocusChanged  = cb_onWindowFocusChanged;
    activity->callbacks->onNativeWindowCreated = cb_onNativeWindowCreated;
    activity->callbacks->onNativeWindowResized = cb_onNativeWindowResized;
    activity->callbacks->onNativeWindowRedrawNeeded = cb_onNativeWindowRedrawNeeded;
    activity->callbacks->onNativeWindowDestroyed    = cb_onNativeWindowDestroyed;
    activity->callbacks->onInputQueueCreated   = cb_onInputQueueCreated;
    activity->callbacks->onInputQueueDestroyed = cb_onInputQueueDestroyed;
    activity->callbacks->onConfigurationChanged = cb_onConfigurationChanged;
    activity->callbacks->onLowMemory           = cb_onLowMemory;
    activity->callbacks->onSaveInstanceState   = cb_onSaveInstanceState;

    // 命令管道（UI 线程写、渲染线程读）
    int fds[2];
    if (pipe(fds) != 0)
    {
        WSI_ALOGW("ANativeActivity_onCreate: pipe 失败: %s", strerror(errno));
        return;
    }
    g_wsi.android.msgread  = fds[0];
    g_wsi.android.msgwrite = fds[1];

    pthread_mutex_init(&g_wsi.android.mutex, NULL);
    pthread_cond_init(&g_wsi.android.cond, NULL);

    // 起渲染线程，等其 looper 就绪后再返回（保证后续窗口/输入回调有对端消费）
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&g_wsi.android.thread, &attr, android_thread_entry, NULL);
    pthread_attr_destroy(&attr);

    pthread_mutex_lock(&g_wsi.android.mutex);
    while (!g_wsi.android.running)
        pthread_cond_wait(&g_wsi.android.cond, &g_wsi.android.mutex);
    pthread_mutex_unlock(&g_wsi.android.mutex);
}

///////////////////////////////////////////////////////////////////////////////
// 平台初始化 / 终止
///////////////////////////////////////////////////////////////////////////////

int android_init(void)
{
    android_poll_monitors();
    return true;
}

void android_terminate(void)
{
    if (g_wsi.android.config)
    {
        AConfiguration_delete(g_wsi.android.config);
        g_wsi.android.config = NULL;
    }
    memset(&g_wsi.android, 0, sizeof(g_wsi.android));
}

///////////////////////////////////////////////////////////////////////////////
// 帧驱动入口（渲染线程事件循环）
///////////////////////////////////////////////////////////////////////////////

int android_run(sc_wsi_app_cb after_startup, sc_wsi_window_cb main_window_created,
                sc_wsi_app_cb on_frame, sc_wsi_app_cb before_cleanup)
{
    s_after_startup       = after_startup;
    s_main_window_created = main_window_created;
    s_on_frame            = on_frame;
    s_before_cleanup      = before_cleanup;

    // 幂等：onCreate 已 startup，此处 no-op（纯 NativeActivity 无 Application 形态下
    // 亦已由 onCreate 触发；不会二次 memset）。
    if (!sc_wsi_app_startup())
        return 1;

    // 渲染线程 looper + 命令管道 fd + choreographer
    g_wsi.android.looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    ALooper_addFd(g_wsi.android.looper, g_wsi.android.msgread, ANDROID_LOOPER_ID_MAIN,
                  ALOOPER_EVENT_INPUT, NULL, NULL);
    g_wsi.android.choreographer = AChoreographer_getInstance();

    // 通知 UI 线程：渲染线程已就绪
    pthread_mutex_lock(&g_wsi.android.mutex);
    g_wsi.android.running = true;
    pthread_cond_broadcast(&g_wsi.android.cond);
    pthread_mutex_unlock(&g_wsi.android.mutex);

    // 子系统就绪、建窗前回调一次
    if (s_after_startup)
        s_after_startup();

    // 事件循环：命令/输入由 looper 唤醒；帧由 AChoreographer vsync 驱动
    while (!g_wsi.android.destroyRequested)
    {
        int events = 0;
        void* data = NULL;
        const int ident = ALooper_pollOnce(-1, NULL, &events, &data);
        if (ident == ANDROID_LOOPER_ID_MAIN)
            android_process_cmd();
        else if (ident == ANDROID_LOOPER_ID_INPUT)
            android_process_input();
    }

    if (s_before_cleanup)
        s_before_cleanup();

    if (g_wsi.android.window)
        android_teardown_window();

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
// 事件泵（Android 由框架/looper 推送，公共 loop_* 为退化实现）
///////////////////////////////////////////////////////////////////////////////

void android_poll_events(void)                  { /* 渲染线程由 android_run 的 looper 拥有 */ }
void android_wait_events(void)                  { }
void android_wait_events_timeout(double timeout) { (void) timeout; }
void android_post_empty_event(void)             { if (g_wsi.android.looper) ALooper_wake(g_wsi.android.looper); }

///////////////////////////////////////////////////////////////////////////////
// 窗口销毁（vtable）：ANativeWindow 归框架所有，仅解引用，不 release
///////////////////////////////////////////////////////////////////////////////

void android_destroy_window(window_st* window)
{
    if (g_wsi.android.window == window)
        g_wsi.android.window = NULL;
    window->android.window = NULL;
}

///////////////////////////////////////////////////////////////////////////////
// 窗口属性查询（真实值） / 其余为 no-op（全屏、不可调、无装饰）
///////////////////////////////////////////////////////////////////////////////

void android_get_window_size(window_st* window, int* width, int* height)
{
    if (width)  *width  = window->android.width;
    if (height) *height = window->android.height;
}

void android_get_framebuffer_size(window_st* window, int* width, int* height)
{
    if (width)  *width  = window->android.fbWidth;
    if (height) *height = window->android.fbHeight;
}

void android_get_window_content_scale(window_st* window, float* xscale, float* yscale)
{
    if (xscale) *xscale = window->android.xscale;
    if (yscale) *yscale = window->android.yscale;
}

void android_get_window_pos(window_st* window, int* xpos, int* ypos)
{
    (void) window;
    if (xpos) *xpos = 0;
    if (ypos) *ypos = 0;
}

void android_get_window_frame_size(window_st* window, int* left, int* top, int* right, int* bottom)
{
    (void) window;
    if (left)   *left = 0;
    if (top)    *top = 0;
    if (right)  *right = 0;
    if (bottom) *bottom = 0;
}

bool android_window_focused(window_st* window)   { (void) window; return g_wsi.android.focused; }
bool android_window_iconified(window_st* window) { (void) window; return g_wsi.android.suspended; }
bool android_window_visible(window_st* window)   { (void) window; return !g_wsi.android.suspended; }
bool android_window_maximized(window_st* window) { (void) window; return true; }
bool android_window_hovered(window_st* window)   { (void) window; return false; }
float android_get_window_opacity(window_st* window) { (void) window; return 1.f; }

// 全屏不可变：以下窗口操作在 Android 上均为 no-op
void android_set_window_title(window_st* window, const char* title) { (void) window; (void) title; }
void android_set_window_icon(window_st* window, int count, const sc_wsi_img* images) { (void) window; (void) count; (void) images; }
void android_set_window_pos(window_st* window, int xpos, int ypos) { (void) window; (void) xpos; (void) ypos; }
void android_set_window_size(window_st* window, int width, int height) { (void) window; (void) width; (void) height; }
void android_set_window_size_limits(window_st* window, int minw, int minh, int maxw, int maxh) { (void) window; (void) minw; (void) minh; (void) maxw; (void) maxh; }
void android_set_window_aspect_ratio(window_st* window, int numer, int denom) { (void) window; (void) numer; (void) denom; }
void android_set_window_monitor(window_st* window, monitor_st* monitor, int xpos, int ypos, int width, int height, int refreshRate) { (void) window; (void) monitor; (void) xpos; (void) ypos; (void) width; (void) height; (void) refreshRate; }
void android_set_window_resizable(window_st* window, bool enabled) { (void) window; (void) enabled; }
void android_set_window_decorated(window_st* window, bool enabled) { (void) window; (void) enabled; }
void android_set_window_floating(window_st* window, bool enabled) { (void) window; (void) enabled; }
void android_set_window_opacity(window_st* window, float opacity) { (void) window; (void) opacity; }
void android_set_window_mouse_passthrough(window_st* window, bool enabled) { (void) window; (void) enabled; }
void android_iconify_window(window_st* window) { (void) window; }
void android_restore_window(window_st* window) { (void) window; }
void android_maximize_window(window_st* window) { (void) window; }
void android_show_window(window_st* window) { (void) window; }
void android_hide_window(window_st* window) { (void) window; }
void android_request_window_attention(window_st* window) { (void) window; }
void android_focus_window(window_st* window) { (void) window; }

///////////////////////////////////////////////////////////////////////////////
// 光标 / 剪贴板（Android 无光标；剪贴板暂不接入）
///////////////////////////////////////////////////////////////////////////////

void android_get_cursor_pos(window_st* window, double* xpos, double* ypos)
{
    (void) window;
    if (xpos) *xpos = 0.0;
    if (ypos) *ypos = 0.0;
}
void android_set_cursor_pos(window_st* window, double xpos, double ypos) { (void) window; (void) xpos; (void) ypos; }
void android_set_cursor_mode(window_st* window, int mode) { (void) window; (void) mode; }
void android_set_cursor(window_st* window, cursor_st* cursor) { (void) window; (void) cursor; }
bool android_create_cursor(cursor_st* cursor, const sc_wsi_img* image, int xhot, int yhot) { (void) cursor; (void) image; (void) xhot; (void) yhot; return false; }
bool android_create_standard_cursor(cursor_st* cursor, int shape) { (void) cursor; (void) shape; return false; }
void android_destroy_cursor(cursor_st* cursor) { (void) cursor; }
void android_set_raw_mouse_motion(window_st* window, bool enabled) { (void) window; (void) enabled; }
bool android_raw_mouse_motion_supported(void) { return false; }
const char* android_get_scancode_name(int scancode) { (void) scancode; return ""; }
int android_get_key_scancode(int key) { (void) key; return -1; }
void android_set_clipboard_string(const char* string) { (void) string; }
const char* android_get_clipboard_string(void) { return ""; }

///////////////////////////////////////////////////////////////////////////////
// vtable 装配
///////////////////////////////////////////////////////////////////////////////

bool android_connect(int platformID, platform_st* platform)
{
    (void) platformID;
    const platform_st android =
    {
        .platformID = SC_PLATFORM_ANDROID,
        .init = android_init,
        .terminate = android_terminate,

        .pollEvents                 = android_poll_events,
        .waitEvents                 = android_wait_events,
        .waitEventsTimeout          = android_wait_events_timeout,
        .postEmptyEvent             = android_post_empty_event,

        .destroyWindow              = android_destroy_window,
        .setWindowTitle             = android_set_window_title,
        .setWindowIcon              = android_set_window_icon,
        .setWindowMonitor           = android_set_window_monitor,
        .setWindowMousePassthrough  = android_set_window_mouse_passthrough,

        .setWindowDecorated         = android_set_window_decorated,
        .setWindowResizable         = android_set_window_resizable,
        .setWindowFloating          = android_set_window_floating,
        .setWindowOpacity           = android_set_window_opacity,
        .getWindowOpacity           = android_get_window_opacity,

        .getWindowPos               = android_get_window_pos,
        .setWindowPos               = android_set_window_pos,
        .getWindowSize              = android_get_window_size,
        .getFramebufferSize         = android_get_framebuffer_size,
        .setWindowSize              = android_set_window_size,
        .getWindowFrameSize         = android_get_window_frame_size,
        .setWindowSizeLimits        = android_set_window_size_limits,
        .getWindowContentScale      = android_get_window_content_scale,
        .setWindowAspectRatio       = android_set_window_aspect_ratio,

        .showWindow                 = android_show_window,
        .hideWindow                 = android_hide_window,
        .maximizeWindow             = android_maximize_window,
        .restoreWindow              = android_restore_window,
        .focusWindow                = android_focus_window,
        .iconifyWindow              = android_iconify_window,
        .requestWindowAttention     = android_request_window_attention,

        .windowVisible              = android_window_visible,
        .windowMaximized            = android_window_maximized,
        .windowFocused              = android_window_focused,
        .windowHovered              = android_window_hovered,
        .windowIconified            = android_window_iconified,

        .setCursor                  = android_set_cursor,
        .createStandardCursor       = android_create_standard_cursor,
        .createCursor               = android_create_cursor,
        .destroyCursor              = android_destroy_cursor,
        .setCursorMode              = android_set_cursor_mode,
        .setCursorPos               = android_set_cursor_pos,
        .getCursorPos               = android_get_cursor_pos,
        .setRawMouseMotion          = android_set_raw_mouse_motion,
        .rawMouseMotionSupported    = android_raw_mouse_motion_supported,

        .getKeyScancode             = android_get_key_scancode,
        .getScancodeName            = android_get_scancode_name,
        .getClipboardString         = android_get_clipboard_string,
        .setClipboardString         = android_set_clipboard_string,

        .freeMonitor                = android_free_monitor,
        .getMonitorPos              = android_get_monitor_pos,
        .getMonitorWorkarea         = android_get_monitor_work_area,
        .getMonitorContentScale     = android_get_monitor_content_scale,
        .getVideoModes              = android_get_video_modes,
        .getVideoMode               = android_get_video_mode,
        .getGammaRamp               = android_get_gamma_ramp,
        .setGammaRamp               = android_set_gamma_ramp,
    };

    *platform = android;
    return true;
}

#endif // WSI_ANDROID
