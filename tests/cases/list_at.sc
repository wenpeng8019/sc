# 段式裸 @ 自动指针容器 list 回归用例：覆盖
#   1. push 取一份 retain（目标 in++）；声明即构造 / 退域 drop 自动 release 全部元素；
#   2. get 借用（返回句柄不改计数）→ (x: T@) 还原绑定 / (x: T&) 裸强转读取；
#   3. set 改写（retain 新、release 旧）、insert/remove_at 段内搬移、index_of 按 .p 比对；
#   4. reverse 原地反转、sort 按 cmp(元素 .p) 排序（句柄裸搬移不改计数）；
#   5. pop / clear release 元素，触零经句柄自带 dtor 自析构（容器无需知具体类型 T）。
# 用 --check=ref 运行可验证每元素 retain/release 守恒（退域无悬挂、无泄漏）。
inc adt.sc

@def node: {
    v: i4
    drop: fnc
        printf("drop %d\n", this->v)
}

@fnc node_cmp -> list_cmp
    return (a: node&)->v - (b: node&)->v       # cmp 收元素 .p 实体基址

@fnc main: i4
    var l: list

    var a: node@ = node()
    a->v = 10
    var b: node@ = node()
    b->v = 30
    var c: node@ = node()
    c->v = 20

    l.push((a: @))                              # 三次 push：各取一份 retain
    l.push((b: @))
    l.push((c: @))
    printf("len=%llu\n", l.len())

    # 借用 + 还原读取
    var g: node@ = (l.get(1): node@)
    printf("get1=%d\n", g->v)
    printf("get2_raw=%d\n", (l.get(2): node&)->v)   # 借用 + 裸强转读取

    # index_of：按 .p 实体基址查找
    printf("idx_b=%lld idx_a=%lld\n", l.index_of((b: @)), l.index_of((a: @)))

    # sort：升序 10,20,30
    l.sort(node_cmp)
    var i: u8 = 0
    for i = 0; i < l.len(); i++
        printf("sorted[%llu]=%d\n", i, (l.get(i): node&)->v)

    # reverse：30,20,10
    l.reverse()
    printf("rev0=%d rev2=%d\n", (l.get(0): node&)->v, (l.get(2): node&)->v)

    # insert 一个新元素到头部，remove_at 删除尾部
    var d: node@ = node()
    d->v = 99
    l.insert(0, (d: @))                         # retain d
    printf("after_insert len=%llu head=%d\n", l.len(), (l.get(0): node&)->v)
    l.remove_at(l.len() - 1)                    # release 尾元素
    printf("after_remove len=%llu\n", l.len())

    # set：改写 index 1（release 旧、retain 新）
    l.set(1, (a: @))
    printf("set1=%d\n", (l.get(1): node&)->v)

    # pop：release 尾元素
    l.pop()
    printf("after_pop len=%llu\n", l.len())

    # clear：release 全部剩余元素，保留段容量
    l.clear()
    printf("after_clear len=%llu\n", l.len())

    return 0                                    # 退域：l.drop（已空）+ a/b/c/d 逐根归零自释放
