#ifndef SC_WSI_JNI_H
#define SC_WSI_JNI_H
/* ============================================================
 * wsi_jni.h —— wsi 提供的「通用 JNI 反射代理」C API
 * ============================================================
 * 目的：让上层（尤其 ui 模块的 android 后端）用**纯 C** 驱动任意
 *   android.widget.* / java.* 对象，而无需为每个控件/方法手写 JNI 胶水，
 *   也无需编写任何 per-feature 的 Java 代码。
 *
 * 边界（与 ScApplication 同级、收敛的「JNI trojan」）：
 *   - 创建 / 配置 / 读回：纯 C 经本 API 反射完成（FindClass/GetMethodID/Call*）。
 *   - 回调（Java→C）与主线程投递：由 wsi 内**唯一**的通用 Java shim
 *     com.sc.wsi.Bridge 承接（Proxy + InvocationHandler + Handler.post），
 *     它永不随控件增长。见 java/com/sc/wsi/Bridge.java。
 *
 * 线程：Android 的 View 操作必须在 UI 主线程。渲染/逻辑线程应经
 *   sc_jni_post_ui() 把变更投到主线程，且尽量批量（一帧一跳）。
 *
 * 句柄语义：sc_jref 内部是 JNI **global ref**（对调用者不透明），
 *   持久对象须 sc_jni_release() 释放；类句柄常驻缓存不必释放。
 *
 * 可用性：仅 android 且 JavaVM 就绪时可用；其它平台所有函数为安全空实现
 *   （sc_jni_available()==0，返回 0/NULL），故本头可无条件包含。
 * ============================================================ */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 不透明句柄：0 表示 null。sc_jref 承载 jobject/jclass 的 global ref；
 * sc_jmethod / sc_jfield 直接透传 jmethodID / jfieldID。 */
typedef uint64_t  sc_jref;
typedef uintptr_t sc_jmethod;
typedef uintptr_t sc_jfield;

/* 参数/返回变体标签。 */
enum
{
    SC_JV_VOID = 0,
    SC_JV_INT,
    SC_JV_LONG,
    SC_JV_FLOAT,
    SC_JV_DOUBLE,
    SC_JV_BOOL,
    SC_JV_OBJ,   /* v.o = sc_jref 对象句柄 */
    SC_JV_STR    /* v.s = UTF-8 C 串，调用期临时转 jstring（调用后释放） */
};

typedef struct sc_jval
{
    int tag;
    union
    {
        int32_t     i;
        int64_t     j;
        float       f;
        double      d;
        int         z;   /* bool：非 0 = true */
        sc_jref     o;
        const char* s;
    } v;
} sc_jval;

/* 便捷构造子。 */
static inline sc_jval sc_jv_i(int32_t x)     { sc_jval v; v.tag = SC_JV_INT;    v.v.i = x; return v; }
static inline sc_jval sc_jv_j(int64_t x)     { sc_jval v; v.tag = SC_JV_LONG;   v.v.j = x; return v; }
static inline sc_jval sc_jv_f(float x)       { sc_jval v; v.tag = SC_JV_FLOAT;  v.v.f = x; return v; }
static inline sc_jval sc_jv_d(double x)      { sc_jval v; v.tag = SC_JV_DOUBLE; v.v.d = x; return v; }
static inline sc_jval sc_jv_z(int x)         { sc_jval v; v.tag = SC_JV_BOOL;   v.v.z = x ? 1 : 0; return v; }
static inline sc_jval sc_jv_o(sc_jref x)     { sc_jval v; v.tag = SC_JV_OBJ;    v.v.o = x; return v; }
static inline sc_jval sc_jv_s(const char* x) { sc_jval v; v.tag = SC_JV_STR;    v.v.s = x; return v; }
static inline sc_jval sc_jv_void(void)       { sc_jval v; v.tag = SC_JV_VOID; v.v.j = 0; return v; }

/* ---------------- 可用性 / 上下文 ---------------- */

/* 代理是否可用（android 且 JavaVM/Bridge 就绪）。其它平台恒 0。 */
int sc_jni_available(void);

/* 当前 Activity 对象句柄（可作 android.content.Context 用，如 new Button(context)）。
 * 借用语义：不转移所有权，勿 release。未就绪返回 0。 */
sc_jref sc_jni_activity(void);

/* Android UI 覆盖层根容器（一个铺满窗口的 FrameLayout，经 Activity.addContentView
 * 叠加在 NativeActivity 的渲染表面之上，供 ui 模块挂载原生控件）。首次调用时在
 * UI 主线程惰性创建（会阻塞至创建完成）。借用语义：由 wsi 持有，勿 release。
 * 非 android 或未就绪返回 0。 */
sc_jref sc_wsi_android_ui_root(void);


/* ---------------- 类 / 方法 / 字段查找（内部缓存） ---------------- */

/* 查找类，name 用 "java/lang/String" 斜杠形式；返回常驻 global ref 句柄，
 * 失败返回 0。注意：跨线程 FindClass 只能解析框架类（android 与 java 包）；
 * app 自有类须在库加载期解析（本模块已缓存 com.sc.wsi.Bridge）。 */
sc_jref sc_jni_find_class(const char* name);

