# AVL/红黑 融合有序映射 bst 回归用例：覆盖
#   1. red_depth 双模式——AVL(0) 与红黑(1) 同一套接口；
#   2. key 三态——数值定长(>0) 默认按宽度有符号比较、拷贝字符串(-1) strcmp；
#   3. put 取一份 retain（目标 in++）、替换 release 旧 retain 新、remove/clear/drop 释放；
#      （value 句柄存于 holder 数组，root 引用持有至 main 退域，故析构集中在末尾）
#   4. get 借用（返回句柄不改计数）→ (x: T&) 裸强转读取；has 命中判定；
#   5. each 中序（升序）回调 + first/next/last/prev 游标双向遍历（升序/降序）；
#   6. index_of <-> at 序号双向换算；most(<=)/least(>=) 最接近项；
#   7. 大量乱序/顺序插入触发旋转与变色后，中序仍严格升序（树平衡正确性）；
#   8. 触零经句柄自带 dtor 自析构（容器无需知具体类型 T）。
# 用 --check=ref 运行可验证每 value retain/release 守恒（无悬挂、无泄漏）。
inc adt.sc

@def node: {
    v: i4
    drop: fnc
        printf("drop %d\n", this->v)
}

@def acc: {
    sum: i4
    cnt: i4
}

@fnc sum_each -> bst_each_fn                      # 中序回调：累加 value，验证遍历到全部且升序
    var a: acc& = (ctx: acc&)
    a->sum += (value: node&)->v
    a->cnt += 1
    return true

# 中序升序打印 key 序列（游标正向）
@fnc dump_keys: i4, t: bst&
    var c: i8 = t->first()
    var n: i4 = 0
    while c != 0
        var kp: i4& = (t->key_at(c): i4&)
        printf(" %d", kp[0])
        n += 1
        c = t->next(c)
    printf("\n")
    return n

@fnc make: node@, x: i4
    var p: node@ = node()
    p->v = x
    return p

