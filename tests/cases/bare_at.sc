# 裸自动指针 @（类型擦除）回归用例：覆盖
#   1. 声明 var x: @（sc_afat 零初始化）与 = nil 解绑；
#   2. 构造即擦除 var d: @ = T()（dtor 取构造类型 T 的析构，随句柄存）；
#   3. 从 T@ 擦除 var e: @ = t（借边，t 目标 in++）；
#   4. 裸 @ 之间重绑 f = d（dtor 随源句柄）；
#   5. 擦除强转 (t: @) 作 @ 形参实参；恢复强转 (p: T@) 调方法/读成员；
#   6. 退域逐根 sc_afat_unbind（dtor 随句柄析构，无需静态类型）。
# 仅做产物快照（--emit-c / --emit-sc）。
@def node: {
    v: i4
    bump: fnc
        this->v = this->v + 1
    drop: fnc
        printf("drop %d\n", this->v)
}

# 形参为裸 @：恢复为 node@ 后调方法 / 读成员
@fnc take: i4, p: @
    (p: node@)->bump()
    return (p: node@)->v

@fnc main: i4
    var d: @ = node()                 # 构造即擦除：sc_afat_bind(dtor=node_drop)
    (d: node@)->v = 7                 # 恢复后写成员

    var t: node@ = node()             # 具体 T@
    t->v = 40
    var e: @ = t                      # 从 T@ 擦除（借边，t 目标 in++）
    var r: i4 = take((t: @))          # 擦除强转作实参
    printf("d=%d e=%d r=%d\n", (d: node@)->v, (e: node@)->v, r)

    var f: @ = nil                    # 裸 @ 声明 + nil
    f = d                             # 裸 @ 之间重绑（dtor 随 d 句柄）
    printf("f=%d\n", (f: node@)->v)
    return 0                          # 退域：f/e/d 逐根 sc_afat_unbind，t 拆边
