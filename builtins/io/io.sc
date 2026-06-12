# io —— sc 输入输出内置模块（print / string_of 语言关键字的支持库）
# 唯一事实源：C ABI 契约见同目录 io.h，默认实现见 io_impl.c
#
# 用法：inc io.sc
#
# print —— 日志输出（语言关键字，编译器生成 sc_print 调用）：
#   print("x = %d", x)          # C printf 风格格式化
#   print("E: open %s 失败", p) # fmt 前缀 "X:" 指定级别，X ∈ F/E/W/I/D/V
#                               # F=致命 E=错误 W=警告 I=状态 D=调试(默认) V=详尽
#   - 输出格式：HH:MM:SS.mmm L| 文本（自动补换行）
#   - 级别过滤：环境变量 SC_LOG=F/E/W/I/D/V（默认 D；高于该级别的输出被丢弃）
#
# string_of —— 字符串格式化（语言关键字，编译器按静态类型生成格式化器）：
#   var s: string = string_of(x)   # 返回 adt string，用完需 s.drop()
#   - 标量 → 数字；bool → true/false；char → 'a'
#   - char& / char 数组 → "文本"；其它指针 → 0x 地址（nil → nil）
#   - 结构体/联合体 → {字段: 值, ...} 递归展开（链表 _prev/_next 不展开）
#   - 一维数组 → [v, v, ...]；adt string → "内容"
#   - 结构体一级指针 → 解引用展开内容
#   依赖 adt string（inc adt.sc），暂不支持多维数组
