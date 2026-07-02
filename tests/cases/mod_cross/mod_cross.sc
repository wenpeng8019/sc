inc stdio.h
inc mod_cross_lib.sc                 # 引入模块库（其 mod store 导出 add/sum）

fnc main: i4
    store.add(10)
    store.add(32)
    ::printf("sum = %d\n", store.sum())     # 跨模块调用导出成员函数
    return 0
