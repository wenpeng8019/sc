// ============================================================
// android_jni_proxy.c —— wsi 通用 JNI 反射代理实现（wsi_jni.h 的后端）
//
// 让上层用纯 C 反射驱动任意 android.widget.* / java.* 对象；回调与主线程投递
// 由唯一的通用 Java shim com.sc.wsi.Bridge 承接（见 java/com/sc/wsi/Bridge.java）。
//
// 与 android_jni.c 的关系：JNI_OnLoad（唯一，在 android_jni.c）在注册 ScApplication
// 之后调用本文件的 wsi_jni_proxy_register(env)，解析并缓存 Bridge、绑定其两个
// native 方法（nativeInvoke / nativeRunUi）。故本 TU 由 JNI_OnLoad 引用而被拉入。
//
// 整文件由 WSI_ANDROID 守卫；非 android 目标编为安全空实现（sc_jni_* 返回 0/NULL，
// sc_jni_available()==0），令 wsi_jni.h 可被任意平台无条件包含。
// ============================================================
#include "internal.h"
#include "../wsi_jni.h"

#if defined(WSI_ANDROID)

#include <jni.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <android/log.h>

#define JP_TAG "sc.wsi"
#define JP_LOGW(...) __android_log_print(ANDROID_LOG_WARN, JP_TAG, __VA_ARGS__)

#define SC_JNI_MAXARGS 16

// JavaVM（由 android_jni.c 的 JNI_OnLoad 落地并持有）。
extern JavaVM* sc_wsi_android_get_vm(void);

// ---- Bridge 缓存 + 状态 ----
static jclass     g_bridge_cls    = NULL;
static jclass     g_string_cls    = NULL;
static jmethodID  g_mid_newproxy  = NULL;   // static Object newProxy(long, String[])
static jmethodID  g_mid_postui    = NULL;   // static void postUi(long)
static int        g_bridge_ready  = 0;
static pthread_t  g_ui_thread;              // JNI_OnLoad 所在线程（= app 主/UI 线程）

// ---- JNIEnv 获取（渲染线程按需 attach）----
static JNIEnv* env_get(void)
{
    JavaVM* vm = sc_wsi_android_get_vm();
    if (!vm) return NULL;
    JNIEnv* env = NULL;
    jint r = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (r == JNI_OK) return env;
    if (r == JNI_EDETACHED && (*vm)->AttachCurrentThread(vm, &env, NULL) == JNI_OK)
        return env;
    return NULL;
}

static void exc_clear(JNIEnv* e)
{
    if ((*e)->ExceptionCheck(e))
    {
        (*e)->ExceptionDescribe(e);
        (*e)->ExceptionClear(e);
    }
}

// ============================================================
// 类缓存（名少，线性表足矣）
// ============================================================
typedef struct { char* name; jclass cls; } class_entry;
static class_entry     g_classes[128];
static int             g_nclasses = 0;
static pthread_mutex_t g_cls_mtx = PTHREAD_MUTEX_INITIALIZER;

// ============================================================
// 回调登记表：proxy（随代理常驻）与 post（一次性）
// ============================================================
typedef struct { sc_jni_invoke_cb cb; void* user; } proxy_entry;
static proxy_entry     g_proxies[512];
static int             g_nproxy = 0;

typedef struct { void (*fn)(void*); void* user; int used; } post_entry;
static post_entry      g_posts[512];
static int             g_nposts = 0;

static pthread_mutex_t g_tbl_mtx = PTHREAD_MUTEX_INITIALIZER;

static long proxy_alloc(sc_jni_invoke_cb cb, void* user)
{
    long tag = -1;
    pthread_mutex_lock(&g_tbl_mtx);
    if (g_nproxy < (int)(sizeof(g_proxies) / sizeof(g_proxies[0])))
    {
        int i = g_nproxy++;
        g_proxies[i].cb = cb;
        g_proxies[i].user = user;
        tag = i + 1;
    }
    pthread_mutex_unlock(&g_tbl_mtx);
    return tag;
}

