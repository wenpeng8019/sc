# feature33 —— 语言级泛型宏（单态化模板）
#
# def name: <T,...>, N  —— 尖括号内为「类型参数」，编译器对每个 mix 实例
#   克隆宏体、把类型参数替换为具体类型、重新解析为「真实声明」并参与语义检查，
#   最终生成具体的 C 结构体/函数（而非文本 #define）。
#
# 与文本宏的关键区别：
#   - 类型参数可直接用 sc 原生类型名（i4 / f8 / 用户结构体），类型映射与校验照常生效；
#   - 每个实例是独立的具体类型，享受完整类型检查，不依赖 C 预处理器。
# N 为「名参数」：纯文本，用 \ 粘贴拼接出各实例的具体名字（Vec_int / Vec_dbl ...）。

# 用户结构体——稍后作为类型实参传入泛型容器
def Point: {
    x: i4
    y: i4
}

# 泛型容器：T=元素类型，N=实例名后缀
def Vec: <T>, N
    def Vec_\N: {
        data[8]: T
        len: i4
    }
    fnc Vec_\N\_push: v: Vec_\N&, x: T
        v->data[v->len] = x
        v->len = v->len + 1
    fnc Vec_\N\_get: T, v: Vec_\N&, i: i4
        return v->data[i]

# 泛型函数：取较大者
def max_of: <T>, N
    fnc max_\N: T, a: T, b: T
        if a > b
            return a
        return b

# 在不同类型上实例化——每个 mix 生成一组具体声明
mix Vec(i4, int)
mix Vec(f8, dbl)
mix Vec(Point, pt)
mix max_of(i4, i)
mix max_of(f8, d)

fnc main: i4
    var vi: Vec_int
    vi.len = 0
    Vec_int_push(&vi, 10)
    Vec_int_push(&vi, 20)
    Vec_int_push(&vi, 30)
    ::printf("Vec_int: %d %d %d\n", Vec_int_get(&vi, 0), Vec_int_get(&vi, 1), Vec_int_get(&vi, 2))

    var vd: Vec_dbl
    vd.len = 0
    Vec_dbl_push(&vd, 1.5)
    Vec_dbl_push(&vd, 2.5)
    ::printf("Vec_dbl: %g %g\n", Vec_dbl_get(&vd, 0), Vec_dbl_get(&vd, 1))

    var vp: Vec_pt
    vp.len = 0
    var p0: Point = {3, 4}
    Vec_pt_push(&vp, p0)
    var got: Point = Vec_pt_get(&vp, 0)
    ::printf("Vec_pt[0]: (%d, %d)\n", got.x, got.y)

    ::printf("max_i(3,7)=%d  max_d(1.5,0.5)=%g\n", max_i(3, 7), max_d(1.5, 0.5))
    return 0
