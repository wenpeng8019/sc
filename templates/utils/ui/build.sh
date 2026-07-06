#!/bin/bash
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

SRC_DIR="src"
TARGET=""
CC=""
AR=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --target|-t) TARGET="$2"; shift 2 ;;
        --cc)        CC="$2"; shift 2 ;;
        --ar)        AR="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: $0 [--target <triple>] [--cc <compiler>] [--ar <archiver>]"
            exit 0 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

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
            msvc) AR="lib" ;;
            gnu)  AR="ar" ;;
        esac
    fi
}

resolve_target() {
    if [[ -z "$TARGET" ]]; then
        case "$TOOLCHAIN" in
            msvc)
                case "$(uname -m 2>/dev/null || echo x86_64)" in
                    x86_64|amd64) ARCH="x86_64" ;;
                    aarch64|arm64) ARCH="aarch64" ;;
                    *) ARCH="x86_64" ;;
                esac
                TARGET="${ARCH}-windows-msvc"
                ;;
            gnu)
                TARGET=$($CC -dumpmachine 2>/dev/null || echo unknown)
                ;;
        esac
        IS_HOST=1
    else
        IS_HOST=0
    fi
}

detect_toolchain
resolve_target

echo "=== ui build ==="
echo "  TOOLCHAIN: $TOOLCHAIN"
echo "  CC:        $CC"
echo "  AR:        $AR"
echo "  TARGET:    $TARGET"
[[ $IS_HOST -eq 1 ]] && echo "  MODE:      host"

# ---- 从三元组判定平台后端（参考 wsi）----
# 共享逻辑 ui.c + 平台后端（cocoa 已实现；其余用 null 空实现占位）
SHARED_SRCS="ui.c"
case "$TARGET" in
    *-apple-darwin*)
        PLAT="darwin"
        PLAT_DEFINES="SC_UI_COCOA"
        PLAT_SRCS="cocoa_ui.m" ;;
    *-windows-*|*-win32|*-msvc|*-mingw*|*-cygwin*)
        PLAT="win32"
        PLAT_DEFINES="SC_UI_WIN32"
        PLAT_SRCS="win32_ui.c" ;;
    *-linux-*|*-linux)
        PLAT="nk"
        PLAT_DEFINES="SC_UI_NK"
        PLAT_SRCS="nk_ui.c" ;;
    *)
        PLAT="null"
        PLAT_DEFINES=""
        PLAT_SRCS="null_ui.c" ;;
esac

DEFINE_FLAGS=""
for d in $PLAT_DEFINES; do DEFINE_FLAGS="$DEFINE_FLAGS -D$d"; done
MSVC_DEFS=""
for d in $PLAT_DEFINES; do MSVC_DEFS="$MSVC_DEFS /D$d"; done

echo "  PLAT:      $PLAT ($PLAT_DEFINES)"
echo "  PLAT_SRCS: $PLAT_SRCS"

OBJ_DIR="build/$TARGET"
rm -rf "$OBJ_DIR"
mkdir -p "$OBJ_DIR"

compile_gnu() {
    local src="$1"
    local obj="$OBJ_DIR/$(basename "${src%.*}").o"
    local cflags="-c -I. $DEFINE_FLAGS"
    case "$src" in
        *.m) cflags="$cflags -x objective-c" ;;
    esac
    echo "  CC $src"
    "$CC" $cflags "$SRC_DIR/$src" -o "$obj"
    ALL_OBJS="$ALL_OBJS $obj"
}

compile_msvc() {
    local src="$1"
    local obj="$OBJ_DIR/$(basename "${src%.*}").obj"
    echo "  CC $src"
    "$CC" /nologo /utf-8 /c /I. $MSVC_DEFS "$SRC_DIR/$src" /Fo"$obj"
    ALL_OBJS="$ALL_OBJS $obj"
}

ALL_OBJS=""
case "$TOOLCHAIN" in
    gnu)
        for src in $SHARED_SRCS $PLAT_SRCS; do compile_gnu "$src"; done
        ;;
    msvc)
        for src in $SHARED_SRCS $PLAT_SRCS; do compile_msvc "$src"; done
        ;;
esac

case "$TOOLCHAIN" in
    msvc)
        "$AR" /nologo /out:"libui.$TARGET.a" $ALL_OBJS
        ;;
    gnu)
        "$AR" rcs "libui.$TARGET.a" $ALL_OBJS
        ;;
esac

rm -rf "$OBJ_DIR"
echo "  -> libui.$TARGET.a"

if [[ $IS_HOST -eq 1 ]]; then
    rm -f libui.a
    ln -sf "libui.$TARGET.a" libui.a 2>/dev/null || cp "libui.$TARGET.a" libui.a
    echo "  -> libui.a -> libui.$TARGET.a"
fi

echo "=== done ==="
