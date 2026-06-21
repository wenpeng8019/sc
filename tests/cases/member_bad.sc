# 负向用例：访问结构体不存在的成员 → 语义检查报「结构体 '…' 没有成员 '…'」。
@def point: {
    x: i4
    y: i4
}

@fnc main: i4
    var p: point
    p.x = 1
    return p.z
