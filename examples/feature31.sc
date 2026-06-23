# 特性 31：结构化宏 def / mix（一一映射 C #define）
#
# def NAME: = value         对象宏   → #define NAME value
# def name: p... + 缩进体    函数宏   → #define name(p...) \ <body 各句以 \ 续行>
#     末参 ...   可变参                → __VA_ARGS__
#     粘贴 \     中缀                  → ## （拼接标识符）
#     串化 `name`                      → #  （把参数名转成字符串字面量）
# mix name(args)            展开宏：顶层展开声明 / 函数体内展开语句
#
# 宏体声明的存储类（var/let/fnc）：
#     var/let X      模块私有 → static T X;（默认；不污染其它单元）
#     @var/@let X    导出     → extern T X; T X;（外部链接，供其它单元引用）
# 宏展开生成的符号经语义层自动登记，下游可直接引用，无需手写 let X:: 认领。

# ---- 对象宏：直接当常量标识符使用 ----
def CAP: = 4

# ---- 函数宏 + ` 串化：打印「变量名 = 值」 ----
def dump: x
    printf("  %s = %d\n", `x`, x)

# ---- 函数宏 + \ 粘贴：自带计数器的局部，体内自增并打印 ----
def tally: tag
    var tag\_n: i4 = 0
    tag\_n = tag\_n + CAP
    printf("  %s_n = %d\n", `tag`, tag\_n)

# ---- 可变参函数宏：实参透传到 printf ----
def logf: fmt, ...
    printf(fmt, __VA_ARGS__)

# ---- 顶层 mix 展开出一对全局：@var 导出 + var 模块私有（static） ----
# 符号由语义层自动登记，main 中可直接引用（无需手写 let :: 认领）。
def gpair: pfx
    @var pfx\_lo: i4 = 10
    var pfx\_hi: i4 = 20

mix gpair(cfg)

fnc main: i4
    var count: i4 = CAP
    printf("object macro: CAP=%d\n", CAP)

    printf("stringify:\n")
    mix dump(count)

    printf("paste:\n")
    mix tally(item)

    printf("variadic + macro globals:\n")
    mix logf("  sum=%d range=[%d,%d]\n", count + cfg_lo, cfg_lo, cfg_hi)

    return 0