static proxy_entry* proxy_lookup(long tag)
{
    if (tag < 1 || tag > g_nproxy) return NULL;
    return &g_proxies[tag - 1];
}

static long post_alloc(void (*fn)(void*), void* user)
{
    long tag = -1;
    pthread_mutex_lock(&g_tbl_mtx);
    int i = 0;
    for (; i < g_nposts; i++)
        if (!g_posts[i].used) break;
    if (i == g_nposts && g_nposts < (int)(sizeof(g_posts) / sizeof(g_posts[0])))
        g_nposts++;
    if (i < g_nposts)
    {
        g_posts[i].fn = fn;
        g_posts[i].user = user;
        g_posts[i].used = 1;
        tag = i + 1;
    }
    pthread_mutex_unlock(&g_tbl_mtx);
    return tag;
}

static post_entry post_take(long tag)
{
    post_entry r = {0, 0, 0};
    if (tag < 1) return r;
    pthread_mutex_lock(&g_tbl_mtx);
    int i = (int)(tag - 1);
    if (i < g_nposts && g_posts[i].used)
    {
        r = g_posts[i];
        g_posts[i].used = 0;
        g_posts[i].fn = NULL;
        g_posts[i].user = NULL;
    }
    pthread_mutex_unlock(&g_tbl_mtx);
    return r;
}

// ============================================================
// 参数编排：sc_jval[] → jvalue[]（SC_JV_STR 临时 jstring，调用后释放）
// ============================================================
static int marshal(JNIEnv* e, const sc_jval* a, int n, jvalue* out, jobject* tofree, int* nfree)
{
    *nfree = 0;
    if (n < 0 || n > SC_JNI_MAXARGS) return -1;
    for (int i = 0; i < n; i++)
    {
        switch (a[i].tag)
        {
            case SC_JV_INT:    out[i].i = a[i].v.i; break;
            case SC_JV_LONG:   out[i].j = a[i].v.j; break;
            case SC_JV_FLOAT:  out[i].f = a[i].v.f; break;
            case SC_JV_DOUBLE: out[i].d = a[i].v.d; break;
            case SC_JV_BOOL:   out[i].z = a[i].v.z ? JNI_TRUE : JNI_FALSE; break;
            case SC_JV_OBJ:    out[i].l = (jobject)(uintptr_t)a[i].v.o; break;
            case SC_JV_STR:
            {
                jstring s = (*e)->NewStringUTF(e, a[i].v.s ? a[i].v.s : "");
                out[i].l = s;
                tofree[(*nfree)++] = s;
                break;
            }
            default: out[i].l = NULL; break;
        }
    }
    return 0;
}

static void marshal_free(JNIEnv* e, jobject* tofree, int nfree)
{
    for (int i = 0; i < nfree; i++)
        if (tofree[i]) (*e)->DeleteLocalRef(e, tofree[i]);
}

static sc_jref to_global(JNIEnv* e, jobject local)
{
    if (!local) return 0;
    jobject g = (*e)->NewGlobalRef(e, local);
    (*e)->DeleteLocalRef(e, local);
    return (sc_jref)(uintptr_t)g;
}

// ============================================================
// 公开 API
// ============================================================
int sc_jni_available(void)
{
    return (sc_wsi_android_get_vm() != NULL) && g_bridge_ready;
}

sc_jref sc_jni_activity(void)
{
    // view-tree 外壳（ScActivity）形态：返回外壳 Activity 对象
    if (g_wsi.android.usesShell && g_wsi.android.shellActivity)
        return (sc_jref)(uintptr_t)g_wsi.android.shellActivity;   // 借用，勿 release
    // 纯 NativeActivity 形态
    if (!g_wsi.android.activity) return 0;
    return (sc_jref)(uintptr_t)g_wsi.android.activity->clazz;      // 借用，勿 release
}

