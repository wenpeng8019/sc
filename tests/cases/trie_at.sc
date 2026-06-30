# 前缀树 trie 回归用例：覆盖
#   1. 字符串键 → 裸 @ 映射：put/has/get/len，replace 替换 release 旧值；
#   2. 前缀能力：has_prefix/count_prefix（O(prefix)）/each_prefix（字典序自动补全）/longest_prefix；
#   3. 遍历回调天然字典序（首子/次兄有序链），ctx 透传计数；
#   4. 空串键边界（任意串的前缀，longest_prefix 返 0）；
#   5. remove 剪枝空节点但保留共享前缀（删 card 不伤 car/cart）；clear/drop；
#   6. value 拥有（容器无需知 T），触零经句柄自带 dtor 自析构。
# 用 --check=ref 运行可验证每 value retain/release 守恒（无悬挂、无泄漏）。
inc adt.sc

@def node: {
    v: i4
    drop: fnc
        printf("drop %d\n", this->v)
}

@fnc make: node@, x: i4
    var p: node@ = node()
    p->v = x
    return p

# 遍历回调：按字典序打印键
@fnc print_key: bool, key: const char&, value: *, ctx: &
    printf(" %s", key)
    return true

# 遍历回调：打印 key=val，并经 ctx(i8&) 累加键数
@fnc dump_kv: bool, key: const char&, value: *, ctx: &
    var cnt: i8& = (ctx: i8&)
    cnt[0] += 1
    printf(" %s=%d", key, (value: node&)->v)
    return true

@fnc main: i4
    var tt: trie                                     # 无参 init → 声明即构造

    # ---------- A：基本 put/has/get/len + replace ----------
    var ha[6]: node@                                 # holder：持有 value root 引用至退域
    ha[0] = make(10)
    tt.put("cat", (ha[0]: *))
    ha[1] = make(20)
    tt.put("car", (ha[1]: *))
    ha[2] = make(30)
    tt.put("card", (ha[2]: *))
    ha[3] = make(40)
    tt.put("dog", (ha[3]: *))
    ha[4] = make(50)
    tt.put("do", (ha[4]: *))
    ha[5] = make(60)
    tt.put("cart", (ha[5]: *))
    printf("A len=%llu\n", tt.len())
    printf("A has: car=%d ca=%d dog=%d xyz=%d\n",
           tt.has("car"), tt.has("ca"), tt.has("dog"), tt.has("xyz"))
    printf("A get card=%d\n", (tt.get("card"): node&)->v)

    # replace：car 改挂新 value 99（容器 release 旧 20、retain 新；len 不变）
    var hrep: node@ = make(99)
    tt.put("car", (hrep: *))
    printf("A replace car=%d len=%llu\n", (tt.get("car"): node&)->v, tt.len())

    # ---------- B：前缀查询 / 字典序遍历 / 最长前缀 ----------
    printf("B has_prefix: ca=%d car=%d xy=%d\n",
           tt.has_prefix("ca"), tt.has_prefix("car"), tt.has_prefix("xy"))
    printf("B count: ca=%llu car=%llu all=%llu\n",
           tt.count_prefix("ca"), tt.count_prefix("car"), tt.count_prefix(""))
    printf("B each_ca:")
    tt.each_prefix("ca", print_key, nil)             # car card cart cat
    printf("\n")
    printf("B each_all:")
    tt.each(print_key, nil)                          # car card cart cat do dog
    printf("\n")
    printf("B each_car_kv:")
    var total: i8 = 0
    tt.each_prefix("car", dump_kv, (&total: &))       # car=99 card=30 cart=60
    printf(" (n=%lld)\n", total)
    printf("B longest: cartoon=%lld dog=%lld do=%lld xyz=%lld\n",
           tt.longest_prefix("cartoon"), tt.longest_prefix("dog"),
           tt.longest_prefix("do"), tt.longest_prefix("xyz"))

    # ---------- C：空串键 / remove 剪枝 / clear / drop ----------
    var hempty: node@ = make(70)
    tt.put("", (hempty: *))                          # 空串键：任意串的前缀
    printf("C empty: has=%d get=%d all=%llu lp_zzz=%lld\n",
           tt.has(""), (tt.get(""): node&)->v, tt.count_prefix(""), tt.longest_prefix("zzz"))

    # remove + 剪枝：删 card 不伤共享前缀的 car/cart
    printf("C remove card=%d again=%d miss=%d\n",
           tt.remove("card"), tt.remove("card"), tt.remove("nope"))
    printf("C after_remove: has_card=%d has_car=%d has_cart=%d count_car=%llu len=%llu\n",
           tt.has("card"), tt.has("car"), tt.has("cart"), tt.count_prefix("car"), tt.len())

    tt.clear()
    printf("C after_clear len=%llu has_car=%d\n", tt.len(), tt.has("car"))
    tt.drop()
    # main 退域：holder 逆序释放 value root → 集中析构

    return 0
