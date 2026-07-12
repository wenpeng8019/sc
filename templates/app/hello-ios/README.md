# hello-ios —— iOS app 脚手架（wsi iOS 后端验证 · 模拟器）

最小的 sc iOS app：启动即建全屏 UIKit 窗口、由 `CADisplayLink` 逐帧回调、
干净退出。同时是 wsi **iOS 后端形态**的验证工程。拷走改名即为你自己 iOS app 的起点。

## 前置

- 已构建 scc 编译器（`compiler/build/scc`）。
- 安装 Xcode（含 iOS 模拟器运行时），命令行工具 `xcrun`/`simctl` 可用。

## 构建并运行（iOS 模拟器）

```sh
./build.sh                  # 编译 → 打包 .app → 装进模拟器 → 启动（--console 打印 app 输出）
DEV="iPhone 16" ./build.sh  # 指定机型（默认 iPhone 16 Pro）
```

启动后模拟器显示全屏黑窗（CAMetalLayer 未接图形，纯窗口预期为黑），
终端每约 60 帧打印一次心跳，证明帧循环在跑：

```
hello-ios: 进入 UIKit 主循环（wsi_app_run）
hello-ios: 全屏窗口已创建 · 帧缓冲 1206 x 2622
hello-ios: 帧 60
hello-ios: 帧 120
...
```

## 为什么不是 `scc run`

iOS 可执行文件的 Mach-O 平台标记为 iOS，须打包成 `.app` bundle 由 iOS 运行环境
加载，不能 `./app` 直接跑。M 芯片 Mac 虽能跑 iOS app，但那是 App Store 分发路径，
无务实命令行入口。故走 iOS **模拟器**：`iphonesimulator` SDK 交叉编译 → 打包 `.app`
→ `xcrun simctl boot/install/launch`。M Mac 上模拟器是原生 arm64（跑 iOS 运行时，
非指令模拟），速度好。细节见 [build.sh](build.sh)。

## 循环模型（对照桌面）

- **桌面**（[hello-mac](../hello-mac/)）：`main` 里显式 `while` 泵事件，程序掌控循环。
- **iOS**：`main` 仍在，但只调 `wsi_app_run(after_startup, main_window_created, on_frame, before_cleanup)` 进入
  `UIApplicationMain`——循环体被 `UIApplication` 收走。该 API 与 Android 统一（表面
  调用形式一致；iOS 底下 = 自有主循环，Android = 事件注册）。app 逻辑改由四回调交付：
  - `after_startup`：子系统就绪、建窗前回调一次（可为 nil）。
  - `main_window_created`：主窗口创建时回调（参数为窗口句柄，恒非 nil）。移动端
    窗口恒全屏、由 wsi 自建（app **不再调** `wsi_win_create`）；在此初始化 gpu/gfx，
    并经 `wsi_win_set_callback` 挂 destroy 委托（窗口丢失经它交付）。
  - `on_frame`：`CADisplayLink` 每帧回调，更新 + 渲染 present。
  - `before_cleanup`：`applicationWillTerminate` 时调用（可为 nil）。
- `main` **不调** `wsi_app_startup`——后端在 `didFinishLaunching` 里自行启动，并自建全屏主窗口。

平台 app 模型总览见 [../README.md](../README.md)。

## 说明

- **尺寸**：iOS 恒全屏、忽略窗口尺寸（后端按 `UIScreen.bounds` 铺满）。窗口由 wsi 在
  `didFinishLaunching` 里自建（传 1×1 占位以过通用校验，尺寸被忽略），经 `main_window_created` 交付。
- **链接**：`inc wsi.sc` 按目标（`arm64-apple-ios-simulator`）自动注入 wsi 模块
  `.sc` 段 `[*simulator*]` 声明的框架（UIKit / QuartzCore / Metal / Foundation），
  无需手动传 `SCC_LDFLAGS`。
- **渲染**：本模板只做纯窗口（黑屏）。要上图形，在 `on_frame` 里接 gpu/gfx 模块
  （iOS = Metal 后端，CAMetalLayer 同款）。
- **打包**：本目录**无 `Info.plist`**——零清单即可跑：`ios-sim-pkg.sh` 拼 `.app` 前若
  发现 app 目录无 plist，会自动生成默认 plist（`CFBundleExecutable`=产物基名、bundle id /
  名称由目录名派生，含空 `UILaunchScreen` 换全屏、`LSRequiresIPhoneOS`/`MinimumOSVersion`/
  `UIDeviceFamily`）落到 build 目录。如需定制（图标/权限/方向），在本目录放 `Info.plist` 即覆盖。
- **真机**：本脚手架走模拟器；真机需签名 + 描述文件（`iphoneos` SDK、`arm64-apple-ios`
  三元组、`-mios-version-min`），wsi 的 `[*ios*]` 段已备设备形态。
