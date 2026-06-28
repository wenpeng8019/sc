inc stdio.h

# 模块单例：声明即创建（def 类型 counter_m + var 实例 counter）
@mod counter:
    n: i4
    tag: char&

    fnc init:
        this->n = 100
        this->tag = "counter"
        return

    fnc drop:
        printf("drop %s n=%d\n", this->tag, this->n)
        return

    @fnc bump:
        this->do_step()
        return

    @fnc value: i4
        return this->n

    fnc do_step:
        this->n = this->n + 1
        return

fnc main: i4
    counter.bump()
    counter.bump()
    printf("value = %d\n", counter.value())
    return 0
