# 由 scc --emit-sc 从 AST 再生成

inc stdio.h

inc adt.sc

fnc main: i4
    var s: string
    s.append("hello")
    printf("string=%s len=%llu cap=%llu\n", s.cstr(), s.len(), s.cap)
    s.drop()
    return 0
