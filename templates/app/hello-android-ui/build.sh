#!/bin/bash
# ============================================================
# hello-android-ui 构建并运行 —— 验证 ui 原生控件后端 + wsi JNI 反射代理
#
# 与 hello-android/build.sh 同构，额外预编 ui 的 android 变体库（libui.<triple>.a）。
# app.sc 用了 inc ui.sc → add libui.a，故须先备好 android 变体。
#
# 前置：Android NDK（ANDROID_NDK_HOME）+ SDK（ANDROID_HOME）。
# 用法：
#   ANDROID_NDK_HOME=... ANDROID_HOME=... ./build.sh
# ============================================================
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"
ROOT="$(cd ../../.. && pwd)"

SCC="${SCC:-$ROOT/compiler/build/scc}"
[[ -x "$SCC" ]] || SCC="$(command -v scc || true)"
[[ -n "$SCC" ]] || { echo "错误：找不到 scc（先构建编译器或设 SCC=<路径>）"; exit 1; }

TARGET="$ROOT/templates/.scenv/targets/android.target"

# 1. 预编 wsi（含 ScApplication + Bridge dex）与 ui 的 android 变体库
echo "==> 构建 libwsi + wsi-android.dex（android）"
"$ROOT/templates/.scenv/modules/wsi/build.sh" --target "$TARGET"
echo "==> 构建 libui（android）"
"$ROOT/templates/.scenv/modules/ui/build.sh" --target "$TARGET"

# 2. 一条龙：构建 app → 打 APK → adb 部署启动
exec "$SCC" app.sc --target android
