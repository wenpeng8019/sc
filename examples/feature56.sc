# feature56：set 变更检测 + modified 强制刷新（对齐 c_prototype 的 C_input / C_modified）。
#
# 语义（与 c_prototype 一致）：
#   · set 仅在「新值 ≠ 原值」时才落值并触发依赖级联——值未变则静默丢弃，follow 不再跑
#     （C_input：`if (C_equal(newVal, oldVal)) return;`）。
#       - 直赋型 token（无 combine 体）：新值即输入值。
#       - form 候选（有 combine 体）：新值为 combine 合成结果（如取峰：set 较小值→合成不变→抑制）。
#   · tok_modified()：combine 体内 `return tok_modified()` 返回 modified 哨兵——
#     即便合成结果与原值「相等」也强制传播（C_equal 对 modified 恒返回 false）。
#
# 本例用一个「计数 token」记录 follow 实际被触发的次数：follow 体仅在 active>=0（运行期或门
# 变更）时计数，忽略 form 时的就绪事件（active=TOK_ANY_READY=-1），从而干净地度量「变更传播」。

# ---- 直赋型信号：无 combine 体，新值即输入 ----
tok sig: "cd.sig"

# ---- form 候选：取峰 combine；额外约定 input==0 表示「强制刷新」→ 返回 modified ----
tok gauge: "cd.gauge"
    var i: i8 = (this->input: i8)
    if i == 0
        return tok_modified()                # 强制刷新：值视为已变更，即便合成不变也传播
    var b: i8 = (this->base: i8)
    if i > b
        return (i: @)                        # 取较大者
    return (b: @)                            # 不大于当前峰值 → 合成不变 → 触发变更检测抑制

# ---- 计数 token（直赋型）：被各 follow 自增，度量传播次数 ----
tok hits: "cd.hits"
tok ghits: "cd.ghits"

# ---- sig 变更 → hits 自增（仅运行期变更，忽略就绪事件）----
dep any: s:"cd.sig"
    if this->active >= 0
        var h: i8 = (hits->get(): i8)
        hits->set(((h + 1): @), 0)
    return false

# ---- gauge 变更 → ghits 自增 ----
dep any: g:"cd.gauge"
    if this->active >= 0
        var c: i8 = (ghits->get(): i8)
        ghits->set(((c + 1): @), 0)
    return false

fnc main: i4
    form sig, (0: @)
    form gauge, (0: @)
    form hits, (0: @)
    form ghits, (0: @)

    # ---- 直赋型变更检测：相同值连发只传播一次 ----
    sig->set((5: @), 0)                      # 0→5 变更 → hits=1
    sig->set((5: @), 0)                      # 5→5 不变 → 抑制 → hits=1
    sig->set((5: @), 0)                      # 5→5 不变 → 抑制 → hits=1
    var h1: i8 = (hits->get(): i8)
    printf("直赋 set(5) x3:   hits=%lld (期望 1)\n", h1)

    sig->set((7: @), 0)                      # 5→7 变更 → hits=2
    var h2: i8 = (hits->get(): i8)
    printf("直赋 set(7):      hits=%lld (期望 2)\n", h2)

    # ---- combine 变更检测：取峰，较小值合成不变 → 抑制 ----
    gauge->set((10: @), 0)                   # max(0,10)=10 变更 → ghits=1
    gauge->set((3: @), 0)                    # max(10,3)=10 不变 → 抑制 → ghits=1
    var g1: i8 = (ghits->get(): i8)
    printf("combine 取峰抑制:  ghits=%lld (期望 1)\n", g1)

    # ---- modified 强制刷新：合成「不变」也传播 ----
    gauge->set((0: @), 0)                    # input==0 → combine 返回 modified → 强制传播 → ghits=2
    var g2: i8 = (ghits->get(): i8)
    printf("modified 强制刷新: ghits=%lld (期望 2)\n", g2)

    return 0
