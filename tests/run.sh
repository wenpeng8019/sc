#!/bin/bash
# ============================================================
# sc 回归测试 —— 黄金快照（golden snapshot）
# ============================================================
# 对确定性产物做"产物不变"回归，专用于在 AST / codegen 优化后做回归验证：
#   --emit-c   sc → C 源码（纯函数，不含运行时地址/绝对路径，可稳定快照）
#   --emit-sc  AST → 规范化 sc 源码（同上）
# 另对负向用例比对编译错误信息（stderr）。
#
# 用例来源：
#   examples/feature*.sc   语言特性示例（复用）
#   tests/cases/*.sc       精简专项用例（针对易回归的表达式/codegen 点）
# 黄金文件存于 tests/golden/<用例名>.{c,sc,err}。
#
# 用法：
#   tests/run.sh            比对黄金文件，发现差异即失败（退出码 1）
#   tests/run.sh --update   重新生成全部黄金文件（有意改动产物后执行）
# ============================================================
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCC="$ROOT/compiler/build/scc"
GOLDEN="$ROOT/tests/golden"

UPDATE=0
[ "${1:-}" = "--update" ] && UPDATE=1

[ -x "$SCC" ] || { echo "错误: 未找到 scc，请先 ./build.sh build （期望 $SCC）" >&2; exit 1; }
mkdir -p "$GOLDEN"

# 正向用例（emit-c + emit-sc 双快照）：源文件相对 ROOT 路径
POSITIVE=(
    examples/feature1.sc
    examples/feature2.sc
    examples/feature3.sc
    examples/feature4.sc
    examples/feature5.sc
    examples/feature6.sc
    examples/feature7.sc
    examples/feature8.sc
    examples/feature9.sc
    examples/feature_forward.sc
    examples/feature_export_inc.sc
    tests/cases/cast.sc
    tests/cases/expr.sc
)

# 负向用例（预期编译失败，比对 stderr 错误信息）
NEGATIVE=(
    examples/feature_bad_value_cycle.sc
)

pass=0 fail=0 upd=0
TMP="$(mktemp)"
trap 'rm -f "$TMP"' EXIT

# 归一化错误输出：去掉源文件路径前缀（绝对/相对差异 → 仅留 文件名.sc）
norm_err() { sed -E 's#^([^ ]*/)?([A-Za-z0-9_]+\.sc):#\2:#'; }

# 比对或更新一个快照。 $1=标签  $2=黄金文件  $3=实际内容
snapshot() {
    local label=$1 gold=$2 got=$3
    if [ "$UPDATE" = 1 ]; then
        printf '%s\n' "$got" > "$gold"
        upd=$((upd + 1))
        return
    fi
    if [ ! -f "$gold" ]; then
        echo "  ✗ $label：缺少黄金文件 ${gold#$ROOT/}（首次请运行 ./build.sh test --update）"
        fail=$((fail + 1))
        return
    fi
    if printf '%s\n' "$got" | diff -u "$gold" - > "$TMP" 2>&1; then
        pass=$((pass + 1))
    else
        echo "  ✗ $label：产物与黄金文件不一致"
        sed 's/^/      /' "$TMP"
        fail=$((fail + 1))
    fi
}

cd "$ROOT"
[ "$UPDATE" = 1 ] && echo "==> 回归：更新黄金快照" || echo "==> 回归：黄金快照比对"

for src in "${POSITIVE[@]}"; do
    name="$(basename "$src" .sc)"
    if c="$("$SCC" "$src" --emit-c 2>/dev/null)"; then
        snapshot "$name (emit-c)" "$GOLDEN/$name.c" "$c"
    else
        echo "  ✗ $name (emit-c)：scc 失败"; fail=$((fail + 1))
    fi
    if s="$("$SCC" "$src" --emit-sc 2>/dev/null)"; then
        snapshot "$name (emit-sc)" "$GOLDEN/$name.sc" "$s"
    else
        echo "  ✗ $name (emit-sc)：scc 失败"; fail=$((fail + 1))
    fi
done

for src in "${NEGATIVE[@]}"; do
    name="$(basename "$src" .sc)"
    err="$("$SCC" "$src" 2>&1 1>/dev/null)"; rc=$?
    if [ "$rc" = 0 ]; then
        echo "  ✗ $name (err)：预期编译失败，却成功了"; fail=$((fail + 1)); continue
    fi
    err="$(printf '%s' "$err" | norm_err)"
    snapshot "$name (err)" "$GOLDEN/$name.err" "$err"
done

echo
if [ "$UPDATE" = 1 ]; then
    echo "==> 已更新 $upd 个黄金文件"
    exit 0
fi
echo "==> 回归通过 ${pass}，失败 ${fail}"
[ "$fail" = 0 ] || exit 1
