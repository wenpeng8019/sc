#!/bin/bash
# ============================================================
# android-pkg.sh —— 打包器：把 scc 构建的 libapp.so 打成可安装 APK
# 被 android.target 的 pkg= 引用；scc 构建产物后调用，传参见环境变量契约。
#
# 环境（scc 导出）：
#   SCC_ARTIFACT   构建产物（.../build/lib<name>.so）——亦作位置参数 $1
#   SCC_APP_DIR    app 源目录（AndroidManifest.xml 所在）
#   SCC_BUILD_DIR  构建输出目录（APK 落此）
#   SCC_APP_NAME   产物基名（= .so 去 lib 前缀去 .so；须与清单 lib_name 一致）
#   SCC_TARGET_DIR 目标档目录（定位同级 wrapper 与 wsi 的 dex）
# 额外环境：
#   ANDROID_HOME   SDK 根（build-tools/platforms/aapt/d8/zipalign/apksigner）
#   ABI            目标 ABI（默认 arm64-v8a，须与 cc-android.sh 一致）
#
# 不用 gradle：直接调 SDK 底层命令行工具手工拼 APK（清单 + .so + dex + 签名），
# 呈现最小构建链路。ScApplication → classes.dex 不在此构建——由 wsi/build.sh 预编
# 交付（wsi-android.dex），本脚本只取用（模块使用者不该编 wsi 的 Java 垫片）。
# ============================================================
set -e
ARTIFACT="${1:-$SCC_ARTIFACT}"
[[ -f "$ARTIFACT" ]] || { echo "android-pkg: 找不到构建产物（$ARTIFACT）"; exit 1; }
: "${ANDROID_HOME:=${ANDROID_SDK_ROOT:-}}"
[[ -d "$ANDROID_HOME" ]] || { echo "android-pkg: 未设置 ANDROID_HOME（SDK 根）"; exit 1; }
ABI="${ABI:-arm64-v8a}"
NAME="${SCC_APP_NAME:-app}"
BUILD="${SCC_BUILD_DIR:?}"
APPDIR="${SCC_APP_DIR:?}"

# 清单：优先用 app 目录内的 AndroidManifest.xml（可定制）；缺失则自动生成默认清单
# 落到 build 目录（app 目录零配置即可跑）。默认包名/标签由 app 目录名派生，
# lib_name/meta-data 用产物基名（$NAME，须与 scc 构建的 libapp.so 一致）。
# android-run.sh 同样按「app 目录 → build 目录」顺序解析 package，保持一致。
MANIFEST="$APPDIR/AndroidManifest.xml"
if [[ ! -f "$MANIFEST" ]]; then
  SLUG="$(basename "$APPDIR" | tr '[:upper:]' '[:lower:]' | tr -cd 'a-z0-9')"
  PKG="com.sc.${SLUG:-app}"
  LABEL="$(basename "$APPDIR")"
  MANIFEST="$BUILD/AndroidManifest.xml"
  echo "==> app 目录无 AndroidManifest.xml → 自动生成默认清单（package=${PKG}, lib_name=${NAME}）"
  cat > "$MANIFEST" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<!-- 由 android-pkg.sh 自动生成的默认清单（app 目录未提供 AndroidManifest.xml）。
     如需定制（图标/权限/多 Activity 等），在 app 目录放置 AndroidManifest.xml 即覆盖此文件。 -->
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
          package="$PKG"
          android:versionCode="1"
          android:versionName="1.0">

    <uses-sdk android:minSdkVersion="24" android:targetSdkVersion="34"/>

    <application android:name="com.sc.wsi.ScApplication"
                 android:label="$LABEL"
                 android:hasCode="true"
                 android:allowBackup="false">

        <meta-data android:name="com.sc.wsi.lib_name"
                   android:value="$NAME"/>

        <activity android:name="android.app.NativeActivity"
                  android:label="$LABEL"
                  android:exported="true"
                  android:configChanges="orientation|keyboardHidden|screenSize|screenLayout|density">

            <meta-data android:name="android.app.lib_name"
                       android:value="$NAME"/>

            <intent-filter>
                <action android:name="android.intent.action.MAIN"/>
                <category android:name="android.intent.category.LAUNCHER"/>
            </intent-filter>
        </activity>
    </application>
</manifest>
EOF
fi

# ScApplication + Bridge 的 classes.dex（wsi 预编交付；进程级垫片 + JNI 反射 shim，hasCode=true）
WSI_DEX="${SCC_TARGET_DIR:?}/../modules/wsi/wsi-android.dex"
[[ -f "$WSI_DEX" ]] || { echo "android-pkg: 缺 wsi-android.dex（$WSI_DEX）；先跑 wsi/build.sh（android）"; exit 1; }

# SDK 组件定位
BT="$(ls -d "$ANDROID_HOME"/build-tools/* 2>/dev/null | sort -V | tail -1)"
PLATFORM="$(ls -d "$ANDROID_HOME"/platforms/android-* 2>/dev/null | sort -V | tail -1)"
[[ -n "$BT" && -n "$PLATFORM" ]] || { echo "android-pkg: 缺 SDK build-tools/platforms"; exit 1; }

# ---- 拼 APK：aapt 打清单 → 塞 .so 与 classes.dex → 对齐 → 签名 ----
APKDIR="$BUILD/apk"; rm -rf "$APKDIR"; mkdir -p "$APKDIR/lib/$ABI"
cp "$ARTIFACT" "$APKDIR/lib/$ABI/$(basename "$ARTIFACT")"
cp "$WSI_DEX" "$BUILD/classes.dex"

echo "==> aapt 打清单 → 未签名 APK，再塞入 .so 与 classes.dex"
"$BT/aapt" package -f -M "$MANIFEST" -I "$PLATFORM/android.jar" -F "$BUILD/base.apk"
( cd "$APKDIR" && "$BT/aapt" add "$BUILD/base.apk" "lib/$ABI/$(basename "$ARTIFACT")" >/dev/null )
( cd "$BUILD" && "$BT/aapt" add "$BUILD/base.apk" "classes.dex" >/dev/null )

echo "==> zipalign + 签名（debug keystore）"
"$BT/zipalign" -f 4 "$BUILD/base.apk" "$BUILD/aligned.apk"
KS="$HOME/.android/debug.keystore"
if [[ ! -f "$KS" ]]; then
  mkdir -p "$HOME/.android"
  keytool -genkeypair -v -keystore "$KS" -alias androiddebugkey \
    -storepass android -keypass android -keyalg RSA -keysize 2048 -validity 10000 \
    -dname "CN=Android Debug,O=Android,C=US"
fi
"$BT/apksigner" sign --ks "$KS" --ks-pass pass:android \
  --ks-key-alias androiddebugkey --key-pass pass:android \
  --out "$BUILD/$NAME.apk" "$BUILD/aligned.apk"
echo "==> 已生成 $BUILD/$NAME.apk"