@fnc main: i4
    # ---------- A：AVL 模式（red_depth=0），数值 key（i4）----------
    var ta: bst
    ta.init(0, 4, nil, nil)

    # 乱序插入 7 个 key（值 = key*10），触发 AVL 旋转
    var ha[8]: node@                                 # holder：持有 value root 引用至退域
    var keys[7]: i4
    keys[0] = 50
    keys[1] = 30
    keys[2] = 70
    keys[3] = 20
    keys[4] = 40
    keys[5] = 60
    keys[6] = 80
    var i: i4 = 0
    while i < 7
        ha[i] = make(keys[i] * 10)
        ta.put((&keys[i]: const &), (ha[i]: *))
        i += 1
    printf("A len=%llu\n", ta.len())
    printf("A inorder:")
    dump_keys(&ta)

    # has / get / miss
    var k40: i4 = 40
    var k99: i4 = 99
    printf("A has40=%d miss99=%d get40=%d\n",
           ta.has((&k40: const &)), ta.has((&k99: const &)),
           (ta.get((&k40: const &)): node&)->v)

    # 中序 each：sum=10*(20+30+40+50+60+70+80)=3500，cnt=7
    var sa: acc
    sa.sum = 0
    sa.cnt = 0
    ta.each(sum_each, (&sa: &))
    printf("A each sum=%d cnt=%d\n", sa.sum, sa.cnt)

    # 双向游标：正反一致
    var fs: i4 = 0
    var c: i8 = ta.first()
    while c != 0
        fs += (ta.value_at(c): node&)->v
        c = ta.next(c)
    var bs: i4 = 0
    c = ta.last()
    while c != 0
        bs += (ta.value_at(c): node&)->v
        c = ta.prev(c)
    printf("A fwd_sum=%d bwd_sum=%d\n", fs, bs)

    # index_of <-> at 双向换算（key 40 应是第 2 序号；序号 0 应是 key 20）
    printf("A idx40=%lld\n", ta.index_of((&k40: const &)))
    var c0: i8 = ta.at(0)
    var c0k: i4& = (ta.key_at(c0): i4&)
    printf("A at0_key=%d at0_val=%d\n", c0k[0], (ta.value_at(c0): node&)->v)

    # most(<=45) 应得 40；least(>=45) 应得 50
    var k45: i4 = 45
    var cm: i8 = ta.most((&k45: const &))
    var cl: i8 = ta.least((&k45: const &))
    var cmk: i4& = (ta.key_at(cm): i4&)
    var clk: i4& = (ta.key_at(cl): i4&)
    printf("A most45=%d least45=%d\n", cmk[0], clk[0])

    # 替换 key 40 的 value（release 旧 400、retain 新 999；旧 400 由 ha[4] 持有至退域）
    ha[7] = make(999)
    ta.put((&k40: const &), (ha[7]: *))
    printf("A put40b=%d len=%llu\n", (ta.get((&k40: const &)): node&)->v, ta.len())

    # 删除（触发删除旋转）：删 50（双子根）、20（叶）、70
    var k50: i4 = 50
    var k20: i4 = 20
    var k70: i4 = 70
    printf("A rm50=%d rm20=%d rm70=%d rmmiss=%d len=%llu\n",
           ta.remove((&k50: const &)), ta.remove((&k20: const &)),
           ta.remove((&k70: const &)), ta.remove((&k99: const &)), ta.len())
    printf("A inorder2:")
    dump_keys(&ta)                                   # release bst 对 500/200/700 的 retain
    ta.drop()                                        # release 剩余（300/999/600/800）的 retain
    printf("A after_drop len=%llu\n", ta.len())

    # ---------- B：红黑 模式（red_depth=1），顺序插入 1..15 ----------
    # 升序插入是朴素 BST 的最坏退化，红黑须保持平衡——中序必为 1..15
    var tb: bst
    tb.init(1, 4, nil, nil)
    var hb[15]: node@                                # holder
    var bk[15]: i4
    i = 0
    while i < 15
        bk[i] = i + 1
        hb[i] = make(101 + i)                        # value 101..115（与 A/C 不冲突）
        tb.put((&bk[i]: const &), (hb[i]: *))
        i += 1
    printf("B len=%llu\n", tb.len())
    printf("B inorder:")
    dump_keys(&tb)
    # at(7) 应是第 8 个 → key 8
    var b8: i8 = tb.at(7)
    var b8k: i4& = (tb.key_at(b8): i4&)
    printf("B at7_key=%d idx_key10=%lld\n", b8k[0], tb.index_of((&bk[9]: const &)))

    # 删除所有偶数 key（触发红黑删除变色/旋转），中序留奇数
    i = 1
    while i < 15
        tb.remove((&bk[i]: const &))                 # bk[1]=2, bk[3]=4 ... 偶数
        i += 2
    printf("B after_rm_even len=%llu inorder:", tb.len())
    dump_keys(&tb)
    tb.drop()

    # ---------- C：拷贝字符串 key（key_size=-1，strcmp 升序）----------
    var tc: bst
    tc.init(0, 0 - 1, nil, nil)
    var hc[4]: node@                                 # holder
    hc[0] = make(4)
    tc.put("delta", (hc[0]: *))
    hc[1] = make(1)
    tc.put("alpha", (hc[1]: *))
    hc[2] = make(3)
    tc.put("charlie", (hc[2]: *))
    hc[3] = make(2)
    tc.put("bravo", (hc[3]: *))
    printf("C len=%llu inorder:", tc.len())
    var cc: i8 = tc.first()
    while cc != 0
        printf(" %s=%d", (tc.key_at(cc): char&), (tc.value_at(cc): node&)->v)
        cc = tc.next(cc)
    printf("\n")
    printf("C get_charlie=%d has_x=%d\n",
           (tc.get(("charlie": const &)): node&)->v, tc.has(("zzz": const &)))
    tc.remove(("alpha": const &))                    # release bst 对 1 的 retain
    printf("C after_rm len=%llu\n", tc.len())
    tc.drop()                                        # release 剩余 4/3/2 的 retain
    # main 退域：holder 数组逆序释放 value root → 集中析构

    return 0
