# 由 scc --emit-sc 从 AST 再生成

@def point: {
    x: i4
    y: i4
}

@def node: {
    v: i4
    pt: point
    child: node@
}

@fnc make: node@, val: i4
    var n: node@ = node()
    n->v = val
    return n

@fnc main: i4
    var root: node@ = make(1)
    root->pt.x = 10
    root->pt.y = 20
    root->child = node()
    root->child->v = 2
    printf("root.v=%d child.v=%d\n", root->v, root->child->v)
    var alias: node@ = root
    printf("alias.v=%d\n", alias->v)
    var px: point@ = &root->pt
    printf("px.x=%d px.y=%d\n", px->x, px->y)
    root->child = nil
    printf("after-detach root.v=%d\n", root->v)
    return 0
