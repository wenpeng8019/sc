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

echo "=== layout build ==="
echo "  TOOLCHAIN: $TOOLCHAIN"
echo "  CC:        $CC"
echo "  AR:        $AR"
echo "  TARGET:    $TARGET"
[[ $IS_HOST -eq 1 ]] && echo "  MODE:      host"

OBJ_DIR="build/$TARGET"
rm -rf "$OBJ_DIR"
mkdir -p "$OBJ_DIR"

compile_gnu() {
    local src="$1"
    local obj="$OBJ_DIR/$(basename "${src%.*}").o"
    "$CC" -c -I. "$SRC_DIR/$src" -o "$obj"
    ALL_OBJS="$ALL_OBJS $obj"
}

compile_msvc() {
    local src="$1"
    local obj="$OBJ_DIR/$(basename "${src%.*}").obj"
    "$CC" /nologo /c /I. "$SRC_DIR/$src" /Fo"$obj"
    ALL_OBJS="$ALL_OBJS $obj"
}

ALL_OBJS=""
case "$TOOLCHAIN" in
    gnu)
        compile_gnu "layout.c"
        ;;
    msvc)
        compile_msvc "layout.c"
        ;;
esac

case "$TOOLCHAIN" in
    msvc)
        "$AR" /nologo /out:"liblayout.$TARGET.a" $ALL_OBJS
        ;;
    gnu)
        "$AR" rcs "liblayout.$TARGET.a" $ALL_OBJS
        ;;
esac

rm -rf "$OBJ_DIR"
echo "  -> liblayout.$TARGET.a"

if [[ $IS_HOST -eq 1 ]]; then
    rm -f liblayout.a
    ln -sf "liblayout.$TARGET.a" liblayout.a 2>/dev/null || cp "liblayout.$TARGET.a" liblayout.a
    echo "  -> liblayout.a -> liblayout.$TARGET.a"
fi

echo "=== done ==="
