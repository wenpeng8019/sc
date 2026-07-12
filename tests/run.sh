#!/bin/bash
# ============================================================
# sc 统一测试运行入口（run）
# ============================================================
# 单一入口分发全部测试/验证子命令，替代此前散落的多个脚本：
#   golden       黄金快照回归（默认）——确定性产物“产物不变”回归：
#                  --emit-c（sc→C）/--emit-sc（AST→规范化 sc）双快照 +
#                  负向用例 stderr + 运行时守卫 trap + --test TAP +
#                  proggraph JSON + 着色器双 emit；黄金存 tests/golden/。
#   linalg       默认核 vs SCC_WITH_LAPACK 路径数值一致性（需 OpenBLAS，缺则跳过）
#   bench        ts 运行时轻量性能基线（ts_basic + dnn，可选 LAPACK 路径）
#   linux        Linux/WSL 平台适配验证（构建 + 回归 + 可选 Vulkan/shader-tri）
#   win-remote   Windows/MSVC 远程构建对拍（本机 run vs 远程 run，需远程目标）
#   numpy        与 numpy 对拍 smoke（需 numpy；委托 numpy_parity_smoke.py）
#   all          离线安全全集（golden + linalg + bench + numpy）
#
# 用法：
#   tests/run.sh                    # golden 比对（等价 tests/run.sh golden）
#   tests/run.sh --update           # 重生成全部黄金文件（向后兼容旧调用）
#   tests/run.sh golden --update
#   tests/run.sh linalg
#   tests/run.sh bench
#   tests/run.sh linux [--quick|--with-shader-tri|--with-vulkaninfo]
#   tests/run.sh win-remote [case...]
#   tests/run.sh numpy
#   tests/run.sh all
# ============================================================
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCC="$ROOT/compiler/build/scc"
GOLDEN="$ROOT/tests/golden"
SELF="$ROOT/tests/run.sh"
OPENBLAS_PREFIX="${OPENBLAS_PREFIX:-/opt/homebrew/opt/openblas}"

require_scc() {
    [ -x "$SCC" ] || { echo "错误: 未找到 scc，请先 ./build.sh build （期望 $SCC）" >&2; exit 1; }
}

# ============================================================
# golden —— 黄金快照回归（默认子命令）
# ============================================================
cmd_golden() {
    require_scc
    local UPDATE=0
    [ "${1:-}" = "--update" ] && UPDATE=1
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
    tests/cases/proto_at.sc
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
    tests/cases/codec_test.sc
    tests/cases/zstream_test.sc
    tests/cases/jstream_test.sc
    tests/cases/bmp_test.sc
    tests/cases/tga_test.sc
    tests/cases/png_test.sc
    tests/cases/jpg_test.sc
    tests/cases/add_sc_test.sc
    tests/cases/inl_test.sc
    tests/cases/io_seek_test.sc
    tests/cases/rsa_proxy_test.sc
    tests/cases/ssl_test.sc
    tests/cases/ws_test.sc
    tests/cases/securechn_test.sc
)

# 程序结构依赖图（proggraph）：--graph=unit 导出 JSON，快照锁定节点/边结构。
GRAPH_UNIT=(
    tests/cases/graph_demo.sc
)

# 着色器双 emit 黄金（syntax-s）：同一 .ss 锁两条链——默认链（SPIR-V→SPIRV-Cross
# 反译，stdout 文本）+ --emit-glsl（自研文本发射，对照/兜底通道）。
# 纯文本比对，无 spirv-val/glslangValidator 外部工具依赖。
SHADER_EMIT=(
    tests/cases/shader_p0.ss
    tests/cases/shader_p1.ss
    tests/cases/shader_p1_solo.ss
    tests/cases/shader_p1_size.ss
    tests/cases/shader_p1_projlod.ss
    tests/cases/shader_p1_proj_vert.ss
    tests/cases/shader_p1_shadow.ss
    tests/cases/shader_p1_grad.ss
    tests/cases/shader_p1_align.ss
    tests/cases/shader_p1_gaps.ss
    tests/cases/shader_p1_pointcoord.ss
    tests/cases/shader_p1_basic.ss
    tests/cases/shader_p1_multi.ss
    tests/cases/shader_p1_array.ss
    tests/cases/shader_p1_proj.ss
    tests/cases/shader_spec.ss
    tests/cases/shader_spec_use.ss
)

pass=0 fail=0 upd=0
TMP="$(mktemp)"
TMPERR="$(mktemp)"
trap 'rm -f "$TMP" "$TMPERR"' EXIT

# 归一化错误输出：去掉源文件路径前缀（绝对/相对差异 → 仅留 文件名.sc）
norm_err() { sed -E 's#^([^ ]*/)?([A-Za-z0-9_]+\.sc):#\2:#'; }

