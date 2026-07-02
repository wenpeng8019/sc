# 特性 25：final 作用域退出钩子（类 defer，但绑定作用域而非函数）
#
# final 注册一段在“当前作用域退出时”执行的清理代码，覆盖所有退出路径
# （正常落出 / return / break / continue）；多个 final 按后进先出（LIFO）执行。
#   - 注册即生效：控制流到达 final 才登记，更早的退出点不触发
#   - LIFO：后注册先执行，契合资源获取/释放的栈式配对
#   - 与 T@ 协作：final 块体先于退域自动拆边执行，块内访问的自动指针仍有效

# 多退出点：final 在 early return 与正常 return 处都执行
@fnc pick: i4, n: i4
    final
        ::printf("  [pick] final 执行\n")
    if n > 0
        ::printf("  [pick] early return\n")
        return 1
    ::printf("  [pick] 正常 return\n")
    return 0

# LIFO：两个 final 逆序执行（后注册先跑）
@fnc lifo: i4
    final
        ::printf("  [lifo] 先注册（后执行）\n")
    final
        ::printf("  [lifo] 后注册（先执行）\n")
    return 0

# 循环内 final：break/continue/正常落出三条路径均触发
@fnc loopy: i4, n: i4
    var i: i4 = 0
    for i = 0; i < n; i++
        final
            ::printf("  [loopy] iter %d 清理\n", i)
        if i == 1
            continue
        if i == 2
            break
    return 0

@fnc main: i4
    ::printf("== 多退出点 ==\n")
    pick(1)
    pick(0)
    ::printf("== LIFO 顺序 ==\n")
    lifo()
    ::printf("== 循环内 final ==\n")
    loopy(4)
    return 0
