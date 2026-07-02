# 特性 34：C 宏桥接（def name::）—— 把「由 C 实现的宏」映射进 sc
#
# 与 fnc name:: / let name:: 同理：尾缀 :: 表示「实现在 C 侧」。
#   def NAME:: p1, ... 声明一个由 C 头提供 #define 的宏（sc 不再生成 #define），
#   其缩进体只允许 fnc:: 与 let:: / var:: 映射声明——即该 C 宏展开时所「拼装」
#   出来的定义对象（函数原型 + 全局符号）。
#
# mix NAME(args) 原样输出宏调用，由 C 预处理器（已 #include 真实 #define）展开；
# sc 凭映射体登记的符号，对调用点与后续引用做名字 / 类型检查。
#
# 适用范围：def:: 专用于「展开后产生声明」的定义型宏（assemble 出函数 / 全局）。
#   表达式 / 语句位置的「求值型」C 宏（展开为一个值 / 一次调用）不是定义对象，
#   不属于本机制（头已 #include 后可直接写名调用）。

inc "feature34_cmac.h"          # 提供定义型 C 宏 DEFINE_COUNTER

# 映射「定义型」C 宏：每次实例化拼装出 counter_<N> 全局 + inc/get 两个函数
def DEFINE_COUNTER:: N
    let counter_\N:: i4
    fnc counter_\N\_inc::
    fnc counter_\N\_get:: i4

# 实例化定义型 C 宏：展开点就地产生 static 计数器与函数
mix DEFINE_COUNTER(hits)
mix DEFINE_COUNTER(miss)

fnc main: i4
    counter_hits_inc()
    counter_hits_inc()
    counter_hits_inc()
    counter_miss_inc()
    ::printf("hits=%d miss=%d\n", counter_hits_get(), counter_miss_get())
    ::printf("counter_hits(direct)=%d\n", counter_hits)
    return 0
