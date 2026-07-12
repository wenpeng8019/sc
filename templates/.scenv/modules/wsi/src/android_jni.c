// ============================================================
// android_jni.c —— ScApplication ⇄ wsi 进程级生命周期 JNI 桥（tier A：进程/库）
//
// 对照 uikit（iOS）：iOS 的进程级 init 藏在后端 uikit_run 里（sc_wsi_app_startup 于
// didFinishLaunching 前调用）；Android 没有等价的「app 对象 native 入口」，故由
// 自定义的 Java Application 子类 com.sc.wsi.ScApplication 经本文件的 JNI 桥补上。
//
// 职责边界（只做进程/库级 tier A）：
//   nativeOnCreate    → sc_wsi_app_startup（每进程一次，最早、最可靠的库级 init）
//   nativeOnTrimMemory→ 进入后台/内存吃紧 ≈ 可能被杀 → 建议保存 / on_suspend
//   nativeOnLowMemory → 同上（旧 API 路径）
//   nativeOnTerminate → 仅模拟器触发，真机不可依赖 → sc_wsi_app_cleanup（对称补全）
// 窗口/帧级（tier B/C：ANativeWindow/AChoreographer/AInputQueue）不在此文件，
// 属 android 后端（android_platform.c）。
//
// 状态：Application ⇄ JNI 桥完整可用；桥到的 wsi 内部钩子（on_suspend/save）在
// android 后端就绪前为契约占位（见 TODO）。整文件由 WSI_ANDROID 守卫，非 android
// 目标编译为空翻译单元（可安全随全量 src/* 一起编）。
// ============================================================
#if defined(WSI_ANDROID)

#include <jni.h>
#include <android/log.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "internal.h"   // g_wsi、sc_wsi_app_startup/cleanup、impl_on_suspend/resume（契约）

#define WSI_JNI_TAG "sc.wsi"
#define WSI_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  WSI_JNI_TAG, __VA_ARGS__)
#define WSI_LOGW(...) __android_log_print(ANDROID_LOG_WARN,  WSI_JNI_TAG, __VA_ARGS__)

// Android app 全局唯一 JavaVM（JNI_OnLoad 落地）。android 后端就绪后，渲染线程
// 需用它 AttachCurrentThread 以回调 Java；此处先集中持有并对外暴露。
static JavaVM* g_android_vm = NULL;

JavaVM* sc_wsi_android_get_vm(void) { return g_android_vm; }

// ============================================================
// 原生 stdout/stderr → logcat 重定向
//
// Android 上 app 进程的原生 fd 1/2（printf / sc 的 print 走 write(1,...)）默认落
// /dev/null——`log.redirect-stdio` 只转 Java 的 System.out/err，不管原生 fd。故 sc
// print 在设备上看不到。这里用管道把 fd 1/2 接到一个读线程，逐行转 __android_log，
// tag=sc.stdout，让 print 输出可经 logcat 观察。进程内幂等，只装一次。
// ============================================================
static void* wsi_stdio_pump(void* arg)
{
    int fd = (int)(intptr_t)arg;
    char buf[512];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0)
    {
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) n--;
        buf[n] = '\0';
        if (n > 0)
            __android_log_write(ANDROID_LOG_INFO, "sc.stdout", buf);
    }
    return NULL;
}

static void wsi_redirect_stdio(void)
{
    static int done = 0;
    if (done) return;
    done = 1;

    setvbuf(stdout, NULL, _IONBF, 0);   // 行/全缓冲会吞输出，改无缓冲即时可见
    setvbuf(stderr, NULL, _IONBF, 0);

    int pfd[2];
    if (pipe(pfd) != 0) return;
    dup2(pfd[1], STDOUT_FILENO);
    dup2(pfd[1], STDERR_FILENO);
    close(pfd[1]);

    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &attr, wsi_stdio_pump, (void*)(intptr_t)pfd[0]);
    pthread_attr_destroy(&attr);
}

// 通用 JNI 反射代理注册（android_jni_proxy.c）：在 JNI_OnLoad 里于 app 主线程
// 调用，解析 com.sc.wsi.Bridge 并绑定其 native 方法。返回非 0 表示代理就绪。
extern int wsi_jni_proxy_register(JNIEnv* env);

// view-tree 外壳注册（android_platform.c）：向 com.sc.wsi.ScActivity 绑定 native
// 方法；无该类时安全跳过。
extern int wsi_android_shell_register(JNIEnv* env);


// ---- 进程级钩子实现（从 Java native 方法转发到 wsi C 侧）----

static void wsi_android_process_create(void) {
    // tier A：进程/库级 init。Application.onCreate 是每进程一次、早于任何 Activity
    // 的最可靠落点。sc_wsi_app_startup 幂等；android 平台后端就绪后由它建平台连接（ALooper
    // 命令管道等）。当前后端未实现，init 预期返回失败——记录但不崩，保持垫片可跑通。
    if (sc_wsi_app_startup()) {
        WSI_LOGI("wsi 进程级 init 完成（Application.onCreate → sc_wsi_app_startup）");
    } else {
        WSI_LOGW("sc_wsi_app_startup 失败");
    }
}

