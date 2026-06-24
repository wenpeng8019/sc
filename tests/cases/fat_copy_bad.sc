# 内嵌自动指针 T@ 的按值聚合不可整体拷贝：拷贝会把胖指针字段当裸字节复制，
# 绕过 in/out 记账（重复解绑 / 悬垂）→ 编译期报错（预期失败用例）。
@def node: {
    v: i4
    child: node@
}

@def holder: {
    p: node@
}

@fnc main: i4
    var h: holder
    h.p = node()
    var g: holder = h        # 拷贝构造内嵌 T@ 的按值聚合 → 报错
    return 0
