#!/bin/bash
# ============================================================
# android-run.sh —— 部署启动器：装 APK 到设备/模拟器、启动、看日志
# 被 android.target 的 run= 引用；scc 打包后调用（$1=构建产物，此处用不到，
# 实际部署的是 pkg 产出的 APK，经环境变量定位）。
#
# 环境（scc 导出）：SCC_BUILD_DIR（APK 所在）、SCC_APP_DIR（清单所在）、SCC_APP_NAME。
# 额外环境：ANDROID_HOME（platform-tools/adb）。
# ============================================================
set -e
: "${ANDROID_HOME:=${ANDROID_SDK_ROOT:-}}"
NAME="${SCC_APP_NAME:-app}"
BUILD="${SCC_BUILD_DIR:?}"
APK="$BUILD/$NAME.apk"
[[ -f "$APK" ]] || { echo "android-run: 找不到 APK（$APK）"; exit 1; }

ADB="$(command -v adb || echo "$ANDROID_HOME/platform-tools/adb")"
[[ -x "$ADB" ]] || { echo "android-run: 找不到 adb（装 platform-tools 或设 ANDROID_HOME）"; exit 1; }

# 包名从清单读取（package="..."）
PKG="$(sed -n 's/.*package="\([^"]*\)".*/\1/p' "$SCC_APP_DIR/AndroidManifest.xml" | head -1)"
[[ -n "$PKG" ]] || { echo "android-run: 无法从清单解析 package"; exit 1; }

echo "==> adb 安装并启动 $PKG"
"$ADB" install -r "$APK"
"$ADB" shell setprop log.redirect-stdio true      # app 的 stdout(print) → logcat
"$ADB" shell am start -n "$PKG/android.app.NativeActivity"
echo "==> logcat（Ctrl-C 结束）"
exec "$ADB" logcat -s stdout:V sc.wsi:V
