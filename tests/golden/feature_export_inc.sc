# 由 scc --emit-sc 从 AST 再生成

@inc stdio.h

@fnc puts_wrap: i4, s&: u1
    return puts(s)
