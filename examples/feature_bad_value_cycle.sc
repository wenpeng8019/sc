# 验证：按值互相包含应在语义阶段报错

def aa: {
    b: bb
}

def bb: {
    a: aa
}

fnc main: i4
    return 0
