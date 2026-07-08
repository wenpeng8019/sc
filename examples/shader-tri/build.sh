#!/usr/bin/env bash
# ============================================================
# syntax-s demo 构建脚本：.ss → GLSL → SPIR-V → 本机可执行三角形
# ============================================================
set -euo pipefail
cd "$(dirname "$0")"

SCC=${SCC:-../../compiler/build/scc}
BREW_PREFIX=$(brew --prefix 2>/dev/null || echo /opt/homebrew)
OUT=build
mkdir -p "$OUT"

echo "==> [1/3] scc: tri.ss → Vulkan-GLSL"
"$SCC" tri.ss -o "$OUT/x"      # 生成 vs_main.vert / fs_main.frag / tri.reflect.json

echo "==> [2/3] glslangValidator: GLSL → SPIR-V"
glslangValidator -V "$OUT/vs_main.vert" -o "$OUT/vs_main.spv"
glslangValidator -V "$OUT/fs_main.frag" -o "$OUT/fs_main.spv"

echo "==> [3/3] cc: 编译 Vulkan/MoltenVK host"
cc -O2 -std=c11 main.c -o "$OUT/tri" \
    -I"$BREW_PREFIX/include" \
    -L"$BREW_PREFIX/lib" \
    -lglfw -lvulkan

echo "==> 完成: $OUT/tri  （运行: ./$OUT/tri）"
