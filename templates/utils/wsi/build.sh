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
# 产出：libwsi.<triple>.a
#   宿主构建另生成 libwsi.a（sc 本机构建直接解析）
#   交叉编译只生成带后缀版本（sc 通过 targetSuffix 匹配）
#
# 平台适配（glfw 多后端设计：一个库可同时含多个后端，运行时选择）：
#   darwin  → Cocoa            (WSI_COCOA)
#   linux   → X11 + Wayland    (WSI_X11 + WSI_WAYLAND) + posix poll
#             二者同时编入，运行时由 wsi_select_platform 依 XDG_SESSION_TYPE /
#             WAYLAND_DISPLAY / DISPLAY 自动选择；Wayland 协议代码由 wayland-scanner
#             从 protocols/*.xml 现场生成。
#   windows → Win32            (WSI_WIN32)
#
#   线程/TLS/时钟统一由 sc 的 builtins/platform.h 跨平台层提供。
#
# 工具链：
#   gcc/clang  — Unix 系默认；macOS 用 -x objective-c 编译 .m
#   MSVC (cl)  — Windows 原生工具链；lib.exe 替代 ar
# ============================================================
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

SRC_DIR="src"
# sc 跨平台层头（platform.h：编译期 TLS + 单调时钟等）
BUILTINS_DIR="$(cd "${SCRIPT_DIR}/../../../builtins" && pwd)"

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

echo "=== wsi build ==="
echo "  TOOLCHAIN: ${TOOLCHAIN}"
echo "  CC:        ${CC}"
echo "  AR:        ${AR}"
echo "  TARGET:    ${TARGET}"
[[ $IS_HOST -eq 1 ]] && echo "  MODE:      host (auto-detect)"

# ---- 从三元组判定平台族 ----
# PLAT_DEFINES 可含多个后端宏（glfw 多后端）；WAYLAND=1 触发协议代码生成
WAYLAND=0
case "$TARGET" in
    *-apple-darwin*)
        PLAT="darwin"
        PLAT_DEFINES="WSI_COCOA"
        PLAT_SRCS="cocoa_init.m cocoa_monitor.m cocoa_window.m"
        POLL_SRCS=""
        EXTRA_CFLAGS="-x objective-c" ;;
    *-linux-*|*-linux)
        # glfw 多后端：Linux 同时编入 X11 与 Wayland，运行时由 wsi_select_platform 选择
        PLAT="linux"
        PLAT_DEFINES="WSI_X11 WSI_WAYLAND"
        PLAT_SRCS="x11_init.c x11_monitor.c x11_window.c xkb_unicode.c wl_init.c wl_monitor.c wl_window.c"
        POLL_SRCS="posix_poll.c"
        EXTRA_CFLAGS=""
        WAYLAND=1 ;;
    *-windows-*|*-mingw*|*-msys*|*-cygwin*|*-msvc*)
        PLAT="windows"
        # OEMRESOURCE：暴露 OCR_* 标准光标资源常量；须在任何 <windows.h> 之前生效，
        # 而 platform.h 会先行引入 <windows.h>，故经命令行 -D 保证最先定义（MSVC 必需）。
        PLAT_DEFINES="WSI_WIN32 OEMRESOURCE"
        PLAT_SRCS="win32_init.c win32_monitor.c win32_window.c"
        POLL_SRCS=""
        EXTRA_CFLAGS="" ;;
    *)
        echo "错误: 无法从三元组 '${TARGET}' 判定平台族"
        echo "  支持: *-apple-darwin* / *-linux-* / *-windows-*"
        exit 1 ;;
esac

# 共享源文件（所有平台）
SHARED_SRCS="init.c input.c monitor.c window.c platform.c native.c"
NULL_SRCS="null_init.c null_monitor.c null_window.c"

# 平台后端宏（可多个，glfw 多后端）→ 展开为 -D 传给编译器
DEFINE_FLAGS=""
for d in $PLAT_DEFINES; do DEFINE_FLAGS="$DEFINE_FLAGS -D$d"; done

