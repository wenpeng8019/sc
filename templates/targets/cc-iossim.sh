#!/bin/sh
# ============================================================
# cc-iossim.sh —— scc 交叉 C 编译器包装（iOS 模拟器 clang）
# 被 ios-sim.target 的 cc= 引用；scc 逐 TU 经此调用 xcrun clang。
#
# 环境：IOS_MIN  最低部署目标（默认 13.0）。
# 固定 -target（含部署目标）+ -isysroot 指向模拟器 SDK。
# ============================================================
set -e
IOS_MIN="${IOS_MIN:-13.0}"
SDK="$(xcrun -sdk iphonesimulator --show-sdk-path)"
exec xcrun -sdk iphonesimulator clang \
  -target arm64-apple-ios${IOS_MIN}-simulator \
  -isysroot "$SDK" \
  "$@"
