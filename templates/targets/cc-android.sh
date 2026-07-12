#!/bin/sh
# ============================================================
# cc-android.sh —— scc 交叉 C 编译器包装（Android NDK clang）
# 被 android.target 的 cc= 引用；scc 逐 TU 经此调用 NDK clang。
#
# 环境：
#   ANDROID_NDK_HOME  NDK 根目录（必需；亦认 ANDROID_NDK_ROOT）
#   ABI               目标 ABI（默认 arm64-v8a；armeabi-v7a/x86_64/x86）
#   API               最低 API 级别（默认 24）
# NDK clang 自带 sysroot；--target=<triple><api> 决定 ABI 与最低 API，-fPIC 供共享库。
# ============================================================
set -e
: "${ANDROID_NDK_HOME:=${ANDROID_NDK_ROOT:-}}"
[ -d "$ANDROID_NDK_HOME" ] || { echo "cc-android: 未设置 ANDROID_NDK_HOME（指向 NDK 根目录）" >&2; exit 1; }
ABI="${ABI:-arm64-v8a}"
API="${API:-24}"
case "$ABI" in
  arm64-v8a)   TRIPLE="aarch64-linux-android" ;;
  armeabi-v7a) TRIPLE="armv7a-linux-androideabi" ;;
  x86_64)      TRIPLE="x86_64-linux-android" ;;
  x86)         TRIPLE="i686-linux-android" ;;
  *) echo "cc-android: 不支持的 ABI '$ABI'" >&2; exit 1 ;;
esac
HOSTTAG="$(uname | tr 'A-Z' 'a-z')-x86_64"   # darwin-x86_64 / linux-x86_64
CLANG="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOSTTAG/bin/clang"
[ -x "$CLANG" ] || { echo "cc-android: 找不到 NDK clang（$CLANG）" >&2; exit 1; }
exec "$CLANG" --target=${TRIPLE}${API} -fPIC "$@"
