#!/bin/sh
# ============================================================
# spc graph 面算子内核预编（design §18）——产物随 git 入库
# ============================================================
# 用法：改 kernels/*.ss 后从仓库根重跑本脚本（同 wsi .a 预编交付范式）：
#   builtins/spc/kernels/build.sh
# 产物（out/，git 入库；spc.sc `add` 编入运行时）：
#   matmul.shader.c/.h     GPU 三目标（vulkan/metal/gles 条目）
#   matmul_cpu.cpu.c       CPU SPMD 源码（构造器自注册）+ 其 shader.c reflect
#   conv2d / rowops / elementwise：四目标单文件（1D 无 shared，cpu 直编）
set -e
cd "$(dirname "$0")"
SCC="${SCC:-../../../compiler/build/scc}"
[ -x "$SCC" ] || { echo "错误: 未找到 scc（$SCC；先构建编译器）" >&2; exit 1; }

"$SCC" matmul.ss      -o out/matmul
"$SCC" matmul_cpu.ss  -o out/matmul_cpu
"$SCC" conv2d.ss      -o out/conv2d
"$SCC" rowops.ss      -o out/rowops
"$SCC" elementwise.ss -o out/elementwise
echo "kernels: 预编完成（out/ 入库）"
