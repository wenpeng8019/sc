# final 域退出钩子回归用例：覆盖正常落出 / 早退 return / 循环 break/continue /
# 与 T@ 胖指针交互（final 先于自动拆边）。仅做产物快照（--emit-c / --emit-sc）。

@def node: {
    v: i4
    child: node@
}

# 多退出点：final 在 early return 与正常 return 处各执行一次
@fnc pick: i4, n: i4
    final
        ::printf("final A\n")
    if n > 0
        return 1
    return 0

# 循环内 final：break/continue/正常落出三条路径均执行
@fnc loopy: i4, n: i4
    var i: i4 = 0
    for i = 0; i < n; i++
        final
            ::printf("iter %d\n", i)
        if i == 1
            continue
        if i == 2
            break
    return 0

# 与胖指针交互：final 先于退域 unbind，访问的指针仍有效
@fnc withfat: i4
    var p: node@ = node()
    final
        ::printf("v=%d\n", p->v)
    p->v = 9
    return 0