sc_jref sc_jni_find_class(const char* name)
{
    JNIEnv* e = env_get();
    if (!e || !name) return 0;

    pthread_mutex_lock(&g_cls_mtx);
    for (int i = 0; i < g_nclasses; i++)
        if (strcmp(g_classes[i].name, name) == 0)
        {
            jclass c = g_classes[i].cls;
            pthread_mutex_unlock(&g_cls_mtx);
            return (sc_jref)(uintptr_t)c;
        }
    pthread_mutex_unlock(&g_cls_mtx);

    jclass local = (*e)->FindClass(e, name);
    if (!local) { exc_clear(e); return 0; }
    jclass g = (jclass)(*e)->NewGlobalRef(e, local);
    (*e)->DeleteLocalRef(e, local);

    pthread_mutex_lock(&g_cls_mtx);
    if (g_nclasses < (int)(sizeof(g_classes) / sizeof(g_classes[0])))
    {
        g_classes[g_nclasses].name = strdup(name);
        g_classes[g_nclasses].cls = g;
        g_nclasses++;
    }
    pthread_mutex_unlock(&g_cls_mtx);
    return (sc_jref)(uintptr_t)g;
}

sc_jmethod sc_jni_method(sc_jref clazz, const char* name, const char* sig)
{
    JNIEnv* e = env_get();
    if (!e || !clazz) return 0;
    jmethodID m = (*e)->GetMethodID(e, (jclass)(uintptr_t)clazz, name, sig);
    if (!m) exc_clear(e);
    return (sc_jmethod)(uintptr_t)m;
}

sc_jmethod sc_jni_static_method(sc_jref clazz, const char* name, const char* sig)
{
    JNIEnv* e = env_get();
    if (!e || !clazz) return 0;
    jmethodID m = (*e)->GetStaticMethodID(e, (jclass)(uintptr_t)clazz, name, sig);
    if (!m) exc_clear(e);
    return (sc_jmethod)(uintptr_t)m;
}

sc_jfield sc_jni_field(sc_jref clazz, const char* name, const char* sig)
{
    JNIEnv* e = env_get();
    if (!e || !clazz) return 0;
    jfieldID f = (*e)->GetFieldID(e, (jclass)(uintptr_t)clazz, name, sig);
    if (!f) exc_clear(e);
    return (sc_jfield)(uintptr_t)f;
}

sc_jfield sc_jni_static_field(sc_jref clazz, const char* name, const char* sig)
{
    JNIEnv* e = env_get();
    if (!e || !clazz) return 0;
    jfieldID f = (*e)->GetStaticFieldID(e, (jclass)(uintptr_t)clazz, name, sig);
    if (!f) exc_clear(e);
    return (sc_jfield)(uintptr_t)f;
}

sc_jref sc_jni_new(sc_jref clazz, sc_jmethod ctor, const sc_jval* args, int nargs)
{
    JNIEnv* e = env_get();
    if (!e || !clazz || !ctor) return 0;
    jvalue jv[SC_JNI_MAXARGS]; jobject tf[SC_JNI_MAXARGS]; int nf;
    if (marshal(e, args, nargs, jv, tf, &nf)) return 0;
    jobject r = (*e)->NewObjectA(e, (jclass)(uintptr_t)clazz, (jmethodID)(uintptr_t)ctor, jv);
    marshal_free(e, tf, nf);
    if ((*e)->ExceptionCheck(e)) { exc_clear(e); return 0; }
    return to_global(e, r);
}

void sc_jni_call_void(sc_jref obj, sc_jmethod m, const sc_jval* args, int nargs)
{
    JNIEnv* e = env_get();
    if (!e || !obj || !m) return;
    jvalue jv[SC_JNI_MAXARGS]; jobject tf[SC_JNI_MAXARGS]; int nf;
    if (marshal(e, args, nargs, jv, tf, &nf)) return;
    (*e)->CallVoidMethodA(e, (jobject)(uintptr_t)obj, (jmethodID)(uintptr_t)m, jv);
    marshal_free(e, tf, nf);
    if ((*e)->ExceptionCheck(e)) exc_clear(e);
}

