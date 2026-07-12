#!/bin/bash
# ============================================================
# hello-android-vk-memimg 构建并运行 —— Vulkan memimg 无表面渲染自检
#
# 结构同 hello-android-vk/build.sh：先（重）编 wsi 的 android 变体库与
# ScApplication dex，再跑 scc 一条龙。app.sc 在 on_after_startup 里跑一次性
# Vulkan memimg 回读自检（MEMORY surface → 渲染 → dequeue → map → logcat 打印像素）。
#
# 校验：logcat 看 "memimg 自检" 各行——center 应为三角形色、corner 应为深蓝底
#   BGRA≈76/25/12，末行 "Vulkan memimg 回读链路端到端 OK"。
#
# 用法：
#   ANDROID_NDK_HOME=~/Android/ndk/27.x ANDROID_HOME=~/Android/sdk ./build.sh
#   DETACH=1 ./build.sh                    # 部署后不阻塞在 logcat（调试用）
# ============================================================
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"
ROOT="$(cd ../../.. && pwd)"

SCC="${SCC:-$ROOT/compiler/build/scc}"
[[ -x "$SCC" ]] || SCC="$(command -v scc || true)"
[[ -n "$SCC" ]] || { echo "错误：找不到 scc（先构建编译器或设 SCC=<路径>）"; exit 1; }

TARGET="$ROOT/templates/.scenv/targets/android.target"

echo "==> 构建 libwsi + wsi-android.dex（android）"
"$ROOT/templates/.scenv/modules/wsi/build.sh" --target "$TARGET"

exec "$SCC" app.sc --target android
