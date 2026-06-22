# 结构化宏：对象宏 / 函数宏 / mix 展开 / \ 粘贴 / ` 串化 / 可变参
inc stdio.h

# 对象宏 → #define TAG 7
def TAG: = 7

# 函数宏 + \ 粘贴：用前缀拼接出一对全局名
def decl_pair: pfx
    var pfx\_lo: i4 = 0
    var pfx\_hi: i4 = 1

# 函数宏 + ` 串化：打印变量名与值
def show: x
    printf("%s = %d\n", `x`, x)

# 可变参函数宏
def sumprint: fmt, ...
    printf(fmt, __VA_ARGS__)

# 顶层展开：生成 g_lo / g_hi 两个全局
mix decl_pair(g)

fnc main: i4
    var count: i4 = TAG
    mix show(count)
    mix sumprint("sum=%d\n", count)
    return count
