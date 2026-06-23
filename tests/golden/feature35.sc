# 由 scc --emit-sc 从 AST 再生成

@def node: {
    v: i4
    child: node@
    drop: fnc
        if this->child != nil
            printf("drop v=%d -> 释放子节点 v=%d\n", this->v, this->child->v)
            this->child = nil
        else
            printf("drop v=%d（叶子）\n", this->v)
}

@fnc main: i4
    var root: node@ = node()
    root->v = 1
    root->child = node()
    root->child->v = 2
    root->child->child = node()
    root->child->child->v = 3
    printf("构建链 1 -> 2 -> 3，即将退出作用域\n")
    return 0
