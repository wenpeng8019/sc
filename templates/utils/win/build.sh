#!/bin/bash
# ============================================================
# build.sh —— sc win 跨平台窗口库构建脚本
#
# 用法：
#   ./build.sh                       # 宿主构建，自动检测平台和工具链
#   ./build.sh --target aarch64-linux-gnu  # 交叉编译
#   ./build.sh --cc cl --ar lib      # Windows MSVC
#   ./build.sh --cc x86_64-w64-mingw32-gcc --target x86_64-windows-gnu
#
# 产出：libwin.<triple>.a
#   宿主构建另生成 libwin.a（sc 本机构建直接解析）
#   交叉编译只生成带后缀版本（sc 通过 targetSuffix 匹配）
#
# 平台适配：
#   darwin  → Cocoa   (_GLFW_COCOA)   + posix 线程/时间
#   linux   → X11     (_GLFW_X11)     + posix 线程/时间/poll
#   windows → Win32   (_GLFW_WIN32)   + win32 线程/时间
#
# 工具链：
#   gcc/clang  — Unix 系默认；macOS 用 -x objective-c 编译 .m
#   MSVC (cl)  — Windows 原生工具链；lib.exe 替代 ar
# ============================================================
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

SRC_DIR="src"

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
    if [[ -z "$CC" ]]; then
        if command -v cl &>/dev/null && [[ "${TARGET:-host}" == *windows* || "${TARGET:-host}" == *msvc* ]]; then
            CC="cl"
            TOOLCHAIN="msvc"
        else
            CC="cc"
            TOOLCHAIN="gnu"
        fi
    else
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

# ---- 三重检测 ----
resolve_target() {
    if [[ -z "$TARGET" ]]; then
        case "$TOOLCHAIN" in
            msvc)
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

echo "=== win build ==="
echo "  TOOLCHAIN: ${TOOLCHAIN}"
echo "  CC:        ${CC}"
echo "  AR:        ${AR}"
echo "  TARGET:    ${TARGET}"
[[ $IS_HOST -eq 1 ]] && echo "  MODE:      host (auto-detect)"

# ---- 从三元组判定平台族 ----
case "$TARGET" in
    *-apple-darwin*)
        PLAT="darwin"
        PLAT_DEFINE="_GLFW_COCOA"
        PLAT_SRCS="cocoa_init.m cocoa_monitor.m cocoa_window.m macos_time.c"
        THREAD_SRCS="posix_thread.c"
        TIME_SRCS=""  # macos_time.c already in PLAT_SRCS
        POLL_SRCS=""
        EXTRA_CFLAGS="-x objective-c" ;;
    *-linux-*|*-linux)
        PLAT="linux"
        PLAT_DEFINE="_GLFW_X11"
        PLAT_SRCS="x11_init.c x11_monitor.c x11_window.c xkb_unicode.c"
        THREAD_SRCS="posix_thread.c"
        TIME_SRCS="posix_time.c"
        POLL_SRCS="posix_poll.c"
        EXTRA_CFLAGS="" ;;
    *-windows-*|*-mingw*|*-msys*|*-cygwin*|*-msvc*)
        PLAT="windows"
        PLAT_DEFINE="_GLFW_WIN32"
        PLAT_SRCS="win32_init.c win32_monitor.c win32_window.c"
        THREAD_SRCS="win32_thread.c"
        TIME_SRCS="win32_time.c"
        POLL_SRCS=""
        EXTRA_CFLAGS="" ;;
    *)
        echo "错误: 无法从三元组 '${TARGET}' 判定平台族"
        echo "  支持: *-apple-darwin* / *-linux-* / *-windows-*"
        exit 1 ;;
esac

# 共享源文件（所有平台）
SHARED_SRCS="init.c input.c monitor.c window.c platform.c"
NULL_SRCS="null_init.c null_monitor.c null_window.c"

echo "  PLAT:      ${PLAT} (${PLAT_DEFINE})"
echo "  PLAT_SRCS: ${PLAT_SRCS}"

# ---- 编译 ----
OBJ_DIR="build/${TARGET}"
rm -rf "$OBJ_DIR"
mkdir -p "$OBJ_DIR"

ALL_OBJS=""

compile_gnu() {
    local src="$1"
    local obj="${OBJ_DIR}/$(basename "${src%.*}").o"
    local cflags="-c -I${SRC_DIR} -D${PLAT_DEFINE} -D_GLFW_BUILD_DLL"
    case "${src}" in
        *.m) cflags="$cflags -x objective-c" ;;
    esac
    echo "  CC $src"
    $CC $cflags "${SRC_DIR}/${src}" -o "$obj"
    ALL_OBJS="$ALL_OBJS $obj"
}

compile_msvc() {
    local src="$1"
    local obj="${OBJ_DIR}/$(basename "${src%.*}").obj"
    echo "  CC $src"
    $CC /nologo /c /I"${SRC_DIR}" /D"${PLAT_DEFINE}" /D_GLFW_BUILD_DLL "${SRC_DIR}/${src}" /Fo"$obj"
    ALL_OBJS="$ALL_OBJS $obj"
}

case "$TOOLCHAIN" in
    gnu)
        for src in $SHARED_SRCS $PLAT_SRCS $THREAD_SRCS $TIME_SRCS $POLL_SRCS $NULL_SRCS; do
            [[ -f "${SRC_DIR}/${src}" ]] && compile_gnu "$src"
        done
        ;;
    msvc)
        for src in $SHARED_SRCS $PLAT_SRCS $THREAD_SRCS $TIME_SRCS $POLL_SRCS $NULL_SRCS; do
            [[ -f "${SRC_DIR}/${src}" ]] && compile_msvc "$src"
        done
        ;;
esac

# ---- 打包（带三元组后缀）----
case "$TOOLCHAIN" in
    msvc)
        $AR /nologo /out:"libwin.${TARGET}.a" $ALL_OBJS
        ;;
    gnu)
        $AR rcs "libwin.${TARGET}.a" $ALL_OBJS
        ;;
esac
rm -rf "$OBJ_DIR"
echo "  -> libwin.${TARGET}.a"

# ---- 宿主构建额外生成无后缀版本 ----
if [[ $IS_HOST -eq 1 ]]; then
    rm -f libwin.a
    ln -sf "libwin.${TARGET}.a" libwin.a 2>/dev/null || \
        cp "libwin.${TARGET}.a" libwin.a
    echo "  -> libwin.a -> libwin.${TARGET}.a"
fi

echo "=== done ==="
