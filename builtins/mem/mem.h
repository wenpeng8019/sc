/* mem.h —— sc 内存池化管理模块的 C ABI 契约（与 builtins/mem/mem.sc 同步维护）
 *
 * 三件套（对应 malloc/realloc/free）：chunk / refit / recycle；外加 chunk0(calloc)。
 * 返回的指针对齐到 16 字节（满足 max_align_t），可直接用于任意标量/SIMD。
 *
 * 架构（默认实现见 mem_impl.c，平台适配经 builtins/platform.h）：
 *   - size-class 分离空闲链表（44 档，16B..64KiB，均 16 对齐，避免内部碎片）；
 *   - 每线程私有堆（TLS）：分配 / 同线程释放走本地链表，无锁；
 *   - 跨线程释放：经物主堆的原子 MPSC 单链表回收，物主下次分配批量并回（mimalloc 思路）；
 *   - 大对象（> MEM_SMALL_MAX）直接 malloc/free 透传；
 *   - 池化页保留不还 OS（换分配速度），mem_teardown() 干净释放（仅在线程静止时）。
 *
 * 对象头（16 字节，位于返回指针前方）：
 *   owner: 已分配=物主堆指针 / 空闲（在链表中）=下一块链接 / 大对象=哨兵 MEM_LARGE_OWNER
 *   info : 小对象=size-class 序号 / 大对象=用户请求字节数
 * owner 与 info 在"已分配 / 本地空闲 / 跨线程待回收"三态分时复用，互不冲突。
 */
#ifndef SC_MEM_H
#define SC_MEM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 池化小对象上限：超过则走大对象（malloc/free）直通 */
#define MEM_SMALL_MAX   65536u

/* ---------------- 通用池化分配 ---------------- */

void*    chunk(uint64_t size);              /* malloc：size==0 视为 1；失败 NULL */
void*    chunk0(uint64_t size);             /* calloc：分配并清零；失败 NULL */
void*    chunk_array(uint64_t count, uint64_t size);  /* calloc(count,size)：count*size 溢出返 NULL */
void*    chunk_aligned(uint64_t size, uint64_t align); /* 超对齐分配：align 须为 2 的幂；失败 NULL */
void*    refit(void *p, uint64_t size);     /* realloc：保留内容；失败 NULL 且原块有效 */
void     recycle(void *p);                  /* free：recycle(NULL) 安全空操作 */
uint64_t mem_usable(void *p);               /* p 的实际可用字节数（size-class 上取整）；NULL→0 */
uint64_t mem_trim(void);                    /* 归还当前线程空闲堆页回 OS（仅本线程无存活分配时）；返释放字节 */
void     mem_teardown(void);                /* 释放全部池化页与每线程堆（仅线程静止时调用） */

/* ---------------- 内存统计 ----------------
 * mem_stat 汇总当前快照：小对象遍历各线程堆累加（无锁，并发下为近似值），
 * 大对象走全局原子计数。跨线程归还在物主线程下次分配并回前，仍计入 live/count，
 * 尚未计入 frees。cumulative allocs/frees 随 mem_teardown 释放堆而清零。
 * 需精确一致的数值时，在所有线程静止（无并发分配/释放）时调用。
 */
typedef struct mem_stat_t {
    uint64_t reserved;   /* 向 OS 申请并仍持有的总字节（池化页 + 活跃大对象，含对象头） */
    uint64_t live;       /* 当前分配给用户的可用字节（usable 口径） */
    uint64_t peak_live;  /* live 历史峰值（单线程精确；多线程为各线程峰值之和的上界） */
    uint64_t count;      /* 当前活跃（未归还）分配块数 */
    uint64_t allocs;     /* 累计成功分配次数 */
    uint64_t frees;      /* 累计成功归还次数 */
} mem_stat_t;

void     mem_stat(mem_stat_t *out);         /* 填充统计快照；out==NULL 空操作 */

/* ---------------- arena：区域分配器（批量同生命周期） ---------------- */

typedef struct arena {
    void *h;       /* 实现私有区指针（区域块链表） */
} arena;

void     arena_init(arena *_this, uint64_t cap);   /* cap 单块默认容量（0→64KiB） */
void     arena_drop(arena *_this);                 /* 释放全部区域块 */
void     arena_reset(arena *_this);                /* 保留最新块、清零用量（帧复用） */
void*    arena_chunk(arena *_this, uint64_t size); /* bump 分配；不可单独释放；失败 NULL */

/* ---------------- shm：跨进程命名共享内存（跨平台） ----------------
 * 命名内存区（由 name 标识），多个进程各自 make 映射后共享读写。
 * 平台适配经 builtins/platform.h：
 *   - POSIX  ：shm_open + ftruncate + mmap（命名持久，需显式 shm_remove 销毁）
 *   - Windows：CreateFileMapping(INVALID_HANDLE_VALUE) + MapViewOfFile
 *              （内核对象引用计数，最后句柄关闭即销毁，shm_remove 为空操作）
 * 命名建议：简单标记（字母/数字/下划线），勿含路径分隔符；POSIX 内部自动加 '/' 前缀。
 * 链接注意：较老 glibc 的 shm_open/shm_unlink 在 librt（需 -lrt）；
 *   glibc >= 2.34 已并入 libc。macOS/BSD 在 libc 内。
 */
typedef struct shm {
    void *h;       /* 实现私有句柄（映射地址 + 容量 + 平台 fd/HANDLE） */
} shm;

/* shm_make 标志位（可按位或；0 = 默认读写共享、不存在则创建） */
#define SHM_RDONLY  1u   /* 只读映射（POSIX PROT_READ / Windows FILE_MAP_READ）；单独使用时仅附着不创建 */
#define SHM_EXCL    2u   /* 独占创建（POSIX O_EXCL / Windows ERROR_ALREADY_EXISTS）：区已存在则失败 */

/* 创建或附着命名共享内存。name 标识区，size 期望字节数（0→1，向上取整到页）。
 * flags 见 SHM_* 位（0 为默认）。区不存在则创建并定容；已存在则附着
 * （要求其容量 >= 申请页数，且 shm_size 回报其真实容量）。
 * 成功 1 并完成映射（shm_data 可用）；失败 0（_this->h 置 NULL）。 */
bool     shm_make(shm *_this, const char *name, uint64_t size, uint32_t flags);

void*    shm_data(shm *_this);   /* 映射首地址；未映射/失败 NULL */
uint64_t shm_size(shm *_this);   /* 实际映射字节数（附着时为底层区真实容量）；未映射 0 */
void     shm_drop(shm *_this);   /* 解除映射 + 关闭句柄（不删除命名）；可重复调用 */

/* 删除命名区（POSIX shm_unlink）；Windows 无显式删除，返回 1。成功 1 / 失败 0。 */
bool     shm_remove(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* SC_MEM_H */
