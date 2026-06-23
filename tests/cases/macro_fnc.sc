# 宏声明函数：宏体内 fnc + tls，顶层 mix 展开后符号自动登记（无需手写认领）

# 计数器工厂宏：每次展开生成一个独立计数器（tls 状态 + 自增函数）
def make_counter: nm
    tls cnt_\nm: i4 = 0
    fnc bump_\nm: i4
        cnt_\nm = cnt_\nm + 1
        return cnt_\nm

# 顶层展开：生成 cnt_a/bump_a 与 cnt_b/bump_b，符号经语义层自动登记
mix make_counter(a)
mix make_counter(b)

fnc main: i4
    # 直接引用宏生成的函数：bump_a / bump_b 已登记，无需 let:: 认领
    printf("%d %d %d\n", bump_a(), bump_a(), bump_b())
    return 0
