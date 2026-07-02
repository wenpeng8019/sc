# 全局自动指针 T@（标量 + 一维/多维数组）回归用例：覆盖文件作用域声明零初始化、
# 函数内下标/标量赋值带头分配与绑根边、以及进程退出经 SC_DESTRUCTOR(__sc_gfat_fini)
# 钩子逐元素 / 逐个 sc_fat_unbind_d（带 drop 释放系统资源）。仅做产物快照（--emit-c / --emit-sc）。
@def node: {
    v: i4
    drop: fnc
        ::printf("drop v=%d\n", this->v)
}

var g: node@                          # 全局标量 T@：static sc_fat g = {0}
var arr[3]: node@                     # 全局一维数组 T@：static sc_fat arr[3] = {0}
var grid[2][2]: node@                 # 全局多维数组 T@：static sc_fat grid[2][2] = {0}

@fnc main: i4
    g = node()                        # 标量赋值：带头分配 + 绑根边
    g->v = 1
    var i: i4 = 0
    for i = 0; i < 3; i++
        arr[i] = node()               # 一维下标赋值：带头分配 + 绑根边
        arr[i]->v = i * 10
    grid[0][0] = node()               # 多维下标赋值
    grid[0][0]->v = 99
    ::printf("%d %d %d\n", g->v, arr[2]->v, grid[0][0]->v)
    return 0                          # 退出：__sc_gfat_fini 逐个/逐元素拆边触发 drop