# 归一化 TAP 报告：把行内任意绝对/相对路径前缀压成 文件名.sc（assert 失败定位行用）
norm_tap() { sed -E 's#(/[^ ]*/)?([A-Za-z0-9_]+\.sc:)#\2#g'; }

# 归一化 proggraph JSON：把绝对 ROOT 路径前缀压成工作区相对路径（跨机器稳定）
norm_graph() { sed "s#${ROOT}/##g"; }

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

# 采集正向产物快照：要求 scc 退出码 0 且【stderr 为空】。
# 正向用例的编译期 stderr 本应干净——任何告警（宏重定义 / conflicting types …）
# 都在此暴露为失败，杜绝历史上 “2>/dev/null 吞掉告警、回归照绿” 的隐患。
# $1=标签  $2=黄金文件  其后=传给 scc 的参数
emit_snapshot() {
    local label=$1 gold=$2; shift 2
    local out rc
    out="$("$SCC" "$@" 2>"$TMPERR")"; rc=$?
    if [ "$rc" != 0 ]; then
        echo "  ✗ ${label}：scc 失败"; fail=$((fail + 1)); return
    fi
    if [ -s "$TMPERR" ]; then
        echo "  ✗ ${label}：编译期 stderr 非空（疑似被吞的告警）："
        sed 's/^/      /' "$TMPERR"; fail=$((fail + 1)); return
    fi
    snapshot "$label" "$gold" "$out"
}

cd "$ROOT"
[ "$UPDATE" = 1 ] && echo "==> 回归：更新黄金快照" || echo "==> 回归：黄金快照比对"

for src in "${POSITIVE[@]}"; do
    name="$(basename "$src" .sc)"
    emit_snapshot "$name (emit-c)" "$GOLDEN/$name.c" "$src" --emit-c
    emit_snapshot "$name (emit-sc)" "$GOLDEN/$name.sc" "$src" --emit-sc
done

for src in "${CHECKMEM[@]}"; do
    name="$(basename "$src" .sc)"
    emit_snapshot "$name (emit-c --check=mem)" "$GOLDEN/$name.mem.c" "$src" --emit-c --check=mem
done

for src in "${CHECKPTR[@]}"; do
    name="$(basename "$src" .sc)"
    emit_snapshot "$name (emit-c --check=ptr)" "$GOLDEN/$name.ptr.c" "$src" --emit-c --check=ptr
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

# 程序结构依赖图：--graph=unit 导出 JSON，归一化绝对路径后比对 .graph.json 黄金。
for src in "${GRAPH_UNIT[@]}"; do
    name="$(basename "$src" .sc)"
    g="$("$SCC" "$src" --graph=unit 2>"$TMPERR")"; rc=$?
    if [ "$rc" != 0 ]; then
        echo "  ✗ $name (graph=unit)：scc 失败"; fail=$((fail + 1))
    elif [ -s "$TMPERR" ]; then
        echo "  ✗ $name (graph=unit)：编译期 stderr 非空（疑似被吞的告警）："
        sed 's/^/      /' "$TMPERR"; fail=$((fail + 1))
    else
        g="$(printf '%s' "$g" | norm_graph)"
        snapshot "$name (graph=unit)" "$GOLDEN/$name.graph.json" "$g"
    fi
done

# 着色器双 emit：默认链（stdout 文本）与 --emit-glsl 各锁一份黄金。
for src in "${SHADER_EMIT[@]}"; do
    name="$(basename "$src" .ss)"
    t="$("$SCC" "$src" 2>&1)"; rc=$?
    if [ "$rc" != 0 ]; then
        echo "  ✗ $name (shader)：scc 失败"; sed 's/^/      /' <<<"$t"; fail=$((fail + 1))
    else
        snapshot "$name (shader)" "$GOLDEN/$name.shader.txt" "$t"
    fi
    t="$("$SCC" "$src" --emit-glsl 2>&1)"; rc=$?
    if [ "$rc" != 0 ]; then
        echo "  ✗ $name (shader emit-glsl)：scc 失败"; sed 's/^/      /' <<<"$t"; fail=$((fail + 1))
    else
        snapshot "$name (shader emit-glsl)" "$GOLDEN/$name.shader.glsl.txt" "$t"
    fi
done

echo
if [ "$UPDATE" = 1 ]; then
    echo "==> 已更新 $upd 个黄金文件"
    return 0
fi
echo "==> 回归通过 ${pass}，失败 ${fail}"
[ "$fail" = 0 ]
}

