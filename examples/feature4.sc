# 特性 4：sc 模块导入（inc x.sc，单元编译+链接）：builtins/adt/adt.sc
inc stdio.h
inc adt.sc

fnc main: i4
    # 使用导入模块中的 @def 类型与 @fnc 方法（声明即构造）
    var s: string
    s.append("hello")
    printf("string=%s len=%llu cap=%llu\n", s.cstr(), s.len(), s.cap)
    s.drop()
    return 0
