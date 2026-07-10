#!/bin/bash
# ============================================================
# build.sh —— wsi 模块库构建（scc 薄包装）
#
# 平台后端宏 / 头路径 / 链接项全部在模块 .sc 段配置（scc 直读，
# 见 compiler.md §7.4/§7.6）；本脚本只做两件事：
#   1. Linux 目标：wayland-scanner 从 wayland-protocols/*.xml 生成协议头
#      到 build/wayland-protocols（wl_*.c include；[linux] 段 inc 指向）
#   2. 调 scc 模块库构建：scc . --build [透传参数]
#
# 用法：
#   ./build.sh                                   # 宿主构建 → libwsi.a
#   ./build.sh --target ../../targets/xx.target  # 交叉 → libwsi.<suffix|triple>.a
#   SCC_TARGET_TRIPLE=aarch64-linux-gnu ./build.sh
#
# 工具链选择、变体命名、MSVC 远端构建等一律走 scc 机制
# （SCC_CC/SCC_AR/--target 目标档/远程构建，见 compiler.md §5/§6）。
# ============================================================
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# scc：环境变量 SCC > 仓库内构建产物 > PATH
SCC="${SCC:-${SCRIPT_DIR}/../../../compiler/build/scc}"
[[ -x "$SCC" ]] || SCC="$(command -v scc || true)"
[[ -n "$SCC" ]] || { echo "错误: 找不到 scc（先构建编译器或设 SCC=<路径>）"; exit 1; }

# ---- Wayland 协议头生成（仅 Linux 目标需要）----
# 目标族判定：SCC_TARGET_TRIPLE / 参数串 / 宿主 uname，含 linux 即生成
NEED_WAYLAND=0
case "${SCC_TARGET_TRIPLE:-}$*$(uname -s)" in *[Ll]inux*) NEED_WAYLAND=1 ;; esac

if [[ $NEED_WAYLAND -eq 1 ]]; then
    if ! command -v wayland-scanner &>/dev/null; then
        echo "错误: Linux 目标需要 wayland-scanner 生成协议头"
        echo "  Debian/Ubuntu: apt install libwayland-bin libwayland-dev"
        exit 1
    fi
    WL_PROTO_DIR="build/wayland-protocols"
    rm -rf "$WL_PROTO_DIR"; mkdir -p "$WL_PROTO_DIR"
    # <生成基名>:<XML 文件名>（基名与 wl_*.c 的 #include 逐字对应）
    WL_PROTOS="wayland-client-protocol:wayland.xml \
xdg-shell-client-protocol:xdg-shell.xml \
xdg-decoration-unstable-v1-client-protocol:xdg-decoration-unstable-v1.xml \
viewporter-client-protocol:viewporter.xml \
relative-pointer-unstable-v1-client-protocol:relative-pointer-unstable-v1.xml \
pointer-constraints-unstable-v1-client-protocol:pointer-constraints-unstable-v1.xml \
fractional-scale-v1-client-protocol:fractional-scale-v1.xml \
xdg-activation-v1-client-protocol:xdg-activation-v1.xml \
idle-inhibit-unstable-v1-client-protocol:idle-inhibit-unstable-v1.xml"
    for entry in $WL_PROTOS; do
        base="${entry%%:*}"; xml="wayland-protocols/${entry##*:}"
        wayland-scanner client-header "$xml" "${WL_PROTO_DIR}/${base}.h"
        wayland-scanner private-code  "$xml" "${WL_PROTO_DIR}/${base}-code.h"
    done
    echo "wayland 协议头已生成 -> ${WL_PROTO_DIR}"
fi

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
