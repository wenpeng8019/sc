# 负向用例：函数名拼写错误 → 报「未定义的函数」并给出近似名提示。
@fnc compute_sum: i4, a: i4, b: i4
    return a + b

@fnc main: i4
    return compute_sun(1, 2)
