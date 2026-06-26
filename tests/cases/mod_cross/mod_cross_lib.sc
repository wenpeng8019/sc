# 模块库单元：导出成员函数供其他模块跨模块调用
@mod store:
    total: i4

    fnc init:
        this->total = 0
        return

    @fnc add: i4, n: i4
        this->total = this->total + n
        return this->total

    @fnc sum: i4
        return this->total

    fnc reset
        this->total = 0
        return
