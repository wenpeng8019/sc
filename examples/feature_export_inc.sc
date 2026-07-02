# 专项验证：@inc 会导出到生成头文件（--emit-c -o 时检查 .h，无 main 不可运行）

@inc stdio.h

@fnc puts_wrap: i4, s: u1&
    return ::puts(s)