echo "  PLAT:      ${PLAT} (${PLAT_DEFINES})"
echo "  PLAT_SRCS: ${PLAT_SRCS}"

# ---- 编译 ----
OBJ_DIR="build/${TARGET}"
rm -rf "$OBJ_DIR"
mkdir -p "$OBJ_DIR"

# ---- Wayland 协议代码生成（仅 Linux）----
# wl_*.c include 的 <proto>-client-protocol.h / -code.h 由 wayland-scanner 从
# wsi 自带的协议 XML（protocols/）现场生成到 build/wayland-protocols。
WL_INC=""
if [[ "${WAYLAND}" -eq 1 ]]; then
    if ! command -v wayland-scanner &>/dev/null; then
        echo "错误: 编译 Wayland 后端需要 wayland-scanner"
        echo "  Debian/Ubuntu: apt install libwayland-bin libwayland-dev wayland-protocols"
        echo "  Fedora:        dnf install wayland-devel wayland-protocols-devel"
        exit 1
    fi
    WL_XML_DIR="${SCRIPT_DIR}/protocols"
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
        base="${entry%%:*}"; xml="${WL_XML_DIR}/${entry##*:}"
        wayland-scanner client-header "$xml" "${WL_PROTO_DIR}/${base}.h"
        wayland-scanner private-code  "$xml" "${WL_PROTO_DIR}/${base}-code.h"
    done
    WL_INC="-I${WL_PROTO_DIR}"
    echo "  WAYLAND:   协议已生成 -> ${WL_PROTO_DIR}"
fi

ALL_OBJS=""

compile_gnu() {
    local src="$1"
    local obj="${OBJ_DIR}/$(basename "${src%.*}").o"
    local cflags="-c -I${SRC_DIR} -I${BUILTINS_DIR} ${WL_INC} ${DEFINE_FLAGS} -DWSI_SHARED -DWSI_EXPORTS"
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
    local defs=""
    for d in $PLAT_DEFINES; do defs="$defs /D$d"; done
    # MSYS/Git Bash 下 cl 需 Windows 形式的绝对包含路径：有 cygpath 则转换
    local binc="${BUILTINS_DIR}"
    command -v cygpath >/dev/null 2>&1 && binc="$(cygpath -w "${BUILTINS_DIR}")"
    echo "  CC $src"
    $CC /nologo /utf-8 /c /I"${SRC_DIR}" /I"${binc}" $defs /DWSI_SHARED /DWSI_EXPORTS "${SRC_DIR}/${src}" /Fo"$obj"
    ALL_OBJS="$ALL_OBJS $obj"
}

case "$TOOLCHAIN" in
    gnu)
        for src in $SHARED_SRCS $PLAT_SRCS $POLL_SRCS $NULL_SRCS; do
            [[ -f "${SRC_DIR}/${src}" ]] && compile_gnu "$src"
        done
        ;;
    msvc)
        for src in $SHARED_SRCS $PLAT_SRCS $POLL_SRCS $NULL_SRCS; do
            [[ -f "${SRC_DIR}/${src}" ]] && compile_msvc "$src"
        done
        ;;
esac

# ---- 打包（带三元组后缀）----
case "$TOOLCHAIN" in
    msvc)
        $AR /nologo /out:"libwsi.${TARGET}.a" $ALL_OBJS
        ;;
    gnu)
        $AR rcs "libwsi.${TARGET}.a" $ALL_OBJS
        ;;
esac
rm -rf "$OBJ_DIR"
echo "  -> libwsi.${TARGET}.a"

# ---- 宿主构建额外生成无后缀版本 ----
if [[ $IS_HOST -eq 1 ]]; then
    rm -f libwsi.a
    ln -sf "libwsi.${TARGET}.a" libwsi.a 2>/dev/null || \
        cp "libwsi.${TARGET}.a" libwsi.a
    echo "  -> libwsi.a -> libwsi.${TARGET}.a"
fi

echo "=== done ==="
