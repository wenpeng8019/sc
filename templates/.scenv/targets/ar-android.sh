#!/bin/sh
# ============================================================
# ar-android.sh —— scc 交叉静态库归档器包装（NDK llvm-ar）
# 被 android.target 的 ar= 引用。环境：ANDROID_NDK_HOME（亦认 ANDROID_NDK_ROOT）。
# ============================================================
set -e
: "${ANDROID_NDK_HOME:=${ANDROID_NDK_ROOT:-}}"
[ -d "$ANDROID_NDK_HOME" ] || { echo "ar-android: 未设置 ANDROID_NDK_HOME（指向 NDK 根目录）" >&2; exit 1; }
HOSTTAG="$(uname | tr 'A-Z' 'a-z')-x86_64"
AR="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOSTTAG/bin/llvm-ar"
[ -x "$AR" ] || { echo "ar-android: 找不到 NDK llvm-ar（$AR）" >&2; exit 1; }
exec "$AR" "$@"
