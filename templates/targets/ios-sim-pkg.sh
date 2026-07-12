#!/bin/bash
# ============================================================
# ios-sim-pkg.sh —— 打包器：把 scc 构建的可执行文件打成最小 .app bundle
# 被 ios-sim.target 的 pkg= 引用；scc 构建产物后调用。
#
# 环境（scc 导出）：
#   SCC_ARTIFACT   构建产物（.../build/<name> 可执行）——亦作位置参数 $1
#   SCC_APP_DIR    app 源目录（Info.plist 所在）
#   SCC_BUILD_DIR  构建输出目录（.app 落此）
#   SCC_APP_NAME   产物基名（须与 Info.plist 的 CFBundleExecutable 一致）
#
# .app bundle 即「可执行 + Info.plist」——手工拼，与 android APK 手工链路对称。
# ============================================================
set -e
ARTIFACT="${1:-$SCC_ARTIFACT}"
[[ -f "$ARTIFACT" ]] || { echo "ios-sim-pkg: 找不到构建产物（$ARTIFACT）"; exit 1; }
NAME="${SCC_APP_NAME:-app}"
BUILD="${SCC_BUILD_DIR:?}"
APPDIR="${SCC_APP_DIR:?}"
PLIST="$APPDIR/Info.plist"
[[ -f "$PLIST" ]] || { echo "ios-sim-pkg: 缺 Info.plist（$PLIST）"; exit 1; }

APP="$BUILD/$NAME.app"
rm -rf "$APP"; mkdir -p "$APP"
cp "$ARTIFACT" "$APP/$NAME"          # 名须与 Info.plist 的 CFBundleExecutable 一致
cp "$PLIST" "$APP/Info.plist"
echo "==> 已打包 $APP"
