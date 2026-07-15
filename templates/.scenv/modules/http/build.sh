#!/bin/bash
# ============================================================
# build.sh —— 通用 http 组件的 native 依赖构建
#
# 产物：
#   宿主：  libcurl.a + libmbedtls_all.a
#   交叉：  libcurl.<签名>.a + libmbedtls_all.<签名>.a
#
# 源码在 vendor/curl 与 vendor/mbedtls；实际 libcurl 配置沿用
# vendor/curl/build.sh（HTTP(S)-only、mbedTLS、无 curl 命令行工具）。
#
# 用法：
#   ./build.sh
#   ./build.sh --target ../../targets/aarch64-linux.target
#   SCC_TARGET_TRIPLE=x86_64-windows-gnu \
#     SCC_CC=x86_64-w64-mingw32-gcc SCC_AR=x86_64-w64-mingw32-ar ./build.sh
# ============================================================
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
VENDOR="$REPO/vendor/curl"

TARGET_FILE=""
while [ $# -gt 0 ]; do
    case "$1" in
        --target)   TARGET_FILE="$2"; shift 2 ;;
        --target=*) TARGET_FILE="${1#--target=}"; shift ;;
        -h|--help)  sed -n '2,25p' "$0"; exit 0 ;;
        *) echo "未知参数: $1（仅支持 --target <目标档>）" >&2; exit 1 ;;
    esac
done

tf_get() {
    { [ -n "$TARGET_FILE" ] && [ -f "$TARGET_FILE" ]; } || return 0
    sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*//p" "$TARGET_FILE" \
        | sed 's/[[:space:]]*#.*$//; s/[[:space:]]*$//' | head -n1
}

CC="${SCC_CC:-$(tf_get cc)}"; CC="${CC:-cc}"
AR="${SCC_AR:-$(tf_get ar)}"; AR="${AR:-ar}"
TRIPLE="${SCC_TARGET_TRIPLE:-$(tf_get triple)}"
SUFFIX="${SCC_TARGET_SUFFIX:-$(tf_get target_suffix)}"
[ -n "$SUFFIX" ] || SUFFIX="$TRIPLE"
SYSROOT="${SCC_SYSROOT:-$(tf_get sysroot)}"
TARGET_FLAGS="${SCC_TARGET_FLAGS:-$(tf_get target_flags)}"
[ -n "$SYSROOT" ] && TARGET_FLAGS="$TARGET_FLAGS --sysroot=$SYSROOT"

# vendor/curl/build.sh 使用 TAG 区分输出目录；这里用目标签名保持与 scc
# add 的 resolveAddArtifact 一致。宿主使用 host，交叉使用 suffix/triple。
TAG="${SUFFIX:-host}"
OUT="$SCRIPT_DIR"
VOUT="$VENDOR/out/$TAG"

[ -d "$VENDOR" ] || { echo "错误: 缺少 $VENDOR（先 vendor libcurl）" >&2; exit 1; }

# mbedTLS 直编阶段需要消费侧工具链；CMake 阶段的交叉参数由 CMAKE_EXTRA
# 透传（例如 CMAKE_TOOLCHAIN_FILE、ANDROID_ABI）。
export CC
export AR
export CMAKE_EXTRA="${CMAKE_EXTRA:-}"
if [ -n "$TARGET_FLAGS" ]; then
    export CFLAGS="${CFLAGS:-} $TARGET_FLAGS"
fi

TAG="$TAG" "$VENDOR/build.sh"

CURL_A="$(find "$VOUT/lib" -maxdepth 1 -name 'libcurl*.a' -print -quit)"
MBED_A="$VOUT/lib/libmbedtls_all.a"
[ -f "$CURL_A" ] || { echo "错误: 未找到 $VOUT/lib/libcurl*.a" >&2; exit 1; }
[ -f "$MBED_A" ] || { echo "错误: 未找到 $MBED_A" >&2; exit 1; }

if [ -n "$TRIPLE" ] || [ -n "${SCC_TARGET_SUFFIX:-$(tf_get target_suffix)}" ]; then
    cp -f "$CURL_A" "$OUT/libcurl.$SUFFIX.a"
    cp -f "$MBED_A" "$OUT/libmbedtls_all.$SUFFIX.a"
else
    cp -f "$CURL_A" "$OUT/libcurl.a"
    cp -f "$MBED_A" "$OUT/libmbedtls_all.a"
fi

echo "完成：http libcurl + mbedTLS（TAG=${TAG}）"