static void wsi_android_process_trim(int level) {
    // ComponentCallbacks2 常量：TRIM_MEMORY_UI_HIDDEN=20（UI 全隐/进入后台）、
    // TRIM_MEMORY_COMPLETE=80（内存极度吃紧、最可能被杀）。此为「可能即将消失」的
    // 唯一可靠信号——Android 后台被杀是 SIGKILL、无 destroy 回调，故须尽早保存。
    WSI_LOGI("onTrimMemory level=%d → 建议尽早保存状态", level);
    // TODO(android 后端)：level >= TRIM_MEMORY_UI_HIDDEN 时调 g_wsi 的挂起/保存路径
    //   （对照 impl_on_suspend）。当前无 android 平台状态，占位待接。
    (void)level;
}

static void wsi_android_process_terminate(void) {
    // 仅模拟器会走到（真机进程被 SIGKILL）。对称补全库级 terminate。
    WSI_LOGI("Application.onTerminate（仅模拟器）→ sc_wsi_app_cleanup");
    sc_wsi_app_cleanup();
}

// ---- JNI native 方法（绑定 com.sc.wsi.ScApplication 的 native 声明）----

static void jni_ScApplication_nativeOnCreate(JNIEnv* env, jobject thiz) {
    (void)env; (void)thiz;
    wsi_android_process_create();
}
static void jni_ScApplication_nativeOnTrimMemory(JNIEnv* env, jobject thiz, jint level) {
    (void)env; (void)thiz;
    wsi_android_process_trim((int)level);
}
static void jni_ScApplication_nativeOnLowMemory(JNIEnv* env, jobject thiz) {
    (void)env; (void)thiz;
    wsi_android_process_trim(80 /* ≈ TRIM_MEMORY_COMPLETE */);
}
static void jni_ScApplication_nativeOnTerminate(JNIEnv* env, jobject thiz) {
    (void)env; (void)thiz;
    wsi_android_process_terminate();
}

static const JNINativeMethod kScApplicationMethods[] = {
    { "nativeOnCreate",     "()V",  (void*)jni_ScApplication_nativeOnCreate },
    { "nativeOnTrimMemory", "(I)V", (void*)jni_ScApplication_nativeOnTrimMemory },
    { "nativeOnLowMemory",  "()V",  (void*)jni_ScApplication_nativeOnLowMemory },
    { "nativeOnTerminate",  "()V",  (void*)jni_ScApplication_nativeOnTerminate },
};

// JNI_OnLoad：库加载即运行一次（ScApplication.onCreate 里 System.loadLibrary 触发，
// 早于 NativeActivity）。持有 JavaVM 并用 RegisterNatives 显式绑定，避免依赖符号
// 名字修饰（Java_com_sc_wsi_ScApplication_...）。
//
// 注：android_platform.c（tier B/C 后端）刻意「不」定义 JNI_OnLoad（NDK 的
//   ANativeActivity/ALooper/AInputQueue/AChoreographer 均为纯 C API，无需 JNI），
//   故本文件独占 .so 唯一的 JNI_OnLoad。构建须 -u JNI_OnLoad 强制从静态库拉入本 TU。
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)reserved;
    g_android_vm = vm;

    wsi_redirect_stdio();   // 原生 stdout/stderr → logcat（sc print 设备可见）

    JNIEnv* env = NULL;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK || env == NULL) {
        WSI_LOGW("JNI_OnLoad: GetEnv 失败");
        return JNI_ERR;
    }

    jclass cls = (*env)->FindClass(env, "com/sc/wsi/ScApplication");
    if (cls != NULL) {
        if ((*env)->RegisterNatives(env, cls, kScApplicationMethods,
                (jint)(sizeof(kScApplicationMethods) / sizeof(kScApplicationMethods[0]))) != 0) {
            WSI_LOGW("JNI_OnLoad: RegisterNatives(ScApplication) 失败");
        } else {
            WSI_LOGI("JNI_OnLoad: 已注册 ScApplication native 桥");
        }
        (*env)->DeleteLocalRef(env, cls);
    } else {
        // 纯 NativeActivity 场景（未启用 ScApplication）也会走到这里——非致命：
        // 类不存在只是没引入垫片，清掉待决异常继续。
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        WSI_LOGI("JNI_OnLoad: 未找到 com.sc.wsi.ScApplication（纯 NativeActivity 形态）");
    }

    // 通用 JNI 反射代理（android_jni_proxy.c）：解析并缓存 com.sc.wsi.Bridge、
    // 绑定其 nativeInvoke / nativeRunUi。app 类须在此线程（app 主线程、app
    // 类加载器）解析——渲染线程的 FindClass 找不到 app 类。此处的引用亦是把
    // android_jni_proxy.c TU 从静态库拉入的锚点。Bridge 缺失（纯 gpu app）非致命。
    if (wsi_jni_proxy_register(env))
        WSI_LOGI("JNI_OnLoad: 已注册 com.sc.wsi.Bridge 反射代理");
    else
        WSI_LOGI("JNI_OnLoad: 未启用 Bridge 反射代理（纯 gpu 形态或 dex 缺失）");

    // view-tree 外壳（android_platform.c）：向 com.sc.wsi.ScActivity 注册 native
    // 方法。无该类（纯 gpu NativeActivity 形态）时安全跳过。此处引用亦是把外壳代码
    // 从静态库拉入的锚点之一。
    wsi_android_shell_register(env);

    return JNI_VERSION_1_6;
}

#endif // WSI_ANDROID
