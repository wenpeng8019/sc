# 负向用例：传入实参个数超过形参个数 → 语义检查报「期望至多 N 个实参」。
# （注意：少传实参是合法的，缺省自动补 0；仅多传才报错。）
@fnc add: i4, a: i4, b: i4
    return a + b

@fnc main: i4
    return add(1, 2, 3)
