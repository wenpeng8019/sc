# 由 scc --emit-sc 从 AST 再生成

inc adt.sc

@fnc main: i4
    var s: string@ = string()
    var n: i4 = 42
    var name: char& = "sc"
    print<s> "hello ", name, " n=", n
    print<s> " | second line"
    print<s>("; x=%d y=%d", 7, 8)
    print "stdout still works: ", n
    printf("collected string = [%s]\n", s.cstr())
    s.drop()
    return 0
