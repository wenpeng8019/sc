# 由 scc --emit-sc 从 AST 再生成

inc stdio.h

inc async.sc

def session: {
    name: char&
    seq: i4
}

fnc async_proc: i4, id: future_id, f: future&
    var v: i4 = (f->get(): i4)
    var s: session& = (f->ctx(): session&)
    case id:
        conn:
            printf("派发 conn[%s#%d]: v=%d\n", s->name, s->seq, v)
        data:
            printf("派发 data[%s#%d]: v=%d\n", s->name, s->seq, v)
        term:
            printf("派发 term[%s#%d]: v=%d（终止事件，停循环）\n", s->name, s->seq, v)
            return -1
    return 0

fnc main: i4
    async_init()
    var s1: session = {"alpha", 1}
    var s2: session = {"beta", 2}
    var c: future& = future<conn>(&s1)
    done c, 7
    var d1: future& = future<data>(&s2)
    done d1, 42
    var d2: future& = future<data>(&s1)
    done d2, 43
    var x: future& = future<term>(&s2)
    done x, 0
    async_loop(async_proc)
    async_final()
    return 0
