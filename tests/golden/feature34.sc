# 由 scc --emit-sc 从 AST 再生成

inc stdio.h

inc "feature34_cmac.h"

def DEFINE_COUNTER:: N
    let counter_\N:: i4
    fnc counter_\N\_inc::: void
    fnc counter_\N\_get::: i4

mix DEFINE_COUNTER(hits)

mix DEFINE_COUNTER(miss)

fnc main: i4
    counter_hits_inc()
    counter_hits_inc()
    counter_hits_inc()
    counter_miss_inc()
    printf("hits=%d miss=%d\n", counter_hits_get(), counter_miss_get())
    printf("counter_hits(direct)=%d\n", counter_hits)
    return 0
