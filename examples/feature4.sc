# 特性 4：sc 模块导入（inc x.sc，单元编译+链接）：builtins/adt.sc
inc stdio.h
inc adt.sc

fnc main: i4
    # 使用导入模块中的 @def 类型
    var s: string
    s.size = 5
    s.capacity = 8
    printf("string size=%llu cap=%llu\n", s.size, s.capacity)
    return 0
