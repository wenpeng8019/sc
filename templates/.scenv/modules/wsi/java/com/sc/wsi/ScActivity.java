// ScActivity.java — sc 的 view-tree 外壳 Activity
//
// 与纯 android.app.NativeActivity（框架独占整窗 Surface + AInputQueue，纯 gpu 轻量档）
// 相对：本 Activity 拥有 root（FrameLayout），底层放一张 SurfaceView 供 gpu 渲染，
// ui 控件（TextView/Button/... 由 ui 模块 android 后端经反射创建）叠在其上，成为
// SurfaceView 的兄弟视图，落在 FrameLayout 里，故既可显示、又能经 Java 视图系统收触摸。
//
// 生命周期 / SurfaceHolder 回调 / SurfaceView 区域触摸经下列 native 方法转交 wsi 的
// android_platform.c（复用其渲染线程 + 命令管道机制，见 wsi_android_shell_register）。
//
package com.sc.wsi;

import android.app.Activity;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.ViewGroup;
import android.widget.FrameLayout;

public class ScActivity extends Activity implements SurfaceHolder.Callback
{
    private static final String DEFAULT_LIB = "app";

    private FrameLayout root;         // ui 控件挂载根（返回给 sc_wsi_android_ui_root）
    private SurfaceView surfaceView;  // gpu 渲染面（root 的底层子视图）

    @Override
    protected void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        // 载入 app .so（若 ScApplication 已在 tier A 载入，则此处为 no-op）。
        // JNI_OnLoad 已注册 ScActivity 的 native 方法，nativeOnCreate 可安全调用。
        System.loadLibrary(resolveLibName());

        root = new FrameLayout(this);
        surfaceView = new SurfaceView(this);
        root.addView(surfaceView, new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        setContentView(root);
        surfaceView.getHolder().addCallback(this);

        float density = getResources().getDisplayMetrics().density;
        nativeOnCreate(this, root, density);
    }

    // ui 挂载根（供 sc_wsi_android_ui_root 经 native 缓存的 global ref 取用）
    public FrameLayout getUiRoot() { return root; }

    // ── SurfaceHolder.Callback：Surface → native（native 侧 ANativeWindow_fromSurface）
    @Override public void surfaceCreated(SurfaceHolder h) { nativeSurfaceCreated(h.getSurface()); }
    @Override public void surfaceChanged(SurfaceHolder h, int fmt, int w, int ht) { nativeSurfaceChanged(h.getSurface(), w, ht); }
    @Override public void surfaceDestroyed(SurfaceHolder h) { nativeSurfaceDestroyed(); }

    // ── 生命周期 → native
    @Override protected void onResume()  { super.onResume();  nativeOnResume(); }
    @Override protected void onPause()   { nativeOnPause();    super.onPause(); }
    @Override protected void onDestroy() { nativeOnDestroy();  super.onDestroy(); }
    @Override public void onWindowFocusChanged(boolean f) { super.onWindowFocusChanged(f); nativeOnFocus(f); }

    // ── SurfaceView 区域触摸 → native（控件触摸由视图系统各自消费，不落到这里）
    @Override
    public boolean onTouchEvent(MotionEvent e)
    {
        final int n = e.getPointerCount();
        final int[]   ids = new int[n];
        final float[] xs  = new float[n];
        final float[] ys  = new float[n];
        for (int i = 0; i < n; i++)
        {
            ids[i] = e.getPointerId(i);
            xs[i]  = e.getX(i);
            ys[i]  = e.getY(i);
        }
        nativeOnTouch(e.getActionMasked(), e.getActionIndex(), ids, xs, ys);
        return true;
    }

    // 解析 app 库名：优先 meta-data com.sc.wsi.lib_name，兼容 android.app.lib_name，
    // 缺省 "app"（与 ScApplication 一致）。
    private String resolveLibName()
    {
        try
        {
            ApplicationInfo ai = getPackageManager().getApplicationInfo(
                getPackageName(), PackageManager.GET_META_DATA);
            if (ai.metaData != null)
            {
                String n = ai.metaData.getString("com.sc.wsi.lib_name");
                if (n != null && !n.isEmpty()) return n;
                String an = ai.metaData.getString("android.app.lib_name");
                if (an != null && !an.isEmpty()) return an;
            }
        }
        catch (Exception ignored) {}
        return DEFAULT_LIB;
    }

    // ── native（由 wsi_android_shell_register 经 RegisterNatives 绑定）
    private native void nativeOnCreate(Object activity, Object uiRoot, float density);
    private native void nativeSurfaceCreated(Surface s);
    private native void nativeSurfaceChanged(Surface s, int w, int h);
    private native void nativeSurfaceDestroyed();
    private native void nativeOnResume();
    private native void nativeOnPause();
    private native void nativeOnDestroy();
    private native void nativeOnFocus(boolean focused);
    private native void nativeOnTouch(int action, int actionIndex, int[] ids, float[] xs, float[] ys);
}