sc_jref sc_jni_call_obj(sc_jref obj, sc_jmethod m, const sc_jval* args, int nargs)
{
    JNIEnv* e = env_get();
    if (!e || !obj || !m) return 0;
    jvalue jv[SC_JNI_MAXARGS]; jobject tf[SC_JNI_MAXARGS]; int nf;
    if (marshal(e, args, nargs, jv, tf, &nf)) return 0;
    jobject r = (*e)->CallObjectMethodA(e, (jobject)(uintptr_t)obj, (jmethodID)(uintptr_t)m, jv);
    marshal_free(e, tf, nf);
    if ((*e)->ExceptionCheck(e)) { exc_clear(e); return 0; }
    return to_global(e, r);
}

int32_t sc_jni_call_int(sc_jref obj, sc_jmethod m, const sc_jval* args, int nargs)
{
    JNIEnv* e = env_get();
    if (!e || !obj || !m) return 0;
    jvalue jv[SC_JNI_MAXARGS]; jobject tf[SC_JNI_MAXARGS]; int nf;
    if (marshal(e, args, nargs, jv, tf, &nf)) return 0;
    jint r = (*e)->CallIntMethodA(e, (jobject)(uintptr_t)obj, (jmethodID)(uintptr_t)m, jv);
    marshal_free(e, tf, nf);
    if ((*e)->ExceptionCheck(e)) { exc_clear(e); return 0; }
    return (int32_t)r;
}

int64_t sc_jni_call_long(sc_jref obj, sc_jmethod m, const sc_jval* args, int nargs)
{
    JNIEnv* e = env_get();
    if (!e || !obj || !m) return 0;
    jvalue jv[SC_JNI_MAXARGS]; jobject tf[SC_JNI_MAXARGS]; int nf;
    if (marshal(e, args, nargs, jv, tf, &nf)) return 0;
    jlong r = (*e)->CallLongMethodA(e, (jobject)(uintptr_t)obj, (jmethodID)(uintptr_t)m, jv);
    marshal_free(e, tf, nf);
    if ((*e)->ExceptionCheck(e)) { exc_clear(e); return 0; }
    return (int64_t)r;
}

float sc_jni_call_float(sc_jref obj, sc_jmethod m, const sc_jval* args, int nargs)
{
    JNIEnv* e = env_get();
    if (!e || !obj || !m) return 0.f;
    jvalue jv[SC_JNI_MAXARGS]; jobject tf[SC_JNI_MAXARGS]; int nf;
    if (marshal(e, args, nargs, jv, tf, &nf)) return 0.f;
    jfloat r = (*e)->CallFloatMethodA(e, (jobject)(uintptr_t)obj, (jmethodID)(uintptr_t)m, jv);
    marshal_free(e, tf, nf);
    if ((*e)->ExceptionCheck(e)) { exc_clear(e); return 0.f; }
    return (float)r;
}

int sc_jni_call_bool(sc_jref obj, sc_jmethod m, const sc_jval* args, int nargs)
{
    JNIEnv* e = env_get();
    if (!e || !obj || !m) return 0;
    jvalue jv[SC_JNI_MAXARGS]; jobject tf[SC_JNI_MAXARGS]; int nf;
    if (marshal(e, args, nargs, jv, tf, &nf)) return 0;
    jboolean r = (*e)->CallBooleanMethodA(e, (jobject)(uintptr_t)obj, (jmethodID)(uintptr_t)m, jv);
    marshal_free(e, tf, nf);
    if ((*e)->ExceptionCheck(e)) { exc_clear(e); return 0; }
    return r == JNI_TRUE ? 1 : 0;
}

void sc_jni_call_static_void(sc_jref clazz, sc_jmethod m, const sc_jval* args, int nargs)
{
    JNIEnv* e = env_get();
    if (!e || !clazz || !m) return;
    jvalue jv[SC_JNI_MAXARGS]; jobject tf[SC_JNI_MAXARGS]; int nf;
    if (marshal(e, args, nargs, jv, tf, &nf)) return;
    (*e)->CallStaticVoidMethodA(e, (jclass)(uintptr_t)clazz, (jmethodID)(uintptr_t)m, jv);
    marshal_free(e, tf, nf);
    if ((*e)->ExceptionCheck(e)) exc_clear(e);
}

