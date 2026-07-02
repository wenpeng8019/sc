# chain.sort 回归用例：Simon Tatham 自底向上 O(n log n) 归并排序（utlist DL_SORT2 改写）。
# 覆盖：
#   1. 升序/降序：同一比较回调取反即得逆序；
#   2. 稳定性：等键元素保持原相对次序（用 (key, seq) 双字段验证）；
#   3. 规模与边界：空链/单元素/已序/逆序/含重复键；
#   4. 排序后 chain 约定不变：head._prev = rear、rear._next = nil（正反双向遍历均自洽）。
# chain 为 op.sc 默认导入机制，无需 inc。

def item: ~ {
    key: i4
    seq: i4      # 原始入链序，用于验证稳定排序
}

# 升序比较：实参为节点首址，(a: item&) 还原回元素
@fnc by_key_asc: i4, a: &, b: &
    var x: item& = (a: item&)
    var y: item& = (b: item&)
    return x->key - y->key

# 降序比较
@fnc by_key_desc: i4, a: &, b: &
    var x: item& = (a: item&)
    var y: item& = (b: item&)
    return y->key - x->key

fnc dump: tag: char&, l: chain&
    ::printf("%s:", tag)
    var it: item& = l->first(): item&
    while it != nil
        ::printf(" %d", it->key)
        it = it->next
    ::printf("\n")

# 反向遍历，验证排序后 _prev 链（head._prev = rear）自洽
fnc dump_rev: tag: char&, l: chain&
    ::printf("%s:", tag)
    var it: item& = l->last(): item&
    while it != nil
        ::printf(" %d", it->key)
        it = it->prev
    ::printf("\n")

# 带稳定性标记的遍历：打印 key.seq
fnc dump_stable: tag: char&, l: chain&
    ::printf("%s:", tag)
    var it: item& = l->first(): item&
    while it != nil
        ::printf(" %d.%d", it->key, it->seq)
        it = it->next
    ::printf("\n")

@fnc main: i4
    var n[8]: item
    var i: i4
    var keys[8]: i4
    keys[0] = 5
    keys[1] = 2
    keys[2] = 8
    keys[3] = 2
    keys[4] = 9
    keys[5] = 1
    keys[6] = 5
    keys[7] = 2
    for i = 0; i < 8; i++
        n[i].key = keys[i]
        n[i].seq = i

    # ---------- 一般乱序：升序 ----------
    var l: chain
    for i = 0; i < 8; i++
        l.append(&n[i])
    dump("before  ", &l)
    l.sort(by_key_asc)
    dump("asc     ", &l)
    dump_rev("asc(rev)", &l)
    dump_stable("stable  ", &l)        # 等键 (2.1 2.3 2.7) 保持原序证明稳定

    # ---------- 降序 ----------
    l.sort(by_key_desc)
    dump("desc    ", &l)
    dump_rev("desc(rev", &l)

    # ---------- 边界：空链 ----------
    var e: chain
    e.sort(by_key_asc)
    ::printf("empty head_nil=%d\n", e.first() == nil)

    # ---------- 边界：单元素 ----------
    var s: chain
    var one: item
    one.key = 42
    one.seq = 0
    s.append(&one)
    s.sort(by_key_asc)
    var sf: item& = s.first(): item&
    var sl: item& = s.last(): item&
    ::printf("single key=%d first==last=%d rear_next_nil=%d\n", sf->key, sf == sl, sf->next == nil)

    # ---------- 边界：已有序再排（幂等）----------
    dump("sorted2 ", &l)
    l.sort(by_key_asc)
    dump("re-asc  ", &l)
    return 0
