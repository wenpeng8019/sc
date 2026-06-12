#!/bin/bash
# sc 语言一键脚本：构建编译器、运行示例、安装 scc 与 VSCode 插件
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT/compiler/build"
PREFIX="${PREFIX:-/usr/local}"
EXT_BASE="$HOME/.vscode/extensions"
# 插件目录:安装名
EXTS=("vscode-sc:sc-lang-0.1.0" "vscode-sc-ast:sc-ast-view-0.1.0")

usage() {
    cat <<EOF
用法: ./build.sh <命令>

命令:
  build      构建 scc 编译器 (默认)
  test       构建并用 examples/feature*.sc 做端到端验证
  install    安装 scc 到 \$PREFIX/bin (默认 /usr/local/bin)，并安装 VSCode 插件（高亮 + AST 视图）
  uninstall  卸载 scc 与 VSCode 插件
  clean      清理构建产物
EOF
}

do_build() {
    echo "==> 构建 scc"
    cmake -B "$BUILD_DIR" -S "$ROOT/compiler" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR"
    echo "==> 完成: $BUILD_DIR/scc"
}

do_test() {
    do_build
    echo "==> 端到端验证 examples/feature*.sc"
    # 可运行特性系列：默认模式（编译+执行）
    local f
    for f in feature1 feature2 feature3 feature4 feature5 feature6 feature_forward; do
        echo "--- $f.sc（默认模式）---"
        "$BUILD_DIR/scc" "$ROOT/examples/$f.sc"
    done
    # emit-c 模式：转译 C 后手动编译运行
    echo "--- feature1.sc（--emit-c 模式）---"
    local tmp
    tmp="$(mktemp -d)"
    "$BUILD_DIR/scc" "$ROOT/examples/feature1.sc" --emit-c -o "$tmp/feature1.c"
    cc "$tmp/feature1.c" -o "$tmp/feature1"
    "$tmp/feature1"
    # @导出头文件生成（feature_export_inc 无 main，仅验证 .h）
    echo "--- feature_export_inc.sc（--emit-c 头文件）---"
    "$BUILD_DIR/scc" "$ROOT/examples/feature_export_inc.sc" --emit-c -o "$tmp/exp.c"
    [ -f "$tmp/exp.h" ] && echo "exp.h 已生成"
    rm -rf "$tmp"
    # 负向用例：应在语义阶段报错
    echo "--- feature_bad_value_cycle.sc（预期报错）---"
    if "$BUILD_DIR/scc" "$ROOT/examples/feature_bad_value_cycle.sc" 2>/dev/null; then
        echo "错误：负向用例未报错" >&2
        exit 1
    fi
    echo "已按预期报错"
    echo "==> 验证通过"
}

do_install() {
    do_build
    echo "==> 安装 scc -> $PREFIX/bin/scc"
    if [ -w "$PREFIX/bin" ]; then
        install -m 755 "$BUILD_DIR/scc" "$PREFIX/bin/scc"
    else
        sudo install -m 755 "$BUILD_DIR/scc" "$PREFIX/bin/scc"
    fi
    echo "==> 安装 VSCode 插件"
    mkdir -p "$EXT_BASE"
    for e in "${EXTS[@]}"; do
        local src="${e%%:*}" name="${e##*:}"
        rm -rf "$EXT_BASE/$name"
        ln -s "$ROOT/$src" "$EXT_BASE/$name"
        echo "    $src -> $EXT_BASE/$name"
    done
    echo "==> 安装完成（重启 VSCode 后 .sc 文件生效高亮与 AST 视图）"
}

do_uninstall() {
    echo "==> 卸载 scc 与 VSCode 插件"
    if [ -e "$PREFIX/bin/scc" ]; then
        if [ -w "$PREFIX/bin" ]; then rm -f "$PREFIX/bin/scc"; else sudo rm -f "$PREFIX/bin/scc"; fi
    fi
    for e in "${EXTS[@]}"; do
        rm -rf "$EXT_BASE/${e##*:}"
    done
    echo "==> 卸载完成"
}

do_clean() {
    rm -rf "$BUILD_DIR"
    echo "==> 已清理 $BUILD_DIR"
}

case "${1:-build}" in
    build)     do_build ;;
    test)      do_test ;;
    install)   do_install ;;
    uninstall) do_uninstall ;;
    clean)     do_clean ;;
    -h|--help|help) usage ;;
    *) usage; exit 1 ;;
esac
