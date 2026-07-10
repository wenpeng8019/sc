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

name="$(basename "$SCRIPT_DIR")"
plain="lib${name}.a"
cross=0
for a in "$@"; do
    if [[ "$a" == "--target" || "$a" == "-o" ]]; then cross=1; fi
done
# 清旧别名/软链，令本次 scc 产真文件（而非写穿软链）
if [[ $cross -eq 0 && -e "$plain" ]]; then rm -f "$plain"; fi

"$SCC" . --build "$@" || exit $?

# host 构建（无 --target/-o，scc 产裸名 lib<名>.a）：改名为带三元组的目标变体
# lib<名>.<triple>.a（可入库交付），再反向软链 lib<名>.a → 变体，令 host demo 构建
# （add lib<名>.a 在空 suffix 下回退裸名）仍可解析——变相构建本平台的 target 变体。
# 三元组：SCC_TARGET_SUFFIX > SCC_TARGET_TRIPLE > $(cc -dumpmachine)。
if [[ $cross -eq 0 && -f "$plain" ]]; then
    triple="${SCC_TARGET_SUFFIX:-${SCC_TARGET_TRIPLE:-$(${SCC_CC:-cc} -dumpmachine 2>/dev/null)}}"
    if [[ -n "$triple" ]]; then
        variant="lib${name}.${triple}.a"
        mv -f "$plain" "$variant"
        ln -sf "$variant" "$plain"
        echo "  -> ${variant}（+ 反向软链 ${plain}）"
    fi
fi
