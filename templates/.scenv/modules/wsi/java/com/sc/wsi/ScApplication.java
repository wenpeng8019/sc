// ============================================================
// ScApplication.java —— wsi 进程级生命周期垫片（tier A：进程/库）
//
// 背景（见对话里的三层生命周期模型）：
//   A 进程/库级：wsi_app_startup（每进程一次，最早、最可靠的 native 落点）
//   B/C 窗口+帧级：on_init/on_frame/on_cleanup（挂在 NativeActivity/ANativeWindow）
//   「进入后台/可能被杀」：onTrimMemory → 尽早保存（不可靠的 destroy 之外的唯一信号）
//
// Android 事实：
//   * NativeActivity 只覆盖 B/C（Activity 生命周期 ≠ 进程生命周期）。Application
//     才是「每进程一次、早于任何 Activity」的对象——但它是纯 Java、无 native 入口。
//   * 本类就是补上那个缺口：一个 app 无关的 Application 子类，把 Application 的
//     进程级回调经自定义 JNI（ScApplication 的 native 方法）桥到 wsi C 侧。
//   * onCreate  = 每进程一次、最早 → 触发 wsi 进程/库 init（sc_wsi_app_startup）。
//   * onTrimMemory(UI_HIDDEN/COMPLETE) = 进入后台/内存吃紧 ≈ 可能即将被杀 →
//     通知 wsi 尽早保存状态（Android 后台被杀是 SIGKILL，无 destroy 回调）。
//   * onTerminate = 仅模拟器会调，真机永不触发——不可依赖，仅作对称补全。
//
// 这是「接线蓝图」：Application ⇄ JNI 桥是完整可用的；桥到的 wsi C 侧进程级
// 钩子（android_jni.c）在 android 后端就绪前为占位/契约（带 TODO）。
//
// 与 NativeActivity 共存：Application 无窗口，不能取代 NativeActivity 做渲染；
// 二者并存——Application 管进程级 init，NativeActivity 管窗口/帧。引入本类会把
// manifest 的 hasCode 翻成 true（多出一个极小的 classes.dex）。
// ============================================================
package com.sc.wsi;

import android.app.Application;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;

public class ScApplication extends Application {

    // 缺省 native 库名（与 NativeActivity 的 android.app.lib_name 惯例一致）。
    // 可被 manifest 的 <application> 级 meta-data "com.sc.wsi.lib_name" 覆盖。
    private static final String DEFAULT_LIB = "hello";

    // —— 自定义 native 桥（实现见 wsi 的 android_jni.c，经 JNI_OnLoad 注册）——
    private native void nativeOnCreate();
    private native void nativeOnTrimMemory(int level);
    private native void nativeOnLowMemory();
    private native void nativeOnTerminate();

    @Override
    public void onCreate() {
        super.onCreate();
        // 早于任何 Activity 加载 app .so：触发其 JNI_OnLoad（注册本类 native 方法）。
        // System.loadLibrary 幂等——NativeActivity 稍后按 lib_name 再加载同一库不会重复。
        System.loadLibrary(resolveLibName());
        nativeOnCreate();   // → wsi 进程/库级 init（tier A）
    }

    @Override
    public void onTrimMemory(int level) {
        super.onTrimMemory(level);
        // TRIM_MEMORY_UI_HIDDEN(20) / TRIM_MEMORY_COMPLETE(80) ≈ 进入后台/可能被杀。
        nativeOnTrimMemory(level);   // → wsi 建议保存状态 / on_suspend
    }

    @Override
    public void onLowMemory() {
        super.onLowMemory();
        nativeOnLowMemory();
    }

    @Override
    public void onTerminate() {
        // 仅模拟器触发；真机进程被 SIGKILL 时不会调用——不可依赖。
        nativeOnTerminate();
        super.onTerminate();
    }

    // native 库名解析：优先 <application> 级 meta-data，否则回落缺省。
    private String resolveLibName() {
        try {
            ApplicationInfo ai = getPackageManager().getApplicationInfo(
                    getPackageName(), PackageManager.GET_META_DATA);
            if (ai.metaData != null) {
                String v = ai.metaData.getString("com.sc.wsi.lib_name");
                if (v != null && !v.isEmpty()) {
                    return v;
                }
            }
        } catch (PackageManager.NameNotFoundException e) {
            // 落回缺省
        }
        return DEFAULT_LIB;
    }
}
