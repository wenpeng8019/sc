# 由 scc --emit-sc 从 AST 再生成

let MAX_VIEW:: i4

var g_tick:: i8

fnc demo: i4
    printf("v=%d\n", 1)
    var a: i4 = abs(-3)
    var b: i4 = MAX_VIEW
    g_tick = (g_tick + 1)
    return a + b
