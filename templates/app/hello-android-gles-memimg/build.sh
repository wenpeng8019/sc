#!/bin/bash
# ============================================================
# hello-android-gles-memimg 构建并运行 —— GLES memimg（AHardwareBuffer）无表面渲染自检
#
# 结构同 hello-android-vk-memimg/build.sh：先（重）编 wsi 的 android 变体库与
# ScApplication dex，再跑 scc 一条龙。app.sc 在 on_after_startup 里跑一次性 GLES
# memimg 回读自检（MEMORY surface = AHardwareBuffer → EGLImage → GL 纹理 →
# 渲染 → dequeue → AHardwareBuffer_lock → logcat 打印像素）。
#
# AHardwareBuffer 全套 API 需 API 26+，故此处置 API=26（默认 24 不含 AHB，
# gpu 编译期 __ANDROID_API__>=26 门控失效 → memimg 不可用）。
#
# 校验：logcat 看 "memimg 自检" 各行——center 应为三角形色、corner 应为深蓝底
#   RGBA≈12/25/76，末行 "Android GLES memimg / AHardwareBuffer 回读链路端到端 OK"。
#
# 用法：
#   ANDROID_NDK_HOME=~/Android/ndk/27.x ANDROID_HOME=~/Android/sdk ./build.sh
#   DETACH=1 ./build.sh                    # 部署后不阻塞在 logcat（调试用）
# ============================================================
set -e
export API="${API:-26}"                     # AHardwareBuffer 需 API 26+
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"
ROOT="$(cd ../../.. && pwd)"

SCC="${SCC:-$ROOT/compiler/build/scc}"
[[ -x "$SCC" ]] || SCC="$(command -v scc || true)"
[[ -n "$SCC" ]] || { echo "错误：找不到 scc（先构建编译器或设 SCC=<路径>）"; exit 1; }

TARGET="$ROOT/templates/.scenv/targets/android.target"

echo "==> 构建 libwsi + wsi-android.dex（android，API=$API）"
"$ROOT/templates/.scenv/modules/wsi/build.sh" --target "$TARGET"

exec "$SCC" app.sc --target android
