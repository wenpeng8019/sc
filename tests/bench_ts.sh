#!/bin/bash
# Lightweight baseline benchmark for ts runtime scenarios.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCC="$ROOT/compiler/build/scc"
OPENBLAS_PREFIX="${OPENBLAS_PREFIX:-/opt/homebrew/opt/openblas}"

[ -x "$SCC" ] || { echo "missing scc: $SCC" >&2; exit 1; }

run_case() {
    local name="$1" cmd="$2"
    echo "== $name =="
    /usr/bin/time -l sh -c "$cmd" >/dev/null
}

echo "[baseline]"
run_case "ts_basic default" "$SCC $ROOT/tests/cases/ts_basic.sc"
run_case "dnn default" "$SCC $ROOT/templates/dnn-framework/dnn.sc"

if [ -f "$OPENBLAS_PREFIX/include/lapacke.h" ]; then
    echo "[lapack+openblas]"
    run_case "ts_basic lapack" "SCC_CFLAGS='-DSCC_WITH_LAPACK -I$OPENBLAS_PREFIX/include' SCC_LDFLAGS='-L$OPENBLAS_PREFIX/lib -lopenblas' $SCC $ROOT/tests/cases/ts_basic.sc"
fi

echo "benchmark done"
