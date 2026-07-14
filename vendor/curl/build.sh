#!/bin/bash
# libcurl vendor 构建（sagent http 通道用；跨平台）
# 产物：vendor/curl/out/<tag>/libcurl.a + 前置 vendor/mbedtls out 三库
#
# 用法：
#   ./build.sh                    # host 构建（mac/linux）
#   TAG=android CMAKE_EXTRA="-DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26" ./build.sh
#   TAG=mingw   CMAKE_EXTRA="-DCMAKE_TOOLCHAIN_FILE=..." ./build.sh
# 环境：TAG=产物子目录名（缺省 host）；CMAKE_EXTRA=透传 cmake 的交叉参数。
set -e
cd "$(dirname "$0")"
ROOT="$(cd .. && pwd)"                       # vendor/
TAG="${TAG:-host}"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

MBED_SRC="$ROOT/mbedtls"
MBED_OUT="$ROOT/mbedtls/out/$TAG"
CURL_OUT="$PWD/out/$TAG"

# 1) mbedtls 合并静态库（vendor 无 framework 子模块，弃其 cmake，直编 library/*.c
#    ——与 scc 自身 CMakeLists 的 SCC_MBEDTLS_SRC glob 同款做法；交叉用 CC 环境变量）
MBED_A="$MBED_OUT/libmbedtls_all.a"
if [ ! -f "$MBED_A" ]; then
    echo "==> 构建 mbedtls（$TAG，直编 library/*.c）"
    mkdir -p "$MBED_OUT/obj"
    CC_BIN="${CC:-cc}"
    for f in "$MBED_SRC"/library/*.c; do
        o="$MBED_OUT/obj/$(basename "${f%.c}").o"
        [ -f "$o" ] || "$CC_BIN" -O2 -c "$f" -I "$MBED_SRC/include" -I "$MBED_SRC/library" -o "$o"
    done
    ar rcs "$MBED_A" "$MBED_OUT/obj"/*.o
fi

# 2) libcurl 静态库：HTTP(S) only + mbedtls 后端，掐掉一切可选依赖
echo "==> 构建 libcurl（$TAG，HTTP(S)+mbedtls 最小化）"
cmake -B "$CURL_OUT" -S . -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON \
    -DBUILD_CURL_EXE=OFF -DBUILD_LIBCURL_DOCS=OFF -DBUILD_MISC_DOCS=OFF \
    -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF \
    -DCURL_USE_MBEDTLS=ON -DCURL_USE_LIBPSL=OFF -DCURL_USE_LIBSSH2=OFF \
    -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF -DCURL_ZLIB=OFF -DUSE_NGHTTP2=OFF \
    -DUSE_LIBIDN2=OFF -DCURL_DISABLE_LDAP=ON -DCURL_DISABLE_LDAPS=ON \
    -DCURL_DISABLE_FTP=ON -DCURL_DISABLE_FILE=ON -DCURL_DISABLE_TELNET=ON \
    -DCURL_DISABLE_DICT=ON -DCURL_DISABLE_TFTP=ON -DCURL_DISABLE_GOPHER=ON \
    -DCURL_DISABLE_POP3=ON -DCURL_DISABLE_IMAP=ON -DCURL_DISABLE_SMTP=ON \
    -DCURL_DISABLE_SMB=ON -DCURL_DISABLE_MQTT=ON -DCURL_DISABLE_RTSP=ON \
    -DCURL_DISABLE_IPFS=ON -DCURL_DISABLE_WEBSOCKETS=ON \
    -DMBEDTLS_INCLUDE_DIRS="$MBED_SRC/include" \
    -DMBEDTLS_LIBRARY="$MBED_A" \
    -DMBEDX509_LIBRARY="$MBED_A" \
    -DMBEDCRYPTO_LIBRARY="$MBED_A" \
    ${CMAKE_EXTRA:-} > /dev/null
cmake --build "$CURL_OUT" -j "$JOBS" > /dev/null

A="$(find "$CURL_OUT/lib" -name 'libcurl*.a' | head -1)"
cp -f "$MBED_A" "$CURL_OUT/lib/"          # 产物归一目录（消费方单 -L）
echo "==> 完成：$A"
echo "    mbedtls：$CURL_OUT/lib/libmbedtls_all.a"
