# hello-android —— Android app 脚手架（wsi Android 后端形态设计）

最小的 sc Android app 的**使用形态设计**：NativeActivity 拉起、无 main、渲染线程回调驱动。
与 [hello-ios](../hello-ios/) 几乎同构——同一份 app 逻辑（`after_startup`/`main_window_created`/`on_frame`/`before_cleanup`），
只换驱动入口。

> **状态：可用。** wsi 的 Android C 后端已实现并端到端跑通（NDK 交叉编译 → APK →
> 模拟器/真机；实测 Pixel_Tablet_API_31 arm64）。

## 目录内容

- [app.sc](app.sc) — app 逻辑（四回调 + `app_main` 入口，**无 main**）
- [build.sh](build.sh) — （重）编 wsi 的 android 变体，再跑 `scc app.sc --target android` 一条龙
- **无 `AndroidManifest.xml`**：本目录零清单即可跑——`android-pkg.sh` 打包前若发现
  app 目录无清单，会自动生成默认清单（`package`/`label` 由目录名派生、`lib_name`=
  产物基名 `app`、`ScApplication`+`NativeActivity`+`hasCode=true`）落到 build 目录。
  如需定制（图标/权限/多 Activity 等），在本目录放置 `AndroidManifest.xml` 即覆盖默认。
- wsi 侧垫片（可复用基础设施，不在本目录）：
  [ScApplication.java](../../utils/wsi/java/com/sc/wsi/ScApplication.java) — 进程级 Application 子类；
  [android_jni.c](../../utils/wsi/src/android_jni.c) — Application ⇄ wsi 的 JNI 桥
- 本 README

## 三类平台的入口/循环模型（为什么 Android 要单独一套）

| 平台 | 入口 | 主循环归属 | app 逻辑形态 |
|------|------|-----------|-------------|
| 桌面 | 程序自己的 `main` | 程序掌控：显式 `while` | 裸模板 |
| iOS | `main` → `UIApplicationMain` | 系统 app 对象封装：回调驱动 | `wsi_app_run(after_startup,main_window_created,frame,cleanup)` |
| **Android** | **无 main**；框架 `NativeActivity` → JNI → `ANativeActivity_onCreate` | 框架 Looper 推事件，**另起渲染线程**跑帧 | `wsi_app_run(after_startup,main_window_created,frame,cleanup)` |

两条 Android 事实决定了这套设计（见对话）：
1. **可直接用 NativeActivity**：`android.app.NativeActivity` 是框架**自带的 Java 类**，
   你写零行 Java，manifest 里引用它 + `meta-data android.app.lib_name` 指向你的 `.so`，
   框架经 JNI 调 `.so` 里的 `ANativeActivity_onCreate`。**它只管窗口/帧（tier B/C）**。
2. **进程级 init 归 Application**：Activity 生命周期 ≠ 进程生命周期。`Application.onCreate`
   才是「每进程一次、早于任何 Activity、最可靠」的 native init 落点——但 Application 是纯
   Java、无 native 入口。故 wsi 提供一个 **app 无关的 Application 子类 `ScApplication`**，
   把它的进程级回调经**自定义 JNI**（`android_jni.c`）桥到 wsi C 侧（tier A）。
   Application 无窗口、不做渲染，与 NativeActivity **并存**。

### 三层生命周期→Android 锚点映射

| 层 | 作用域 | wsi 接口 | Android 锚点 |
|----|--------|----------|-------------|
| **A 进程/库** | 每进程一次 | `sc_wsi_app_startup` | `ScApplication.onCreate` → JNI `nativeOnCreate` |
| **B/C 窗口+帧** | 每窗口/帧 | `main_window_created`/`on_frame`/`before_cleanup` | `NativeActivity` / `ANativeWindow` / `AChoreographer` |
| **可能被杀** | 进入后台 | 建议保存 / on_suspend | `Application.onTrimMemory` → JNI `nativeOnTrimMemory` |