# ============================================================
# linalg —— 默认核 vs SCC_WITH_LAPACK 路径数值一致性
# ============================================================
cmd_linalg() ( set -euo pipefail
    require_scc
    local CASE="$ROOT/tests/cases/ts_basic.sc"

    local out_def
    out_def="$("$SCC" "$CASE" 2>/dev/null)"

    if [ ! -f "$OPENBLAS_PREFIX/include/lapacke.h" ]; then
        echo "WARN: lapacke.h not found under $OPENBLAS_PREFIX/include; skip LAPACK path"
        echo "default path smoke: PASS"
        exit 0
    fi

    local out_lap
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
    assert_close "$(num_from_line "$out_def" det)"        "$(num_from_line "$out_lap" det)"        det
    assert_close "$(num_from_line "$out_def" inv\ at0)"   "$(num_from_line "$out_lap" inv\ at0)"   inv00
    assert_close "$(num_from_line "$out_def" solve\ at0)" "$(num_from_line "$out_lap" solve\ at0)" solve_x0
    assert_close "$(num_from_line "$out_def" lo)"         "$(num_from_line "$out_lap" lo)"         eigh_lo

    # QR 允许符号翻转，比较 |R00| 与 R11。
    local r00_d r00_l abs_r00_d abs_r00_l
    r00_d="$(num_from_line "$out_def" R00)"; r00_l="$(num_from_line "$out_lap" R00)"
    abs_r00_d="$(awk -v x="$r00_d" 'BEGIN{if(x<0)x=-x;print x}')"
    abs_r00_l="$(awk -v x="$r00_l" 'BEGIN{if(x<0)x=-x;print x}')"
    assert_close "$abs_r00_d" "$abs_r00_l" qr_abs_r00
    assert_close "$(num_from_line "$out_def" R11)" "$(num_from_line "$out_lap" R11)" qr_r11

    local mesh_d mesh_l
    mesh_d="$(echo "$out_def" | grep 'meshgrid ok=' | head -1)"
    mesh_l="$(echo "$out_lap" | grep 'meshgrid ok=' | head -1)"
    [ "$mesh_d" = "$mesh_l" ] || { echo "FAIL meshgrid line mismatch"; echo "def: $mesh_d"; echo "lap: $mesh_l"; exit 1; }

    echo "dual path linalg check: PASS"
)

# ============================================================
# bench —— ts 运行时轻量性能基线
# ============================================================
cmd_bench() ( set -euo pipefail
    require_scc
    run_case() {
        local name="$1" cmd="$2"
        echo "== $name =="
        /usr/bin/time -l sh -c "$cmd" >/dev/null
    }
    echo "[baseline]"
    run_case "ts_basic default" "$SCC $ROOT/tests/cases/ts_basic.sc"
    run_case "dnn default" "$SCC $ROOT/templates/dnn-framework/hello-dnn/hello.sc"
    if [ -f "$OPENBLAS_PREFIX/include/lapacke.h" ]; then
        echo "[lapack+openblas]"
        run_case "ts_basic lapack" "SCC_CFLAGS='-DSCC_WITH_LAPACK -I$OPENBLAS_PREFIX/include' SCC_LDFLAGS='-L$OPENBLAS_PREFIX/lib -lopenblas' $SCC $ROOT/tests/cases/ts_basic.sc"
    fi
    echo "benchmark done"
)

# ============================================================
# linux —— Linux/WSL 平台适配验证（构建 + 回归 + 可选 Vulkan/shader-tri）
# ============================================================
cmd_linux() ( set -euo pipefail
    cd "$ROOT"
    local RUN_REGRESSION=1 RUN_SHADER_TRI=0 RUN_VULKANINFO=0
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --quick|--skip-regression) RUN_REGRESSION=0 ;;
            --with-shader-tri)         RUN_SHADER_TRI=1 ;;
            --with-vulkaninfo)         RUN_VULKANINFO=1 ;;
            -h|--help)
                cat <<'EOF'
用法: tests/run.sh linux [选项]

选项:
  --quick             快速模式（跳过快照回归）
  --skip-regression   跳过回归（同 --quick）
  --with-shader-tri   额外构建 examples/shader-tri（覆盖 gpu builtin 内建路径）
  --with-vulkaninfo   额外执行 vulkaninfo 摘要检查
  -h, --help          显示帮助
EOF
                exit 0 ;;
            *) echo "未知参数: $1" >&2; exit 1 ;;
        esac
        shift
    done

    echo "==> [env] 平台信息"
    uname -a
    if grep -qi microsoft /proc/version 2>/dev/null; then
        echo "检测到 WSL 环境"
    else
        echo "检测到原生 Linux 环境"
    fi

    echo "==> [1/3] 构建 scc"
    ./build.sh build

    echo "==> [2/3] 语言特性与黄金回归"
    if [[ "$RUN_REGRESSION" == "1" ]]; then
        # 子进程调用 golden，隔离其“计数失败不 set -e”的执行语义
        bash "$SELF" golden
    else
        echo "跳过快照回归（quick 模式）"
    fi

    if [[ "$RUN_VULKANINFO" == "1" ]]; then
        echo "==> [3/3] Vulkan 工具链摘要"
        if command -v vulkaninfo >/dev/null 2>&1; then
            vulkaninfo --summary || echo "警告: vulkaninfo 执行失败（可能未配置 ICD/GPU 驱动）" >&2
        else
            echo "警告: 未安装 vulkaninfo（Ubuntu 可安装 vulkan-tools）" >&2
        fi
    else
        echo "==> [3/3] Vulkan 工具链摘要：跳过（使用 --with-vulkaninfo 开启）"
    fi

    if [[ "$RUN_SHADER_TRI" == "1" ]]; then
        echo "==> [extra] shader-tri 构建验证（内建 gpu builtin 路径）"
        ( cd "$ROOT/examples/shader-tri" && ./build.sh )
    fi

    echo "==> 完成：Linux/WSL 平台基线验证通过"
)

