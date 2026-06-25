# for-in 两协议遍历 adt 容器回归用例：
#   1. 映射协议 for k, v in（first/next 游标 + key_at/value_at）：dict / bst / lru；
#      - k=键（默认 const void&，注解 k: i4& 下转）、v=值（借用 @，(v: T&) 读取）；
#      - 终止判据=value_at 空句柄，与游标哨兵无关（dict -1 / bst·lru 0 通用）；
#      - bst 升序、lru MRU→LRU、revert 逆序、num 截断；遍历期间借用不改计数。
#   2. 序列索引协议 for v in（len + 整型 get/at）：list（@ 借用元素）/ array（元素指针）；
#      - 可选 , i 索引；revert 逆序；num 截断。
# 用 --check=ref 运行可验证遍历全程借用守恒（无多余 retain/release、无泄漏）。
inc adt.sc

@def node: {
    v: i4
    drop: fnc
        printf("drop %d\n", this->v)
}

@fnc main: i4
    # ---------- 映射协议：dict（数值 key，累加值——桶序无关）----------
    var d: dict
    d.init(4)
    var d1: node@ = node()
    d1->v = 10
    var d2: node@ = node()
    d2->v = 20
    var d3: node@ = node()
    d3->v = 30
    var k1: i4 = 1
    var k2: i4 = 2
    var k3: i4 = 3
    d.put((&k1: const &), (d1: @))
    d.put((&k2: const &), (d2: @))
    d.put((&k3: const &), (d3: @))
    var dsum: i4 = 0
    var dksum: i4 = 0
    for k, v in d
        dksum += (k: i4&)[0]
        dsum += (v: node&)->v
    printf("dict ksum=%d vsum=%d\n", dksum, dsum)

    # ---------- 映射协议：bst（升序 + revert 逆序 + num 截断）----------
    var t: bst
    t.init(0, 4, nil, nil)
    var t1: node@ = node()
    t1->v = 100
    var t2: node@ = node()
    t2->v = 200
    var t3: node@ = node()
    t3->v = 300
    var t4: node@ = node()
    t4->v = 400
    var b1: i4 = 5
    var b2: i4 = 15
    var b3: i4 = 25
    var b4: i4 = 35
    t.put((&b3: const &), (t3: @))
    t.put((&b1: const &), (t1: @))
    t.put((&b4: const &), (t4: @))
    t.put((&b2: const &), (t2: @))
    printf("bst asc:")
    for k: i4&, v in t
        printf(" %d=%d", k[0], (v: node&)->v)
    printf("\n")
    printf("bst desc:")
    for k: i4&, v in t revert
        printf(" %d", k[0])
    printf("\n")
    printf("bst first2:")
    for k: i4&, v in t num 2
        printf(" %d", k[0])
    printf("\n")

    # ---------- 映射协议：lru（MRU→LRU 顺序）----------
    var lc: lru
    lc.init(4, 0)
    var c1: node@ = node()
    c1->v = 11
    var c2: node@ = node()
    c2->v = 22
    var c3: node@ = node()
    c3->v = 33
    var m1: i4 = 7
    var m2: i4 = 8
    var m3: i4 = 9
    lc.put((&m1: const &), (c1: @))
    lc.put((&m2: const &), (c2: @))
    lc.put((&m3: const &), (c3: @))         # MRU=9, then 8, 7=LRU
    printf("lru mru->lru:")
    for k: i4&, v in lc
        printf(" %d=%d", k[0], (v: node&)->v)
    printf("\n")

    # ---------- 序列索引协议：list（@ 借用元素，可选索引、revert）----------
    var ls: list
    ls.init()
    var e1: node@ = node()
    e1->v = 1000
    var e2: node@ = node()
    e2->v = 2000
    var e3: node@ = node()
    e3->v = 3000
    ls.push((e1: @))
    ls.push((e2: @))
    ls.push((e3: @))
    printf("list fwd:")
    for v, i in ls
        printf(" [%d]=%d", i, (v: node&)->v)
    printf("\n")
    printf("list rev:")
    for v in ls revert
        printf(" %d", (v: node&)->v)
    printf("\n")

    # ---------- 序列索引协议：array（元素指针，i4 值块）----------
    var ar: array
    ar.init(4)
    var x0: i4 = 41
    var x1: i4 = 42
    var x2: i4 = 43
    ar.push((&x0: &))
    ar.push((&x1: &))
    ar.push((&x2: &))
    var asum: i4 = 0
    for v in ar
        asum += (v: i4&)[0]
    printf("array sum=%d\n", asum)

    # 析构：容器持有的 value retain 在 drop 时释放（dict/bst/lru/list）；array 为值块无 retain
    d.drop()
    t.drop()
    lc.drop()
    ls.drop()
    ar.drop()
    return 0
