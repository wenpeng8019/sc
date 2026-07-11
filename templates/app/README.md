# app —— 各平台 app 脚手架模板

把「打开一个原生窗口并跑起主循环」的最小工程，按平台拆成可直接复制起步的
脚手架。每个子目录是一个独立的 app 模板：拷走改名即为你自己的工程起点。

底座是 [wsi 窗口系统集成层](../utils/wsi/)（抽象自 glfw，只做窗口/输入/事件，
不含图形上下文——渲染由 gpu/gfx 模块经原生句柄接管）。

## 平台 app 模型（为什么要按平台分脚手架）

三类平台的「程序入口 + 主循环」归属根本不同，脚手架据此分形态：

| 平台 | main 入口 | 主循环归属 | 脚手架形态 |
|------|-----------|-----------|-----------|
| **桌面**（mac/win/linux） | 程序自己的 `main` | **程序掌控**：显式 `while` 泵事件 | 裸模板，本目录已实现 |
| **iOS** | 保留 `main`（→ `UIApplicationMain`） | 系统 app 对象**封装**：回调驱动帧 | `wsi_app_run(after_startup, main_window_created, frame, cleanup)`（wsi 已实现） |
| **Android** | **无 main**：`ANativeActivity` 接管，入口 `app_main` | 系统 framework **接管**，另起渲染线程跑帧 | `wsi_app_run(after_startup, main_window_created, frame, cleanup)`（脚手架已落地，wsi C 后端待补） |

- **桌面**：程序是主人——`main` 里显式写 `while (!should_close) { pump_events; update; render; }`。
- **iOS**：`main` 还在，但循环体被 `UIApplication` 收走了；你把「每帧要做的事」交给
  `frame` 回调，由 `CADisplayLink` 按屏幕刷新率回调你。
- **Android**：连 `main` 都没有——`ANativeActivity_onCreate` 是入口，事件/窗口/
  生命周期全由系统 `Looper` 投递。惯例是起一个工作线程跑你的循环。

> 桌面脚手架（本目录 mac/win/linux）与 iOS 脚手架（hello-ios）已落地并验证；
> hello-android 脚手架（设计草图）已落地，展示使用形态，待 wsi 的 android C 后端就绪后即可构建；
> 同一份 app 逻辑仅换驱动入口。

## 目录

- [hello-mac/](hello-mac/) — macOS（Cocoa）桌面 app 模板
- [hello-win/](hello-win/) — Windows（Win32）桌面 app 模板（本地 mingw 交叉编译）
- [hello-linux/](hello-linux/) — Linux（X11/Wayland）桌面 app 模板
- [hello-ios/](hello-ios/) — iOS（UIKit）app 模板（iOS 模拟器验证 wsi iOS 后端）
- [hello-android/](hello-android/) — Android（NativeActivity）app 模板（设计草图，wsi android 后端待补）
- [hello-android@gradle/](hello-android@gradle/) — 同上 Android 模型，但用 gradle/AGP 编排打包（对照手工版；设计草图）

每个目录内含：

- `app.sc` — app 逻辑（初始化 → 创建窗口 → 主循环 → 清理）
- `build.sh` — 一键构建并运行（封装好本平台的构建/链接细节）
- `README.md` — 该平台的前置依赖与用法

## 快速上手

```sh
# macOS（本机直接跑）
./templates/app/hello-mac/build.sh

# Linux（本机，需 X11/Wayland 开发库 + wayland-scanner）
./templates/app/hello-linux/build.sh

# Windows（在 macOS/Linux 上交叉编译出 hello.exe，需 mingw-w64）
./templates/app/hello-win/build.sh

# iOS（在 M 芯片 Mac 上用 iOS 模拟器运行，需 Xcode）
./templates/app/hello-ios/build.sh

# Android（设计草图；需 NDK + SDK，wsi android 后端就绪后可跑）
ANDROID_NDK_HOME=... ANDROID_HOME=... ./templates/app/hello-android/build.sh

# Android（gradle 工程化版，对照；需 SDK + NDK）
ANDROID_HOME=... ./templates/app/hello-android@gradle/build.sh
```

按 **ESC** 或点关闭按钮退出。
