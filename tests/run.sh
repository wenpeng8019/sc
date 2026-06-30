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
    examples/feature10.sc
    examples/feature11.sc
    examples/feature12.sc
    examples/feature13.sc
    examples/feature14.sc
    examples/feature15.sc
    examples/feature16.sc
    examples/feature17.sc
    examples/feature18.sc
    examples/feature19.sc
    examples/feature20.sc
    examples/feature21.sc
    examples/feature22.sc
    examples/feature23.sc
    examples/feature24.sc
    examples/feature25.sc
    examples/feature26.sc
    examples/feature27.sc
    examples/feature28.sc
    examples/feature29.sc
    examples/feature31.sc
    examples/feature32.sc
    examples/feature33.sc
    examples/feature34.sc
    examples/feature35.sc
    examples/feature36.sc
    examples/feature37.sc
    examples/feature38.sc
    examples/feature39.sc
    examples/feature40.sc
    examples/feature41.sc
    examples/feature42.sc
    examples/feature43.sc
    examples/feature44.sc
    examples/feature45.sc
    examples/feature30/feature30.sc
    examples/feature46/feature46.sc
    examples/feature47.sc
    examples/feature48.sc
    examples/feature49.sc
    examples/feature50.sc
    examples/feature51.sc
    examples/feature52.sc
    examples/feature53.sc
    examples/feature54.sc
    examples/feature55.sc
    examples/feature56.sc
    examples/feature_forward.sc
    examples/feature_export_inc.sc
    examples/feature4_lib.sc
    tests/cases/cast.sc
    tests/cases/expr.sc
    tests/cases/auto_ptr.sc
    tests/cases/object_at.sc
    tests/cases/bare_at.sc
    tests/cases/list_at.sc
    tests/cases/dict_at.sc
    tests/cases/thin_at.sc
    tests/cases/ring_at.sc
    tests/cases/bst_at.sc
    tests/cases/heap_at.sc
    tests/cases/trie_at.sc
    tests/cases/lru_at.sc
    tests/cases/print_str_chn.sc
    tests/cases/forin_adt.sc
    tests/cases/chain_sort.sc
    tests/cases/sync_rpc.sc
    tests/cases/array.sc
    tests/cases/fat_array.sc
    tests/cases/fat_global.sc
    tests/cases/final.sc
    tests/cases/atom.sc
    tests/cases/stack_canary.sc
    tests/cases/goto_scope.sc
    tests/cases/ptr_check.sc
    tests/cases/qualifiers.sc
    tests/cases/cbridge.sc
    tests/cases/macro.sc
    tests/cases/macro_fnc.sc
    tests/cases/args_native/args_native.sc
    tests/cases/mod_basic.sc
    tests/cases/mod_cross/mod_cross.sc
    tests/cases/ts_basic.sc
    tests/cases/nn_train.sc
    tests/cases/neuron_train.sc
    tests/cases/cnn_train.sc
    tests/cases/attn_train.sc
)

# --check=mem 越界 canary 用例：复用既有 sc 源，比对 mem-check 下的 emit-c 产物
# （golden 后缀 .mem.c，独立于默认 emit-c，证明头尾哨兵注入路径稳定）
CHECKMEM=(
    tests/cases/auto_ptr.sc
    tests/cases/stack_canary.sc
)

# --check=ptr 运行时守卫用例：比对 ptr-check 下的 emit-c 产物（golden 后缀 .ptr.c）
CHECKPTR=(
    tests/cases/ptr_check.sc
)

# 负向用例（预期编译失败，比对 stderr 错误信息）
NEGATIVE=(
    examples/feature_bad_value_cycle.sc
    tests/cases/goto_bad_cross.sc
    tests/cases/abi_fat_bad.sc
    tests/cases/fat_array_bad.sc
    tests/cases/fat_copy_bad.sc
    tests/cases/undef_func_bad.sc
    tests/cases/undef_var_bad.sc
    tests/cases/arg_count_bad.sc
    tests/cases/member_bad.sc
    tests/cases/typo_hint_bad.sc
    tests/cases/type_mismatch_bad.sc
    tests/cases/bad_op_bad.sc
    tests/cases/div_zero_bad.sc
    tests/cases/let_reassign_bad.sc
    tests/cases/dup_def_bad.sc
    tests/cases/dead_code_bad.sc
    tests/cases/assign_lvalue_bad.sc
    tests/cases/missing_return_bad.sc
    tests/cases/cross_module_bad.sc
    tests/cases/int_range_bad.sc
    tests/cases/case_exhaustive_bad.sc
    tests/cases/bare_rpc_bad.sc
    tests/cases/mod_export_init_bad.sc
    tests/cases/mod_private_export_bad.sc
    tests/cases/dep_cycle_bad.sc
)

