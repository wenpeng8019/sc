# cbridge_demo —— :: sc↔C 映射符演示（可直接运行）
#
#   let/var X:: T 认领 C 侧已定义的全局符号（转 C 为 extern T X）
#   C 函数/宏直接用名调用（无需在 sc 声明）
#
# 运行： scc examples/cbridge_demo.sc

inc stdio.h

# 把实现 C 全局符号的源文件加入工程（编译并链接）。
add cbridge_demo_globals.c

# 认领 C 侧已定义的全局（不分配存储、无初值）。
let SC_BUILD_ID:: i4         # extern int32_t SC_BUILD_ID;   —— 只读视图
var sc_call_count:: i8       # extern int64_t sc_call_count; —— 可读写

fnc tick: i4
    sc_call_count = sc_call_count + 1     # 写入认领的可变 C 全局
    return abs(-7)                        # 直接调用 C 函数

fnc main: i4
    # 直接调用未在 sc 声明的 C 变参函数。
    printf("build=%d\n", SC_BUILD_ID)     # 读取认领的 C 全局
    var a: i4 = tick()
    var b: i4 = tick()
    printf("a=%d b=%d calls=%lld\n", a, b, sc_call_count)
    return 0
