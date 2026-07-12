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

# 包名从清单读取（package="..."）——app 目录优先，缺失则用 pkg 自动生成落在 build 目录的清单
MANIFEST="$SCC_APP_DIR/AndroidManifest.xml"
[[ -f "$MANIFEST" ]] || MANIFEST="$BUILD/AndroidManifest.xml"
PKG="$(sed -n 's/.*package="\([^"]*\)".*/\1/p' "$MANIFEST" | head -1)"
[[ -n "$PKG" ]] || { echo "android-run: 无法从清单解析 package（$MANIFEST）"; exit 1; }

echo "==> adb 安装并启动 $PKG"
"$ADB" install -r "$APK"
"$ADB" shell am force-stop "$PKG"                 # 清掉旧进程，确保新 APK 生效
"$ADB" shell setprop log.redirect-stdio true      # app 的 stdout(print) → logcat
# 启动清单声明的 LAUNCHER Activity（NativeActivity / ScActivity 皆可，用 monkey 免硬编）
"$ADB" shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
# DETACH=1：启动后不挂 logcat（不阻塞）——供自动化/AI 调试用；
#   看日志：adb logcat -s stdout:V sc.wsi:V
#   看画面：adb exec-out screencap -p > out.png
if [[ -n "$DETACH" ]]; then
    echo "==> 已启动 ${PKG}（DETACH：不挂 logcat，启动即返回）"
    exit 0
fi
echo "==> logcat（Ctrl-C 结束）"
exec "$ADB" logcat -s stdout:V sc.wsi:V
