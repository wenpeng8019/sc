#!/bin/bash
# ============================================================
# hello-android 构建并运行 —— 一条龙：构建 → 打包 APK → adb 部署启动
#
# 核心就是一条 scc 命令（目标档驱动交叉编译 + 打包 + 部署）：
#   scc app.sc --target android
# app.sc 无 main（@fnc app_main + ANativeActivity_onCreate 入口）→ scc 自动构建为
# 共享库 libapp.so；目标档配了 pkg/run → scc 依次调 android-pkg.sh（打 APK）与
# android-run.sh（adb 装+启动+logcat）。参数经环境变量契约传入（见 compiler.md §5.8）。
#
# 本脚本只做两件事：先（重）编 wsi 的 android 变体库与 ScApplication dex（模块自身
# 职责，非 app 职责——app 不该编 wsi 的 Java 垫片），再跑上面的 scc 一条龙。
#
# 打包工具链：不用 gradle，直接调 SDK 底层工具手工拼 APK（清单 + .so + dex + 签名），
# 呈现最小构建链路（细节见 templates/.scenv/targets/android-pkg.sh，与 hello-ios 手工拼 .app 对称）。
# 本目录无 AndroidManifest.xml——android-pkg.sh 打包前自动生成默认清单（零配置即可跑）；
# 如需定制，在本目录放 AndroidManifest.xml 即覆盖。
#
# 前置：Android NDK（ANDROID_NDK_HOME）+ SDK（ANDROID_HOME，含 build-tools/platform-tools）。
# 用法：
#   ANDROID_NDK_HOME=~/Android/ndk/27.x ANDROID_HOME=~/Android/sdk ./build.sh
#   ABI=x86_64 API=24 ./build.sh          # 指定 ABI/最低 API（默认 arm64-v8a / 24）
# ============================================================
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"
ROOT="$(cd ../../.. && pwd)"

# scc：环境变量 SCC > 仓库内构建产物 > PATH
SCC="${SCC:-$ROOT/compiler/build/scc}"
[[ -x "$SCC" ]] || SCC="$(command -v scc || true)"
[[ -n "$SCC" ]] || { echo "错误：找不到 scc（先构建编译器或设 SCC=<路径>）"; exit 1; }

TARGET="$ROOT/templates/.scenv/targets/android.target"

# 1. （重）编 wsi 的 android 变体库 + ScApplication dex（预编交付，app 不该编 wsi 的 Java 垫片）
echo "==> 构建 libwsi + wsi-android.dex（android）"
"$ROOT/templates/.scenv/modules/wsi/build.sh" --target "$TARGET"

# 2. 一条龙：构建 app → 打 APK → adb 部署启动（--target 裸名经 .scenv/targets 自动解析）
exec "$SCC" app.sc --target android
