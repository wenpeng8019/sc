#!/bin/bash
# ============================================================
# hello-ios-memimg 构建并运行 —— iOS(Metal) memimg 无表面渲染自检
#
# 结构同 hello-ios-gfx/build.sh：一条龙 scc（目标档驱动交叉编译 + 打包 .app +
# simctl 部署启动）。app.sc 在 on_after_startup 里跑一次性 Metal memimg 回读自检
# （MEMORY surface → 渲染 → dequeue → map → 控制台打印像素）。
#
# 校验：控制台看 "memimg 自检" 各行——center 应为三角形色、corner 应为深蓝底
#   BGRA≈76/25/12，末行 "Metal memimg 回读链路端到端 OK"。
#
# 前置：安装 Xcode（含 iOS 模拟器运行时与 xcrun）。
# 用法：
#   ./build.sh                 # 构建 + 装进模拟器 + 启动
#   DEV="iPhone 16" ./build.sh # 指定模拟器机型（默认 iPhone 16 Pro）
# ============================================================
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"
ROOT="$(cd ../../.. && pwd)"

SCC="${SCC:-$ROOT/compiler/build/scc}"
[[ -x "$SCC" ]] || SCC="$(command -v scc || true)"
[[ -n "$SCC" ]] || { echo "错误：找不到 scc（先构建编译器或设 SCC=<路径>）"; exit 1; }

command -v xcrun >/dev/null || { echo "错误：需要 Xcode 命令行工具（xcrun）"; exit 1; }

TARGET="$ROOT/templates/.scenv/targets/ios-sim.target"

echo "==> 构建 libwsi（iOS 模拟器）"
"$ROOT/templates/.scenv/modules/wsi/build.sh" --target "$TARGET"

exec "$SCC" app.sc --target ios-sim
