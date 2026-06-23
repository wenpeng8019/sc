# 特性 32：用结构化宏实现泛型 / 模板
#
# 宏体支持完整的 sc 语法（var/let/tls/fnc/嵌套 def/控制流…），且宏形参是
# 纯文本记号。于是 `def 域: T, N` 中的形参既可当「类型」也可当「名字」：
#   - T 传入一个类型 → 宏体里凡用到 T 的地方都被替换为该类型
#   - N 传入一个后缀 → 配合 `\` 粘贴为每次实例化造出独立的类型/函数名
# 这就等价于 C++ 模板 / 泛型：一份「定义域」代码，按类型实例化出多份具体实现。
#
# 类型实参规则（重要）：
#   - 用户 def 的类型、C 类型名（int32_t/double/char&…）可直接做类型实参
#     （它们在 C 里就是同名标识符）
#   - sc 基本类型别名 i4/f8/u1… 不会被翻译（宏体是文本透传），不能直接做实参；
#     需要时传 C 名（int32_t/double…）或先 def 一个别名
#   - 宏体内 sc 不知道形参类型，故指针成员访问要显式写 `->`（值访问写 `.`）

# ---- 泛型函数：在任意可比较类型 T 上求较大者 ----
def def_max: T, N
    fnc max_\N: T, a: T, b: T
        if a > b
            return a
        return b

mix def_max(int32_t, i)          # → int32_t max_i(int32_t, int32_t)
mix def_max(double, d)           # → double  max_d(double, double)

# ---- 泛型容器：定长向量 Vec<T>，实例化出独立的结构体 + 方法族 ----
def def_vec: T, N
    def Vec_\N: {                # 实例化出具体结构体类型 Vec_<N>
        data: T&
        len: i4
        cap: i4
    }
    fnc Vec_\N\_push: v: Vec_\N&, x: T
        v->data[v->len] = x      # v 是指针形参：宏体内显式写 ->
        v->len = v->len + 1
    fnc Vec_\N\_get: T, v: Vec_\N&, i: i4
        return v->data[i]

mix def_vec(int32_t, int)        # → struct Vec_int + Vec_int_push/get
mix def_vec(double, dbl)         # → struct Vec_dbl + Vec_dbl_push/get

# ---- 用户自定义类型也能做类型实参（C 里同名）----
def Point: {
    x: i4
    y: i4
}
mix def_vec(Point, pt)           # → struct Vec_pt 装 Point

fnc main: i4
    printf("max_i(3,7)=%d  max_d(1.5,0.5)=%g\n", max_i(3, 7), max_d(1.5, 0.5))

    var ibuf[4]: i4
    var iv: Vec_int = { ibuf, 0, 4 }
    Vec_int_push(&iv, 10)
    Vec_int_push(&iv, 20)
    printf("Vec_int: %d %d\n", Vec_int_get(&iv, 0), Vec_int_get(&iv, 1))

    var dbuf[4]: f8
    var dv: Vec_dbl = { dbuf, 0, 4 }
    Vec_dbl_push(&dv, 1.5)
    printf("Vec_dbl: %g\n", Vec_dbl_get(&dv, 0))

    var pbuf[4]: Point
    var pv: Vec_pt = { pbuf, 0, 4 }
    var p0: Point = { 3, 4 }
    Vec_pt_push(&pv, p0)
    var p: Point = Vec_pt_get(&pv, 0)
    printf("Vec_pt: %d %d\n", p.x, p.y)
    return 0
