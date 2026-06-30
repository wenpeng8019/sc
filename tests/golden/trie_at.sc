# 由 scc --emit-sc 从 AST 再生成

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

@fnc print_key: bool, key: const char&, value:*, ctx: &
    printf(" %s", key)
    return true

@fnc dump_kv: bool, key: const char&, value:*, ctx: &
    var cnt: i8& = (ctx: i8&)
    cnt[0] += 1
    printf(" %s=%d", key, (value: node&)->v)
    return true

@fnc main: i4
    var tt: trie
    var ha[6]: node@
    ha[0] = make(10)
    tt.put("cat", (ha[0]: @))
    ha[1] = make(20)
    tt.put("car", (ha[1]: @))
    ha[2] = make(30)
    tt.put("card", (ha[2]: @))
    ha[3] = make(40)
    tt.put("dog", (ha[3]: @))
    ha[4] = make(50)
    tt.put("do", (ha[4]: @))
    ha[5] = make(60)
    tt.put("cart", (ha[5]: @))
    printf("A len=%llu\n", tt.len())
    printf("A has: car=%d ca=%d dog=%d xyz=%d\n", tt.has("car"), tt.has("ca"), tt.has("dog"), tt.has("xyz"))
    printf("A get card=%d\n", (tt.get("card"): node&)->v)
    var hrep: node@ = make(99)
    tt.put("car", (hrep: @))
    printf("A replace car=%d len=%llu\n", (tt.get("car"): node&)->v, tt.len())
    printf("B has_prefix: ca=%d car=%d xy=%d\n", tt.has_prefix("ca"), tt.has_prefix("car"), tt.has_prefix("xy"))
    printf("B count: ca=%llu car=%llu all=%llu\n", tt.count_prefix("ca"), tt.count_prefix("car"), tt.count_prefix(""))
    printf("B each_ca:")
    tt.each_prefix("ca", print_key, nil)
    printf("\n")
    printf("B each_all:")
    tt.each(print_key, nil)
    printf("\n")
    printf("B each_car_kv:")
    var total: i8 = 0
    tt.each_prefix("car", dump_kv, (&total: &))
    printf(" (n=%lld)\n", total)
    printf("B longest: cartoon=%lld dog=%lld do=%lld xyz=%lld\n", tt.longest_prefix("cartoon"), tt.longest_prefix("dog"), tt.longest_prefix("do"), tt.longest_prefix("xyz"))
    var hempty: node@ = make(70)
    tt.put("", (hempty: @))
    printf("C empty: has=%d get=%d all=%llu lp_zzz=%lld\n", tt.has(""), (tt.get(""): node&)->v, tt.count_prefix(""), tt.longest_prefix("zzz"))
    printf("C remove card=%d again=%d miss=%d\n", tt.remove("card"), tt.remove("card"), tt.remove("nope"))
    printf("C after_remove: has_card=%d has_car=%d has_cart=%d count_car=%llu len=%llu\n", tt.has("card"), tt.has("car"), tt.has("cart"), tt.count_prefix("car"), tt.len())
    tt.clear()
    printf("C after_clear len=%llu has_car=%d\n", tt.len(), tt.has("car"))
    tt.drop()
    return 0