sc_jref sc_jni_call_static_obj(sc_jref clazz, sc_jmethod m, const sc_jval* args, int nargs)
{
    JNIEnv* e = env_get();
    if (!e || !clazz || !m) return 0;
    jvalue jv[SC_JNI_MAXARGS]; jobject tf[SC_JNI_MAXARGS]; int nf;
    if (marshal(e, args, nargs, jv, tf, &nf)) return 0;
    jobject r = (*e)->CallStaticObjectMethodA(e, (jclass)(uintptr_t)clazz, (jmethodID)(uintptr_t)m, jv);
    marshal_free(e, tf, nf);
    if ((*e)->ExceptionCheck(e)) { exc_clear(e); return 0; }
    return to_global(e, r);
}

int32_t sc_jni_call_static_int(sc_jref clazz, sc_jmethod m, const sc_jval* args, int nargs)
{
    JNIEnv* e = env_get();
    if (!e || !clazz || !m) return 0;
    jvalue jv[SC_JNI_MAXARGS]; jobject tf[SC_JNI_MAXARGS]; int nf;
    if (marshal(e, args, nargs, jv, tf, &nf)) return 0;
    jint r = (*e)->CallStaticIntMethodA(e, (jclass)(uintptr_t)clazz, (jmethodID)(uintptr_t)m, jv);
    marshal_free(e, tf, nf);
    if ((*e)->ExceptionCheck(e)) { exc_clear(e); return 0; }
    return (int32_t)r;
}

int32_t sc_jni_get_static_int(sc_jref clazz, sc_jfield field)
{
    JNIEnv* e = env_get();
    if (!e || !clazz || !field) return 0;
    jint v = (*e)->GetStaticIntField(e, (jclass)(uintptr_t)clazz, (jfieldID)(uintptr_t)field);
    if ((*e)->ExceptionCheck(e)) { exc_clear(e); return 0; }
    return (int32_t)v;
}

char* sc_jni_string_utf8(sc_jref jstr)
{
    JNIEnv* e = env_get();
    if (!e || !jstr) return NULL;
    const char* c = (*e)->GetStringUTFChars(e, (jstring)(uintptr_t)jstr, NULL);
    if (!c) return NULL;
    char* d = strdup(c);
    (*e)->ReleaseStringUTFChars(e, (jstring)(uintptr_t)jstr, c);
    return d;
}

sc_jref sc_jni_new_string(const char* utf8)
{
    JNIEnv* e = env_get();
    if (!e) return 0;
    jstring j = (*e)->NewStringUTF(e, utf8 ? utf8 : "");
    return to_global(e, j);
}

sc_jref sc_jni_new_object_array(sc_jref elemClass, int n)
{
    JNIEnv* e = env_get();
    if (!e || !elemClass || n < 0) return 0;
    jobjectArray a = (*e)->NewObjectArray(e, n, (jclass)(uintptr_t)elemClass, NULL);
    if ((*e)->ExceptionCheck(e)) { exc_clear(e); return 0; }
    return to_global(e, a);
}

void sc_jni_set_object_array(sc_jref arr, int index, sc_jref value)
{
    JNIEnv* e = env_get();
    if (!e || !arr) return;
    (*e)->SetObjectArrayElement(e, (jobjectArray)(uintptr_t)arr, index, (jobject)(uintptr_t)value);
    if ((*e)->ExceptionCheck(e)) exc_clear(e);
}


void sc_jni_release(sc_jref obj)
{
    if (!obj) return;
    JNIEnv* e = env_get();
    if (!e) return;
    (*e)->DeleteGlobalRef(e, (jobject)(uintptr_t)obj);
}

sc_jref sc_jni_retain(sc_jref obj)
{
    if (!obj) return 0;
    JNIEnv* e = env_get();
    if (!e) return 0;
    jobject g = (*e)->NewGlobalRef(e, (jobject)(uintptr_t)obj);
    return (sc_jref)(uintptr_t)g;
}

