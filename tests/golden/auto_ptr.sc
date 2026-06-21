# 由 scc --emit-sc 从 AST 再生成

@def node: {
    v: i4
    child: node@
}

@fnc make: node@, val: i4
    var n: node@ = node()
    n->v = val
    return n

@fnc main: i4
    var root: node@ = make(1)
    root->child = node()
    root->child->v = 2
    var alias: node@ = root
    var sub: node@ = &root->child
    root->child = nil
    return 0
