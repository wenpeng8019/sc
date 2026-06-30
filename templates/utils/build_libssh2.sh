#!/bin/sh
# Build a self-contained static library for the ssh component module.
#
#   templates/utils/libssh2.a  =  libssh2 (vendor/libssh2) objects
#                              +  mbedTLS  (vendor/mbedtls)  objects
#
# libssh2 has no cryptography of its own; it delegates to mbedTLS. We bundle
# both into a single archive so ssh.sc only needs `add libssh2.a` (plus the
# C glue) — a fully self-contained, zero-system-dependency link.
#
# This is a HOST build (the resulting .a matches the machine that runs this
# script). ssh is a component library, not a language base module, so host
# linkage is acceptable; for another target, re-run with CC / AR pointing at
# that toolchain. The .a is a build artifact (git-ignored), not committed.
#
# Windows: build under MSYS2/MinGW (this POSIX sh runs there with CC=gcc/clang).
# The libssh2 sources select the winsock path via _WIN32 in libssh2_config.h.
# A Windows user program must additionally link the system libraries
# -lws2_32 -lbcrypt -ladvapi32 at the final link step. (Windows path is
# prepared but currently untested.)
#
# Usage:  sh templates/utils/build_libssh2.sh
#         CC=clang AR=ar sh templates/utils/build_libssh2.sh   # override toolchain
set -eu

CC=${CC:-cc}
AR=${AR:-ar}

# repo root = two levels up from this script (templates/utils/..)
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo=$(CDPATH= cd -- "$here/../.." && pwd)

ssh_src="$repo/vendor/libssh2/src"
ssh_inc="$repo/vendor/libssh2/include"
mbed_src="$repo/vendor/mbedtls/library"
mbed_inc="$repo/vendor/mbedtls/include"
out="$here/libssh2.a"

[ -d "$ssh_src" ] || { echo "error: missing $ssh_src (vendor libssh2 first)" >&2; exit 1; }
[ -d "$mbed_src" ] || { echo "error: missing $mbed_src (vendor mbedtls first)" >&2; exit 1; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

echo "building libssh2 objects (mbedTLS backend, no zlib)..."
for c in "$ssh_src"/*.c; do
    o="$work/ssh_$(basename "${c%.c}").o"
    "$CC" -O2 -DLIBSSH2_MBEDTLS -DHAVE_CONFIG_H \
        -I "$ssh_src" -I "$ssh_inc" -I "$mbed_inc" \
        -c "$c" -o "$o"
done

echo "building mbedTLS objects..."
for c in "$mbed_src"/*.c; do
    o="$work/mbed_$(basename "${c%.c}").o"
    "$CC" -O2 -I "$mbed_inc" -I "$mbed_src" -c "$c" -o "$o"
done

echo "archiving -> $out"
rm -f "$out"
"$AR" rcs "$out" "$work"/*.o

echo "done: $out ($(ls -lh "$out" | awk '{print $5}'))"
