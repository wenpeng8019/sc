# 负向用例：调用未声明的自由函数 → 语义检查报「未定义的函数」。
@fnc main: i4
    var r: i4 = compute(1, 2)
    return r
