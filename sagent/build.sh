#!/bin/bash
# sagent 一键脚本：构建 / 安装 / 卸载 sca 二进制
set -e
cd "$(dirname "$0")"

SCC="${SCC:-../compiler/build/scc}"
PREFIX="${PREFIX:-/usr/local}"

usage() {
    cat <<EOF
用法: ./build.sh <命令>

命令:
  build      构建 sca → build/sca (默认)
  install    构建并安装 sca 到 \$PREFIX/bin (默认 /usr/local/bin)
  uninstall  卸载 \$PREFIX/bin/sca
  clean      清理构建产物
EOF
}

do_build() {
    if [ ! -x "$SCC" ]; then
        echo "sagent: 未找到 scc（$SCC）——先在仓库根 ./build.sh build" >&2
        exit 1
    fi
    mkdir -p build
    "$SCC" sagent.sc --build -o build/sca
    echo "sagent: 构建完成 → $(pwd)/build/sca"
}

do_install() {
    do_build
    echo "==> 安装 sca -> $PREFIX/bin/sca"
    if [ -w "$PREFIX/bin" ]; then
        install -m 755 build/sca "$PREFIX/bin/sca"
    else
        sudo install -m 755 build/sca "$PREFIX/bin/sca"
    fi
    echo "==> 安装完成（任意项目目录内 sca init 即可开始）"
}

do_uninstall() {
    echo "==> 卸载 $PREFIX/bin/sca"
    if [ -w "$PREFIX/bin" ]; then
        rm -f "$PREFIX/bin/sca"
    else
        sudo rm -f "$PREFIX/bin/sca"
    fi
    echo "==> 卸载完成"
}

case "${1:-build}" in
    build)     do_build ;;
    install)   do_install ;;
    uninstall) do_uninstall ;;
    clean)     rm -rf build; echo "sagent: 已清理 build/" ;;
    -h|--help|help) usage ;;
    *) usage; exit 1 ;;
esac
