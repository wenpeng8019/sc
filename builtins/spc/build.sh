#!/bin/bash
# ============================================================
# build.sh —— sc spc（多维空间并行计算）构建脚本
#
# 产出：libspc.<triple>.a（宿主构建另生成 libspc.a）
#
# 后端矩阵（一期仅 darwin；与 gfx 不同——计算路不承载 GL）：
#   darwin → Metal kernel + MPSGraph 算子 + CoreML 整图（ANE）
#   linux  → 待补（Vulkan/GLES31 kernel、RKNN model）
#
# 依赖：utils/gpu（sc_gpu_device）；builtins/ts 头（编译期结构，无链接依赖）
# ============================================================
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

SRC_DIR="src"
TARGET=""
TARGET_EXPLICIT=""
CC=""
AR=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --target|-t) TARGET="$2"; TARGET_EXPLICIT=1; shift 2 ;;
        --cc)        CC="$2";     shift 2 ;;
        --ar)        AR="$2";     shift 2 ;;
        -h|--help)
            echo "用法: $0 [--target <triple>] [--cc <compiler>] [--ar <archiver>]"
            exit 0 ;;
        *) echo "未知参数: $1"; exit 1 ;;
    esac
done

if [[ -z "$CC" ]]; then CC="cc"; fi
if [[ -z "$AR" ]]; then AR="ar"; fi

if [[ -z "$TARGET" ]]; then
    TARGET=$($CC -dumpmachine 2>/dev/null || echo unknown)
fi

case "$TARGET" in
    *apple*|*darwin*) PLAT="darwin" ;;
    *linux*)          PLAT="linux" ;;
    *) echo "未知平台: $TARGET"; exit 1 ;;
esac

BUILD_DIR="build/$TARGET"
mkdir -p "$BUILD_DIR"

CFLAGS="-O2 -Wall -Wextra -I. -Isrc"
OBJS=()

compile_c() {
    local src="$1" extra="$2"
    local obj="$BUILD_DIR/$(basename "${src%.*}").o"
    echo "  CC $src"
    # shellcheck disable=SC2086
    $CC $CFLAGS $extra -c "$src" -o "$obj"
    OBJS+=("$obj")
}

echo "=== 构建 libspc ($TARGET) ==="

compile_c "$SRC_DIR/spc.c" ""

case "$PLAT" in
    darwin)
        compile_c "$SRC_DIR/metal_spc.m" "-fobjc-arc -x objective-c"
        compile_c "$SRC_DIR/mpsg_spc.m" "-fobjc-arc -x objective-c"
        compile_c "$SRC_DIR/coreml_spc.m" "-fobjc-arc -x objective-c"
        ;;
esac

LIB="libspc.$TARGET.a"
rm -f "$LIB"
REAL_OBJS=()
for o in "${OBJS[@]}"; do [[ -n "$o" ]] && REAL_OBJS+=("$o"); done
$AR rcs "$LIB" "${REAL_OBJS[@]}"
echo "  -> $LIB"

if [[ -z "$TARGET_EXPLICIT" ]]; then
    ln -sf "$LIB" libspc.a
    echo "  -> libspc.a -> $LIB"
fi
echo "=== done ==="
