# C 桥接映射 :: 专项用例 —— sc↔C 互通
#   let/var X:: T 认领 C 侧已定义的全局符号（emit extern T X）
#   C 函数/宏直接用名调用（libc 白名单或未解析头 → 放宽未定义检查）

# 认领 C 全局符号（不分配存储、无初值）
let MAX_VIEW:: i4
var g_tick:: i8

fnc demo: i4
    # 直接调用 C 变参函数
    ::printf("v=%d\n", 1)
    # 调用 C 函数，结果赋给 sc 变量
    var a: i4 = ::abs(-3)
    # 读取已认领的 C 全局符号
    var b: i4 = MAX_VIEW
    # 认领的可变 C 全局可读写
    g_tick = g_tick + 1
    return a + b
