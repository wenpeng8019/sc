# 由 scc --emit-sc 从 AST 再生成

inc stdio.h

@mod counter:
    n: i4
    tag: char&
    fnc init
        this->n = 100
        this->tag = "counter"
        return
    fnc drop
        printf("drop %s n=%d\n", this->tag, this->n)
        return
    @fnc bump
        this->do_step()
        return
    @fnc value: i4
        return this->n
    fnc do_step
        this->n = (this->n + 1)
        return


fnc main: i4
    counter.bump()
    counter.bump()
    printf("value = %d\n", counter.value())
    return 0
