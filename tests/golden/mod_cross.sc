# 由 scc --emit-sc 从 AST 再生成

inc stdio.h

inc mod_cross_lib.sc

fnc main: i4
    store.add(10)
    store.add(32)
    ::printf("sum = %d\n", store.sum())
    return 0
