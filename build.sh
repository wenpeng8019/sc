#!/bin/bash
# sc 语言一键脚本：构建编译器、运行示例、安装 scc 与 VSCode 插件
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT/compiler/build"
PREFIX="${PREFIX:-/usr/local}"
EXT_BASE="$HOME/.vscode/extensions"
# 插件目录:安装名
EXTS=("vscode-sc:sc-lang-0.1.0" "vscode-sg:sg-lang-0.1.0" "vscode-ast:sc-ast-view-0.1.0")

usage() {
    cat <<EOF
用法: ./build.sh <命令>

命令:
  build      构建 scc 编译器 (默认)
  dist       构建发行版 scc（builtins 资源 + 预编译 adt.a 内嵌，单二进制免携带 builtins）
  test       构建 + examples 冒烟运行 + tests 黄金快照回归（--emit-c/--emit-sc 产物比对）
             加 --update 重新生成黄金文件：./build.sh test --update
  install    安装 scc 到 \$PREFIX/bin (默认 /usr/local/bin)，并安装 VSCode 插件（高亮 + AST 视图 + Markdown 预览 sc 高亮）
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

do_dist() {
    echo "==> 构建发行版 scc（内嵌 builtins）"
    cmake -B "$BUILD_DIR-dist" -S "$ROOT/compiler" -DCMAKE_BUILD_TYPE=Release -DSCC_EMBED_BUILTINS=ON
    cmake --build "$BUILD_DIR-dist"
    # 冒烟验证：在无 builtins 目录的环境下编译运行 adt 程序（依赖内嵌资源释放）
    echo "==> 验证内嵌 builtins（脱离仓库目录运行）"
    local tmp
    tmp="$(mktemp -d)"
    cat > "$tmp/t.sc" <<'EOF'
inc adt.sc
inc mt.sc
inc mem.sc
fnc main: i4
    var s: string
    s.append("dist-ok")
    var mu: mutex
    mu.lock()
    var p: & = chunk(128)
    recycle(p)
    mem_teardown()
    printf("%s\n", s.cstr())
    mu.unlock()
    mu.drop()
    s.drop()
    return 0
EOF
    (cd "$tmp" && "$BUILD_DIR-dist/scc" t.sc)
    rm -rf "$tmp"
    echo "==> 完成: $BUILD_DIR-dist/scc"
}

do_test() {
    local update="${1:-}"
    do_build
    echo "==> 端到端验证 examples/feature*.sc"
    # 可运行特性系列：默认模式（编译+执行）
    local f
    for f in feature1 feature2 feature3 feature4 feature5 feature6 feature7 feature8 feature9 feature10 feature11 feature12 feature13 feature14 feature15 feature16 feature17 feature18 feature19 feature20 feature21 feature22 feature23 feature24 feature25 feature26 feature27 feature28 feature29 feature31 feature32 feature33 feature34 feature35 feature36 feature37 feature_forward; do
        echo "--- $f.sc（默认模式）---"
        "$BUILD_DIR/scc" "$ROOT/examples/$f.sc"
    done
    # 根模块标记（@@）子目录示例：feature30 与其消费单元同目录
    echo "--- feature30/feature30.sc（默认模式）---"
    "$BUILD_DIR/scc" "$ROOT/examples/feature30/feature30.sc"
    # 跨模块泛型实例化：附属模块定义/实例化泛型模板并导出实例签名函数，入口跨模块调用
    echo "--- feature38/feature38.sc（默认模式 跨模块泛型）---"
    "$BUILD_DIR/scc" "$ROOT/examples/feature38/feature38.sc"
    # 根模块标记（@@）+ ARGS 原生机制：子模块经注入直接访问根的 mix 展开全局 ARGS_verbose
    echo "--- args_native/args_native.sc（默认模式 @@ 注入）---"
    "$BUILD_DIR/scc" "$ROOT/tests/cases/args_native/args_native.sc" -- -v -n 3 -i data.txt -f a b c x y
    # 单元测试框架：--test 运行 tst 用例（含故意失败，退出码非零属预期，不中断脚本）
    echo "--- test_demo.sc（--test 模式）---"
    "$BUILD_DIR/scc" "$ROOT/examples/test_demo.sc" --test || true
    # emit-c 模式：转译 C 后手动编译运行
    echo "--- feature1.sc（--emit-c 模式）---"
    local tmp
    tmp="$(mktemp -d)"
    "$BUILD_DIR/scc" "$ROOT/examples/feature1.sc" --emit-c -o "$tmp/feature1.c"
    # 生成的 C 统一 #include "platform.h"：手动编译需 -I builtins（scc 默认自带）
    cc "$tmp/feature1.c" -I "$ROOT/builtins" -o "$tmp/feature1"
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
    # 黄金快照回归：emit-c / emit-sc 产物比对（详见 tests/run.sh）
    echo "==> 产物回归 tests/run.sh"
    bash "$ROOT/tests/run.sh" $update
    if [ "$update" = "--update" ]; then
        echo "==> 黄金文件已更新"
        return
    fi
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
    # Markdown Preview Enhanced：预览面板里的 sc 代码块高亮依赖本仓库 .crossnote/parser.js 钩子
    echo "==> 安装 Markdown Preview Enhanced（预览面板 sc 代码块高亮）"
    if command -v code >/dev/null 2>&1; then
        if code --install-extension shd101wyy.markdown-preview-enhanced --force >/dev/null 2>&1; then
            echo "    已就绪（.crossnote/parser.js 提供 sc 高亮，重载窗口后生效）"
        else
            echo "    跳过：安装失败，请在 VSCode 中手动安装 'Markdown Preview Enhanced'"
        fi
    else
        echo "    跳过：未找到 code CLI，请在 VSCode 中手动安装 'Markdown Preview Enhanced'"
    fi
    echo "==> 安装完成（重启 VSCode 后 .sc/.ss 文件生效高亮与 AST 视图）"
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
    rm -rf "$BUILD_DIR" "$BUILD_DIR-dist"
    echo "==> 已清理 $BUILD_DIR ($BUILD_DIR-dist)"
}

case "${1:-build}" in
    build)     do_build ;;
    dist)      do_dist ;;
    test)      do_test "${2:-}" ;;
    install)   do_install ;;
    uninstall) do_uninstall ;;
    clean)     do_clean ;;
    -h|--help|help) usage ;;
    *) usage; exit 1 ;;
esac