int sc_jni_on_ui_thread(void)
{
    return g_bridge_ready && pthread_equal(pthread_self(), g_ui_thread);
}

void sc_jni_post_ui(void (*fn)(void* user), void* user)
{
    if (!fn) return;
    JNIEnv* e = env_get();
    if (!e || !g_bridge_ready)   // Bridge 缺失（纯 gpu app）：退化为同步执行
    {
        fn(user);
        return;
    }
    long tag = post_alloc(fn, user);
    if (tag < 0) { fn(user); return; }
    (*e)->CallStaticVoidMethod(e, g_bridge_cls, g_mid_postui, (jlong)tag);
    if ((*e)->ExceptionCheck(e))
    {
        exc_clear(e);
        post_entry pe = post_take(tag);
        if (pe.fn) pe.fn(pe.user);
    }
}

// UI 主线程同步执行（阻塞至完成）。
typedef struct
{
    void (*fn)(void*);
    void* user;
    pthread_mutex_t m;
    pthread_cond_t  c;
    int done;
} sync_task;

static void sync_trampoline(void* p)
{
    sync_task* t = (sync_task*)p;
    t->fn(t->user);
    pthread_mutex_lock(&t->m);
    t->done = 1;
    pthread_cond_signal(&t->c);
    pthread_mutex_unlock(&t->m);
}

void sc_jni_run_ui_sync(void (*fn)(void* user), void* user)
{
    if (!fn) return;
    if (!g_bridge_ready || sc_jni_on_ui_thread())   // 就地执行
    {
        fn(user);
        return;
    }
    sync_task t;
    t.fn = fn; t.user = user; t.done = 0;
    pthread_mutex_init(&t.m, NULL);
    pthread_cond_init(&t.c, NULL);
    sc_jni_post_ui(sync_trampoline, &t);
    pthread_mutex_lock(&t.m);
    while (!t.done) pthread_cond_wait(&t.c, &t.m);
    pthread_mutex_unlock(&t.m);
    pthread_mutex_destroy(&t.m);
    pthread_cond_destroy(&t.c);
}

// ---- UI 挂载根容器 ----
// view-tree 外壳（ScActivity）形态：根 = ScActivity 的 FrameLayout（其底层子视图是
// gpu 渲染的 SurfaceView，ui 控件叠其上）。该 FrameLayout 由 android_platform.c 的
// shell_nativeOnCreate 缓存为 g_wsi.android.uiRoot（global ref）。
// 纯 gpu（NativeActivity）形态：无原生 UI 外壳，返回 0——ui 控件无处挂载（属预期，
// 该档定位为无原生 UI 的纯 gpu 轻量档）。
sc_jref sc_wsi_android_ui_root(void)
{
    if (g_wsi.android.usesShell && g_wsi.android.uiRoot)
        return (sc_jref)(uintptr_t)g_wsi.android.uiRoot;   // 借用，勿 release

    JP_LOGW("sc_wsi_android_ui_root: 当前为纯 gpu(NativeActivity)形态，无 view-tree "
            "外壳可挂载 ui 控件；ui app 请在 manifest 使用 com.sc.wsi.ScActivity");
    return 0;
}


sc_jref sc_jni_new_proxy(const char* const* ifaces, int niface, sc_jni_invoke_cb cb, void* user)
{
    JNIEnv* e = env_get();
    if (!e || !g_bridge_ready || !cb || niface <= 0) return 0;
    long tag = proxy_alloc(cb, user);
    if (tag < 0) return 0;

    jobjectArray arr = (*e)->NewObjectArray(e, niface, g_string_cls, NULL);
    if (!arr) { exc_clear(e); return 0; }
    for (int i = 0; i < niface; i++)
    {
        jstring s = (*e)->NewStringUTF(e, ifaces[i] ? ifaces[i] : "");
        (*e)->SetObjectArrayElement(e, arr, i, s);
        (*e)->DeleteLocalRef(e, s);
    }
    jobject proxy = (*e)->CallStaticObjectMethod(e, g_bridge_cls, g_mid_newproxy, (jlong)tag, arr);
    (*e)->DeleteLocalRef(e, arr);
    if ((*e)->ExceptionCheck(e)) { exc_clear(e); return 0; }
    return to_global(e, proxy);
}