# 运行时守卫触发用例：编译并运行（带 --check），比对程序 stderr 报错（golden .trap）。
# 证明三套内存保护在运行期真正拦截，而非仅产物快照：
#   ptr 空指针/越界（致命 abort）、mem 栈缓冲上溢（报告）、ref 悬挂引用（报告）。
# 每项格式 "源文件 检查档"。
RUNTIME_TRAP=(
    "tests/cases/trap_ptr_nil.sc ptr"
    "tests/cases/trap_ptr_oob.sc ptr"
    "tests/cases/trap_mem_overflow.sc mem"
    "tests/cases/trap_ref_dangle.sc ref"
    "tests/cases/auto_ptr_drop_bad.sc ref"
)

# 单元测试框架用例：--test 编译并运行 tst 用例，比对归一化后的 TAP 报告（golden .tap）。
# 覆盖 通过 / 软失败（assert 值回显）/ tst.skip 三态与汇总行；退出码=失败用例数。
TEST_RUN=(
    examples/test_demo.sc
    tests/cases/crypto_test.sc
    tests/cases/ssl_test.sc
    tests/cases/ws_test.sc
    tests/cases/securechan_test.sc
)

pass=0 fail=0 upd=0
TMP="$(mktemp)"
trap 'rm -f "$TMP"' EXIT

# 归一化错误输出：去掉源文件路径前缀（绝对/相对差异 → 仅留 文件名.sc）
norm_err() { sed -E 's#^([^ ]*/)?([A-Za-z0-9_]+\.sc):#\2:#'; }

# 归一化 TAP 报告：把行内任意绝对/相对路径前缀压成 文件名.sc（assert 失败定位行用）
norm_tap() { sed -E 's#(/[^ ]*/)?([A-Za-z0-9_]+\.sc:)#\2#g'; }

# 比对或更新一个快照。 $1=标签  $2=黄金文件  $3=实际内容
snapshot() {
    local label=$1 gold=$2 got=$3
    if [ "$UPDATE" = 1 ]; then
        printf '%s\n' "$got" > "$gold"
        upd=$((upd + 1))
        return
    fi
    if [ ! -f "$gold" ]; then
        echo "  ✗ ${label}：缺少黄金文件 ${gold#$ROOT/}（首次请运行 ./build.sh test --update）"
        fail=$((fail + 1))
        return
    fi
    if printf '%s\n' "$got" | diff -u "$gold" - > "$TMP" 2>&1; then
        pass=$((pass + 1))
    else
        echo "  ✗ ${label}：产物与黄金文件不一致"
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

for src in "${CHECKMEM[@]}"; do
    name="$(basename "$src" .sc)"
    if c="$("$SCC" "$src" --emit-c --check=mem 2>/dev/null)"; then
        snapshot "$name (emit-c --check=mem)" "$GOLDEN/$name.mem.c" "$c"
    else
        echo "  ✗ $name (emit-c --check=mem)：scc 失败"; fail=$((fail + 1))
    fi
done

for src in "${CHECKPTR[@]}"; do
    name="$(basename "$src" .sc)"
    if c="$("$SCC" "$src" --emit-c --check=ptr 2>/dev/null)"; then
        snapshot "$name (emit-c --check=ptr)" "$GOLDEN/$name.ptr.c" "$c"
    else
        echo "  ✗ $name (emit-c --check=ptr)：scc 失败"; fail=$((fail + 1))
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

# 运行时守卫：编译并运行带 --check 的程序，捕获其 stderr 报错并比对 .trap 黄金。
# （致命类 abort 退出码非 0、报告类退出 0；统一以 stderr 报错文本为准，足证拦截发生）
for entry in "${RUNTIME_TRAP[@]}"; do
    set -- $entry
    src=$1; flag=$2
    name="$(basename "$src" .sc)"
    trap_err="$("$SCC" "$src" --check="$flag" 2>&1 1>/dev/null)"
    trap_err="$(printf '%s' "$trap_err" | norm_err)"
    snapshot "$name (trap --check=$flag)" "$GOLDEN/$name.trap" "$trap_err"
done

# 单元测试框架：--test 运行 tst 用例，捕获 TAP 报告（含失败用例非零退出码）并归一化比对。
for src in "${TEST_RUN[@]}"; do
    name="$(basename "$src" .sc)"
    tap="$("$SCC" "$src" --test 2>&1)"
    tap="$(printf '%s' "$tap" | norm_tap)"
    snapshot "$name (test)" "$GOLDEN/$name.tap" "$tap"
done

echo
if [ "$UPDATE" = 1 ]; then
    echo "==> 已更新 $upd 个黄金文件"
    exit 0
fi
echo "==> 回归通过 ${pass}，失败 ${fail}"
[ "$fail" = 0 ] || exit 1
