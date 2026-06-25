# 开放寻址裸 @ 自动指针映射 dict 回归用例：覆盖
#   1. 三态 key_size——数值定长(>0)、引用字符串(0,借用)、拷贝字符串(-1,自持)；
#   2. put 取一份 retain（目标 in++）、替换 release 旧 retain 新、remove/clear/drop 释放；
#   3. get 借用（返回句柄不改计数）→ (x: T&) 裸强转读取；has 命中判定；
#   4. each 无序回调遍历 + first/next/last/prev 整数游标双向遍历（同桶序）；
#   5. 触零经句柄自带 dtor 自析构（容器无需知具体类型 T）。
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

@fnc sum_each -> dict_each_fn                     # 无序回调：累加 value，验证遍历到全部
    var a: acc& = (ctx: acc&)
    a->sum += (value: node&)->v
    a->cnt += 1
    return true

@fnc main: i4
    # ---------- 模式 A：数值定长 key（key_size = 4，i4）----------
    var da: dict
    da.init(4)

    var a1: node@ = node()
    a1->v = 100
    var a2: node@ = node()
    a2->v = 200
    var a3: node@ = node()
    a3->v = 300

    var k1: i4 = 11
    var k2: i4 = 22
    var k3: i4 = 33
    da.put((&k1: const &), (a1: @))              # 三次 put：各取一份 retain
    da.put((&k2: const &), (a2: @))
    da.put((&k3: const &), (a3: @))
    printf("A len=%llu\n", da.len())
    printf("A has22=%d has99=%d\n", da.has((&k2: const &)), da.has((&k3: const &)))
    var kx: i4 = 99
    printf("A miss=%d\n", da.has((&kx: const &)))
    printf("A get33=%d\n", (da.get((&k3: const &)): node&)->v)

    # 替换：key 22 改指向新 value（release 旧 200、retain 新 250）
    var a2b: node@ = node()
    a2b->v = 250
    da.put((&k2: const &), (a2b: @))             # 旧 200 触零 → drop 200
    printf("A put22b=%d len=%llu\n", (da.get((&k2: const &)): node&)->v, da.len())

    # each 无序遍历：sum 应为 100+250+300=650，cnt=3
    var sa: acc
    sa.sum = 0
    sa.cnt = 0
    da.each(sum_each, (&sa: &))
    printf("A each sum=%d cnt=%d\n", sa.sum, sa.cnt)

    # 游标双向遍历：正反向 sum/cnt 一致
    var fs: i4 = 0
    var fc: i4 = 0
    var i: i8 = da.first()
    while i >= 0
        fs += (da.value_at(i): node&)->v
        fc += 1
        i = da.next(i)
    var bs: i4 = 0
    var bc: i4 = 0
    i = da.last()
    while i >= 0
        bs += (da.value_at(i): node&)->v
        bc += 1
        i = da.prev(i)
    printf("A fwd sum=%d cnt=%d  bwd sum=%d cnt=%d\n", fs, fc, bs, bc)

    # remove：删除 key 11（release 100 → drop 100），未命中返回 false
    printf("A rm11=%d rm99=%d len=%llu\n",
           da.remove((&k1: const &)), da.remove((&kx: const &)), da.len())

    da.drop()                                     # 释放剩余 250、300 → drop（桶序）
    printf("A after_drop len=%llu\n", da.len())

    # ---------- 模式 B：引用字符串 key（key_size = 0，借用）----------
    var db: dict
    db.init(0)
    var b1: node@ = node()
    b1->v = 10
    var b2: node@ = node()
    b2->v = 20
    db.put("alpha", (b1: @))
    db.put("beta", (b2: @))
    printf("B len=%llu get_beta=%d has_alpha=%d has_x=%d\n",
           db.len(), (db.get("beta"): node&)->v, db.has("alpha"), db.has("zzz"))
    db.remove("alpha")                            # drop 10
    printf("B after_rm len=%llu\n", db.len())
    db.drop()                                     # drop 20

    # ---------- 模式 C：拷贝字符串 key（key_size = -1，自持）----------
    var dc: dict
    dc.init(0 - 1)
    var c1: node@ = node()
    c1->v = 70
    var buf[8]: char
    buf[0] = 'k'
    buf[1] = 'e'
    buf[2] = 'y'
    buf[3] = 0
    dc.put((&buf[0]: const &), (c1: @))           # dict 拷贝 "key" 一份
    buf[0] = 'X'                                  # 篡改原缓冲——拷贝键不受影响
    printf("C has_key=%d get_key=%d\n",
           dc.has((&buf[0]: const &)), dc.has(("key": const &)))
    printf("C lookup=%d\n", (dc.get(("key": const &)): node&)->v)
    dc.drop()                                     # 回收键拷贝 + drop 70

    return 0                                       # 退域：a1/a3/a2b/b1/b2/c1 各自余根归零自释放
