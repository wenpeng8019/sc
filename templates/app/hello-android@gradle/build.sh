#!/bin/bash
# ============================================================
# hello-android@gradle 构建并运行 —— gradle/AGP 工程化打包 sc 原生 app（设计草图）
#
# 与 ../hello-android/build.sh 同一目标（编译 app.sc → libhello.so → APK → 装启动），
# 差别只在「谁来编排打包」：这里把 aapt/zipalign/apksigner 那几步交给 gradle + AGP，
# 本脚本只是 gradle 的一层薄封装（一键 build + install + 启动 + 看日志）。
#
# ⚠ 状态：设计草图。wsi 的 android C 后端未实现，gradle 的 sccBuild 任务现会失败。
#
# 编译仍是 scc：gradle 的 sccBuild 任务读 SCC / ANDROID_* 环境，逐 ABI 生成 NDK
# clang/ar wrapper 并调 scc 产出 libhello.so，再由 AGP 标准打包链打进 APK。
#
# 前置：Android SDK（AGP/build-tools 由 gradle 自动拉）+ NDK（app/build.gradle 的
#   ndkVersion 指定的版本）+ platform-tools(adb)；系统 gradle（首次生成 wrapper）。
#   环境变量 ANDROID_HOME（SDK 根，或 local.properties 里 sdk.dir）。
#
# 用法：
#   ANDROID_HOME=~/Android/sdk ./build.sh              # build + 装到设备/模拟器 + 启动
#   TASK=assembleDebug ./build.sh                      # 只打 APK，不安装
# ============================================================
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"
ROOT="$(cd ../../.. && pwd)"

# scc：环境变量 SCC > 仓库内构建产物 > PATH（导出供 gradle 的 sccBuild 任务读取）
SCC="${SCC:-$ROOT/compiler/build/scc}"
[[ -x "$SCC" ]] || SCC="$(command -v scc || true)"
[[ -n "$SCC" ]] || { echo "错误：找不到 scc（先构建编译器或设 SCC=<路径>）"; exit 1; }
export SCC

: "${ANDROID_HOME:=${ANDROID_SDK_ROOT:-}}"
[[ -d "$ANDROID_HOME" ]] || { echo "错误：未设置 ANDROID_HOME（指向 Android SDK 根，或写 local.properties 的 sdk.dir）"; exit 1; }
export ANDROID_HOME

PKG="com.sc.hellogradleandroid"          # 与 app/build.gradle 的 applicationId 一致
TASK="${TASK:-installDebug}"             # 默认 build + 安装；设 assembleDebug 只打包

# ---- gradle 选择：优先 wrapper（版本可复现），无则用系统 gradle 先生成 wrapper ----
if [[ -x ./gradlew ]]; then
  GRADLE=(./gradlew)
elif command -v gradle >/dev/null; then
  echo "==> 首次：用系统 gradle 生成 wrapper（gradlew）"
  gradle wrapper
  GRADLE=(./gradlew)
else
  echo "错误：既无 ./gradlew 也无系统 gradle（装 gradle 或先在别处生成 wrapper）"; exit 1
fi

# ---- 1+2. gradle 编排：sccBuild（含 wsiBuild）→ AGP 打包 → 安装 ----
# sccBuild 内部逐 ABI 调 scc 产 libhello.so 落到 jniLibs；wsi android 后端未实现时此步会失败。
echo "==> ./gradlew $TASK"
"${GRADLE[@]}" "$TASK"

# assembleDebug 只出 APK，不安装/启动
if [[ "$TASK" != install* ]]; then
  echo "==> 已生成 APK：app/build/outputs/apk/debug/"
  exit 0
fi

# ---- 3. 启动 + 看输出 ----
command -v adb >/dev/null || { echo "提示：未找到 adb（platform-tools），已安装但跳过启动"; exit 0; }
echo "==> 启动 app"
adb shell setprop log.redirect-stdio true      # 让 app 的 stdout(print) 进 logcat
adb shell am start -n "$PKG/android.app.NativeActivity"
echo "==> 看心跳：adb logcat -s stdout:*"
exec adb logcat -s stdout:V
