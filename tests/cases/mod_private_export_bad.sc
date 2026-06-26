# 期望语义报错：私有模块（未 @ 导出）的成员函数不可导出（@fnc）
mod widget:
    n: i4

    fnc init:
        this->n = 0
        return

    @fnc value: i4
        return this->n
