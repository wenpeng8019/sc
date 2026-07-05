#!/usr/bin/env bash
# 临时：Windows/MSVC 远程构建跨平台正确性对比（本机 run vs 远程 run，比对 stdout+退出码）
# 用法：tests/win_remote_check.sh [case...]；不带参跑默认集
set -u
cd "$(dirname "$0")/.."
SCC=./compiler/build/scc
TGT=templates/targets/windows-x64.target

CASES=("$@")
if [ ${#CASES[@]} -eq 0 ]; then
  CASES=(
    auto_ptr bare_at thin_at object_at bst_at list_at heap_at lru_at
    dict_at ring_at trie_at forin_adt mod_basic array expr cast
    qualifiers macro macro_fnc final fat_array fat_global chain_sort
    ptr_check print_str_chn goto_scope
  )
fi

pass=0; fail=0; skip=0
for c in "${CASES[@]}"; do
  f="tests/cases/$c.sc"
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
