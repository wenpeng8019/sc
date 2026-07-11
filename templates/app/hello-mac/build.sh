#!/bin/bash
# ============================================================
# hello-mac 构建并运行 —— macOS 桌面 app 脚手架
#
# 一键：确保 wsi 库就绪 → scc 编译并运行 app.sc。
# Cocoa/IOKit/CoreFoundation 框架链接由 inc wsi.sc 自动注入，零 SCC_LDFLAGS。
#
# 用法：
#   ./build.sh              # 构建并运行
#   ./build.sh --build -o hello   # 只产二进制不运行（透传给 scc）
# ============================================================
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"
ROOT="$(cd ../../.. && pwd)"

# scc：环境变量 SCC > 仓库内构建产物 > PATH
SCC="${SCC:-$ROOT/compiler/build/scc}"
[[ -x "$SCC" ]] || SCC="$(command -v scc || true)"
[[ -n "$SCC" ]] || { echo "错误：找不到 scc（先构建编译器或设 SCC=<路径>）"; exit 1; }

# wsi 库（libwsi.a 已随仓库入库；缺失才现场构建）
WSI="$ROOT/templates/utils/wsi"
[[ -f "$WSI/libwsi.a" ]] || "$WSI/build.sh"

# 编译并运行
exec "$SCC" app.sc "$@"
