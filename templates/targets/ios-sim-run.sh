#!/bin/bash
# ============================================================
# ios-sim-run.sh —— 部署启动器：装 .app 到 iOS 模拟器并启动（--console 直连输出）
# 被 ios-sim.target 的 run= 引用；scc 打包后调用。
#
# 环境（scc 导出）：SCC_BUILD_DIR（.app 所在）、SCC_APP_DIR（Info.plist 所在）、SCC_APP_NAME。
# 额外环境：DEV（模拟器机型，默认 "iPhone 16 Pro"）。
#
# M 芯片 Mac 上模拟器是原生 arm64（同架构跑 iOS 运行时，非指令模拟），速度好。
# ============================================================
set -e
command -v xcrun >/dev/null || { echo "ios-sim-run: 需要 Xcode 命令行工具（xcrun）"; exit 1; }
NAME="${SCC_APP_NAME:-app}"
BUILD="${SCC_BUILD_DIR:?}"
APP="$BUILD/$NAME.app"
[[ -d "$APP" ]] || { echo "ios-sim-run: 找不到 .app（$APP）"; exit 1; }
DEV="${DEV:-iPhone 16 Pro}"

# bundle id 从 Info.plist 读取
BID="$(/usr/libexec/PlistBuddy -c "Print CFBundleIdentifier" "$SCC_APP_DIR/Info.plist" 2>/dev/null)"
[[ -n "$BID" ]] || { echo "ios-sim-run: 无法解析 CFBundleIdentifier"; exit 1; }

echo "==> 启动模拟器：$DEV"
xcrun simctl boot "$DEV" 2>/dev/null || true    # 已启动则忽略
open -a Simulator                                # 显示模拟器 UI
xcrun simctl bootstatus "$DEV" -b                # 阻塞直到启动完成
xcrun simctl install "$DEV" "$APP"
echo "==> 启动 $BID（--console：app 的 print 直连本终端，Ctrl-C 结束）"
exec xcrun simctl launch --console "$DEV" "$BID"
