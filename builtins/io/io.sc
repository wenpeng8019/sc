# io —— sc 输入输出内置模块（print / stringify(...) 语言关键字的支持库）
#
# 本文件是 io 接口的唯一事实源：
#   @fnc print:: 声明 print 原语（无函数体）：extern 原型，实现在 C 侧
# C ABI 契约见同目录 io.h，默认实现见 io_impl.c
#
# 用法：inc io.sc
#
# print —— 日志输出（语言关键字，编译器生成 print 调用）：
#   print "x = ", x             # 无括号=拼接糖：字符串字面量=纯文本，
#                               # 变量按静态类型自动补 printf 说明符（i4→%d, char&→%s, f8→%f ...）
#   print "x=", x, " y=", y     # 多实参逗号分隔，自动拼接为可变参数
#   print (x: "%.5d")           # (expr: "%fmt") 显式指定该实参格式
#   print("x = %d", x)          # 有括号=C printf 兼容模式：首参格式串，实参原样传递
#   print<3> "通道 3 的日志"     # <chn> 指定 u1 日志通道（整数字面量或宏/常量），默认 0
#   print "E: open ", p, " 失败" # fmt 文本前缀 "X:" 指定级别，X ∈ F/E/W/I/D/V
#                               # F=致命 E=错误 W=警告 I=状态 D=调试(默认) V=详尽
#   - 输出格式：HH:MM:SS.mmm L| 文本（chn!=0 时加通道标记；自动补换行）
#   - 级别过滤：环境变量 SC_LOG=F/E/W/I/D/V（默认 D；高于该级别的输出被丢弃）
#
# stringify(...) —— JSON 字符串格式化（语言关键字，编译器按静态类型生成格式化器，
#                   写入独立 stringify.h 由生成的 .c include；区别于类型 string 与
#                   堆构造 string()）：
#   var s: string = stringify(x)        # 返回 adt string（JSON 文本），用完需 s.drop()
#   var b[256]: char
#   var p: char& = stringify(x, b, 256) # 在给定缓存内构建（截断保证 NUL 结尾），
#                                       # 返回 char&（即缓存首址，无需 drop）
#   选项块 stringify<key:val, ...>(...)：以 (stringify_t){...} 传入格式化器
#     · stringify<compact:1>(x)         # 紧凑单行 {"x":3,"y":4}
#     · 默认（无选项 / compact:0）       # 多行美化（2 空格逐层缩进）
#   - 标量 → 数字；bool → true/false；char → 'a'
#   - char& / char 数组 → "文本"；adt string → "内容"
#   - 结构体/联合体 → {"字段": 值, ...} JSON 对象（链表 _prev/_next 不展开）
#     · 子成员为结构体（值）→ 递归展开
#     · 成员为结构体指针 → "类型名@0x地址"（不深递归）
#     · 成员为标量指针 → "&值"（nil → nil）
#     · 其它指针（void&/多级）→ "0x地址"（nil → nil）
#   - 一维数组 → [v, v, ...]
#   - 结构体一级指针（顶层实参）→ 解引用展开内容（nil → "nil"）
#   依赖 adt string（inc adt.sc）与 io 的 stringify_t（inc io.sc），暂不支持多维数组

# ---------------- print：日志输出原语（C 实现接口） ----------------
# print 关键字由编译器生成对本接口的调用：首参 chn 为 u1 日志通道（透传），
# 其后为 C printf 风格格式串与可变参数（fmt 前缀 "X:" 设级别）。
@fnc print:: chn: u1, fmt: char&, ...

