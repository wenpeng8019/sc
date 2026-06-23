# mem —— sc 内存池化管理内置模块（替代 malloc/realloc/free）
#
# 本文件是 mem 接口的唯一事实源：
#   @fnc name:: 声明 C 侧实现的自由函数（无函数体）：转 C 生成 extern 原型，
#               实现在 mem_impl.c（链接期注入）。
#   @def 定义纯数据布局 + 方法声明（arena）。
# C ABI 契约见同目录 mem.h，默认实现见 mem_impl.c；跨平台经由 builtins/platform.h。
#
# 用法：inc mem.sc
#
# 三件套命名（对应 malloc / realloc / free）：
#   chunk(size)        分配一块内存，返回裸指针 &（失败 nil）
#   refit(p, size)     扩缩容并保留内容，返回新指针（失败 nil，旧块不动）
#   recycle(p)         归还内存到池（recycle(nil) 安全空操作）
#
# 设计（工业级要点，详见 mem.h / builtins/REFERENCE.md）：
#   - 分离空闲链表（segregated free list）按 size-class 分级，O(1) 分配/释放；
#   - 每线程私有堆（TLS）：分配/同线程释放走本地链表，全程无锁；
#   - 跨线程释放：A 线程分配、B 线程 recycle —— 经物主堆的原子单链表(MPSC)回收，
#     物主线程下次分配时批量并回；跨线程路径仅一次 CAS（mimalloc 思路）；
#   - 大对象（> 64KiB）直接透传 malloc/free（不池化）；
#   - 池化页默认不归还 OS（保留以换分配速度），进程退出由 OS 回收，
#     或显式 mem_teardown() 干净释放（仅在所有线程静止时调用）。
#
# 需要"批量同生命周期"对象（一帧/一请求内大量临时分配，结束统一释放）时，
# 用 arena 区域分配器：bump 指针分配、不支持单对象释放、drop 一次性回收全部。

# ---------------- 通用池化分配（C 实现接口）----------------

# 分配 size 字节，返回裸指针（失败 nil）；size==0 视为 1。
@fnc chunk:: &, size: u8

# 分配并清零 size 字节（对应 calloc），返回裸指针（失败 nil）。
@fnc chunk0:: &, size: u8

# 分配并清零 count*size 字节（对应 calloc(count,size)）；count*size 溢出返 nil。
@fnc chunk_array:: &, count: u8, size: u8

# 超对齐分配 size 字节，起始地址对齐到 align（须为 2 的幂）；aligned 块可用 recycle/refit。
# align <= 16 等价 chunk（默认已 16 对齐）；align 非 2 的幂返 nil。适用于 SIMD/缓存行/页对齐。
@fnc chunk_aligned:: &, size: u8, align: u8

# 扩缩容到 size 字节并保留原内容：p==nil 等价 chunk(size)；size==0 等价 recycle(p) 返回 nil；
# 失败返回 nil 且原块保持有效。
@fnc refit:: &, p: &, size: u8

# 归还 p 到池；recycle(nil) 为安全空操作。
@fnc recycle:: p: &

# 返回 p 实际可用字节数（>= 当初申请值，因 size-class 上取整）；p==nil 返回 0。
@fnc mem_usable:: u8, p: &

# 归还当前线程空闲堆页回 OS：仅当本线程无存活分配（count==0）时生效，否则保守不动；
# 返回实际释放的字节数。适合工作线程闲昂时主动压缩驻留内存。
@fnc mem_trim:: u8

# 释放全部池化页与每线程堆（仅在确认所有线程不再分配/释放时调用，用于干净关停/测试）。
@fnc mem_teardown::

# ---------------- 内存统计 ----------------

# 统计快照数据布局（与 mem.h 的 mem_stat_t 同步；字段均为 u8/uint64_t）。
#   reserved 向 OS 申请并仍持有的总字节（池化页 + 活跃大对象，含对象头）
#   live     当前分配给用户的可用字节（usable 口径）
#   peak_live live 历史峰值（单线程精确；多线程为各线程峰值之和的上界）
#   count    当前活跃（未归还）分配块数
#   allocs   累计成功分配次数
#   frees    累计成功归还次数
@def mem_stat_t: {
    reserved: u8
    live: u8
    peak_live: u8
    count: u8
    allocs: u8
    frees: u8
}

# 填充统计快照到 out：小对象遍历各线程堆累加（无锁，并发下为近似值），大对象走全局原子；
# 跨线程归还在物主线程下次并回前仍计入 live/count；cumulative 计数随 mem_teardown 清零。
# 需精确一致数值时在所有线程静止时调用。out==nil 为安全空操作。
@fnc mem_stat:: out: mem_stat_t&

# ---------------- arena：区域分配器（批量同生命周期）----------------

@def arena: {
    h: &                  # 实现私有区指针（区域块链表，实现私有）

    fnc init:: cap: u8    # 构造：cap 为单块默认容量字节（0 用内置默认 64KiB）
    fnc drop::            # 析构：一次性释放全部区域块
    fnc reset::           # 复位：释放多余块、保留最新块并清零用量（帧复用，免重复 malloc）
    fnc chunk:: &, size: u8   # bump 分配 size 字节（失败 nil）；不可单独 recycle，随 drop/reset 整体回收
}

# ---------------- shm：跨进程命名共享内存（跨平台）----------------
#
# 一块由 name 标识的命名内存区，可被多个进程映射到各自地址空间共享读写。
# 跨平台经由 builtins/platform.h：POSIX 用 shm_open + mmap；Windows 用
# CreateFileMapping + MapViewOfFile。
#
# 命名约定（可移植）：简单标记（字母/数字/下划线），勿含路径分隔符；
#   POSIX 实现内部自动加前导 '/'（如 "img" → "/img"），Windows 直接用作内核对象名。
#
# 生命周期：POSIX 命名区一经创建即持久，所有进程 drop 后仍需 shm_remove(name)
#   才真正销毁；Windows 为内核对象引用计数，最后句柄关闭即销毁（shm_remove 空操作）。
#
# 并发：共享页内存一致（同一物理页），区内并发读写同步由调用方负责
#   （可把 m 模块原语或 op.sc 的 sc_* 原子置于区内）。

@def shm: {
    h: &                  # 实现私有句柄（映射地址 + 容量 + 平台 fd/HANDLE，实现私有）

    # 创建或附着命名共享内存：name 标识区，size 期望字节数（0 视为 1，向上取整到页）。
    # flags 按位或：0=默认读写共享；1=只读（SHM_RDONLY，仅附着不创建）；2=独占创建（SHM_EXCL，已存在则失败）。
    # 区不存在则创建并定容；已存在则附着（要求其容量 >= 申请页数，且 size() 回报真实容量）。
    # 成功返回 1 并完成映射（data() 可用）；失败返回 0（h 置 nil）。
    fnc make:: bool, name: const char&, size: u8, flags: u4

    fnc data:: &          # 本进程映射首地址；未映射/失败返回 nil
    fnc size:: u8         # 实际映射字节数（附着时为底层区真实容量）；未映射返回 nil
    fnc drop::            # 解除映射 + 关闭句柄（不删除命名）；可安全重复调用
}

# 删除命名共享内存区（POSIX shm_unlink）：移除 name 后新 make 无法再附着到旧区，
# 已映射者仍可访问至各自 drop。成功（或 Windows 无需删除）返回 1，失败返回 0。
@fnc shm_remove:: bool, name: const char&

