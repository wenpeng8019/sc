#!/bin/bash
# ============================================================
# hello-linux 构建并运行 —— Linux 桌面 app 脚手架（X11 · Wayland）
#
# 一键：为本机构建 wsi 库（含 wayland 协议头生成）→ scc 编译并运行 app.sc。
# X11/Wayland 运行时 dlopen，链接项（-lm -ldl）由 inc wsi.sc 自动注入。
#
# 前置（Debian/Ubuntu）：
#   apt install libwayland-bin libwayland-dev libx11-dev libxkbcommon-dev \
#               libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
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

# wsi 库：Linux 变体未随仓库入库，本机现场构建（build.sh 会先生成 wayland 协议头）
WSI="$ROOT/templates/.scenv/modules/wsi"
"$WSI/build.sh"

# 编译并运行
exec "$SCC" app.sc "$@"
