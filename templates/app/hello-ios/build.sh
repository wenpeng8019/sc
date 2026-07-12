#!/bin/bash
# ============================================================
# hello-ios 构建并运行 —— 一条龙：构建 → 打包 .app → iOS 模拟器部署启动
#
# 核心就是一条 scc 命令（目标档驱动交叉编译 + 打包 + 部署）：
#   scc app.sc --target ios-sim
# app.sc 有 main → scc 构建为可执行文件（iOS Mach-O，须打 .app 由模拟器加载，不能
# 本机直跑）；目标档配了 pkg/run → scc 依次调 ios-sim-pkg.sh（拼 .app）与
# ios-sim-run.sh（simctl 装+启动，--console 直连输出）。参数经环境变量契约传入
# （见 compiler.md §5.8）。M Mac 上模拟器是原生 arm64（同架构跑 iOS 运行时，速度好）。
#
# 本脚本只做两件事：先（重）编 wsi 的 iOS 模拟器变体库，再跑上面的 scc 一条龙。
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

# scc：环境变量 SCC > 仓库内构建产物 > PATH
SCC="${SCC:-$ROOT/compiler/build/scc}"
[[ -x "$SCC" ]] || SCC="$(command -v scc || true)"
[[ -n "$SCC" ]] || { echo "错误：找不到 scc（先构建编译器或设 SCC=<路径>）"; exit 1; }

command -v xcrun >/dev/null || { echo "错误：需要 Xcode 命令行工具（xcrun）"; exit 1; }

TARGET="$ROOT/templates/.scenv/targets/ios-sim.target"

# 1. （重）编 wsi 的 iOS 模拟器变体库（libwsi.arm64-apple-ios-simulator.a）
echo "==> 构建 libwsi（iOS 模拟器）"
"$ROOT/templates/.scenv/modules/wsi/build.sh" --target "$TARGET"

# 2. 一条龙：构建 app → 拼 .app → simctl 部署启动（--target 裸名经 .scenv/targets 自动解析）
exec "$SCC" app.sc --target ios-sim
