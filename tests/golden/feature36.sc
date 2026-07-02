# 由 scc --emit-sc 从 AST 再生成

@def node: {
    v: i4
    left: node@
    right: node@
    drop: fnc
        if this->left != nil
            ::printf("drop v=%d -> 释放左子 v=%d\n", this->v, this->left->v)
            this->left = nil
        if this->right != nil
            ::printf("drop v=%d -> 释放右子 v=%d\n", this->v, this->right->v)
            this->right = nil
}

@def tree: {
    root: node@
    init: fnc
        this->root = node()
        this->root->v = 1
        this->root->left = node()
        this->root->left->v = 2
        this->root->right = node()
        this->root->right->v = 3
    drop: fnc
        if this->root != nil
            ::printf("drop tree -> 释放根 v=%d\n", this->root->v)
            this->root = nil
}

@fnc main: i4
    var t: tree@ = tree()
    ::printf("树已由 init 建好：root=%d left=%d right=%d\n", t->root->v, t->root->left->v, t->root->right->v)
    ::printf("即将退出作用域\n")
    return 0
