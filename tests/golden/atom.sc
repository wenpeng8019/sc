# 由 scc --emit-sc 从 AST 再生成

@def node: {
    v: i4
    child: node@
}

@fnc main: i4
    var root: node@ = node<atom>()
    root->v = 1
    root->child = node<atom>()
    root->child->v = 2
    var alias: node@ = root
    var plain: node@ = node()
    root->child = nil
    return 0
