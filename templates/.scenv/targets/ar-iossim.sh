#!/bin/sh
# ============================================================
# ar-iossim.sh —— scc 交叉静态库归档器包装（iOS 模拟器 ar）
# 被 ios-sim.target 的 ar= 引用。
# ============================================================
set -e
exec xcrun -sdk iphonesimulator ar "$@"
