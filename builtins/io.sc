# 内置 I/O 库：用于演示和工程内基础输出
inc stdio.h

@fnc print_i4: v, x:i4
    printf("%d", x)
    return

@fnc print_b: v, x:b
    printf("%d", x)
    return

@fnc print_nl: v
    printf("\n")
    return
