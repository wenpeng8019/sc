#!/usr/bin/env bash
# ============================================================
# syntax-s 发行路径 demo：.ss → GLSL → SPIR-V → MSL → .metallib
# ------------------------------------------------------------
# 与 ../shader-tri（MoltenVK 运行时翻译 Vulkan→Metal）并列，这里走「离线转译」
# 发行链，直接产出原生 Metal 着色库：
#   scc tri.ss  → *.vert/*.frag        (Vulkan-GLSL)
#     → glslangValidator → *.spv       (SPIR-V，中枢 IR)
#     → spirv-cross --msl → *.metal    (Metal Shading Language)
#     → xcrun metal -c    → *.air      (Apple IR)
#     → xcrun metallib    → tri.metallib
#
# 本 demo（里程碑 A）只验证「发行侧着色器编译链」打通并产出可加载的 .metallib；
# 原生 Metal 渲染 host 为下一里程碑。着色器共用 ../shader-tri/tri.ss（单一真源）。
#
# 依赖：brew install glslang spirv-cross + Xcode/CLT 的 metal 工具链（xcrun metal）。
# ============================================================
set -euo pipefail
cd "$(dirname "$0")"

SCC=${SCC:-../../compiler/build/scc}
SRC=${SRC:-../shader-tri/tri.ss}     # 与 MoltenVK 版共用同一 .ss
MSL_VERSION=${MSL_VERSION:-20000}    # Metal 2.0（macOS 10.13+）
OUT=build
mkdir -p "$OUT"

# 阶段表（并行数组）：entry 同时用作 spirv-cross 的入口重命名目标（main→entry），
# 保证多阶段链接进同一 metallib 时函数名唯一，也方便 host 按名查函数。
STAGE_ENTRIES=(vs_main fs_main)   # scc 产出的入口名（= .ss 里的 stage 名）
STAGE_EXTS=(vert frag)            # scc 产出的 GLSL 扩展名
STAGE_KINDS=(vert frag)           # spirv-cross / metal 阶段种类

echo "==> [1/4] scc: $SRC → Vulkan-GLSL"
"$SCC" "$SRC" -o "$OUT/x"

airs=()
for i in "${!STAGE_ENTRIES[@]}"; do
    entry=${STAGE_ENTRIES[$i]}
    ext=${STAGE_EXTS[$i]}
    stage=${STAGE_KINDS[$i]}

    echo "==> [2/4] glslangValidator: $entry.$ext → SPIR-V"
    glslangValidator -V "$OUT/$entry.$ext" -o "$OUT/$entry.spv"

    echo "==> [3/4] spirv-cross: $entry.spv → MSL（入口 main→${entry}）"
    spirv-cross --msl --msl-version "$MSL_VERSION" --stage "$stage" \
        --rename-entry-point main "$entry" "$stage" \
        "$OUT/$entry.spv" --output "$OUT/$entry.metal"

    echo "==> [4/4] metal: $entry.metal → $entry.air"
    xcrun -sdk macosx metal -c "$OUT/$entry.metal" -o "$OUT/$entry.air"
    airs+=("$OUT/$entry.air")
done

echo "==> metallib: 链接 → tri.metallib"
xcrun -sdk macosx metallib "${airs[@]}" -o "$OUT/tri.metallib"

echo "==> 完成: $OUT/tri.metallib  （入口函数: vs_main / fs_main）"
