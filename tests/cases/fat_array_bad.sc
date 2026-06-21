# 负向用例：非局部位置的 T@ 数组仍未实现 → 编译期报错。
# 这里用结构体字段 T@ 数组（字段位置的引用图清理/记账未实现）。
@def bag: {
    items[4]: node@
}

@def node: {
    v: i4
}

@fnc main: i4
    return 0
