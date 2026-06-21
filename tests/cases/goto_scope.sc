# goto 跨域清理回归：回跳标签时拆解被跳过作用域内的自动指针 T@，
# 避免重入标签体时旧对象泄漏。仅做产物快照（--emit-c / --emit-sc）。
@def node: {
    v: i4
}

@fnc main: i4
    var i: i4 = 0
    loop:
        var n: node@ = node()        # 每次重入分配
        n->v = i
        i = i + 1
        if i < 3
            goto loop                # 回跳：先拆 n（避免泄漏），再重入重新分配
    return 0
