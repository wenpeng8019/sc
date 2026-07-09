#!/bin/bash
# ============================================================
# build.sh —— ssh 组件自包含静态库跨平台构建
#
#   libssh2.a[.<签名>]  =  libssh2 (vendor/libssh2) 对象
#                       +  mbedTLS  (vendor/mbedtls) 对象
#
# libssh2 自身不含密码学，全权委托 mbedTLS；二者归档为单一自包含静态库，
# ssh.sc 只需 `add libssh2.a`（交叉目标自动匹配 libssh2.<签名>.a 变体，
# 见 compiler.md §5 的 resolveAddArtifact 变体匹配）。
#
# ssh 特殊性：libssh2 + mbedTLS 源码在 vendor/（非本模块 src/），故不能走
#   scc . --build 的通用模块库构建（那只编译模块自身 src/*.c），必须由本脚本
#   直接调目标工具链（CC/AR）编译归档。这是与 wsi/build.sh 的关键区别——
#   wsi 薄包装 scc，本脚本自持工具链。
#
# 目标签名约定（与 wsi 同构，令 scc add 变体匹配一致）：
#   宿主构建              →  libssh2.a               （triple 空 → 裸名）
#   交叉构建(--target)    →  libssh2.<target_suffix|triple>.a
#   工具链/三元组/后缀由目标档或 SCC_* 环境变量提供（与 scc 同源）。
#
# 用法：
#   ./build.sh                                                 # 宿主 → libssh2.a
#   ./build.sh --target ../../targets/aarch64-linux.target     # 交叉 → libssh2.aarch64-linux-gnu.a
#   SCC_TARGET_TRIPLE=x86_64-windows-gnu \
#     SCC_CC=x86_64-w64-mingw32-gcc SCC_AR=x86_64-w64-mingw32-ar ./build.sh
#
# Windows（mingw 交叉 / MSYS2）：libssh2 经 _WIN32 选 winsock 路径；消费程序
#   须另链 -lws2_32 -lbcrypt -ladvapi32——已在 ssh/.sc 的 [windows] 段声明，
#   inc ssh.sc 的程序链接时由 scc 自动注入（消费侧模块配置，见 compiler.md §7.6）。
#   MSVC 远端构建不在本脚本范围（那走 scc 的远程机制）。
# ============================================================
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
# 仓库根 = 本脚本三级上级（templates/utils/ssh/..）
REPO="$(cd "$SCRIPT_DIR/../../.." && pwd)"

# ---- 目标档解析（key = value，'#' 起注释；SCC_* 环境变量优先，与 scc 同源）----
TARGET_FILE=""
while [ $# -gt 0 ]; do
    case "$1" in
        --target)   TARGET_FILE="$2"; shift 2 ;;
        --target=*) TARGET_FILE="${1#--target=}"; shift ;;
        -h|--help)  sed -n '2,40p' "$0"; exit 0 ;;
        *) echo "未知参数: $1（仅支持 --target <目标档>）" >&2; exit 1 ;;
    esac
done

# 从目标档取键值（剥离行内 '#' 注释与首尾空白）；无档或键缺失 → 空串。
tf_get() {
    { [ -n "$TARGET_FILE" ] && [ -f "$TARGET_FILE" ]; } || return 0
    sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*//p" "$TARGET_FILE" \
        | sed 's/[[:space:]]*#.*$//; s/[[:space:]]*$//' | head -n1
}

CC="${SCC_CC:-$(tf_get cc)}";  CC="${CC:-cc}"
AR="${SCC_AR:-$(tf_get ar)}";  AR="${AR:-ar}"
TRIPLE="${SCC_TARGET_TRIPLE:-$(tf_get triple)}"
SUFFIX="${SCC_TARGET_SUFFIX:-$(tf_get target_suffix)}"
[ -n "$SUFFIX" ] || SUFFIX="$TRIPLE"
TARGET_FLAGS="${SCC_TARGET_FLAGS:-$(tf_get target_flags)}"
SYSROOT="${SCC_SYSROOT:-$(tf_get sysroot)}"
[ -n "$SYSROOT" ] && TARGET_FLAGS="$TARGET_FLAGS --sysroot=$SYSROOT"

# 交叉（有 triple）产物带签名，宿主产物裸名（与 scc add 变体匹配一致）。
if [ -n "$TRIPLE" ]; then
    OUT="$SCRIPT_DIR/libssh2.$SUFFIX.a"
else
    OUT="$SCRIPT_DIR/libssh2.a"
fi

ssh_src="$REPO/vendor/libssh2/src"
ssh_inc="$REPO/vendor/libssh2/include"
mbed_src="$REPO/vendor/mbedtls/library"
mbed_inc="$REPO/vendor/mbedtls/include"

[ -d "$ssh_src" ]  || { echo "错误: 缺少 $ssh_src（先 vendor libssh2）" >&2; exit 1; }
[ -d "$mbed_src" ] || { echo "错误: 缺少 $mbed_src（先 vendor mbedtls）" >&2; exit 1; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

echo "构建 libssh2 对象（mbedTLS 后端，无 zlib）... [CC=$CC${TRIPLE:+ triple=$TRIPLE}]"
for c in "$ssh_src"/*.c; do
    o="$work/ssh_$(basename "${c%.c}").o"
    "$CC" -O2 $TARGET_FLAGS -DLIBSSH2_MBEDTLS -DHAVE_CONFIG_H \
        -I "$ssh_src" -I "$ssh_inc" -I "$mbed_inc" \
        -c "$c" -o "$o"
done

echo "构建 mbedTLS 对象..."
for c in "$mbed_src"/*.c; do
    o="$work/mbed_$(basename "${c%.c}").o"
    "$CC" -O2 $TARGET_FLAGS -I "$mbed_inc" -I "$mbed_src" -c "$c" -o "$o"
done

echo "归档 -> $OUT"
rm -f "$OUT"
"$AR" rcs "$OUT" "$work"/*.o
echo "完成: $OUT ($(ls -lh "$OUT" | awk '{print $5}'))"
