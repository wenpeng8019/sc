# 特性 24：自动指针 T@（引用图 / ARC 自动释放 / 取址传染 / 返回移动）
#
# T@ 与裸指针 T& 完全独立：T& 8 字节纯 C 语义、不追踪；
# T@ 24 字节，参与引用图记账（in=入边/out=出边），in==0 且 out==0 时 ARC 自动 free。
#   - T()           带头堆分配，绑根边（域退出自动拆边）
#   - a@ = b        绑定新边（共享，目标 in++）
#   - a@ = nil      拆边（归零则自动 free）
#   - return p      移动（所有权转给调用方，跳过退域拆边）
#   - &obj->field   取址传染：对值子成员取址，结果仍是 T@，借用容器堆对象延寿

@def point: {
    x: i4
    y: i4
}

@def node: {
    v: i4
    pt: point
    child: node@
}

# 返回 T@：分配并初始化，所有权移交调用方（移动语义）
@fnc make: node@, val: i4
    var n: node@ = node()
    n->v = val
    return n

@fnc main: i4
    var root: node@ = make(1)        # 接收移动来的堆对象（in=1）
    root->pt.x = 10
    root->pt.y = 20
    root->child = node()             # 成员出边：root.out++，子对象 in++
    root->child->v = 2
    ::printf("root.v=%d child.v=%d\n", root->v, root->child->v)

    var alias: node@ = root          # 绑定新边：root 目标 in=2（共享）
    ::printf("alias.v=%d\n", alias->v)

    var px: point@ = &root->pt       # 取址传染：借用 root 堆对象，px 指向内嵌 pt
    ::printf("px.x=%d px.y=%d\n", px->x, px->y)

    root->child = nil                # 拆成员出边：子对象 in--（归零 → ARC free）
    ::printf("after-detach root.v=%d\n", root->v)
    return 0                         # 域退出：先拆 px/alias/root 的边，再 ARC 回收
