#!/bin/bash
# ============================================================
# hello-ios 构建并运行 —— wsi iOS 后端在 iOS 模拟器上验证
#
# 为什么不是 scc run：iOS 可执行文件不是能 ./app 直接跑的普通程序——它的
# Mach-O 平台标记为 iOS、须打包成 .app bundle 由 iOS 运行环境加载。M 芯片
# Mac 虽能跑 iOS app（"Designed for iPad" 靠 /System/iOSSupport），但那是
# App Store 分发路径，无务实命令行入口。故本脚本走 iOS 模拟器：
#   编译（iphonesimulator SDK）→ 打包 .app → xcrun simctl boot/install/launch。
# M Mac 上模拟器是原生 arm64（同架构跑 iOS 运行时，非指令模拟），速度好。
#
# 前置：安装 Xcode（含 iOS 模拟器运行时）。
#
# 用法：
#   ./build.sh                 # 构建 + 装进模拟器 + 启动（--console 打印 app 输出）
#   DEV="iPhone 16" ./build.sh # 指定模拟器机型（默认 iPhone 16 Pro）
# ============================================================
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"
ROOT="$(cd ../../.. && pwd)"

# scc：环境变量 SCC > 仓库内构建产物 > PATH
SCC="${SCC:-$ROOT/compiler/build/scc}"
[[ -x "$SCC" ]] || SCC="$(command -v scc || true)"
[[ -n "$SCC" ]] || { echo "错误：找不到 scc（先构建编译器或设 SCC=<路径>）"; exit 1; }

command -v xcrun >/dev/null || { echo "错误：需要 Xcode 命令行工具（xcrun）"; exit 1; }

WSI="$ROOT/templates/utils/wsi"
BUILD="$DIR/build"
DEV="${DEV:-iPhone 16 Pro}"
BUNDLE_ID="com.sc.helloios"
IOS_MIN="13.0"
TRIPLE="arm64-apple-ios-simulator"
SDK="$(xcrun -sdk iphonesimulator --show-sdk-path)"

mkdir -p "$BUILD"

# ---- iOS 模拟器工具链包装（scc 经 SCC_CC/SCC_AR 逐 TU 调用）----
# clang 包装：固定 -target（含部署目标）+ -isysroot 指向模拟器 SDK。
CCW="$BUILD/cc-iossim.sh"
cat > "$CCW" <<EOF
#!/bin/sh
exec xcrun -sdk iphonesimulator clang \\
  -target arm64-apple-ios${IOS_MIN}-simulator \\
  -isysroot "$SDK" \\
  "\$@"
EOF
chmod +x "$CCW"

ARW="$BUILD/ar-iossim.sh"
cat > "$ARW" <<EOF
#!/bin/sh
exec xcrun -sdk iphonesimulator ar "\$@"
EOF
chmod +x "$ARW"

export SCC_CC="$CCW"
export SCC_AR="$ARW"
export SCC_TARGET_TRIPLE="$TRIPLE"
export SCC_TARGET_SUFFIX="$TRIPLE"

# ---- 1. 为 iOS 模拟器构建 wsi 库（产 libwsi.arm64-apple-ios-simulator.a）----
# .sc 的 [*simulator*] 段命中，编入 WSI_IOS 后端 + UIKit 框架。
echo "==> 构建 libwsi（iOS 模拟器）"
"$WSI/build.sh"

# ---- 2. 交叉编译 app → Mach-O（平台标记 = iOS 模拟器）----
echo "==> 编译 app.sc"
"$SCC" app.sc --build -o "$BUILD/hello"

# ---- 3. 打包最小 .app bundle ----
APP="$BUILD/hello-ios.app"
rm -rf "$APP"; mkdir -p "$APP"
cp "$BUILD/hello" "$APP/hello"
cp "$DIR/Info.plist" "$APP/Info.plist"
echo "==> 已打包 $APP"

# ---- 4. 启动模拟器 + 安装 + 运行 ----
echo "==> 启动模拟器：$DEV"
xcrun simctl boot "$DEV" 2>/dev/null || true    # 已启动则忽略
open -a Simulator                                # 显示模拟器 UI
xcrun simctl bootstatus "$DEV" -b                # 阻塞直到启动完成
xcrun simctl install "$DEV" "$APP"
echo "==> 启动 app（--console：app 的 print 输出直连本终端，Ctrl-C 结束）"
exec xcrun simctl launch --console "$DEV" "$BUNDLE_ID"
