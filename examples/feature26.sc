# 特性 26：for-in 遍历语法（for name[: T&] in 集合 [选项...]）
# > 统一的集合遍历糖，转 C 后即普通 for/while 循环，零额外开销。
#
# 集合分三类：
#   ① 值序列：范围 [a, b]（闭区间，含 b）/ [a, b)（半开，不含 b）；整数 n（== [0, n)）
#   ② 索引序列：静态数组（编译期已知长度）；字符串（char& / char[]，按 '\0' 终止）
#   ③ 链式序列：双向链 chain；ADT 容器（def T: <C, I>），经 first/next 游标遍历
#
# 循环变量类型：
#   - 默认按集合元素类型推断：范围/整数→i4；数组→元素类型；字符串→char；
#     链→void&（须显式下转）；容器→注入节点 I&（须显式 : T& 下转回元素）。
#   - 可显式注解 name: T& 覆盖（链/容器常用，把游标下转回元素指针）。
#
# 尾随选项（任意顺序，各至多一次）：
#   revert        逆序遍历（范围/数组从尾向头；链/容器用 last/prev，容器须实现 last/prev）
#   step <expr>   每次前进 expr 步（默认 1）
#   offset <expr> 起点跳过 expr 个元素（默认 0）
#   num <expr>    最多遍历 expr 个元素（默认无上限）

inc stdio.h
inc adt.sc

#-------------- ADT 容器（def T: <C, I>，见特性 7）-------------
def tnode: {
    next: tnode&
    prev: tnode&
}
def slist: {
    head: tnode&
    tail: tnode&
    init: fnc
        this->head = nil
        this->tail = nil
    insert: fnc: ret, item: tnode&, tag: i4
        item->prev = this->tail
        item->next = nil
        if this->tail != nil
            this->tail->next = item
        else
            this->head = item
        this->tail = item
        return ok
    remove: fnc: ret, item: tnode&
        return ok
    find: fnc: ret, out: tnode&&, key: i4
        var p: tnode& = this->head
        while p != nil
            var t: task& = p: task&
            if t->id != key
                p = p->next
            else
                *out = p
                return ok
        return -1
    first: fnc: tnode&
        return this->head
    last: fnc: tnode&
        return this->tail
    next: fnc: tnode&, item: tnode&
        return item->next
    prev: fnc: tnode&, item: tnode&
        return item->prev
}
def task: <slist, tnode> {
    id: i4
}

#-------------- 双向链元素（def T: ~，见特性 6）----------------
def cnode: ~ {
    v: i4
}

fnc main: i4

    #========== ① 值序列：范围 / 整数计数 ==========
    printf("闭区间[1,5]:")
    for i in [1, 5]
        printf(" %d", i)                 # 1 2 3 4 5
    printf("\n")

    printf("半开[1,5):")
    for i in [1, 5)
        printf(" %d", i)                 # 1 2 3 4
    printf("\n")

    printf("整数 4:")
    for i in 4                           # 等价 [0, 4)
        printf(" %d", i)                 # 0 1 2 3
    printf("\n")

    printf("范围逆序:")
    for i in [1, 5] revert
        printf(" %d", i)                 # 5 4 3 2 1
    printf("\n")

    printf("范围步进2:")
    for i in [0, 10] step 2
        printf(" %d", i)                 # 0 2 4 6 8 10
    printf("\n")

    #========== ② 索引序列：数组 / 字符串 ==========
    var a[5]: i4
    var k: i4
    for k = 0; k < 5; k++
        a[k] = (k + 1) * 11
    printf("数组:")
    for x in a
        printf(" %d", x)                 # 11 22 33 44 55
    printf("\n")

    printf("数组逆序跳1:")
    for x in a revert offset 1           # 跳过末元素，从倒数第二个起逆序
        printf(" %d", x)                 # 44 33 22 11
    printf("\n")

    var s: char& = "hello"
    printf("字符串:")
    for c in s
        printf(" %c", c)                 # h e l l o
    printf("\n")

    printf("字符串前3:")
    for c in s num 3
        printf(" %c", c)                 # h e l
    printf("\n")

    #========== ③ 链式序列：chain / 容器 ==========
    var l: chain
    var cn[4]: cnode
    for k = 0; k < 4; k++
        cn[k].v = (k + 1) * 100
        l.append(&cn[k])
    printf("链:")
    for it: cnode& in l                  # 默认 void&，显式下转 cnode&
        printf(" %d", it->v)             # 100 200 300 400
    printf("\n")

    printf("链逆序:")
    for it: cnode& in l revert           # last/prev（边界安全）
        printf(" %d", it->v)             # 400 300 200 100
    printf("\n")

    var lst: slist                       # 自动 init
    var t[4]: task
    for k = 0; k < 4; k++
        t[k].id = (k + 1) * 10
        lst.insert(&t[k], 0)
    printf("容器:")
    for it: task& in lst                 # 注入节点 tnode&，: task& 下转回元素
        printf(" %d", it->id)            # 10 20 30 40
    printf("\n")

    printf("容器逆序步2:")
    for it: task& in lst revert step 2
        printf(" %d", it->id)            # 40 20
    printf("\n")

    #========== ④ 索引/坐标变量：for v, i[, j...] in 集合 ==========
    # v 之后逗号列出索引变量，数量须与集合维度一致（一维 0/1 个，N 维数组 N 个）。
    # 取值：可索引集合（数组/标量/范围，可得 count）→ 真实下标，revert 时倒序，v==coll[i] 恒等；
    #       仅 next 迭代集合（字符串/链/容器）→ 0,1,2... 递增计数（与 revert/offset 无关）。
    printf("数组带下标:")
    for x, i in a
        printf(" a[%d]=%d", i, x)        # a[0]=11 ... a[4]=55
    printf("\n")

    printf("数组逆序带下标:")
    for x, i in a revert                 # 下标随元素倒序：a[4] a[3] ...（v==a[i] 恒等）
        printf(" a[%d]=%d", i, x)        # a[4]=55 a[3]=44 ... a[0]=11
    printf("\n")

    printf("链带计数:")
    for it: cnode&, i in l               # 链：仅 next 迭代 → 0,1,2... 计数
        printf(" #%d=%d", i, it->v)      # #0=100 #1=200 #2=300 #3=400
    printf("\n")

    #========== ⑤ 多维数组：N 维 → N 个坐标变量，嵌套遍历全部标量 ==========
    var m[2][3]: i4
    var r: i4
    var c: i4
    for r = 0; r < 2; r++
        for c = 0; c < 3; c++
            m[r][c] = r * 10 + c
    printf("二维:")
    for v, i, j in m                     # v=标量元素，i/j=两维下标
        printf(" m[%d][%d]=%d", i, j, v) # m[0][0]=0 ... m[1][2]=12
    printf("\n")

    printf("二维逆序:")
    for v, i, j in m revert              # 各维均倒序
        printf(" m[%d][%d]=%d", i, j, v) # m[1][2]=12 ... m[0][0]=0
    printf("\n")

    return 0
