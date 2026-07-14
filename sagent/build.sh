#!/bin/sh
# sagent 构建：sca 二进制 → build/sca
# 用法：./build.sh   （SCC 环境变量可指定编译器，缺省用仓库开发版）
set -e
cd "$(dirname "$0")"

SCC="${SCC:-../compiler/build/scc}"
if [ ! -x "$SCC" ]; then
    echo "sagent: 未找到 scc（$SCC）——先在仓库根 ./build.sh build" >&2
    exit 1
fi

mkdir -p build
"$SCC" sagent.sc --build -o build/sca
echo "sagent: 构建完成 → $(pwd)/build/sca"
