# 专项验证（负向用例）：按值互相包含应在语义阶段报错，不可编译

def aa: {
    b: bb
}

def bb: {
    a: aa
}

fnc main: i4
    return 0
