#!/bin/bash
# ============================================================
# build.sh —— sc gfx（渲染层）构建脚本
#
# 用法：
#   ./build.sh                       # 宿主构建，自动检测平台和工具链
#   ./build.sh --target aarch64-linux-gnu  # 交叉编译
#
# 产出：libgfx.<triple>.a（宿主构建另生成 libgfx.a）
#
# 依赖：utils/gpu（env 层，libgpu.a）——后端宏两库同套：
#   darwin  → Metal (SC_GPU_METAL) + GL (SC_GPU_GL) + null
#   linux   → GL (SC_GPU_GL) + null；--gles = OpenGL ES 3.0/3.1 形态
#   windows → null（GL 需加载器、D3D 后端待补）
# ============================================================
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

SRC_DIR="src"
TARGET=""
CC=""
AR=""
GLES=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --target|-t) TARGET="$2"; TARGET_EXPLICIT=1; shift 2 ;;
        --cc)        CC="$2";     shift 2 ;;
        --ar)        AR="$2";     shift 2 ;;
        --gles)      GLES=1;      shift ;;
        -h|--help)
            echo "用法: $0 [--target <triple>] [--cc <compiler>] [--ar <archiver>] [--gles]"
            exit 0 ;;
        *) echo "未知参数: $1"; exit 1 ;;
    esac
done

# ---- 工具链自动检测 ----
if [[ -z "$CC" ]]; then CC="cc"; fi
if [[ -z "$AR" ]]; then AR="ar"; fi

# ---- 目标三元组 ----
if [[ -z "$TARGET" ]]; then
    TARGET=$($CC -dumpmachine 2>/dev/null || echo unknown)
fi

case "$TARGET" in
    *apple*|*darwin*) PLAT="darwin" ;;
    *linux*)          PLAT="linux" ;;
    *windows*|*mingw*) PLAT="windows" ;;
    *) echo "未知平台: $TARGET"; exit 1 ;;
esac

BUILD_DIR="build/$TARGET"
mkdir -p "$BUILD_DIR"

CFLAGS="-O2 -Wall -Wextra -I. -Isrc"
OBJS=()

compile_c() {  # $1=src $2=extra flags
    local src="$1" extra="$2"
    local obj="$BUILD_DIR/$(basename "${src%.*}").o"
    echo "  CC $src"
    # shellcheck disable=SC2086
    $CC $CFLAGS $extra -c "$src" -o "$obj"
    OBJS+=("$obj")
}

echo "=== 构建 libgfx ($TARGET) ==="

# ---- 平台后端选择（与 utils/gpu 的 build.sh 同套宏） ----
BACKEND_DEFS=""
case "$PLAT" in
    darwin)  BACKEND_DEFS="-DSC_GPU_METAL -DSC_GPU_GL" ;;
    linux)   BACKEND_DEFS="-DSC_GPU_GL" ;;
    windows) BACKEND_DEFS="" ;;
esac
if [[ "$GLES" == 1 ]]; then
    if [[ "$PLAT" != linux ]]; then echo "--gles 仅支持 linux 平台"; exit 1; fi
    BACKEND_DEFS="$BACKEND_DEFS -DSC_GPU_GLES -I../gpu/khr"
    LIB_SUFFIX=".gles"
else
    LIB_SUFFIX=""
fi

# 公共层 + null 后端（所有平台）
compile_c "$SRC_DIR/gfx.c" "$BACKEND_DEFS"
compile_c "$SRC_DIR/gfx_reflect.c" "$BACKEND_DEFS"
compile_c "$SRC_DIR/null_gfx.c" "$BACKEND_DEFS"

case "$PLAT" in
    darwin)
        # Metal 渲染后端（ARC：后端私有体含 ObjC 强引用成员）
        compile_c "$SRC_DIR/metal_gfx.m" "$BACKEND_DEFS -fobjc-arc -x objective-c"
        compile_c "$SRC_DIR/gl_gfx.c" "$BACKEND_DEFS"
        ;;
    linux)
        compile_c "$SRC_DIR/gl_gfx.c" "$BACKEND_DEFS"
        ;;
esac

LIB="libgfx.$TARGET$LIB_SUFFIX.a"
rm -f "$LIB"
REAL_OBJS=()
for o in "${OBJS[@]}"; do [[ -n "$o" ]] && REAL_OBJS+=("$o"); done
$AR rcs "$LIB" "${REAL_OBJS[@]}"
echo "  -> $LIB"

# 宿主构建生成无后缀软链
if [[ -z "${TARGET_EXPLICIT:-}" ]]; then
    ln -sf "$LIB" libgfx.a
    echo "  -> libgfx.a -> $LIB"
fi
echo "=== done ==="
