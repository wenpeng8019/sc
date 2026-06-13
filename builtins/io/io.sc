# io —— sc 输入输出内置模块（print / stringify(...) 语言关键字的支持库）
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
# stringify(...) —— JSON 字符串格式化（语言关键字，编译器按静态类型生成格式化器，
#                   写入独立 stringify.h 由生成的 .c include；区别于类型 string 与
#                   堆构造 string()）：
#   var s: string = stringify(x)        # 返回 adt string（JSON 文本），用完需 s.drop()
#   var b[256]: char
#   var p&: char = stringify(x, b, 256) # 在给定缓存内构建（截断保证 NUL 结尾），
#                                       # 返回 char&（即缓存首址，无需 drop）
#   - 标量 → 数字；bool → true/false；char → 'a'
#   - char& / char 数组 → "文本"；adt string → "内容"
#   - 结构体/联合体 → {"字段": 值, ...} JSON 对象（链表 _prev/_next 不展开）
#     · 子成员为结构体（值）→ 递归展开
#     · 成员为结构体指针 → "类型名@0x地址"（不深递归）
#     · 成员为标量指针 → "&值"（nil → nil）
#     · 其它指针（void&/多级）→ "0x地址"（nil → nil）
#   - 一维数组 → [v, v, ...]
#   - 结构体一级指针（顶层实参）→ 解引用展开内容（nil → "nil"）
#   依赖 adt string（inc adt.sc），暂不支持多维数组
