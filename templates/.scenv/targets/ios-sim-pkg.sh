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

# Info.plist：优先用 app 目录内的（可定制）；缺失则自动生成默认 plist 落到 build 目录
# （app 目录零配置即可跑）。CFBundleExecutable 用产物基名（$NAME），bundle id / 名称
# 由 app 目录名派生。ios-sim-run.sh 同样按「app 目录 → build 目录」顺序解析 bundle id。
PLIST="$APPDIR/Info.plist"
if [[ ! -f "$PLIST" ]]; then
  SLUG="$(basename "$APPDIR" | tr '[:upper:]' '[:lower:]' | tr -cd 'a-z0-9')"
  BID="com.sc.${SLUG:-app}"
  LABEL="$(basename "$APPDIR")"
  PLIST="$BUILD/Info.plist"
  echo "==> app 目录无 Info.plist → 自动生成默认 plist（bundle id=${BID}, exe=${NAME}）"
  cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!-- 由 ios-sim-pkg.sh 自动生成的默认 plist（app 目录未提供 Info.plist）。
     如需定制（图标/权限/方向等），在 app 目录放置 Info.plist 即覆盖此文件。 -->
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>$NAME</string>
    <key>CFBundleIdentifier</key>
    <string>$BID</string>
    <key>CFBundleName</key>
    <string>$LABEL</string>
    <key>CFBundleDisplayName</key>
    <string>$LABEL</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>LSRequiresIPhoneOS</key>
    <true/>
    <key>MinimumOSVersion</key>
    <string>13.0</string>
    <key>UILaunchScreen</key>
    <dict/>
    <key>UISupportedInterfaceOrientations</key>
    <array>
        <string>UIInterfaceOrientationPortrait</string>
        <string>UIInterfaceOrientationLandscapeLeft</string>
        <string>UIInterfaceOrientationLandscapeRight</string>
    </array>
    <key>UIDeviceFamily</key>
    <array>
        <integer>1</integer>
        <integer>2</integer>
    </array>
</dict>
</plist>
EOF
fi

APP="$BUILD/$NAME.app"
rm -rf "$APP"; mkdir -p "$APP"
cp "$ARTIFACT" "$APP/$NAME"          # 名须与 Info.plist 的 CFBundleExecutable 一致
cp "$PLIST" "$APP/Info.plist"
echo "==> 已打包 $APP"
