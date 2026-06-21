# 自动指针 T@ 回归用例：覆盖带头分配 / 根绑定 / 成员出边 / & 取址传染 /
# nil 拆边 / 返回移动。仅做产物快照（--emit-c / --emit-sc）。
@def node: {
    v: i4
    child: node@
}

# 返回 T@：分配并初始化一个节点（所有权移交调用方）
@fnc make: node@, val: i4
    var n: node@ = node()
    n->v = val
    return n

@fnc main: i4
    var root: node@ = make(1)        # 接收移动来的堆对象
    root->child = node()             # 成员出边：root.out++，子对象 in++
    root->child->v = 2

    var alias: node@ = root          # 绑定新边，root 目标 in=2

    var sub: node@ = &root->child    # & 取址传染：借用 child 指向的堆对象

    root->child = nil                # 拆成员出边
    return 0
