# add .sc 回归：被 add 的独立子单元。
# 全为默认（未导出/静态）成员——被 add 后应对容器单元可见。
# 自带 inc，作为自完备子单元；被 add 时其 inc 并入容器。
inc mem.sc

# 私有类型：容器经 add 后应可直接使用。
def Vec2: {
    x: i4
    y: i4
}

# 私有函数：纯计算。
fnc h_add: i4, a: i4, b: i4
    return a + b

# 私有函数：使用本子单元的私有类型。
fnc h_manhattan: i4, v: Vec2
    var ax: i4 = v.x
    if ax < 0
        ax = -ax
    var ay: i4 = v.y
    if ay < 0
        ay = -ay
    return ax + ay
