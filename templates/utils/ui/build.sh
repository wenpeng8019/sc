#!/bin/bash
# ============================================================
# build.sh —— ui 模块库构建（scc 薄包装）
#
# 平台后端宏在模块 .sc 段配置（scc 直读，见 compiler.md §7.4/§7.6）：
# darwin=SC_UI_COCOA / linux=SC_UI_NK / windows=SC_UI_WIN32，
# 全部 src/* 一起编，非选中后端由 SC_UI_* 守卫自空化。
#
# 用法：
#   ./build.sh                                   # 宿主构建 → libui.a
#   ./build.sh --target ../../targets/xx.target  # 交叉 → libui.<suffix|triple>.a
# ============================================================
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

SCC="${SCC:-${SCRIPT_DIR}/../../../compiler/build/scc}"
[[ -x "$SCC" ]] || SCC="$(command -v scc || true)"
[[ -n "$SCC" ]] || { echo "错误: 找不到 scc（先构建编译器或设 SCC=<路径>）"; exit 1; }

exec "$SCC" . --build "$@"
