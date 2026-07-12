// ============================================================
// Bridge.java —— wsi 通用 JNI 反射 shim（唯一的 C↔Java 回调/投递边界）
//
// 定位（见 wsi_jni.h）：wsi 内**唯一**、且**永不随控件增长**的 Java 垫片。
//   上层（尤其 ui 的 android 后端）用纯 C 经 sc_jni_* 反射创建/配置/读回任意
//   android.widget.* / java.* 对象；唯独两件事 JNI 无法凭空造出、必须有字节码：
//     1) 回调（Java→C）：实现某 Java 接口并把调用转发给 native ——
//        用 java.lang.reflect.Proxy + 本类（InvocationHandler）forward 到 nativeInvoke。
//     2) 主线程投递：Handler.post 一个 Runnable，其 run() 回到 native ——
//        由 postUi() 承接，回到 nativeRunUi。
//
//   两条路径都只依赖「本类 + 两个 native 方法」这一固定集合；新增控件/事件
//   一律在 C 侧完成，无需改动本文件。
//
// 与 ScApplication 同属 com.sc.wsi 包，随 wsi 的 dex 一同交付（见 build.sh）。
// native 方法在 wsi 的 android_jni_proxy.c 实现，经 JNI_OnLoad → RegisterNatives 绑定。
// ============================================================
package com.sc.wsi;

import android.os.Handler;
import android.os.Looper;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;

public final class Bridge implements InvocationHandler {

    // 对应 native 侧回调登记表的 tag（sc_jni_new_proxy 分配）。
    private final long tag;

    private Bridge(long tag) { this.tag = tag; }

    // ---- 回调代理：造一个实现 ifaces 全部接口、转发到 native 的对象 ----
    // ifaces 用斜杠形式（如 "android/view/View$OnClickListener"），内部转点号。
    // 供 sc_jni_new_proxy 调用；失败（类找不到）返回 null。
    public static Object newProxy(long tag, String[] ifaces) {
        ClassLoader cl = Bridge.class.getClassLoader();
        Class<?>[] cs = new Class<?>[ifaces.length];
        for (int i = 0; i < ifaces.length; i++) {
            try {
                cs[i] = Class.forName(ifaces[i].replace('/', '.'), false, cl);
            } catch (ClassNotFoundException e) {
                return null;
            }
        }
        return Proxy.newProxyInstance(cl, cs, new Bridge(tag));
    }

    @Override
    public Object invoke(Object proxy, Method method, Object[] args) {
        // Object 基类方法本地处理（避免转发到 native 造成递归/NPE）。
        String name = method.getName();
        int argc = method.getParameterTypes().length;
        if (argc == 0) {
            if (name.equals("hashCode")) return System.identityHashCode(proxy);
            if (name.equals("toString"))
                return "sc.wsi.Bridge$Proxy@" + Integer.toHexString(System.identityHashCode(proxy));
        } else if (argc == 1 && name.equals("equals")) {
            return proxy == args[0];
        }
        // 其余转发到 native（listener 等接口方法多为 void 返回 → 返回 null）。
        return nativeInvoke(tag, name, args);
    }

    // ---- 主线程投递：把一个 native 回调 tag 投到 UI 主线程执行 ----
    private static final Handler UI = new Handler(Looper.getMainLooper());

    public static void postUi(final long tag) {
        UI.post(new Runnable() {
            @Override public void run() { nativeRunUi(tag); }
        });
    }

    // ---- native 实现（wsi 的 android_jni_proxy.c，RegisterNatives 绑定）----
    private static native Object nativeInvoke(long tag, String method, Object[] args);
    private static native void   nativeRunUi(long tag);
}
