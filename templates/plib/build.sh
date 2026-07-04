#!/bin/bash
# ============================================================
# build.sh —— 跨平台库构建脚本（支持 gcc/clang/MSVC）
#
# 用法：
#   ./build.sh                       # 宿主构建，自动检测平台和工具链
#   ./build.sh --target aarch64-linux-gnu  # 交叉编译
#   ./build.sh --cc cl --ar lib      # Windows MSVC
#   ./build.sh --cc x86_64-w64-mingw32-gcc --target x86_64-windows-gnu
#
# 产出：libplib.<triple>.a
#   宿主构建另生成 libplib.a（sc 本机构建直接解析）
#   交叉编译只生成带后缀版本（sc 通过 targetSuffix 匹配）
#
# 工具链：
#   gcc/clang  — Unix 系默认；macOS 用 -x objective-c 编译 .m
#   MSVC (cl)  — Windows 原生工具链；lib.exe 替代 ar
# ============================================================
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# ---- 参数解析 ----
TARGET=""
CC=""
AR=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --target|-t) TARGET="$2"; shift 2 ;;
        --cc)        CC="$2";     shift 2 ;;
        --ar)        AR="$2";     shift 2 ;;
        -h|--help)
            echo "用法: $0 [--target <triple>] [--cc <compiler>] [--ar <archiver>]"
            echo "  --target  目标三元组（默认自动检测）"
            echo "  --cc      C 编译器（默认自动检测）"
            echo "  --ar      打包工具（默认自动检测）"
            exit 0 ;;
        *) echo "未知参数: $1"; exit 1 ;;
    esac
done

# ---- 工具链自动检测 ----
detect_toolchain() {
    # 优先用显式指定的 CC/AR
    if [[ -z "$CC" ]]; then
        # MSVC 检测：cl.exe 存在且目标为 windows
        if command -v cl &>/dev/null && [[ "${TARGET:-host}" == *windows* || "${TARGET:-host}" == *msvc* ]]; then
            CC="cl"
            TOOLCHAIN="msvc"
        else
            CC="cc"
            TOOLCHAIN="gnu"
        fi
    else
        # 从 CC 名推断
        case "$(basename "$CC")" in
            cl|cl.exe) TOOLCHAIN="msvc" ;;
            *)         TOOLCHAIN="gnu" ;;
        esac
    fi

    if [[ -z "$AR" ]]; then
        case "$TOOLCHAIN" in
            msvc)  AR="lib" ;;
            gnu)   AR="ar"  ;;
        esac
    fi
}

# ---- 三重检测（先平台族才能做工具链检测）----
resolve_target() {
    if [[ -z "$TARGET" ]]; then
        case "$TOOLCHAIN" in
            msvc)
                # MSVC 无 -dumpmachine，从环境推断
                case "$(uname -m 2>/dev/null || echo x86_64)" in
                    x86_64|amd64) ARCH="x86_64" ;;
                    aarch64|arm64) ARCH="aarch64" ;;
                    *)  ARCH="x86_64" ;;
                esac
                TARGET="${ARCH}-windows-msvc"
                ;;
            gnu)
                TARGET=$("$CC" -dumpmachine 2>/dev/null || echo "unknown")
                ;;
        esac
        IS_HOST=1
    else
        IS_HOST=0
    fi
}

# 先粗略判定平台族（用于工具链检测）
case "${TARGET:-${1:-}}" in
    *windows*|*msvc*|*mingw*|*msys*|*cygwin*) PLAT_GUESS="windows" ;;
    *)                                        PLAT_GUESS="other" ;;
esac

detect_toolchain
resolve_target

echo "=== plib build ==="
echo "  TOOLCHAIN: ${TOOLCHAIN}"
echo "  CC:        ${CC}"
echo "  AR:        ${AR}"
echo "  TARGET:    ${TARGET}"
[[ $IS_HOST -eq 1 ]] && echo "  MODE:      host (auto-detect)"

# ---- 从三元组判定平台族 ----
case "$TARGET" in
    *-apple-darwin*)
        PLAT="darwin"
        SRCFILE="plib_darwin.m" ;;
    *-linux-*|*-linux)
        PLAT="linux"
        SRCFILE="plib_linux.c" ;;
    *-windows-*|*-mingw*|*-msys*|*-cygwin*|*-msvc*)
        PLAT="windows"
        SRCFILE="plib_win.c" ;;
    *)
        echo "错误: 无法从三元组 '${TARGET}' 判定平台族"
        echo "  支持: *-apple-darwin* / *-linux-* / *-windows-*"
        exit 1 ;;
esac

echo "  PLAT:      ${PLAT}"
echo "  SRC:       ${SRCFILE}"

# ---- 编译 ----
case "$TOOLCHAIN" in
    msvc)
        # MSVC: cl /nologo /c <src> /Fo:<obj>
        OBJ="plib.obj"
        # macOS 不需要 MSVC — windows 平台走 plib_win.c (纯 C，无需 -x)
        "$CC" /nologo /c "$SRCFILE" /Fo:"$OBJ"
        ;;
    gnu)
        OBJ="plib.o"
        case "$PLAT" in
            darwin) CFLAGS="-x objective-c" ;;
            *)      CFLAGS="" ;;
        esac
        "$CC" -c $CFLAGS "$SRCFILE" -o "$OBJ"
        ;;
esac

# ---- 打包（带三元组后缀）----
case "$TOOLCHAIN" in
    msvc)
        # MSVC lib 产出 .a：COFF 格式，仅扩展名不同，gcc/clang/msvc link 均可消费
        "$AR" /nologo /out:"libplib.${TARGET}.a" "$OBJ"
        ;;
    gnu)
        "$AR" rcs "libplib.${TARGET}.a" "$OBJ"
        ;;
esac
rm -f "$OBJ"
echo "  -> libplib.${TARGET}.a"

# ---- 宿主构建额外生成无后缀版本（软链接，不支持时回退复制）----
if [[ $IS_HOST -eq 1 ]]; then
    rm -f libplib.a
    ln -sf "libplib.${TARGET}.a" libplib.a 2>/dev/null || \
        cp "libplib.${TARGET}.a" libplib.a
    echo "  -> libplib.a -> libplib.${TARGET}.a"
fi

echo "=== done ==="
