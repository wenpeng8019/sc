# 特性 36：自动指针 T@ 的构造函数（init）—— 经 this 首次构造子成员
#
# 在特性 35（drop 递归析构）之上：构造函数 init 体内可经 this 给胖成员绑「新边」，
# 即 `this->child = T()` —— 在对象诞生时就把子结构建好。
#
# 关键在于「容器自身是否被记账」决定子成员如何创建（§7.4）：
#   1. base = T()：base 赋给自动指针/胖左值 → T 的实例带头存在（ref 版本，有 out 边）；
#   2. 此 ref 上下文经隐藏形参 _self_own 传入 init —— 它就是容器自身的 out 指针；
#   3. init 内 `this->child = T()` 据 _self_own 判定：
#        · 真实出边（SC_OWN_REAL）→ 带头堆分配子对象并记账（owner.out++、child.in++），
#          子对象纳入引用图，随 owner 归零沿 drop 递归回收；
#        · 否则（SC_OWN_RAW：栈值 / 裸堆 / 静态等无头容器）→ 退化为「裸创建」，
#          子对象无头、不追踪，生命周期同 C 手动管理（不参与 ARC）。
#
# 本例用 ref 版本：root.init 在诞生时构造 left/right 两棵子树，退域时根归零，
# 沿 drop 递归把 init 建出来的整棵树清理干净 —— init 建、drop 清，对称闭环。

@def node: {
    v: i4
    left: node@
    right: node@

    drop: fnc
        if this->left != nil
            printf("drop v=%d -> 释放左子 v=%d\n", this->v, this->left->v)
            this->left = nil
        if this->right != nil
            printf("drop v=%d -> 释放右子 v=%d\n", this->v, this->right->v)
            this->right = nil
}

@def tree: {
    root: node@

    # 构造函数：诞生时经 this 把整棵小树建好（ref 容器 → 子对象均被记账）
    init: fnc
        this->root = node()              # this->root：tree.out++，node.in++
        this->root->v = 1
        this->root->left = node()        # 子树挂到 root 出边
        this->root->left->v = 2
        this->root->right = node()
        this->root->right->v = 3

    drop: fnc
        if this->root != nil
            printf("drop tree -> 释放根 v=%d\n", this->root->v)
            this->root = nil             # 根归零 → 沿 node.drop 递归清左右子
}

@fnc main: i4
    var t: tree@ = tree()                # ref 版本：t.init 构造出 1 ->(2,3) 整棵树
    printf("树已由 init 建好：root=%d left=%d right=%d\n",
        t->root->v, t->root->left->v, t->root->right->v)
    printf("即将退出作用域\n")
    return 0                             # 退域：t 归零 → tree.drop → root 归零 → 递归清子
