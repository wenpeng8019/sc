# proto —— sc 协议解析/转换构件（内置基础模块）
#
# 本文件是 proto 接口的唯一事实源：
#   @fnc name: ...   定义回调函数类型（转 C 为函数指针 typedef）
#   @def 定义纯数据布局 + 方法声明（无函数体）：转 C 生成 extern 原型，
#                    实现在 proto_impl.c（编译器自动编译并链接）。
# C ABI 契约见同目录 proto.h；默认实现见 proto_impl.c；底层内存走 mem 模块。
#
# 用法：inc proto.sc
#
# 模型：一个 proto 实例 = 分块存储的「类型化字节记录」序列 + 一种消费纪律。
#   记录三元组 (tag, data, size)：tag 为上下文/角色标记，data/size 为原始字节。
#   · feed  —— 把一条记录入栈（FILO）/ 入队（FIFO）。
#   · drain —— 按纪律取出一条并消费（FILO 顶 / FIFO 头）。
#   · each / build —— 按插入顺序（最旧→最新）遍历重组：触发协议接口做自定义 transform，
#                     用于 encode（结构化→wire）/ decode（wire→结构化）两向转换。
#
# 内存创新（参考 c_prototype 的 stk）：每块 chunk 固定大小，数据从块首向前排、每条 4 字节
#   索引项从块尾向后排（反向）；块满即追加新块（永不 realloc/搬移，指针稳定），释放的整块
#   进空闲缓存复用。索引项 = kind(高8位) | offset(低24位)，单块最大 16MB；单条大小由相邻
#   索引偏移之差推出（不显式存 size）。底层内存走 mem 模块 chunk/recycle。

inc mem.sc     # chunk / recycle（分块内存与结果缓冲）

# 消费纪律（与 proto.h 的 SC_PROTO_FILO / SC_PROTO_FIFO 同值）
@def proto_mode: [
    PROTO_FILO = 0    # 栈：drain 从顶（最后 feed）取
    PROTO_FIFO        # 队列：drain 从头（最早 feed）取
] : u4

# 协议转换回调：把一条记录 (tag,data,size) 转换写入 out（容量 cap），返回写入/所需字节数。
# 两趟约定：out==nil 只测长（返回该条所需字节，不写入），out!=nil 实际写入并返回写入数。
# tag：generic feed 记录回报用户 tag；类型化/字符串/blob 记录回报其 kind 码。
@fnc proto_xform: i4, tag: u4, data: &, size: u4, out: &, cap: u4, ctx: &

@def proto: {
    head:     &     # chunk 链头（最旧块；实现私有）
    tail:     &     # chunk 链尾（最新块，feed 目标）
    cache:    &     # 空闲 chunk 缓存单链
    num:      u4    # 当前记录条数
    chunk_sz: u4    # 标准 chunk 载荷字节
    cache_sz: u4    # 空闲缓存上限（标准块数）
    cache_n:  u4    # 当前缓存块数
    mode:     u4    # PROTO_FILO / PROTO_FIFO

    # 构造：mode 见上；chunk_sz 单块载荷字节（0 用默认 4KiB，向上对齐）；
    #   cache_sz 空闲块缓存上限（0 = 不缓存）。成功返回 true。
    fnc init:: bool, mode: u4, chunk_sz: u4, cache_sz: u4
    fnc drop::                              # 释放全部 chunk 与缓存
    fnc clear::                             # 清空全部记录（保留缓存）
    fnc depth:: u8                         # 当前记录条数
    fnc is_empty:: bool                     # 是否为空

    # ---- feed：通用三元组（拷贝 size 字节 + 用户 tag）----
    fnc feed:: bool, tag: u4, data: const &, size: u4

    # ---- 类型化 feed（可选 printf format 串；fmt==nil 用内置默认格式）----
    # fmt 指针内联存储、不拷贝——须在 build/each 前保持有效（通常为字符串字面量）。
    fnc push_b::   bool, v: bool, fmt: const char&
    fnc push_i1::  bool, v: i1, fmt: const char&
    fnc push_i2::  bool, v: i2, fmt: const char&
    fnc push_i4::  bool, v: i4, fmt: const char&
    fnc push_i8::  bool, v: i8, fmt: const char&
    fnc push_u1::  bool, v: u1, fmt: const char&
    fnc push_u2::  bool, v: u2, fmt: const char&
    fnc push_u4::  bool, v: u4, fmt: const char&
    fnc push_u8::  bool, v: u8, fmt: const char&
    fnc push_f4::  bool, v: f4, fmt: const char&
    fnc push_f8::  bool, v: f8, fmt: const char&
    # 字符串：拷贝内容（内部 NUL 结尾）；trim!=nil 去除末尾属于 trim 集合的字符。
    fnc push_str:: bool, v: const char&, trim: const char&
    # 二进制块：拷贝 size 字节 + 内联 transform 回调（build 时对本条调用 cb）。
    fnc push_blob:: bool, data: const &, size: u4, cb: proto_xform
    # 裸指针：内联存储指针 + transform 回调（不拷贝指向内容）。
    fnc push_ptr:: bool, v: const &, cb: proto_xform

    # ---- drain：按 mode 取出并消费 ----
    # 回填 r_tag / r_data（指向内部缓冲，有效期至下次变更），返回数据字节数；空返回 -1。
    fnc drain:: i4, r_tag: u4&, r_data: &&
    fnc peek::  i4, r_tag: u4&, r_data: &&      # 看一条不消费
    fnc back::  bool, n: i4                      # 丢弃 n 条（FILO 顶 / FIFO 头）

    # ---- 重组转换（不消费，按插入顺序 最旧→最新）----
    # 逐条经 cb 转换累加写入 out（out==nil 只测长）；返回总字节数（> cap 表示 out 不足即所需）。
    fnc each:: i4, cb: proto_xform, ctx: &, out: &, cap: u4
    # 内置格式化重组：各条渲染为文本、条间以 delim 分隔（delim==nil 无分隔）；写入 out（NUL 结尾），
    #   返回不含 NUL 字节数（可测长探测）。类型化经 snprintf、str 直出、blob/ptr 经其 cb、feed 原样。
    fnc build_to:: i4, delim: const char&, out: char&, cap: u4
    # 同 build_to，但由 mem 分配恰好容量的缓冲返回（NUL 结尾），调用方用 recycle 释放；失败 nil。
    fnc build:: char&, delim: const char&
}
