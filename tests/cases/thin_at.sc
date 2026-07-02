# 瘦自动指针 T*（真瘦 24B {p,tar,dtor}）回归用例：覆盖
#   1. 声明 var t: node*（sc_thin 零初始化）与构造即绑 = node()（dtor 随句柄）；
#   2. 从 T@ 借边 var t: node* = a（仅 tar 入边记账，无 own 出边统计）；
#   3. 瘦 → 胖 var c: node@ = t（拷 p/tar，胖侧补 own）；瘦 → 裸 @ 互转；
#   4. 形参为 node*：读成员 / 调方法（前 16B 同构，p 直取）；
#   5. = nil 解绑、退域逐根 sc_thin_unbind（dtor 随句柄析构，无 own 记账）。
# 仅做产物快照（--emit-c / --emit-sc）。
@def node: {
    v: i4
    bump: fnc
        this->v = this->v + 1
    drop: fnc
        ::printf("drop %d\n", this->v)
}

@fnc take: i4, p: node*
    p->bump()
    return p->v

@fnc main: i4
    var a: node@ = node()             # 具体胖根
    a->v = 7
    var t: node* = a                 # 借边：仅 a 目标 in++（无 own）
    var c: node@ = t                  # 瘦 → 胖：拷 p/tar + 补 own
    var b: node* = node()            # 构造即瘦绑：sc_thin_bind(dtor=node_drop)
    b->v = 40
    var e: @ = b                      # 瘦 → 裸 @ 互转
    var r: i4 = take(b)
    ::printf("a=%d c=%d r=%d\n", a->v, c->v, r)
    t = nil                           # 瘦解绑（a 目标 in--）
    return 0                          # 退域：c/a/b 逐根拆边触发 drop
