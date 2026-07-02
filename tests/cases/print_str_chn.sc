inc adt.sc

@fnc main: i4
    var s: string@ = string()
    # 拼接糖：字符串字面量=纯文本，变量按类型自动补说明符
    var n: i4 = 42
    var name: char& = "sc"
    print<s> "hello ", name, " n=", n
    print<s> " | second line"
    # 括号兼容模式：C ::printf 语法
    print<s> ("; x=%d y=%d", 7, 8)
    # 对照：普通 print 仍输出 stdout
    print "stdout still works: ", n
    ::printf("collected string = [%s]\n", s.cstr())
    s.drop()
    return 0
