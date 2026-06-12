# 特性 4：sc 模块导入（inc x.sc，单元编译+链接）：builtins/io.sc
inc io.sc

fnc main: i4
    print_i4(42)
    print_nl()
    return 0