// ============================================================
// Bridge 的两个 native 方法（RegisterNatives 绑定，免符号名修饰）
// ============================================================
static jobject JNICALL bridge_native_invoke(JNIEnv* env, jclass cls,
                                            jlong tag, jstring method, jobjectArray args)
{
    (void)cls;
    proxy_entry* pe = proxy_lookup((long)tag);
    if (!pe || !pe->cb) return NULL;

    const char* m = method ? (*env)->GetStringUTFChars(env, method, NULL) : NULL;
    int n = args ? (*env)->GetArrayLength(env, args) : 0;
    if (n > SC_JNI_MAXARGS) n = SC_JNI_MAXARGS;

    sc_jval av[SC_JNI_MAXARGS];
    jobject held[SC_JNI_MAXARGS];
    int nheld = 0;
    for (int i = 0; i < n; i++)
    {
        jobject a = (*env)->GetObjectArrayElement(env, args, i);
        if (a)
        {
            jobject g = (*env)->NewGlobalRef(env, a);
            held[nheld++] = g;
            av[i] = sc_jv_o((sc_jref)(uintptr_t)g);
            (*env)->DeleteLocalRef(env, a);
        }
        else
            av[i] = sc_jv_o(0);
    }

    sc_jval ret = sc_jv_void();
    pe->cb(pe->user, m ? m : "", av, n, &ret);

    if (m) (*env)->ReleaseStringUTFChars(env, method, m);
    for (int i = 0; i < nheld; i++) (*env)->DeleteGlobalRef(env, held[i]);

    // 绝大多数接口方法（listener）返回 void → NULL。非 void 时回传对象句柄。
    if (ret.tag == SC_JV_OBJ && ret.v.o)
        return (jobject)(uintptr_t)ret.v.o;
    return NULL;
}

static void JNICALL bridge_native_run_ui(JNIEnv* env, jclass cls, jlong tag)
{
    (void)env; (void)cls;
    post_entry pe = post_take((long)tag);
    if (pe.fn) pe.fn(pe.user);
}

// ============================================================
// 注册入口：由 android_jni.c 的 JNI_OnLoad 调用（ScApplication 注册之后）
// ============================================================
int wsi_jni_proxy_register(JNIEnv* env)
{
    g_ui_thread = pthread_self();   // JNI_OnLoad 在 System.loadLibrary 的线程 = app 主线程

    jclass local = (*env)->FindClass(env, "com/sc/wsi/Bridge");
    if (!local)
    {
        // Bridge 未随 APK（纯 gpu、无 ui）——非致命：代理不可用，清异常返回。
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        return 0;
    }
    g_bridge_cls = (jclass)(*env)->NewGlobalRef(env, local);
    (*env)->DeleteLocalRef(env, local);

    g_mid_newproxy = (*env)->GetStaticMethodID(env, g_bridge_cls,
        "newProxy", "(J[Ljava/lang/String;)Ljava/lang/Object;");
    g_mid_postui = (*env)->GetStaticMethodID(env, g_bridge_cls, "postUi", "(J)V");

    static const JNINativeMethod methods[] = {
        { "nativeInvoke", "(JLjava/lang/String;[Ljava/lang/Object;)Ljava/lang/Object;",
          (void*)bridge_native_invoke },
        { "nativeRunUi", "(J)V", (void*)bridge_native_run_ui },
    };
    if ((*env)->RegisterNatives(env, g_bridge_cls, methods,
            (jint)(sizeof(methods) / sizeof(methods[0]))) != 0)
    {
        JP_LOGW("RegisterNatives(Bridge) 失败");
        exc_clear(env);
        return 0;
    }

    jclass s = (*env)->FindClass(env, "java/lang/String");
    g_string_cls = (jclass)(*env)->NewGlobalRef(env, s);
    (*env)->DeleteLocalRef(env, s);

    if (!g_mid_newproxy || !g_mid_postui || !g_string_cls)
    {
        exc_clear(env);
        return 0;
    }

    g_bridge_ready = 1;
    return 1;
}

