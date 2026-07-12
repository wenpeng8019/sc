#!/bin/bash
# ============================================================
# hello-ios-ui 构建并运行 —— 一条龙：构建 → 打包 .app → iOS 模拟器部署启动
#
# 与 hello-ios 同链路，额外预编 ui 的 iOS 模拟器变体库。核心一条 scc 命令：
#   scc app.sc --target ios-sim
# app.sc 有 main → scc 构建为可执行文件（iOS Mach-O，须打 .app 由模拟器加载）；
# 目标档配了 pkg/run → scc 依次调 ios-sim-pkg.sh（拼 .app）与 ios-sim-run.sh
# （simctl 装+启动，--console 直连输出）。
#
# 前置：安装 Xcode（含 iOS 模拟器运行时与命令行工具 xcrun）。
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

# 1. （重）编 wsi 与 ui 的 iOS 模拟器变体库
echo "==> 构建 libwsi（iOS 模拟器）"
"$ROOT/templates/.scenv/modules/wsi/build.sh" --target "$TARGET"
echo "==> 构建 libui（iOS 模拟器）"
"$ROOT/templates/.scenv/modules/ui/build.sh" --target "$TARGET"

# 2. 一条龙：构建 app → 拼 .app → simctl 部署启动
exec "$SCC" app.sc --target ios-sim
