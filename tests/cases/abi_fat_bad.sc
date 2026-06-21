# 含自动指针 T@ 成员的结构体按值跨 C ABI：导出函数 take 以值参接收 node
# （node.child 为 T@），C 侧无法维护胖指针 ARC → 编译期报错（预期失败用例）。
@def node: {
    v: i4
    child: node@
}

@fnc take: i4, b: node
    return b.v

@fnc main: i4
    return 0