> 为什么靠 `onTrimMemory` 而不靠 destroy：Android 后台被杀是 **SIGKILL、无回调**；
> `Application.onTerminate` **只在模拟器**会调，真机永不触发。故模型 = 尽早保存、
> 假设随时静默死亡；`onTrimMemory(UI_HIDDEN/COMPLETE)` 是「可能即将消失」的唯一可靠信号。

## 入口调用链（对照 iOS）

```
Launcher
  → 进程启动：框架先实例化 <application> = com.sc.wsi.ScApplication（tier A）
    → ScApplication.onCreate：System.loadLibrary("hello") → 触发 JNI_OnLoad（注册 native 桥）
      → nativeOnCreate → sc_wsi_app_startup    ← android_jni.c（进程/库级 init）
  → 启动 Launcher Activity = 框架 android.app.NativeActivity（tier B/C，manifest 声明）
    → JNI 加载 lib/<abi>/libhello.so（已加载，幂等）
      → ANativeActivity_onCreate      ← wsi 后端提供（打进 .so，非 app.sc）
        → wsi 建渲染线程 + ALooper，绑定 ANativeWindow / AInputQueue / AChoreographer
          → 在渲染线程调用 sc_app_main()   ← app.sc 的 app_main
            → wsi_app_run(after_startup, main_window_created, on_frame, before_cleanup)
               → 子系统就绪：after_startup()
               → ANativeWindow 就绪：main_window_created(win)（wsi 交付窗口 + 初始化 gpu/gfx）
               → 每次 vsync(AChoreographer)：on_frame()
               → ANativeWindow 销毁：win 的 destroy 委托（后台/旋转，释放交换链）
               → Activity 销毁：before_cleanup()
  进入后台/内存吐紧：Application.onTrimMemory → nativeOnTrimMemory → 建议保存/on_suspend
```

对照 iOS：iOS 的进程/库级 init 藏在后端 `wsi_app_run` 里（`sc_wsi_app_startup` 于
`didFinishLaunching` 前调用）；Android 没有等价的「app 对象 native 入口」，故用
`ScApplication` + 自定义 JNI 补上 tier A。窗口/帧级（tier B/C）两平台同构：
`main/app_main → wsi_app_run → main_window_created/on_frame/before_cleanup`（表面调用形式一致，底下
iOS = 自有主循环、Android = 事件注册）。

## 打包模型（含极小 dex 的 APK）

```
libapp.so（app.sc + libwsi.a 交叉编译，含 ANativeActivity_onCreate + sc_app_main + JNI 桥）
classes.dex（ScApplication.java → javac → d8；进程级垫片，wsi 预编交付 wsi-android.dex）
AndroidManifest.xml（app 目录未提供时由 android-pkg.sh 自动生成：Application=ScApplication +
        NativeActivity + lib_name=app + hasCode=true，落 build 目录）
        │  aapt 打包 + 塞入 lib/<abi>/libapp.so 与 /classes.dex
        ▼
base.apk → zipalign → apksigner(debug) → app.apk
        │  adb install + monkey 启动 LAUNCHER（NativeActivity）
        ▼
设备/模拟器由框架加载（先 ScApplication.onCreate → sc_wsi_app_startup，再 启动 NativeActivity）
```

- `hasCode="true"` + `ScApplication` → APK 含一个**极小 classes.dex**（单个 Application 子类）；
  这是为了拿到进程级生命周期钩子（tier A）——对照早期纯 native（hasCode=false、无 dex）形态。
- `lib_name` 必须与 scc 构建的 app 产物基名（默认 `app` → `libapp.so`）一致；自动生成的清单
  已用产物基名填入 activity 的 `android.app.lib_name` 与 <application> 级 `com.sc.wsi.lib_name`
  （`ScApplication` 据后者解析要 loadLibrary 的 .so）。

## 前置与用法（wsi 后端就绪后）

```sh
ANDROID_NDK_HOME=~/Android/ndk/27.x ANDROID_HOME=~/Android/sdk \
  ./templates/app/hello-android/build.sh
ABI=x86_64 API=24 ./build.sh          # 指定 ABI / 最低 API（默认 arm64-v8a / 24）
```

