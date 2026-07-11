# hello-android@gradle —— gradle 工程化打包 sc 原生 app

与 [hello-android](../hello-android/) **同一份 app 逻辑、同一套 native 模型**
（NativeActivity + `ANativeActivity_onCreate` + 无 main + 三回调），只把**打包编排**
从手工 aapt/zipalign/apksigner 换成 **gradle + Android Gradle Plugin (AGP)**。

> **状态：设计草图。** wsi 的 Android C 后端未实现，`sccBuild` 任务现会失败（同手工版）。
> 本目录用于呈现「gradle 工程化」这条路的使用形态。

## 与 hello-android（手工版）的关系

两者产物一致（同一个 `libhello.so` 打进 APK），差别只在**谁来编排打包**：

| 步骤 | hello-android（手工） | hello-android@gradle（gradle） |
|------|----------------------|------------------------------|
| 交叉编译 app.sc → `.so` | build.sh 里调 scc | 自定义任务 `sccBuild` 调 scc |
| 打包 APK | `aapt package` + `aapt add` | AGP 标准打包链（自动） |
| 对齐 | `zipalign` | AGP 自动 |
| 签名 | `apksigner`（debug keystore） | AGP `signingConfigs`（默认 debug 自动） |
| 安装/启动 | `adb install` + `am start` | `./gradlew installDebug` |

**关键设计**：sc 不是 CMake/ndk-build 工程，所以**不用** AGP 的 `externalNativeBuild`，
而是用一个 gradle 任务 `sccBuild` 调 scc 产出 `libhello.so` 落到 `jniLibs` 目录，
再交给 AGP 标准打包链。gradle 在这里只做「编排 + 打包 + 签名」，**编译仍是 scc**。

## 目录结构

```
hello-android@gradle/
├── build.sh                     # 一键 build + 装 + 启动（gradle 的薄封装）
├── settings.gradle              # 仓库 + 模块声明（:app）
├── build.gradle                 # 根：声明 AGP 版本
├── gradle.properties            # 全局属性
├── gradle/wrapper/
│   └── gradle-wrapper.properties # wrapper 版本（gradlew/jar 未入库，见下）
└── app/
    ├── build.gradle             # 核心：android{} + sccBuild 任务 + jniLibs 接线
    └── src/main/
        └── AndroidManifest.xml  # NativeActivity + lib_name=hello + hasCode=false
```

app.sc **不在本目录**——默认复用 `../hello-android/app.sc`（单一事实源），
由 `sccBuild` 任务引用；`-PscApp=<路径>` 可覆盖。

## sccBuild 任务干了什么（app/build.gradle）

```
wsiBuild        # 为 android 构建 libwsi（待 wsi 后端实现，现会失败）
  → sccBuild    # 按 abiFilters 逐 ABI：
      1. 生成 NDK clang/ar wrapper（cc-<abi>.sh / ar-<abi>.sh，同手工脚本）
      2. 导出 SCC_CC/SCC_AR/SCC_TARGET_* + SCC_LDFLAGS=-shared …
      3. scc app.sc --build -o build/scc-jniLibs/<abi>/libhello.so
  → preBuild    # AGP 公共前置，dependsOn sccBuild
  → AGP 打包链  # merge jniLibs → package APK → zipalign → sign
```

## 前置与用法（wsi 后端就绪后）

一键（build.sh 是 gradle 的薄封装，等价手工版 build.sh 的体验）：

```sh
ANDROID_HOME=~/Android/sdk ./templates/app/hello-android@gradle/build.sh   # build + 装 + 启动 + 看日志
TASK=assembleDebug ./build.sh                                              # 只打 APK
```

或直接用 gradle：

```sh
cd templates/app/hello-android@gradle
gradle wrapper                 # 首次：生成 gradlew + wrapper jar（二进制不入库）
./gradlew assembleDebug        # 交叉编译 + 打 APK → app/build/outputs/apk/debug/
./gradlew installDebug         # 装到已连接的设备/模拟器
adb shell am start -n com.sc.hellogradleandroid/android.app.NativeActivity
adb shell setprop log.redirect-stdio true && adb logcat -s stdout:V   # 看心跳
```

- 需 **Android SDK**（AGP/build-tools 由 gradle 自动拉）+ **NDK**（`ndkVersion` 指定的版本，
  `sdkmanager "ndk;27.0.12077973"`）+ 环境 `ANDROID_HOME`（或 `local.properties` 里 `sdk.dir`）。
- `SCC` 环境变量可指定 scc 路径（默认仓库内 `compiler/build/scc`）。
- gradlew/wrapper jar 是二进制，未入库；用系统 `gradle` 跑一次 `gradle wrapper` 生成。

## gradle 提供了什么额外价值（相对手工版）

- **依赖管理 / 仓库**（google + mavenCentral）——本纯 native app 用不上，但真实 app 需要。
- **变体**（buildType/flavor）、**签名配置**、**资源合并 / manifest merge**、增量与缓存。
- **Java/Kotlin → dex**（本工程 `hasCode=false` 无此步）。
- 与 Android Studio / CI 的一等集成。

## 二创接入点

1. 同 hello-android：wsi android C 后端、`wsi.sc` 导出、`.sc` 的 `[*android*]` 段、
   scc 共享库输出（见 [../hello-android/README.md](../hello-android/README.md) 的清单）。
2. gradle 专属：若日后 sc 提供 CMake 导出，可改用 AGP `externalNativeBuild { cmake {} }`
   让 gradle 直接驱动构建（省去 `sccBuild` 手写任务）；或封成一个可复用的 gradle 插件。
