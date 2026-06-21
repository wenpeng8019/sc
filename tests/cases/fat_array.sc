# 自动指针数组 T@（局部一维）回归用例：覆盖声明零初始化 / 下标赋值带头分配 /
# 元素成员出边 / 元素借用绑定 / 退域逐元素清理。仅做产物快照（--emit-c / --emit-sc）。
@def node: {
    v: i4
    child: node@
}

@fnc main: i4
    var arr[3]: node@                 # 声明：sc_fat[3] 零初始化
    var i: i4 = 0
    for i = 0; i < 3; i++
        arr[i] = node()               # 下标赋值：带头分配 + 绑根边
        arr[i]->v = i * 10            # 元素成员写
    arr[0]->child = node()            # 元素的成员出边
    arr[0]->child->v = 99

    var pick: node@ = arr[1]          # 元素借用绑定（arr[1] 目标 in++）
    printf("%d %d %d\n", arr[0]->v, pick->v, arr[0]->child->v)
    arr[0]->child = nil               # 显式拆元素成员出边（避免释放时悬出边）
    return 0                          # 退域：pick 拆边 + arr 逐元素 unbind
