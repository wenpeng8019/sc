# 特性 35：自动指针 T@ 的析构函数（drop）—— 归零先析构、递归清子成员
#
# 在特性 24（引用图 / ARC 自动释放）之上：当 T@ 堆对象入边归零（in→0）时，
# 若其类型定义了 drop，则「先触发析构、再回收」：
#   1. in→0  → 调用该类型的 drop（析构函数），由它清理自身持有的子成员出边；
#   2. 析构后 out==0 → ARC 释放整块；out!=0（漏清子成员）→ 报「未清理」并不释放。
#
# 这让链式/树形结构能在根对象归零时，沿 drop 递归地把整条链清理干净：
#   根 in→0 → 根.drop 清 child → child in→0 → child.drop 清孙 → …… 逐层回收。
#
# 析构体内可经 this（裸接收者）读胖成员、与 nil 比较、以及 `this->m = nil` 解绑
# （解绑用成员自带的 own/tar 记账，无需重算持有者，故对任意容器都安全）；
# 但不可经裸 base 给胖成员绑「新边」（须经 T@ base），见 §7.4。

@def node: {
    v: i4
    child: node@

    # 析构函数：清理本节点持有的子成员出边（子节点归零会再递归触发其 drop）
    drop: fnc
        if this->child != nil
            printf("drop v=%d -> 释放子节点 v=%d\n", this->v, this->child->v)
            this->child = nil            # 解绑出边：子对象 in--（归零则递归析构 + 回收）
        else
            printf("drop v=%d（叶子）\n", this->v)
}

@fnc main: i4
    var root: node@ = node()             # 带头堆分配，根边
    root->v = 1
    root->child = node()                 # 成员出边：root.out++，子 in++
    root->child->v = 2
    root->child->child = node()          # 孙节点
    root->child->child->v = 3
    printf("构建链 1 -> 2 -> 3，即将退出作用域\n")
    return 0                             # 退域：root 拆边 in→0 → 沿 drop 递归清理整条链
