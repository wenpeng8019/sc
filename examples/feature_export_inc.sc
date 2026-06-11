# 验证：@inc 会导出到生成头文件

@inc stdio.h

@fnc puts_wrap: i4, s&: u1
    return puts(s)
