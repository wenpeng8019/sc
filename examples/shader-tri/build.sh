#!/usr/bin/env bash
# ============================================================
# syntax-s demo 构建脚本：.ss → GLSL → SPIR-V → 本机可执行三角形
# ============================================================
set -euo pipefail
cd "$(dirname "$0")"

SCC=${SCC:-../../compiler/build/scc}
OUT=build
mkdir -p "$OUT"

OS="$(uname -s)"
case "$OS" in
    Darwin)
        BREW_PREFIX=$(brew --prefix 2>/dev/null || echo /opt/homebrew)
        CC_CMD=(cc -O2 -std=c11 main.c -o "$OUT/tri" \
            -I"$BREW_PREFIX/include" \
            -L"$BREW_PREFIX/lib" \
            -lglfw -lvulkan)
        ;;
    Linux)
        if ! command -v pkg-config >/dev/null 2>&1; then
            echo "错误: Linux 需 pkg-config（用于解析 glfw3/vulkan 编译参数）" >&2
            exit 1
        fi
        if ! pkg-config --exists glfw3 vulkan; then
            echo "错误: 未找到 glfw3 或 vulkan 的 pkg-config 条目" >&2
            echo "请先安装依赖，例如: sudo apt install -y libglfw3-dev libvulkan-dev" >&2
            exit 1
        fi
        CFLAGS="$(pkg-config --cflags glfw3 vulkan)"
        LIBS="$(pkg-config --libs glfw3 vulkan)"
        # shellcheck disable=SC2206
        CC_CMD=(cc -O2 -std=c11 main.c -o "$OUT/tri" $CFLAGS $LIBS)
        ;;
    *)
        echo "错误: 暂不支持的平台: $OS" >&2
        exit 1
        ;;
esac

echo "==> [1/3] scc: tri.ss → Vulkan-GLSL"
"$SCC" tri.ss -o "$OUT/x"      # 生成 vs_main.vert / fs_main.frag / tri.reflect.json

echo "==> [2/3] glslangValidator: GLSL → SPIR-V"
glslangValidator -V "$OUT/vs_main.vert" -o "$OUT/vs_main.spv"
glslangValidator -V "$OUT/fs_main.frag" -o "$OUT/fs_main.spv"

echo "==> [3/3] cc: 编译 Vulkan host"
"${CC_CMD[@]}"

echo "==> 完成: $OUT/tri  （运行: ./$OUT/tri）"
