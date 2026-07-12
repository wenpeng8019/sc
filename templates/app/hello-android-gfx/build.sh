#!/bin/bash
# ============================================================
# hello-android-gfx 构建并运行 —— GLES 三角形（桌面 gpu_demo 的 Android 等效）
#
# 结构同 hello-android/build.sh（说明见该文件）：先（重）编 wsi 的 android 变体库
# 与 ScApplication dex，再跑 scc 一条龙（app.sc 无 main → 构建 libapp.so，
# 目标档 pkg/run 打 APK + adb 部署启动）。app.sc 额外 inc gpu.sc/gfx.sc，
# gpu/gfx 源码随 app 静态编入（GLES 后端，见 builtins/gpu|gfx 的 [*android*] 段）。
#
# 前置：着色器产物须含 gles300 三连——
#   ./compiler/build/scc templates/demo/gpu_shader/gpu_tri.ss -o templates/demo/gpu_shader/out/gpu_tri
# （gpu_tri.ss 已含 tar gles@300；out/ 产物随仓库入库，通常无需重跑）。
#
# 用法：
#   ANDROID_NDK_HOME=~/Android/ndk/27.x ANDROID_HOME=~/Android/sdk ./build.sh
#   ABI=x86_64 API=24 ./build.sh          # 指定 ABI/最低 API（默认 arm64-v8a / 24）
#   DETACH=1 ./build.sh                    # 部署后不阻塞在 logcat（调试用，见 sc-mobile-debug）
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

# 2. 一条龙：构建 app（gpu/gfx 静态编入）→ 打 APK → adb 部署启动
exec "$SCC" app.sc --target android