#else  // 非 WSI_ANDROID：安全空实现（保持 wsi_jni.h 可跨平台包含）

int      sc_jni_available(void) { return 0; }
sc_jref  sc_jni_activity(void) { return 0; }
sc_jref  sc_wsi_android_ui_root(void) { return 0; }
sc_jref  sc_jni_find_class(const char* name) { (void)name; return 0; }
sc_jmethod sc_jni_method(sc_jref c, const char* n, const char* s) { (void)c; (void)n; (void)s; return 0; }
sc_jmethod sc_jni_static_method(sc_jref c, const char* n, const char* s) { (void)c; (void)n; (void)s; return 0; }
sc_jfield sc_jni_field(sc_jref c, const char* n, const char* s) { (void)c; (void)n; (void)s; return 0; }
sc_jfield sc_jni_static_field(sc_jref c, const char* n, const char* s) { (void)c; (void)n; (void)s; return 0; }
sc_jref  sc_jni_new(sc_jref c, sc_jmethod m, const sc_jval* a, int n) { (void)c; (void)m; (void)a; (void)n; return 0; }
void     sc_jni_call_void(sc_jref o, sc_jmethod m, const sc_jval* a, int n) { (void)o; (void)m; (void)a; (void)n; }
sc_jref  sc_jni_call_obj(sc_jref o, sc_jmethod m, const sc_jval* a, int n) { (void)o; (void)m; (void)a; (void)n; return 0; }
int32_t  sc_jni_call_int(sc_jref o, sc_jmethod m, const sc_jval* a, int n) { (void)o; (void)m; (void)a; (void)n; return 0; }
int64_t  sc_jni_call_long(sc_jref o, sc_jmethod m, const sc_jval* a, int n) { (void)o; (void)m; (void)a; (void)n; return 0; }
float    sc_jni_call_float(sc_jref o, sc_jmethod m, const sc_jval* a, int n) { (void)o; (void)m; (void)a; (void)n; return 0.f; }
int      sc_jni_call_bool(sc_jref o, sc_jmethod m, const sc_jval* a, int n) { (void)o; (void)m; (void)a; (void)n; return 0; }
void     sc_jni_call_static_void(sc_jref c, sc_jmethod m, const sc_jval* a, int n) { (void)c; (void)m; (void)a; (void)n; }
sc_jref  sc_jni_call_static_obj(sc_jref c, sc_jmethod m, const sc_jval* a, int n) { (void)c; (void)m; (void)a; (void)n; return 0; }
int32_t  sc_jni_call_static_int(sc_jref c, sc_jmethod m, const sc_jval* a, int n) { (void)c; (void)m; (void)a; (void)n; return 0; }
int32_t  sc_jni_get_static_int(sc_jref c, sc_jfield f) { (void)c; (void)f; return 0; }
char*    sc_jni_string_utf8(sc_jref j) { (void)j; return NULL; }
sc_jref  sc_jni_new_string(const char* s) { (void)s; return 0; }
sc_jref  sc_jni_new_object_array(sc_jref c, int n) { (void)c; (void)n; return 0; }
void     sc_jni_set_object_array(sc_jref a, int i, sc_jref v) { (void)a; (void)i; (void)v; }
void     sc_jni_release(sc_jref o) { (void)o; }
sc_jref  sc_jni_retain(sc_jref o) { (void)o; return 0; }
int      sc_jni_on_ui_thread(void) { return 0; }
void     sc_jni_post_ui(void (*fn)(void*), void* u) { if (fn) fn(u); }
void     sc_jni_run_ui_sync(void (*fn)(void*), void* u) { if (fn) fn(u); }
sc_jref  sc_jni_new_proxy(const char* const* i, int n, sc_jni_invoke_cb cb, void* u)
{ (void)i; (void)n; (void)cb; (void)u; return 0; }

#endif // WSI_ANDROID