/* 实例 / 静态方法 ID（clazz 为 sc_jni_find_class 句柄）。失败返回 0。 */
sc_jmethod sc_jni_method(sc_jref clazz, const char* name, const char* sig);
sc_jmethod sc_jni_static_method(sc_jref clazz, const char* name, const char* sig);

/* 实例 / 静态字段 ID。失败返回 0。 */
sc_jfield sc_jni_field(sc_jref clazz, const char* name, const char* sig);
sc_jfield sc_jni_static_field(sc_jref clazz, const char* name, const char* sig);

/* ---------------- 构造 / 调用 ---------------- */

/* 构造对象（ctor = sc_jni_method(clazz, "<init>", sig)）。返回 global ref 句柄。 */
sc_jref sc_jni_new(sc_jref clazz, sc_jmethod ctor, const sc_jval* args, int nargs);

/* 实例方法调用（按返回类型选变体）。返回对象者给 global ref 句柄。 */
void    sc_jni_call_void (sc_jref obj, sc_jmethod m, const sc_jval* args, int nargs);
sc_jref sc_jni_call_obj  (sc_jref obj, sc_jmethod m, const sc_jval* args, int nargs);
int32_t sc_jni_call_int  (sc_jref obj, sc_jmethod m, const sc_jval* args, int nargs);
int64_t sc_jni_call_long (sc_jref obj, sc_jmethod m, const sc_jval* args, int nargs);
float   sc_jni_call_float(sc_jref obj, sc_jmethod m, const sc_jval* args, int nargs);
int     sc_jni_call_bool (sc_jref obj, sc_jmethod m, const sc_jval* args, int nargs);

/* 静态方法调用。 */
void    sc_jni_call_static_void(sc_jref clazz, sc_jmethod m, const sc_jval* args, int nargs);
sc_jref sc_jni_call_static_obj (sc_jref clazz, sc_jmethod m, const sc_jval* args, int nargs);
int32_t sc_jni_call_static_int (sc_jref clazz, sc_jmethod m, const sc_jval* args, int nargs);

/* 读取静态 int 字段（如 android.R.layout.* 资源 id）。失败返回 0。 */
int32_t sc_jni_get_static_int(sc_jref clazz, sc_jfield field);


/* ---------------- 字符串便捷 ---------------- */

/* java.lang.String 句柄 → 新分配的 UTF-8 C 串（调用者 free）。失败返回 NULL。 */
char* sc_jni_string_utf8(sc_jref jstr);

/* UTF-8 C 串 → 新建 java.lang.String（global ref 句柄）。用于把字符串传给
 * 需要 CharSequence/String 对象参数或塞进数组的场景。失败返回 0。 */
sc_jref sc_jni_new_string(const char* utf8);

/* ---------------- 对象数组 ---------------- */

/* 新建长度 n 的对象数组（元素初值 null，元素类型为 elemClass）。返回 global ref。 */
sc_jref sc_jni_new_object_array(sc_jref elemClass, int n);

/* 设置对象数组 index 处元素（value 可为 0=null）。 */
void sc_jni_set_object_array(sc_jref arr, int index, sc_jref value);


/* ---------------- 句柄生命周期 ---------------- */

/* 释放对象句柄（DeleteGlobalRef）。传 0 安全。 */
void sc_jni_release(sc_jref obj);

/* 将「回调期临时对象句柄」提升为常驻 global ref（返回新句柄）。 */
sc_jref sc_jni_retain(sc_jref obj);

/* ---------------- 主线程投递 ---------------- */

/* 把 native 闭包投到 Android UI 主线程执行（经 Bridge 的 Handler）。 */
void sc_jni_post_ui(void (*fn)(void* user), void* user);

/* 在 UI 主线程**同步**执行 fn（阻塞至完成）。若已在 UI 主线程或代理不可用，
 * 则就地直接执行。用于「创建控件后须立刻取回 jobject」这类同步语义。 */
void sc_jni_run_ui_sync(void (*fn)(void* user), void* user);

/* 当前是否在 UI 主线程。 */
int sc_jni_on_ui_thread(void);


/* ---------------- 回调代理（Java→C） ---------------- */

/* 接口方法被调用时触发：
 *   method = 被调 Java 方法名；args/nargs = 参数（对象为**回调期临时**句柄，
 *   仅本次回调有效，需持有请 sc_jni_retain）；ret = 返回值（默认 SC_JV_VOID
 *   表示无返回；填 SC_JV_OBJ/基本类型以回传，用于非 void 接口方法）。 */
typedef void (*sc_jni_invoke_cb)(void* user, const char* method,
                                 const sc_jval* args, int nargs, sc_jval* ret);

/* 造一个实现 ifaces 全部接口、把调用转发到 cb 的代理对象（如
 *   ifaces = {"android/view/View$OnClickListener"}）。返回 global ref 句柄，
 *   可传给 setOnClickListener 等。失败返回 0。cb/user 由内部登记，随代理对象
 *   存活；释放代理句柄后其登记项在下次 GC 侧调用时回收。 */
sc_jref sc_jni_new_proxy(const char* const* ifaces, int niface,
                         sc_jni_invoke_cb cb, void* user);

#ifdef __cplusplus
}
#endif

#endif /* SC_WSI_JNI_H */
