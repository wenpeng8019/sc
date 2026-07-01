# 程序结构依赖图（proggraph）回归用例：自足单元，覆盖
# type / read / write / call / method 等边种类，配合 --graph=unit 快照锁定。

def counter: {
    n: i4
    step: fnc: i4                     # 方法：读写自身字段
        this->n = this->n + 1
        return this->n
}

var total: i4 = 0                     # 全局标量：write / read 边目标

fnc add: i4, x: i4, y: i4            # 纯函数：被 main 调用（call 边）
    return x + y

fnc bump: i4                         # 写 + 读全局 total
    total = total + 1                 # write → total
    return total                      # read  → total

fnc main: i4
    var c: counter                    # type 边 → counter
    var s: i4 = add(1, 2)             # call 边 → add
    var r: i4 = c.step()              # method 边 → counter.step
    bump()                            # call 边 → bump
    printf("%d %d %d %d\n", s, r, bump(), total)
    return 0