# ============================================================
# win-remote —— Windows/MSVC 远程构建跨平台正确性对拍
# ============================================================
# 本机 run vs 远程 run，比对 stdout+退出码；需 windows-x64.target 配置的远程目标。
cmd_winremote() {
    require_scc
    local TGT="$ROOT/templates/.scenv/targets/windows-x64.target"
    local CASES=("$@")
    if [ ${#CASES[@]} -eq 0 ]; then
        CASES=(
            auto_ptr bare_at thin_at object_at bst_at list_at heap_at lru_at
            dict_at ring_at trie_at forin_adt mod_basic array expr cast
            qualifiers macro macro_fnc final fat_array fat_global chain_sort
            ptr_check print_str_chn goto_scope
        )
    fi
    local pass=0 fail=0 skip=0 c f lo ro lrc rrc
    for c in "${CASES[@]}"; do
        f="$ROOT/tests/cases/$c.sc"
        [ -f "$f" ] || { echo "SKIP $c（无文件）"; skip=$((skip+1)); continue; }
        lo=$("$SCC" "$f" 2>/dev/null); lrc=$?
        ro=$("$SCC" --target "$TGT" "$f" 2>/dev/null | tr -d '\r'); rrc=$?
        if [ "$lo" = "$ro" ] && [ "$lrc" = "$rrc" ]; then
            echo "PASS $c (exit=$lrc)"; pass=$((pass+1))
        else
            echo "FAIL $c  本机exit=$lrc 远程exit=$rrc"
            fail=$((fail+1))
            diff <(printf '%s' "$lo") <(printf '%s' "$ro") | head -20
        fi
    done
    echo "==> 远程对比：通过 $pass，失败 $fail，跳过 $skip"
    [ "$fail" = 0 ]
}

# ============================================================
# numpy —— 与 numpy 对拍 smoke（委托 numpy_parity_smoke.py）
# ============================================================
cmd_numpy() {
    require_scc
    python3 "$ROOT/tests/numpy_parity_smoke.py"
}

# ============================================================
# all —— 离线安全全集（子进程隔离，逐项汇总，不因单项失败中断）
# ============================================================
cmd_all() {
    local rc=0 sub
    for sub in golden linalg bench numpy; do
        echo "==================== [all] $sub ===================="
        bash "$SELF" "$sub" || rc=1
    done
    [ "$rc" = 0 ] && echo "==> all：全部通过" || echo "==> all：存在失败"
    return "$rc"
}

usage() {
    cat <<'EOF'
sc 统一测试运行入口

用法: tests/run.sh <子命令> [参数]

子命令:
  golden [--update]   黄金快照回归（默认；无参或 --update 亦走此项）
  linalg              默认核 vs SCC_WITH_LAPACK 数值一致性（需 OpenBLAS，缺则跳过）
  bench               ts 运行时轻量性能基线（ts_basic + dnn，可选 LAPACK）
  linux [选项]        Linux/WSL 平台验证（--quick/--with-shader-tri/--with-vulkaninfo）
  win-remote [case…]  Windows/MSVC 远程构建对拍（需远程目标）
  numpy               与 numpy 对拍 smoke（需 numpy）
  all                 离线安全全集（golden + linalg + bench + numpy）
  help                显示本帮助
EOF
}

# ------------------------------------------------------------
# 分发（无参 / --update 向后兼容 golden，令 build.sh test 不受影响）
# ------------------------------------------------------------
sub="${1:-golden}"
case "$sub" in
    --update)        cmd_golden --update ;;
    golden)          shift; cmd_golden "${1:-}" ;;
    linalg)          cmd_linalg ;;
    bench)           cmd_bench ;;
    linux)           shift; cmd_linux "$@" ;;
    win-remote)      shift; cmd_winremote "$@" ;;
    numpy)           cmd_numpy ;;
    all)             cmd_all ;;
    -h|--help|help)  usage ;;
    *)               echo "未知子命令: $sub" >&2; usage; exit 1 ;;
esac
