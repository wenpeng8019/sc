#!/bin/bash
# ============================================================
# hello-android 构建并运行 —— wsi Android 后端形态（设计草图）
#
# ⚠ 状态：设计草图。wsi 的 Android C 后端（android_platform.c）尚未实现，现在
#   运行会在「构建 libwsi(android)」处失败。保留本脚本是为呈现「实际使用形态」，
#   供理解设计与二创。就绪后即可端到端跑通。
#
# 为什么没有 main / 不能 scc run（对照 hello-ios）：
#   Android app 无 native main——APK 由框架的 Java 类 android.app.NativeActivity
#   拉起，经 JNI 调用打进 .so 的 ANativeActivity_onCreate（由 wsi 提供）。故本 app
#   编译为「共享库 libhello.so」（无 main，导出 ANativeActivity_onCreate 与逻辑入口
#   sc_app_main），再连同 AndroidManifest 打成 APK，经 adb 装到设备/模拟器
#   由框架加载。M 芯片 Mac 上可用 Android 模拟器（arm64 系统镜像）或真机(adb)。
#
# 前置（就绪后）：Android NDK + SDK build-tools + platform-tools(adb)；
#   环境变量 ANDROID_NDK_HOME（NDK 根）、ANDROID_HOME（SDK 根）。
#
# 打包工具链（不用 gradle，直接调 SDK 底层命令行工具手工拼 APK）：
#   本脚本刻意绕开 gradle/AGP，直接用底层工具，呈现最小构建链路——让「APK =
#   清单 + .so + 签名」的本质一目了然（与 hello-ios 手工拼 .app bundle 对称）。
#     * NDK clang / llvm-ar（NDK）        —— 交叉编译出 libhello.so（见下方两个 wrapper）
#     * javac（JDK）                     —— 编译 wsi 的 ScApplication.java → .class（进程级垫片）
#     * d8（SDK build-tools）            —— .class → classes.dex（Android 运行的 Dalvik 字节码）
#     * aapt（SDK build-tools）           —— 读 AndroidManifest + android.jar 打未签名 APK，
#                                            再 aapt add 把 .so / classes.dex 塞进 APK
#     * zipalign（SDK build-tools）       —— 4 字节对齐（APK 本质是 zip，对齐让 .so 可 mmap）
#     * apksigner（SDK build-tools）      —— 用 debug keystore 签名（v1/v2）
#     * keytool（JDK）                    —— 首次生成 ~/.android/debug.keystore
#     * adb（SDK platform-tools）         —— 安装 + am start 启动 + logcat
#   gradle 是什么角色：它是上层「编排器」，自己不打包，而是在底下调同一批工具
#   （aapt2、d8/r8、zipalign、apksigner），外加依赖管理、资源合并、manifest merge、
#   dex、增量构建、变体/签名配置等一整套工程化。gradle 的 assembleDebug 本质就是
#   把这里「编译原生库 + Java→dex → aapt 打包 → 对齐 → 签名」自动编排出来。
#   本项目引入 wsi 的 ScApplication（进程级垫片）后含一个极小 classes.dex（hasCode=true），
#   但仍手工拼（单个 Java 文件，javac+d8 一行搞定，用不上 gradle 的依赖/变体管理）。
#   现代等价工具：aapt2（aapt 继任者，gradle 默认用它）；此处用老 aapt 因单文件手工
#   塞 .so 更省事。真要工程化时可换 gradle + externalNativeBuild 挂 CMake 调 scc。
#
# 用法：
#   ANDROID_NDK_HOME=~/Android/ndk/27.x ANDROID_HOME=~/Android/sdk ./build.sh
#   ABI=x86_64 API=24 ./build.sh          # 指定 ABI/最低 API（默认 arm64-v8a / 24）
# ============================================================
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"
ROOT="$(cd ../../.. && pwd)"

# scc：环境变量 SCC > 仓库内构建产物 > PATH
SCC="${SCC:-$ROOT/compiler/build/scc}"
[[ -x "$SCC" ]] || SCC="$(command -v scc || true)"
[[ -n "$SCC" ]] || { echo "错误：找不到 scc（先构建编译器或设 SCC=<路径>）"; exit 1; }

: "${ANDROID_NDK_HOME:=${ANDROID_NDK_ROOT:-}}"
[[ -d "$ANDROID_NDK_HOME" ]] || { echo "错误：未设置 ANDROID_NDK_HOME（指向 NDK 根目录）"; exit 1; }
: "${ANDROID_HOME:=${ANDROID_SDK_ROOT:-}}"

WSI="$ROOT/templates/utils/wsi"
BUILD="$DIR/build"
ABI="${ABI:-arm64-v8a}"
API="${API:-24}"
PKG="com.sc.helloandroid"
LIB="hello"                    # 须与 AndroidManifest 的 android.app.lib_name 一致 → libhello.so
WSI_JAVA="$WSI/java"           # wsi 的 Java 垫片源根（com/sc/wsi/ScApplication.java）

# ABI → clang 目标三元组
case "$ABI" in
  arm64-v8a)   TRIPLE="aarch64-linux-android" ;;
  armeabi-v7a) TRIPLE="armv7a-linux-androideabi" ;;
  x86_64)      TRIPLE="x86_64-linux-android" ;;
  x86)         TRIPLE="i686-linux-android" ;;
  *) echo "不支持的 ABI: $ABI"; exit 1 ;;
esac

