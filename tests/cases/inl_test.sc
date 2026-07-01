# inl 真内联块单元测试：函数体内命名内联块，调用点原地展开（共享外层栈帧）。
#   · void inl：当语句用，裸 return 早退本块；
#   · 值 inl（带返回类型）：仅作 lhs=name() / var|let x=name() 右侧，
#     每个 return v → lhs=v; goto 尾标签。
#   · 形参在展开处求值一次（绑定一次），不可递归。
# 运行：scc tests/cases/inl_test.sc --test

# --- void inl：共享外层帧 + 裸 return 早退 ---
fnc classify: i4, n: i4
    var acc: i4 = 0
    inl step:
        if n < 0
            acc = -1
            return
        acc = n * 2
    step()
    return acc

# --- 值 inl：带返回类型，多 return 早退 ---
fnc doubler: i4, x: i4
    inl dbl: i4, v: i4
        if v == 0
            return 100
        return v * 2
    var a: i4 = 0
    a = dbl(5)
    let b: i4 = dbl(0)
    return a + b

# --- 值 inl：赋值到成员/下标等左值 ---
fnc sum_first_two: i4, base: i4
    inl inc: i4, d: i4
        return base + d
    var arr[2]: i4
    arr[0] = inc(1)
    arr[1] = inc(2)
    return arr[0] + arr[1]

# --- 形参绑定一次：实参含副作用仅求值一次 ---
var g_calls: i4 = 0

fnc next_id: i4
    g_calls = g_calls + 1
    return 10

fnc bind_once: i4
    inl useit: i4, v: i4
        return v + v
    var r: i4 = 0
    r = useit(next_id())
    # next_id 仅被调用一次 → g_calls==1，r == 10+10
    return r + g_calls

# --- 值 inl 作子表达式：嵌套在更大表达式/实参/初始化里（自动临时提升） ---
fnc add2: i4, a: i4, b: i4
    return a + b

fnc subexpr: i4
    inl dbl: i4, v: i4
        return v * 2
    var r: i4 = 0
    r = 1 + dbl(5)                  # 赋值右侧嵌套 → 11
    var y: i4 = 2 + dbl(3)          # 初始化嵌套   → 8
    var z: i4 = add2(dbl(2), dbl(3))   # 普通函数实参 → 4 + 6 = 10
    var w: i4 = dbl(dbl(2))            # inl 实参嵌套 inl → dbl(4) = 8
    return r + y + z + w               # 11 + 8 + 10 + 8 = 37

# --- 值 inl 作 return 子表达式 + 独立语句丢弃返回值 ---
fnc subexpr_ret: i4, x: i4
    inl dbl: i4, v: i4
        return v * 2
    dbl(x)                          # 独立语句：丢弃返回值（合法）
    return 1 + dbl(x)               # return 子表达式 → 1 + 2x

tst "void inl：共享帧 + 裸 return 早退"
    assert classify(4) == 8, "classify(4)"
    assert classify(-1) == -1, "classify(-1)"

tst "值 inl：多 return 早退，赋值/初始化两种上下文"
    assert doubler(5) == 110, "doubler(5) = 2*5 + 100"

tst "值 inl：赋值到下标左值"
    assert sum_first_two(10) == 23, "sum_first_two(10) = 11 + 12"

tst "形参绑定一次：实参只求值一次"
    assert bind_once() == 21, "bind_once = 20 + 1"

tst "值 inl 作子表达式：赋值/初始化/实参/嵌套 inl"
    assert subexpr() == 37, "subexpr = 11+8+10+8"

tst "值 inl 作 return 子表达式 + 丢弃"
    assert subexpr_ret(10) == 21, "subexpr_ret(10) = 1 + 20"

tst "assert 实参内嵌 inl 调用"
    inl trip: i4, v: i4
        return v * 3
    assert trip(4) == 12, "trip(4)"