- 需 Android **NDK**（交叉编译）+ **SDK build-tools**（aapt/zipalign/apksigner）+
  **platform-tools**（adb）。M 芯片 Mac 用 arm64 系统镜像模拟器或真机。
- **看心跳**：Android 上 stdout 默认不进 logcat，build.sh 已 `setprop log.redirect-stdio true`，
  再 `adb logcat -s stdout:V`。真实 app 宜改用 `__android_log_print`。

## 二创接入点（待实现清单）

编译器 / wsi 侧尚需补齐，均为设计草图里已标注的挂接点：

> ✅ **已实现（本轮）：进程级 Application ⇄ JNI 桥（tier A）** ——
> `ScApplication.java`（wsi/java）+ `android_jni.c`（wsi/src，`JNI_OnLoad` RegisterNatives
> → `sc_wsi_app_startup` / 保存钩子）+ manifest 两锤点 + build.sh 的 javac→d8→dex 打包。
> 桥本身完整可用；桥到的 wsi 内部挂起/保存路径待 android 后端接（android_jni.c 内 TODO）。

1. **wsi Android 窗口/帧后端**（`templates/.scenv/modules/wsi/src/android_platform.c/.h`）：镜像
   `uikit_platform.m` 的 path A——`ANativeActivity_onCreate` 建渲染线程 + ALooper +
   command pipe；`ANativeWindow` create/destroy → 建/毁 surface（交 gpu 做 EGL/Vulkan）；
   `AInputQueue` → `impl_on_touch/key`；`AChoreographer` → 帧驱动 → `on_frame`；
   生命周期 → `impl_on_suspend/resume`。在渲染线程调 `sc_app_main`（app.sc 的 `app_main`）。
   （若它亦定 JNI_OnLoad，需与 android_jni.c 合并为一处注册。）
2. **wsi.sc**：已将统一入口导出为 `wsi_app_run` / `wsi_app_main_window`（回调类型
   `sc_wsi_app_cb`），iOS/Android 共用；app.sc 不再本地前向声明。
3. **native.c / platform.c / wsi_platform.h**：加 `SC_PLATFORM_ANDROID` 接线
   （枚举号已在 wsi.h 预留 0x60008，套 iOS 同样式）。
4. **scc 共享库输出**：app 无 main、需产 `.so`；scc 若无 `--shared` 模式，需补
   （build.sh 现经 `SCC_LDFLAGS=-shared` 传递为临时接入点）。另需让 scc 对
   `app_main` 像 `main` 一样安排模块 init/drop（`inc` 的模块生命周期）。

## 与 iOS 的同构对照（便于二创）

| 维度 | iOS（已实现） | Android（本草图） |
|------|--------------|-------------------|
| 进程/库 init（tier A） | 后端 `wsi_app_run` 内隐式调 `sc_wsi_app_startup` | `ScApplication.onCreate` → JNI `nativeOnCreate` → `sc_wsi_app_startup` |
| 框架入口 | `UIApplicationMain`（`wsi_app_run` 内） | `ANativeActivity_onCreate`（wsi 提供） |
| 逻辑入口符号 | `main`（scc 锚定模块生命周期） | `app_main` → `sc_app_main`（无 main，框架经 wsi 回调） |
| 运行 API（统一） | `wsi_app_run(4 cb)`＝自有主循环 | `wsi_app_run(4 cb)`＝事件注册 |
| 帧驱动 | CADisplayLink | AChoreographer |
| 窗口原生句柄 | UIView(CAMetalLayer) | ANativeWindow |
| 线程 | 全在主线程 | 另起渲染线程（框架 Looper 在主线程） |
| 打包 | .app + Info.plist（simctl） | APK + AndroidManifest + classes.dex（adb） |
| 生命周期 | resign/becomeActive → suspend/resume | Application.onTrimMemory → suspend/save（后台被杀无回调） |