HOSTTAG="$(uname | tr 'A-Z' 'a-z')-x86_64"   # darwin-x86_64 / linux-x86_64（NDK 预编工具链目录名）
TOOLS="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOSTTAG/bin"
[[ -x "$TOOLS/clang" ]] || { echo "错误：找不到 NDK clang（$TOOLS/clang）"; exit 1; }

mkdir -p "$BUILD"

# ---- NDK 工具链包装（scc 经 SCC_CC/SCC_AR 逐 TU 调用；NDK clang 自带 sysroot）----
# --target=<triple><api> 决定 ABI 与最低 API；-fPIC 供共享库。
CCW="$BUILD/cc-android.sh"
cat > "$CCW" <<EOF
#!/bin/sh
exec "$TOOLS/clang" --target=${TRIPLE}${API} -fPIC "\$@"
EOF
chmod +x "$CCW"

ARW="$BUILD/ar-android.sh"
cat > "$ARW" <<EOF
#!/bin/sh
exec "$TOOLS/llvm-ar" "\$@"
EOF
chmod +x "$ARW"

export SCC_CC="$CCW"
export SCC_AR="$ARW"
export SCC_TARGET_TRIPLE="$TRIPLE"
export SCC_TARGET_SUFFIX="$TRIPLE"

# ---- 1. 为 Android 构建 wsi 库（待 android 后端实现：android_platform.c + .sc [*android*] 段）----
echo "==> 构建 libwsi（android/$ABI）  [wsi android 后端待补，此步现会失败]"
"$WSI/build.sh"

# ---- 2. 交叉编译 app → 共享库 libhello.so ----
# 无 main，产共享库；-u 强制保留 ANativeActivity_onCreate（来自 libwsi.a，防被裁剪）。
# 注：scc 需支持共享库输出（此处经 SCC_LDFLAGS 传 -shared；若 scc 尚无 --shared 模式，
#     此为二创接入点之一）。
echo "==> 编译 app.sc → lib$LIB.so"
SCC_LDFLAGS="-shared -u ANativeActivity_onCreate -landroid -llog" \
  "$SCC" app.sc --build -o "$BUILD/lib$LIB.so"

# ---- SDK 组件定位（dex 与打包共用）----
BT="$(ls -d "$ANDROID_HOME"/build-tools/* 2>/dev/null | sort -V | tail -1)"
PLATFORM="$(ls -d "$ANDROID_HOME"/platforms/android-* 2>/dev/null | sort -V | tail -1)"
[[ -n "$BT" && -n "$PLATFORM" ]] || { echo "错误：缺 SDK build-tools/platforms（设 ANDROID_HOME）"; exit 1; }

# ---- 3. 编译 ScApplication.java → classes.dex（进程级垫片，hasCode=true）----
# ScApplication 是 wsi 提供的 app 无关型 Application 子类：Application.onCreate（每进程
# 一次、最早）经自定义 JNI（android_jni.c）触发 sc_wsi_app_startup；onTrimMemory → 建议保存。
# javac 需 android.jar 作 bootclasspath（Application/PackageManager 等框架类）。
echo "==> javac ScApplication.java → d8 → classes.dex"
D8="$BT/d8"                             # d8 在 build-tools（旧版用 dx，新项目用 d8）
command -v javac >/dev/null || { echo "错误：找不到 javac（需 JDK）"; exit 1; }
[[ -x "$D8" ]] || { echo "错误：找不到 d8（$BT/d8，需 SDK build-tools）"; exit 1; }
CLS="$BUILD/classes"; rm -rf "$CLS"; mkdir -p "$CLS"
javac -source 8 -target 8 -bootclasspath "$PLATFORM/android.jar" \
      -d "$CLS" "$WSI_JAVA/com/sc/wsi/ScApplication.java"
# d8 将 .class 集打为 classes.dex（--min-api 与清单 minSdkVersion 一致）
"$D8" --min-api "$API" --output "$BUILD" $(find "$CLS" -name '*.class')

# ---- 4. 打包 APK（AndroidManifest + lib/<abi>/lib*.so + classes.dex）----
APKDIR="$BUILD/apk"; rm -rf "$APKDIR"; mkdir -p "$APKDIR/lib/$ABI"
cp "$BUILD/lib$LIB.so" "$APKDIR/lib/$ABI/lib$LIB.so"

echo "==> aapt 打包清单 → 未签名 APK，再塞入 .so 与 classes.dex"
"$BT/aapt" package -f -M "$DIR/AndroidManifest.xml" -I "$PLATFORM/android.jar" -F "$BUILD/base.apk"
( cd "$APKDIR" && "$BT/aapt" add "$BUILD/base.apk" "lib/$ABI/lib$LIB.so" >/dev/null )
# classes.dex 必须位于 APK 根（框架只认 /classes.dex），故在 $BUILD 下 aapt add
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
  --out "$BUILD/hello-android.apk" "$BUILD/aligned.apk"
echo "==> 已生成 $BUILD/hello-android.apk"

# ---- 5. 安装 + 启动 + 看输出 ----
command -v adb >/dev/null || { echo "提示：未找到 adb（platform-tools），跳过安装"; exit 0; }
echo "==> adb 安装并启动"
adb install -r "$BUILD/hello-android.apk"
adb shell setprop log.redirect-stdio true      # 让 app 的 stdout(print) 进 logcat
adb shell am start -n "$PKG/android.app.NativeActivity"
echo "==> 看心跳：adb logcat -s stdout:*  （或过滤 hello-android）"
exec adb logcat -s stdout:V
