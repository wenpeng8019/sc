#!/bin/bash
# ============================================================
# hello-win 构建 —— Windows 桌面 app 脚手架（本地 mingw-w64 交叉编译）
#
# 一键：为 Windows 目标交叉编译 wsi 库 → scc 交叉编译 app.sc → hello.exe。
# 产物是 Windows PE，本机（macOS/Linux）不能直接执行；拷到 Windows 运行，
# 或在 target 档里启用 wine（run = wine）后由本脚本直接跑。
#
# 前置：安装 mingw-w64 交叉工具链
#   macOS：brew install mingw-w64
#   Debian/Ubuntu：apt install mingw-w64
#
# 用法：
#   ./build.sh              # 交叉编译出 hello.exe
# ============================================================
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"
ROOT="$(cd ../../.. && pwd)"

# scc：环境变量 SCC > 仓库内构建产物 > PATH
SCC="${SCC:-$ROOT/compiler/build/scc}"
[[ -x "$SCC" ]] || SCC="$(command -v scc || true)"
[[ -n "$SCC" ]] || { echo "错误：找不到 scc（先构建编译器或设 SCC=<路径>）"; exit 1; }

TARGET="$ROOT/templates/.scenv/targets/windows-x64-mingw.target"

# wsi 库：为 Windows 目标交叉编译（产 libwsi.x86_64-windows-gnu.a，
# add libwsi.a 在该目标下自动匹配此变体）
WSI="$ROOT/templates/.scenv/modules/wsi"
"$WSI/build.sh" --target "$TARGET"

# 交叉编译 app → hello.exe（--target 裸名经 .scenv/targets 自动解析）
# win32 GUI 库经 SCC_LDFLAGS 传入（wsi 的 win32 后端依赖）；基线 ws2_32 在 target 档。
SCC_LDFLAGS="-lgdi32 -luser32 -lshell32 -limm32 -lole32 -loleaut32 -lversion -luuid -ldwmapi" \
    "$SCC" app.sc --build -o hello.exe --target windows-x64-mingw "$@"

echo "产物：$DIR/hello.exe（拷到 Windows 运行，或在 target 档启用 wine 后 ./build.sh 直接跑）"
