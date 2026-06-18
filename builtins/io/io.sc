# io —— sc 输入输出内置模块（print 语言关键字的支持库）
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
# file(...) —— 文件 com 设备（构造一个以文件为后端的 com 通讯端点）：
#   var c: com& = file("a.txt", true, 1, 0)   # 文本只读、同步
#   - name：文件路径
#   - txt：bool，true=文本模式 / false=二进制模式（"b" 后缀，Windows 下影响换行翻译）
#   - read / write：方向模式 u1，0=禁用该方向 / 1=同步 / 2=异步（自动初始化 ioq 队列）
#       · 仅读→以 "r" 打开；仅写→"w"（创建/截断）；读写→"w+"（创建/截断，可读写）
#       · ==2 时为该方向分配 ioq（com.rq/wq 非 nil = 支持异步 io，可在 rpc 内 await）
#   - 返回 com&（失败返回 nil）；支持 << / >> 收发与 com[...] 句柄（alloc/free 已实现）
#   - 无缓冲（每次 io 即一次系统调用）：写入立即落盘，同进程内另一端口可即时读到
#   - 用完须 c->close()：fclose 文件并释放设备内存（返回 0 / 出错 <0）；之后 c 失效

# ---------------- print：日志输出原语（C 实现接口） ----------------
# print 关键字由编译器生成对本接口的调用：首参 chn 为 u1 日志通道（透传），
# 其后为 C printf 风格格式串与可变参数（fmt 前缀 "X:" 设级别）。
@fnc print:: chn: u1, fmt: char&, ...

# ---------------- file：文件 com 设备（C 实现接口）----------------
# 打开 name 指定文件，构造 com 端点；read/write 取 0/1/2（禁用/同步/异步）。
@fnc file:: com&, name: char&, txt: bool, read: u1, write: u1