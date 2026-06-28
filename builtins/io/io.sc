# io —— sc 输入输出内置模块（file 文件 / stream 内存 com 设备）
#
# 本文件是 io 接口的唯一事实源：
#   @fnc file:: / @fnc stream:: 声明 io 原语（无函数体）：extern 原型，实现在 C 侧
# C ABI 契约见同目录 io.h，默认实现见 io_impl.c
#
# 用法：inc io.sc
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
#
# stream(...) —— 内存 com 设备（把一块现成内存当作 com 通讯端点的后端）：
#   var c: com& = stream(buf, 256, 1, 1)      # 绑定 buf[256]，读写同步
#   - mem：绑定的内存基址（调用方所有）。stream 不复制、不分配数据缓冲，读写直接落在这块内存上
#   - size：u8，这块内存的容量（字节）。读到 size 触底返回 eof；写超过 size 短写报错
#   - read / write：方向模式 u1，0=禁用该方向 / 1=同步 / 2=异步（自动初始化 ioq 队列）
#       · ==2 时为该方向分配 ioq（com.rq/wq 非 nil = 支持异步 io，可在 rpc 内 await；内存恒就绪）
#   - 读写各自独立游标 rpos/wpos：可对同一块内存边写边读，互不干扰
#   - 返回 com&（失败返回 nil）；支持 << / >> 收发与 com[...] 句柄（alloc/free 已实现）
#   - 用完须 c->close()：仅释放端点结构（com + 游标），绝不释放绑定的 mem（由调用方负责）

# ---------------- file：文件 com 设备（C 实现接口）----------------
# 打开 name 指定文件，构造 com 端点；read/write 取 0/1/2（禁用/同步/异步）。
@fnc file:: com&, name: const char&, txt: bool, read: u1, write: u1

# ---------------- stream：内存 com 设备（C 实现接口）----------------
# 把 mem 指向的 size 字节内存绑定为 com 端点后端；read/write 取 0/1/2（禁用/同步/异步）。
# 不分配数据缓冲（绑定调用方内存），close 仅释放端点结构、不碰 mem。
@fnc stream:: com&, mem: &, size: u8, read: u1, write: u1