# 自动指针 T@ 原子计数 T<atom>() 回归用例：覆盖原子目标构造 / 根绑定 /
# 成员出边 / 别名绑定 / nil 拆边。原子节点的引用计数用 __atomic_* 增减，
# 非原子节点走普通 ++/--。仅做产物快照（--emit-c / --emit-sc）。
@def node: {
    v: i4
    child: node@
}

@fnc main: i4
    var root: node@ = node<atom>()   # 原子目标：头 flags=SC_REF_ATOM
    root->v = 1
    root->child = node<atom>()       # 原子成员出边
    root->child->v = 2

    var alias: node@ = root          # 原子绑定：__atomic_fetch_add

    var plain: node@ = node()        # 普通目标：flags=0，走 ++/--

    root->child = nil                # 拆成员出边
    return 0
