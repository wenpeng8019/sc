#!/usr/bin/env python3
"""Small numpy parity smoke tests for ts.

Usage:
  python3 tests/numpy_parity_smoke.py

Prereqs:
  - compiler/build/scc exists
  - numpy installed in current Python env
"""

from __future__ import annotations

import math
import re
import subprocess
import tempfile
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
SCC = ROOT / "compiler/build/scc"

KV_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=([-+0-9.eE]+)")


def run_sc(code: str) -> dict[str, float]:
    with tempfile.TemporaryDirectory() as td:
        p = Path(td) / "case.sc"
        p.write_text(code, encoding="utf-8")
        out = subprocess.check_output([str(SCC), str(p)], cwd=ROOT, text=True, stderr=subprocess.STDOUT)
    got: dict[str, float] = {}
    for m in KV_RE.finditer(out):
        got[m.group(1)] = float(m.group(2))
    return got


def assert_close(name: str, got: float, exp: float, atol: float = 1e-6, rtol: float = 1e-6) -> None:
    if not math.isclose(got, exp, abs_tol=atol, rel_tol=rtol):
        raise AssertionError(f"{name}: got {got}, expect {exp}")


def case_basic_reduce() -> None:
    code = r'''
inc ts.sc

fnc main: i4
    var shp[2]: i4
    shp[0] = 2
    shp[1] = 3
    var b: tensor& = arange(0.0, 6.0, 1.0, DT_F8)
    var b2: tensor& = b->reshape(2, shp)
    var rsh[2]: i4
    rsh[0] = 1
    rsh[1] = 3
    var row: tensor& = ones(2, rsh, DT_F8)
    var bc: tensor& = b2->add(row)
    printf("sum=%g med=%g pct=%g\n", bc->sum_all(), b->median_all(), b->percentile_all(50.0))
    b->drop()
    b2->drop()
    row->drop()
    bc->drop()
    return 0
'''
    got = run_sc(code)

    b = np.arange(0.0, 6.0, 1.0, dtype=np.float64)
    b2 = b.reshape(2, 3)
    bc = b2 + np.ones((1, 3), dtype=np.float64)
    exp = {
        "sum": float(np.sum(bc)),
        "med": float(np.median(b)),
        "pct": float(np.percentile(b, 50.0)),
    }
    for k, v in exp.items():
        assert_close(f"basic:{k}", got[k], v)


def case_linalg() -> None:
    code = r'''
inc ts.sc

fnc main: i4
    var s22[2]: i4
    s22[0] = 2
    s22[1] = 2
    var M: tensor& = zeros(2, s22, DT_F8)
    M->set_at(0, 4.0)
    M->set_at(1, 1.0)
    M->set_at(2, 1.0)
    M->set_at(3, 3.0)

    var bb: tensor& = zeros(1, s22, DT_F8)
    bb->set_at(0, 1.0)
    bb->set_at(1, 2.0)

    var Mi: tensor& = M->inv()
    var xx: tensor& = M->solve(bb)
    var eout[2]: &
    var ok: bool = M->eigh((eout: &))
    var ev: tensor& = (eout[0]: tensor&)

    printf("det=%g nrm=%g inv00=%g inv11=%g x0=%g x1=%g eig0=%g eig1=%g ok=%d\n",
        M->det(), M->norm(2.0), Mi->at(0), Mi->at(3), xx->at(0), xx->at(1), ev->at(0), ev->at(1), (ok: i4))

    M->drop()
    bb->drop()
    Mi->drop()
    xx->drop()
    ev->drop()
    return 0
'''
    got = run_sc(code)

    M = np.array([[4.0, 1.0], [1.0, 3.0]], dtype=np.float64)
    b = np.array([1.0, 2.0], dtype=np.float64)
    w = np.linalg.eigvalsh(M)
    exp = {
        "det": float(np.linalg.det(M)),
        "nrm": float(np.linalg.norm(M)),
        "inv00": float(np.linalg.inv(M)[0, 0]),
        "inv11": float(np.linalg.inv(M)[1, 1]),
        "x0": float(np.linalg.solve(M, b)[0]),
        "x1": float(np.linalg.solve(M, b)[1]),
        "eig0": float(w[0]),
        "eig1": float(w[1]),
    }
    if int(got.get("ok", 0.0)) != 1:
        raise AssertionError("linalg:eigh returned ok=0")
    for k, v in exp.items():
        assert_close(f"linalg:{k}", got[k], v, atol=1e-5, rtol=1e-5)


def case_meshgrid() -> None:
    code = r'''
inc ts.sc

fnc main: i4
    var x: tensor& = arange(0.0, 3.0, 1.0, DT_F8)
    var y: tensor& = arange(0.0, 2.0, 1.0, DT_F8)
    var arr[2]: &
    arr[0] = (x: &)
    arr[1] = (y: &)
    var out[2]: &
    var ok: bool = meshgrid((arr: &), 2, 0, (out: &))
    var gx: tensor& = (out[0]: tensor&)
    var gy: tensor& = (out[1]: tensor&)
    printf("ok=%d gx0=%g gx5=%g gy0=%g gy5=%g\n", (ok: i4), gx->at(0), gx->at(5), gy->at(0), gy->at(5))
    x->drop()
    y->drop()
    gx->drop()
    gy->drop()
    return 0
'''
    got = run_sc(code)

    x = np.arange(0.0, 3.0, 1.0, dtype=np.float64)
    y = np.arange(0.0, 2.0, 1.0, dtype=np.float64)
    gx, gy = np.meshgrid(x, y, indexing="ij")
    exp = {
        "gx0": float(gx.reshape(-1)[0]),
        "gx5": float(gx.reshape(-1)[5]),
        "gy0": float(gy.reshape(-1)[0]),
        "gy5": float(gy.reshape(-1)[5]),
    }
    if int(got.get("ok", 0.0)) != 1:
        raise AssertionError("meshgrid returned ok=0")
    for k, v in exp.items():
        assert_close(f"meshgrid:{k}", got[k], v)


def main() -> None:
    if not SCC.exists():
        raise SystemExit(f"missing scc: {SCC}")

    case_basic_reduce()
    case_linalg()
    case_meshgrid()
    print("numpy parity smoke: PASS")


if __name__ == "__main__":
    main()
