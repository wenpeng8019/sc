# goto 跨非包含作用域跳转：目标标签所在作用域已退出，且活动链上仍有待清理的
# 自动指针 T@（outer），无法安全清理被跳过对象 → 编译期报错（预期失败用例）。
@def node: {
    v: i4
}

@fnc main: i4
    var outer: node@ = node()
    if 1
        a:
            var n: node@ = node()
            n->v = 1
    goto a
    return 0
