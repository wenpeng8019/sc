#!/bin/bash
# ============================================================
# build.sh —— sc gpu 模块构建脚本
#
# 用法：
#   ./build.sh                       # 宿主构建，自动检测平台和工具链
#   ./build.sh --target aarch64-linux-gnu  # 交叉编译
#
# 产出：libgpu.<triple>.a（宿主构建另生成 libgpu.a）
#
# 多后端（同 wsi/glfw：一库可含多后端，运行时选择）：
#   darwin  → Metal (SC_GPU_METAL) + GL (SC_GPU_GL) + null
#   linux   → GL (SC_GPU_GL) + null（Vulkan 后端待补）
#   windows → null（GL 需加载器、D3D 后端待补）
# ============================================================
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
        --cc)        CC="$2";     shift 2 ;;
        --ar)        AR="$2";     shift 2 ;;
        -h|--help)
            echo "用法: $0 [--target <triple>] [--cc <compiler>] [--ar <archiver>]"
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

echo "=== 构建 libgpu ($TARGET) ==="

# ---- 平台后端选择（宏需同时作用于公共层 gpu.c 的后端分派） ----
BACKEND_DEFS=""
case "$PLAT" in
    darwin)  BACKEND_DEFS="-DSC_GPU_METAL -DSC_GPU_GL" ;;
    linux)   BACKEND_DEFS="-DSC_GPU_GL" ;;
    windows) BACKEND_DEFS="" ;;                 # GL 需加载器、D3D 后端待补
esac

# 公共层 + null 后端（所有平台）
compile_c "$SRC_DIR/gpu.c" "$BACKEND_DEFS"
compile_c "$SRC_DIR/gpu_reflect.c" "$BACKEND_DEFS"
compile_c "$SRC_DIR/null_dev.c" "$BACKEND_DEFS"

case "$PLAT" in
    darwin)
        # Metal 后端（ARC：后端私有体含 ObjC 强引用成员）
        compile_c "$SRC_DIR/metal_dev.m" "$BACKEND_DEFS -fobjc-arc -x objective-c"
        # GL 后端（gl_ctx.c 用 NSOpenGL/ObjC，须按 objective-c 编译）
        compile_c "$SRC_DIR/gl_ctx.c" "$BACKEND_DEFS -fobjc-arc -x objective-c"
        compile_c "$SRC_DIR/gl_dev.c" "$BACKEND_DEFS"
        ;;
    linux)
        compile_c "$SRC_DIR/gl_ctx.c" "$BACKEND_DEFS"
        compile_c "$SRC_DIR/gl_dev.c" "$BACKEND_DEFS"
        ;;
esac

LIB="libgpu.$TARGET.a"
rm -f "$LIB"
# 过滤空元素
REAL_OBJS=()
for o in "${OBJS[@]}"; do [[ -n "$o" ]] && REAL_OBJS+=("$o"); done
$AR rcs "$LIB" "${REAL_OBJS[@]}"
echo "  -> $LIB"

# 宿主构建生成无后缀软链
if [[ -z "${TARGET_EXPLICIT:-}" ]]; then
    ln -sf "$LIB" libgpu.a
    echo "  -> libgpu.a -> $LIB"
fi
echo "=== done ==="
