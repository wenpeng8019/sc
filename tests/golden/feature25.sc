# 由 scc --emit-sc 从 AST 再生成

@fnc pick: i4, n: i4
    final
        printf("  [pick] final 执行\n")
    if n > 0
        printf("  [pick] early return\n")
        return 1
    printf("  [pick] 正常 return\n")
    return 0

@fnc lifo: i4
    final
        printf("  [lifo] 先注册（后执行）\n")
    final
        printf("  [lifo] 后注册（先执行）\n")
    return 0

@fnc loopy: i4, n: i4
    var i: i4 = 0
    for i = 0; i < n; i++
        final
            printf("  [loopy] iter %d 清理\n", i)
        if i == 1
            continue
        if i == 2
            break
    return 0

@fnc main: i4
    printf("== 多退出点 ==\n")
    pick(1)
    pick(0)
    printf("== LIFO 顺序 ==\n")
    lifo()
    printf("== 循环内 final ==\n")
    loopy(4)
    return 0
