# 负向用例：引用未声明的变量 → 语义检查报「未定义的标识符」。
@fnc main: i4
    var x: i4 = 1
    return x + y
