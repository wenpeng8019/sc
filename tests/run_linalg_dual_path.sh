#!/bin/bash
# Compare core linalg outputs between default path and SCC_WITH_LAPACK path.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCC="$ROOT/compiler/build/scc"
CASE="$ROOT/tests/cases/ts_basic.sc"
OPENBLAS_PREFIX="${OPENBLAS_PREFIX:-/opt/homebrew/opt/openblas}"

[ -x "$SCC" ] || { echo "missing scc: $SCC" >&2; exit 1; }

out_def="$($SCC "$CASE" 2>/dev/null)"

if [ ! -f "$OPENBLAS_PREFIX/include/lapacke.h" ]; then
    echo "WARN: lapacke.h not found under $OPENBLAS_PREFIX/include; skip LAPACK path"
    echo "default path smoke: PASS"
    exit 0
fi

out_lap="$({ SCC_CFLAGS="-DSCC_WITH_LAPACK -I$OPENBLAS_PREFIX/include" SCC_LDFLAGS="-L$OPENBLAS_PREFIX/lib -lopenblas" "$SCC" "$CASE" 2>/dev/null; })"

num_from_line() {
    local text="$1" key="$2"
    echo "$text" | sed -n "s/.*$key=\([-0-9.eE]*\).*/\1/p" | head -1
}

assert_close() {
    local a="$1" b="$2" name="$3" tol="${4:-1e-5}"
    awk -v x="$a" -v y="$b" -v n="$name" -v t="$tol" 'BEGIN{
        d=x-y; if (d<0) d=-d;
        if (d>t) { printf("FAIL %s: %g vs %g (|d|=%g > %g)\n", n, x, y, d, t); exit 1; }
    }'
}

# Compare deterministic scalar lines.
det_d="$(num_from_line "$out_def" det)"
det_l="$(num_from_line "$out_lap" det)"
assert_close "$det_d" "$det_l" det

inv00_d="$(num_from_line "$out_def" inv\ at0)"
inv00_l="$(num_from_line "$out_lap" inv\ at0)"
assert_close "$inv00_d" "$inv00_l" inv00

x0_d="$(num_from_line "$out_def" solve\ at0)"
x0_l="$(num_from_line "$out_lap" solve\ at0)"
assert_close "$x0_d" "$x0_l" solve_x0

eig0_d="$(num_from_line "$out_def" lo)"
eig0_l="$(num_from_line "$out_lap" lo)"
assert_close "$eig0_d" "$eig0_l" eigh_lo

# QR允许符号翻转，比较 |R00| 与 R11。
r00_d="$(num_from_line "$out_def" R00)"
r00_l="$(num_from_line "$out_lap" R00)"
abs_r00_d="$(awk -v x="$r00_d" 'BEGIN{if(x<0)x=-x;print x}')"
abs_r00_l="$(awk -v x="$r00_l" 'BEGIN{if(x<0)x=-x;print x}')"
assert_close "$abs_r00_d" "$abs_r00_l" qr_abs_r00

r11_d="$(num_from_line "$out_def" R11)"
r11_l="$(num_from_line "$out_lap" R11)"
assert_close "$r11_d" "$r11_l" qr_r11

mesh_d="$(echo "$out_def" | grep 'meshgrid ok=' | head -1)"
mesh_l="$(echo "$out_lap" | grep 'meshgrid ok=' | head -1)"
[ "$mesh_d" = "$mesh_l" ] || { echo "FAIL meshgrid line mismatch"; echo "def: $mesh_d"; echo "lap: $mesh_l"; exit 1; }

echo "dual path linalg check: PASS"
