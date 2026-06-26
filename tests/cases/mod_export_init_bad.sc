# 期望语义报错：模块的构造（init）不可导出（@）
mod widget:
    n: i4

    @fnc init:
        this->n = 0
        return
