/* mem_impl.c —— mem.h 契约的默认实现（编译器自动编译并链接）
 *
 * 跨平台经由 builtins/platform.h（TLS 线程局部存储 + sc_* 原子操作）。
 * 设计要点见 mem.h 顶部注释。无锁热路径：
 *   - 分配：本线程 size-class 空闲链表弹出（无原子）；空了先并回跨线程释放（一次原子
 *     exchange），再不够则向 OS 申请一页切分。
 *   - 释放：同线程→直接压回本地链表（无原子）；跨线程→CAS 压入物主堆的 MPSC 链表。
 *   - 全程无互斥锁；唯一全局共享状态是堆注册表（建堆时一次 CAS）与各堆的 thread_free
 *     （单消费者 exchange 取走整链 + 多生产者 CAS 压入，标准 MPSC，无 ABA）。
 */
#include "mem.h"
#include "platform.h"

/* shm（跨进程共享内存）所需平台头 */
#if !P_WIN
#   include <sys/mman.h>     /* mmap/munmap/shm_open/shm_unlink/MAP_* */
#   include <sys/stat.h>     /* fstat / mode 常量 */
#   include <fcntl.h>        /* O_CREAT/O_RDWR */
#   include <unistd.h>       /* ftruncate/close/sysconf */
#endif

/* ---------------- 常量与对齐 ---------------- */

#define MEM_ALIGN       16u                 /* 返回指针对齐（满足 max_align_t / SIMD） */
#define MEM_NCLASS      44u                 /* size-class 档数（16B..64KiB） */
#define MEM_PAGE_BYTES  (64u * 1024u)       /* 单页目标字节数（切成同 class 多块） */
#define MEM_ARENA_DEF   (64u * 1024u)       /* arena 单块默认容量 */
#define MEM_LARGE_OWNER   ((void*)~(uintptr_t)0)        /* 大对象 owner 哨兵 */
#define MEM_ALIGNED_OWNER ((void*)(~(uintptr_t)0 - 1))  /* 超对齐对象 owner 哨兵（chunk_aligned） */

#define MEM_ALIGN_UP(x, a)  (((uintptr_t)(x) + ((a) - 1)) & ~(uintptr_t)((a) - 1))

/* ---------------- 调试构建（可选）：-DMEM_DEBUG 开启双重释放 / 野指针释放检测 ----------------
 * 开启后对象头增设 magic 守卫字段：分配置 MEM_MAGIC_ALLOC，归还置 MEM_MAGIC_FREE。
 * recycle/refit/mem_usable 校验 magic，捕获重复 recycle、释放非本池指针等错误（best-effort）。
 * 缓冲区越界检测由编译器 --check=mem 金丝雀机制负责，此处不重复。默认关闭，零开销。
 */
#ifdef MEM_DEBUG
#include <stdio.h>
#define MEM_MAGIC_ALLOC  ((size_t)0x5CA11AC1u)
#define MEM_MAGIC_FREE   ((size_t)0x5CF4EEEDu)
static void mem_die(const char *why, void *p) {
    fprintf(stderr, "[mem] 致命：%s (ptr=%p)\n", why, p);
    abort();
}
#endif

/* 对象头：见 mem.h。owner/info 分时复用。 */
typedef struct mem_block {
    void   *owner;      /* 已分配=堆 / 空闲=链接 / 大对象=哨兵 */
    size_t  info;       /* 小=class 序号 / 大=用户字节数 */
#ifdef MEM_DEBUG
    size_t  magic;      /* MEM_MAGIC_ALLOC（用户持有）/ MEM_MAGIC_FREE（已归还），仅调试构建 */
#endif
} mem_block;

/* 调试守卫：标记块为已分配 / 已归还，并在归还前校验状态 */
#ifdef MEM_DEBUG
#define MEM_MARK_ALLOC(b)   ((b)->magic = MEM_MAGIC_ALLOC)
#define MEM_MARK_FREE(b)    ((b)->magic = MEM_MAGIC_FREE)
#define MEM_CHECK_ALLOC(b, p) do { \
        if ((b)->magic == MEM_MAGIC_FREE) mem_die("双重释放 / 释放已归还指针", (p)); \
        else if ((b)->magic != MEM_MAGIC_ALLOC) mem_die("释放非本池 / 已损坏指针", (p)); \
    } while (0)
#else
#define MEM_MARK_ALLOC(b)     ((void)0)
#define MEM_MARK_FREE(b)      ((void)0)
#define MEM_CHECK_ALLOC(b, p) ((void)0)
#endif

/* 头部占用（上取整到 MEM_ALIGN，保证 payload = block + MEM_HDR 始终 16 对齐） */
#define MEM_HDR  ((size_t)MEM_ALIGN_UP(sizeof(mem_block), MEM_ALIGN))

/* 页头：仅需链接，便于 teardown 整页释放（不做单块归还） */
typedef struct mem_page {
    struct mem_page *next;
} mem_page;

/* 每线程私有堆 */
typedef struct mem_heap {
    mem_block        *freelist[MEM_NCLASS]; /* 本地空闲链（LIFO，无锁） */
    void             *thread_free;          /* 跨线程释放 MPSC 链表（原子） */
    mem_page         *pages;                /* 本堆所有页（teardown 用） */
    struct mem_heap  *gnext;               /* 全局堆注册表链接 */
    size_t            abandoned;            /* 1=线程已退出、堆待新线程抢占复用（原子标志） */
    /* ---- 统计计数（仅本堆所属线程读写，无锁；mem_stat 为跨线程近似快照） ---- */
    size_t            st_reserved;          /* 本堆向 OS 申请的页字节总量 */
    size_t            st_live;              /* 当前已分配可用字节（usable 口径） */
    size_t            st_peak;              /* 本堆 st_live 历史峰值 */
    size_t            st_count;             /* 当前活跃（未归还）块数 */
    size_t            st_allocs;            /* 累计分配次数 */
    size_t            st_frees;             /* 累计归还次数 */
} mem_heap;

static TLS mem_heap *t_heap = NULL;         /* 当前线程堆（懒创建） */
static void         *g_heaps = NULL;        /* 全局堆注册表栈（原子，gnext 串联） */

/* ---------------- 大对象全局统计（旁路 malloc/free，跨线程，用原子计数） ---------------- */
static size_t g_large_reserved = 0;         /* 活跃大对象向 OS 申请字节（含对象头） */
static size_t g_large_live     = 0;         /* 活跃大对象可用字节（用户口径） */
static size_t g_large_peak     = 0;         /* 活跃大对象可用字节历史峰值 */
static size_t g_large_count    = 0;         /* 活跃大对象块数 */
static size_t g_large_allocs   = 0;         /* 累计大对象分配次数 */
static size_t g_large_frees    = 0;         /* 累计大对象归还次数 */

/* 大对象 / 超对齐对象统计（reserved 取实际向 OS 申请字节，usable 取用户口径） */
static inline void mem_stat_big_add(size_t reserved, size_t usable) {
    sc_inc(&g_large_reserved, reserved);
    size_t now = sc_inc(&g_large_live, usable);             /* 返回加后新值 */
    sc_inc(&g_large_count, (size_t)1);
    sc_inc(&g_large_allocs, (size_t)1);
    size_t cur = sc_get(&g_large_peak);                     /* CAS-max 更新峰值 */
    while (now > cur && !sc_test_and_set_acq(&g_large_peak, &cur, now)) { /* cur 已被刷新 */ }
}
static inline void mem_stat_big_del(size_t reserved, size_t usable) {
    sc_inc(&g_large_reserved, ~reserved + 1);              /* 模减 */
    sc_inc(&g_large_live, ~usable + 1);
    sc_inc(&g_large_count, ~(size_t)0);                     /* -1 */
    sc_inc(&g_large_frees, (size_t)1);
}
static inline void mem_stat_large_add(size_t usable) { mem_stat_big_add(MEM_HDR + usable, usable); }
static inline void mem_stat_large_del(size_t usable) { mem_stat_big_del(MEM_HDR + usable, usable); }

/* ---------------- 线程退出：废弃堆回收（abandon / reclaim） ----------------
 * 线程结束时把其私有堆标记为 abandoned，新线程可抢占复用——避免死线程堆永驻、
 * 以及其上未并回的跨线程释放永不被消费。g_heaps 为只增注册栈（永不弹出），
 * 故抢占仅需 CAS 标志 1→0，无 ABA。自动触发：POSIX 用 pthread_key 析构器，Windows 用 FLS 回调。
 */
static void mem_thread_exit(void *p);       /* 前置声明 */

#if P_WIN
static DWORD g_tls_fls = FLS_OUT_OF_INDEXES;
static void WINAPI mem_fls_cb(void *p) { mem_thread_exit(p); }
static void mem_key_init(void) {
    if (g_tls_fls == FLS_OUT_OF_INDEXES) {
        DWORD idx = FlsAlloc(mem_fls_cb);
        if (idx != FLS_OUT_OF_INDEXES) g_tls_fls = idx;
    }
}
static void mem_key_set(void *p) { if (g_tls_fls != FLS_OUT_OF_INDEXES) FlsSetValue(g_tls_fls, p); }
#else
static pthread_key_t  g_tls_key;
static pthread_once_t g_tls_once = PTHREAD_ONCE_INIT;
static void mem_key_make(void) { pthread_key_create(&g_tls_key, mem_thread_exit); }
static void mem_key_init(void) { pthread_once(&g_tls_once, mem_key_make); }
static void mem_key_set(void *p) { pthread_setspecific(g_tls_key, p); }
#endif

/* 线程退出回调：把本线程堆标记为可被新线程抢占复用（release 交接） */
static void mem_thread_exit(void *p) {
    mem_heap *h = (mem_heap*)p;
    if (h) sc_set_rel(&h->abandoned, (size_t)1);
}

/* ---------------- size-class 映射（纯函数，无需运行时表） ----------------
 * 档位布局（均为 16 的倍数）：
 *   idx 0..7 : 16,32,...,128            （步长 16）
 *   idx >=8  : 每个 2 的幂区间 (base,2*base] 均分 4 档，base 自 128 起
 *              即 160,192,224,256 / 320,384,448,512 / ... / 40960,49152,57344,65536
 */
static inline unsigned mem_bitwidth(uint32_t n) {  /* = floor(log2(n)) + 1，n>=1 */
#if defined(__GNUC__) || defined(__clang__)
    return 32u - (unsigned)__builtin_clz(n);
#else
    unsigned w = 0; while (n) { w++; n >>= 1; } return w;
#endif
}

/* size(1..65536) → class 序号(0..43) */
static inline unsigned mem_size2class(size_t size) {
    if (size <= 128) return (unsigned)((size + 15) >> 4) - 1;  /* size>=1 */
    unsigned e = mem_bitwidth((uint32_t)(size - 1));           /* ceil_log2(size) */
    size_t base = (size_t)1 << (e - 1);                        /* 区间下界 */
    size_t step = base >> 2;                                   /* >=32 */
    unsigned k = (unsigned)((size - 1 - base) / step);         /* 0..3 */
    return 8u + (e - 8u) * 4u + k;
}

/* class 序号 → 该档对象字节数 */
static inline size_t mem_classsize(unsigned idx) {
    if (idx < 8) return (size_t)(idx + 1) << 4;                /* 16..128 */
    unsigned g = (idx - 8) / 4;                                /* 区间编号 */
    unsigned k = (idx - 8) % 4;                                /* 档内序号 */
    size_t base = (size_t)128 << g;
    return base + (size_t)(k + 1) * (base >> 2);
}

/* ---------------- 堆获取与跨线程并回 ---------------- */

/* 把堆 h 绑定为当前线程堆，并登记到线程退出回调 */
static void mem_bind_heap(mem_heap *h) {
    t_heap = h;
    mem_key_init();
    mem_key_set(h);
}

static void mem_drain(mem_heap *h);         /* 前置声明 */

static mem_heap *mem_get_heap(void) {
    mem_heap *h = t_heap;
    if (h) return h;
    /* 1) 先试抢占一个被废弃的堆（死线程遗留）：CAS abandoned 1→0，成功即归我所有 */
    for (mem_heap *p = (mem_heap*)sc_get_acq(&g_heaps); p; p = p->gnext) {
        size_t expect = 1;
        if (sc_get(&p->abandoned) == 1 &&
            sc_test_and_set_acq(&p->abandoned, &expect, (size_t)0)) {
            mem_bind_heap(p);
            mem_drain(p);                                     /* 并回其遗留的跨线程释放 */
            return p;
        }
    }
    /* 2) 无可抢占则新建 */
    h = (mem_heap*)calloc(1, sizeof(mem_heap));
    if (!h) return NULL;
    void *old = sc_get(&g_heaps);                             /* 注册入全局表（一次 CAS） */
    do { h->gnext = (mem_heap*)old; } while (!sc_test_and_set_rel(&g_heaps, &old, h));
    mem_bind_heap(h);
    return h;
}

/* 取走 thread_free 整链，按 class 并回本地空闲链（单消费者：本堆所属线程） */
static void mem_drain(mem_heap *h) {
    void *listv = sc_get_and_set_acq(&h->thread_free, NULL);   /* exchange acquire */
    mem_block *b = (mem_block*)listv;
    while (b) {
        mem_block *nx = (mem_block*)b->owner;                  /* MPSC 链接存于 owner */
        unsigned cls = (unsigned)b->info;
        b->owner = h->freelist[cls];
        h->freelist[cls] = b;
        h->st_live  -= mem_classsize(cls);                    /* 跨线程释放并回时计入统计 */
        h->st_count -= 1;
        h->st_frees += 1;
        b = nx;
    }
}

/* 向 OS 申请一页，切成 cls 档多块压入本地空闲链；成功返回 1 */
static int mem_refill(mem_heap *h, unsigned cls) {
    size_t csz   = mem_classsize(cls);
    size_t blksz = MEM_HDR + csz;
    size_t navail = MEM_PAGE_BYTES > (sizeof(mem_page) + blksz)
                  ? (MEM_PAGE_BYTES - sizeof(mem_page)) / blksz : 1;
    if (navail < 1) navail = 1;
    /* +MEM_ALIGN 余量供首块对齐 */
    size_t total = sizeof(mem_page) + navail * blksz + MEM_ALIGN;
    mem_page *pg = (mem_page*)malloc(total);
    if (!pg) return 0;
    pg->next = h->pages;
    h->pages = pg;
    h->st_reserved += total;                                  /* 计入向 OS 申请的字节 */
    uintptr_t start = MEM_ALIGN_UP((uintptr_t)pg + sizeof(mem_page), MEM_ALIGN);
    for (size_t i = 0; i < navail; i++) {
        mem_block *b = (mem_block*)(start + i * blksz);
        b->info  = cls;
        b->owner = h->freelist[cls];
        MEM_MARK_FREE(b);                                     /* 初始均为空闲态 */
        h->freelist[cls] = b;
    }
    return 1;
}

/* ---------------- 小对象分配 / 大对象直通 ---------------- */

static void *mem_alloc_small(size_t size) {
    unsigned cls = mem_size2class(size);
    mem_heap *h = mem_get_heap();
    if (!h) return NULL;
    mem_block *b = h->freelist[cls];
    if (!b) {
        mem_drain(h);                                          /* 先并回跨线程释放 */
        b = h->freelist[cls];
        if (!b) {
            if (!mem_refill(h, cls)) return NULL;              /* 再向 OS 要一页 */
            b = h->freelist[cls];
        }
    }
    h->freelist[cls] = (mem_block*)b->owner;                   /* 弹出 */
    b->owner = h;                                              /* 标记物主 */
    b->info  = cls;
    MEM_MARK_ALLOC(b);
    h->st_live   += mem_classsize(cls);                       /* 统计：分配 */
    if (h->st_live > h->st_peak) h->st_peak = h->st_live;     /* 本堆峰值水位（本地，无原子） */
    h->st_count  += 1;
    h->st_allocs += 1;
    return (uint8_t*)b + MEM_HDR;
}

static void *mem_alloc_large(size_t size) {
    mem_block *b = (mem_block*)malloc(MEM_HDR + size);
    if (!b) return NULL;
    b->owner = MEM_LARGE_OWNER;
    b->info  = size;
    MEM_MARK_ALLOC(b);
    mem_stat_large_add(size);                                 /* 统计：大对象分配 */
    return (uint8_t*)b + MEM_HDR;
}

/* 超对齐分配：align 须为 2 的幂且 > MEM_ALIGN；独立哨兵路径（不池化，类大对象直通）。
 * 布局 [raw_base void*][align size_t][mem_block hdr][payload(align 对齐)]，
 * 头前两词保存原始 malloc 基址与对齐，供 recycle/refit 还原。 */
static size_t mem_aligned_front(void) { return sizeof(void*) + sizeof(size_t) + MEM_HDR; }

static void *mem_alloc_aligned(size_t size, size_t align) {
    size_t front = mem_aligned_front();
    size_t total = (align - 1) + front + size;
    uint8_t *raw = (uint8_t*)malloc(total);
    if (!raw) return NULL;
    uintptr_t payload = MEM_ALIGN_UP((uintptr_t)raw + front, align);
    mem_block *b = (mem_block*)(payload - MEM_HDR);
    *((size_t*)((uint8_t*)b - sizeof(size_t)))                 = align;  /* 紧邻头前：对齐 */
    *((void**)((uint8_t*)b - sizeof(size_t) - sizeof(void*)))  = raw;    /* 再前：原始基址 */
    b->owner = MEM_ALIGNED_OWNER;
    b->info  = size;
    MEM_MARK_ALLOC(b);
    mem_stat_big_add(total, size);
    return (void*)payload;
}

static size_t mem_aligned_total(mem_block *b) {                /* 还原该对齐块的 malloc 总字节 */
    size_t align = *((size_t*)((uint8_t*)b - sizeof(size_t)));
    return (align - 1) + mem_aligned_front() + b->info;
}

/* ---------------- 对外接口 ---------------- */

void *chunk(uint64_t size) {
    size_t s = (size_t)size;
    if (s == 0) s = 1;
    return (s > MEM_SMALL_MAX) ? mem_alloc_large(s) : mem_alloc_small(s);
}

void *chunk0(uint64_t size) {
    void *p = chunk(size);
    if (p && size) memset(p, 0, (size_t)size);
    return p;
}

void *chunk_array(uint64_t count, uint64_t size) {
    if (count != 0 && size > (uint64_t)SIZE_MAX / count) return NULL;  /* count*size 溢出 */
    return chunk0(count * size);                                       /* 0 由 chunk0 视为 1 */
}

void *chunk_aligned(uint64_t size, uint64_t align) {
    size_t a = (size_t)align;
    if (a <= MEM_ALIGN) return chunk(size);          /* 默认返回值已 16 对齐 */
    if (a & (a - 1)) return NULL;                     /* align 必须是 2 的幂 */
    size_t s = (size_t)size ? (size_t)size : 1u;
    if (a - 1 > SIZE_MAX - mem_aligned_front() - s) return NULL;  /* 防总量溢出 */
    return mem_alloc_aligned(s, a);
}

void recycle(void *p) {
    if (!p) return;
    mem_block *b = (mem_block*)((uint8_t*)p - MEM_HDR);
    if (b->owner == MEM_LARGE_OWNER) {                        /* 大对象直通 */
        MEM_CHECK_ALLOC(b, p); MEM_MARK_FREE(b);
        mem_stat_large_del(b->info); free(b); return;
    }
    if (b->owner == MEM_ALIGNED_OWNER) {                      /* 超对齐对象直通 */
        MEM_CHECK_ALLOC(b, p); MEM_MARK_FREE(b);
        size_t total = mem_aligned_total(b);
        void  *raw   = *((void**)((uint8_t*)b - sizeof(size_t) - sizeof(void*)));
        mem_stat_big_del(total, b->info); free(raw); return;
    }
    MEM_CHECK_ALLOC(b, p);
    mem_heap *owner = (mem_heap*)b->owner;
    if (owner == t_heap) {                                     /* 同线程：直接压回本地链 */
        unsigned cls = (unsigned)b->info;
        b->owner = owner->freelist[cls];
        MEM_MARK_FREE(b);
        owner->freelist[cls] = b;
        owner->st_live  -= mem_classsize(cls);                /* 统计：同线程归还 */
        owner->st_count -= 1;
        owner->st_frees += 1;
    } else {                                                   /* 跨线程：CAS 压入物主 MPSC 链 */
        MEM_MARK_FREE(b);
        void *old = sc_get(&owner->thread_free);
        do { b->owner = old; } while (!sc_test_and_set_rel(&owner->thread_free, &old, b));
    }
}

uint64_t mem_usable(void *p) {
    if (!p) return 0;
    mem_block *b = (mem_block*)((uint8_t*)p - MEM_HDR);
    if (b->owner == MEM_LARGE_OWNER || b->owner == MEM_ALIGNED_OWNER) return b->info;
    return mem_classsize((unsigned)b->info);
}

void *refit(void *p, uint64_t size) {
    if (!p) return chunk(size);
    if (size == 0) { recycle(p); return NULL; }
    mem_block *b = (mem_block*)((uint8_t*)p - MEM_HDR);
    size_t want = (size_t)size;

    if (b->owner == MEM_ALIGNED_OWNER) {                      /* 超对齐：重分配并保持对齐 */
        MEM_CHECK_ALLOC(b, p);
        size_t oldu  = b->info;
        size_t align = *((size_t*)((uint8_t*)b - sizeof(size_t)));
        if (want <= oldu) return p;                           /* 容量足够、对齐不变：原地 */
        void *np = mem_alloc_aligned(want, align);
        if (!np) return NULL;
        memcpy(np, p, oldu);
        recycle(p);
        return np;
    }

    if (b->owner == MEM_LARGE_OWNER) {
        MEM_CHECK_ALLOC(b, p);
        size_t oldu = b->info;
        if (want > MEM_SMALL_MAX) {                            /* 大→大：原地 realloc */
            mem_block *nb = (mem_block*)realloc(b, MEM_HDR + want);
            if (!nb) return NULL;
            nb->owner = MEM_LARGE_OWNER;
            nb->info  = want;
            sc_inc(&g_large_reserved, (size_t)want - (size_t)oldu);  /* 统计：模加/减容量差 */
            size_t now = sc_inc(&g_large_live, (size_t)want - (size_t)oldu);
            size_t cur = sc_get(&g_large_peak);              /* CAS-max 更新峰值 */
            while (now > cur && !sc_test_and_set_acq(&g_large_peak, &cur, now)) { }
            return (uint8_t*)nb + MEM_HDR;
        }
        void *np = chunk(want);                                /* 大→小：搬迁 */
        if (!np) return NULL;
        memcpy(np, p, want < oldu ? want : oldu);
        mem_stat_large_del(oldu);                              /* 统计：移除大对象（chunk 已计小对象） */
        free(b);
        return np;
    }

    /* 小对象 */
    MEM_CHECK_ALLOC(b, p);
    size_t oldu = mem_classsize((unsigned)b->info);
    if (want <= oldu) return p;                                /* 同档/更小：原地 */
    void *np = chunk(want);                                    /* 增大：搬迁 */
    if (!np) return NULL;
    memcpy(np, p, oldu);
    recycle(p);
    return np;
}

void mem_teardown(void) {
    void *hv = sc_get_and_set_ord(&g_heaps, NULL);            /* 摘下整张注册表 */
    mem_heap *h = (mem_heap*)hv;
    while (h) {
        mem_heap *gn = h->gnext;
        mem_page *pg = h->pages;
        while (pg) { mem_page *n = pg->next; free(pg); pg = n; }
        if (h == t_heap) { t_heap = NULL; mem_key_set(NULL); }  /* 清当前线程 TLS 与退出回调挂点 */
        free(h);
        h = gn;
    }
    /* 大对象全局计数同步清零（teardown 契约：所有线程静止且无未归还分配） */
    sc_set(&g_large_reserved, (size_t)0);
    sc_set(&g_large_live, (size_t)0);
    sc_set(&g_large_peak, (size_t)0);
    sc_set(&g_large_count, (size_t)0);
    sc_set(&g_large_allocs, (size_t)0);
    sc_set(&g_large_frees, (size_t)0);
}

/* 释放当前线程自身堆的全部池化页回 OS——仅当本线程无存活分配（st_count==0）时生效。
 * owner-only 操作，无锁且安全：先并回本堆跨线程释放；若仍有存活块则保守不动（
 * 无法在不追踪页占用的前提下判定哪些页全空）。返回释放的字节数。 */
uint64_t mem_trim(void) {
    mem_heap *h = t_heap;
    if (!h) return 0;
    mem_drain(h);                                            /* 先并回本堆跨线程释放 */
    if (h->st_count != 0) return 0;                          /* 仍有存活分配：保守不动 */
    size_t freed = h->st_reserved;
    mem_page *pg = h->pages;
    while (pg) { mem_page *n = pg->next; free(pg); pg = n; } /* st_count==0 → 所有页块均空闲 */
    h->pages = NULL;
    for (unsigned i = 0; i < MEM_NCLASS; i++) h->freelist[i] = NULL;
    sc_set(&h->thread_free, NULL);                           /* 无存活块 → 不会再有跨线程 push */
    h->st_reserved = 0;
    return (uint64_t)freed;
}

void mem_stat(mem_stat_t *out) {
    if (!out) return;
    size_t reserved = 0, live = 0, peak = 0, count = 0, allocs = 0, frees = 0;
    /* 小对象：遍历堆注册表汇总（快照；并发下各堆计数为近似值） */
    mem_heap *h = (mem_heap*)sc_get(&g_heaps);
    while (h) {
        reserved += h->st_reserved;
        live     += h->st_live;
        peak     += h->st_peak;                              /* 各线程峰值之和（单线程精确，多线程为上界） */
        count    += h->st_count;
        allocs   += h->st_allocs;
        frees    += h->st_frees;
        h = h->gnext;
    }
    /* 大对象：全局原子计数 */
    reserved += sc_get(&g_large_reserved);
    live     += sc_get(&g_large_live);
    peak     += sc_get(&g_large_peak);
    count    += sc_get(&g_large_count);
    allocs   += sc_get(&g_large_allocs);
    frees    += sc_get(&g_large_frees);
    out->reserved  = reserved;
    out->live      = live;
    out->peak_live = peak;
    out->count     = count;
    out->allocs    = allocs;
    out->frees     = frees;
}

/* ---------------- arena：区域分配器 ---------------- */

typedef struct arena_blk {
    struct arena_blk *next;
    size_t            used;
    size_t            cap;
    size_t            _pad;     /* 头补齐到 MEM_ALIGN 的倍数（64 位 32B / 32 位 16B） */
} arena_blk;

typedef struct arena_impl {
    arena_blk *head;            /* 最新块 */
    size_t     deflt;          /* 单块默认容量 */
} arena_impl;

void arena_init(arena *a, uint64_t cap) {
    arena_impl *ai = (arena_impl*)malloc(sizeof(arena_impl));
    a->h = ai;
    if (!ai) return;
    ai->head  = NULL;
    ai->deflt = cap ? (size_t)cap : MEM_ARENA_DEF;
}

void *arena_chunk(arena *a, uint64_t size) {
    arena_impl *ai = (arena_impl*)a->h;
    if (!ai) return NULL;
    size_t sz = (size_t)size;
    arena_blk *b = ai->head;
    if (b) {
        uintptr_t raw   = (uintptr_t)(b + 1) + b->used;
        uintptr_t algn  = MEM_ALIGN_UP(raw, MEM_ALIGN);
        size_t    skip  = (size_t)(algn - (uintptr_t)(b + 1));   /* 对齐后的新 used */
        if (skip + sz <= b->cap) {
            b->used = skip + sz;
            return (void*)algn;
        }
    }
    /* 需新块 */
    {
        size_t cap = sz > ai->deflt ? sz : ai->deflt;
        arena_blk *nb = (arena_blk*)malloc(sizeof(arena_blk) + cap + MEM_ALIGN);
        if (!nb) return NULL;
        nb->cap  = cap + MEM_ALIGN;
        nb->next = ai->head;
        ai->head = nb;
        uintptr_t algn = MEM_ALIGN_UP((uintptr_t)(nb + 1), MEM_ALIGN);
        nb->used = (size_t)(algn - (uintptr_t)(nb + 1)) + sz;
        return (void*)algn;
    }
}

void arena_reset(arena *a) {
    arena_impl *ai = (arena_impl*)a->h;
    if (!ai) return;
    arena_blk *b = ai->head;
    if (!b) return;
    arena_blk *rest = b->next;
    while (rest) { arena_blk *n = rest->next; free(rest); rest = n; }
    b->next = NULL;
    b->used = 0;
    ai->head = b;
}

void arena_drop(arena *a) {
    arena_impl *ai = (arena_impl*)a->h;
    if (!ai) return;
    arena_blk *b = ai->head;
    while (b) { arena_blk *n = b->next; free(b); b = n; }
    free(ai);
    a->h = NULL;
}

/* ---------------- shm：跨进程命名共享内存 ----------------
 * POSIX  ：shm_open(O_CREAT|O_RDWR) + ftruncate + mmap(MAP_SHARED)
 * Windows：OpenFileMappingA / CreateFileMappingA(INVALID_HANDLE_VALUE) + MapViewOfFile
 * h 指向 malloc 的 shm_priv（映射地址 + 容量 + 平台句柄）。容量向上取整到页。
 */
typedef struct shm_priv {
    void  *addr;             /* 本进程映射首地址 */
    size_t size;             /* 映射字节数（页对齐容量） */
#if P_WIN
    HANDLE handle;           /* 文件映射对象句柄 */
#else
    int    fd;              /* 共享内存对象 fd（mmap 后保留以便对称关闭） */
#endif
} shm_priv;

static size_t shm_pagesize(void) {
#if P_WIN
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize ? (size_t)si.dwPageSize : 4096u;
#else
    long ps = sysconf(_SC_PAGESIZE);
    return ps > 0 ? (size_t)ps : 4096u;
#endif
}

#if !P_WIN
/* POSIX 名字规范化：加前导 '/'，丢弃内嵌 '/'；截断到安全长度 */
static void shm_posix_name(const char *in, char *out, size_t cap) {
    size_t j = 0;
    if (cap == 0) return;
    out[j++] = '/';
    for (size_t i = 0; in[i] && j + 1 < cap; i++) {
        if (in[i] == '/') continue;
        out[j++] = in[i];
    }
    out[j] = '\0';
}
#endif

uint8_t shm_make(shm *self, const char *name, uint64_t size, uint32_t flags) {
    if (!self || !name) return 0;
    self->h = NULL;

    int rdonly = (flags & SHM_RDONLY) != 0;
    int excl   = (flags & SHM_EXCL) != 0;

    size_t want   = size ? (size_t)size : 1u;
    size_t usable = (size_t)MEM_ALIGN_UP(want, shm_pagesize());

#if P_WIN
    ULARGE_INTEGER cap;
    cap.QuadPart = (ULONGLONG)usable;
    DWORD mapAcc = rdonly ? FILE_MAP_READ : FILE_MAP_ALL_ACCESS;
    HANDLE h = NULL;
    if (!excl) h = OpenFileMappingA(mapAcc, FALSE, name);  /* 先尝试附着已存在 */
    if (!h) {
        if (rdonly && !excl) return 0;                     /* 只读附着失败：不创建 */
        h = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                               cap.HighPart, cap.LowPart, name);
        if (!h) return 0;
        if (excl && GetLastError() == ERROR_ALREADY_EXISTS) {  /* 独占创建：已存在则失败 */
            CloseHandle(h); return 0;
        }
    }
    void *addr = MapViewOfFile(h, mapAcc, 0, 0, 0);        /* bytesToMap=0：映射整段 */
    if (!addr) { CloseHandle(h); return 0; }
    shm_priv *pv = (shm_priv*)malloc(sizeof(shm_priv));
    if (!pv) { UnmapViewOfFile(addr); CloseHandle(h); return 0; }
    MEMORY_BASIC_INFORMATION mbi;                          /* 回报真实映射区容量 */
    size_t real = usable;
    if (VirtualQuery(addr, &mbi, sizeof(mbi))) real = (size_t)mbi.RegionSize;
    pv->addr = addr;
    pv->size = real;
    pv->handle = h;
    self->h = pv;
    return 1;
#else
    char nm[256];
    shm_posix_name(name, nm, sizeof(nm));

    int prot = rdonly ? PROT_READ : (PROT_READ | PROT_WRITE);
    int created = 0;
    int fd = -1;
    if (excl) {
        fd = shm_open(nm, O_CREAT | O_EXCL | O_RDWR, 0600);  /* 独占创建：已存在则失败 */
        if (fd < 0) return 0;
        created = 1;
    } else {
        int aflag = rdonly ? O_RDONLY : O_RDWR;
        fd = shm_open(nm, aflag, 0600);                      /* 先尝试附着已存在 */
        if (fd < 0) {
            if (rdonly) return 0;                            /* 只读且不存在：失败 */
            fd = shm_open(nm, O_CREAT | O_RDWR, 0600);        /* 不存在则创建 */
            if (fd < 0) return 0;
            created = 1;
        }
    }

    size_t map_len = usable;
    if (created) {
        if (ftruncate(fd, (off_t)usable) != 0) {
            close(fd);
            shm_unlink(nm);
            return 0;
        }
    } else {
        struct stat st;
        if (fstat(fd, &st) != 0) { close(fd); return 0; }
        if ((size_t)st.st_size < usable) {                   /* 已存在区容量不足 */
            close(fd);
            return 0;
        }
        map_len = (size_t)st.st_size;                        /* 回报真实底层容量 */
    }
    void *addr = mmap(NULL, map_len, prot, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        close(fd);
        if (created) shm_unlink(nm);
        return 0;
    }
    shm_priv *pv = (shm_priv*)malloc(sizeof(shm_priv));
    if (!pv) {
        munmap(addr, map_len);
        close(fd);
        if (created) shm_unlink(nm);
        return 0;
    }
    pv->addr = addr;
    pv->size = map_len;
    pv->fd = fd;                                       /* 保留 fd 至 drop（映射不依赖它，仅对称关闭） */
    self->h = pv;
    return 1;
#endif
}

void *shm_data(shm *self) {
    if (!self || !self->h) return NULL;
    return ((shm_priv*)self->h)->addr;
}

uint64_t shm_size(shm *self) {
    if (!self || !self->h) return 0;
    return (uint64_t)((shm_priv*)self->h)->size;
}

void shm_drop(shm *self) {
    if (!self || !self->h) return;
    shm_priv *pv = (shm_priv*)self->h;
#if P_WIN
    if (pv->addr) UnmapViewOfFile(pv->addr);
    if (pv->handle) CloseHandle(pv->handle);
#else
    if (pv->addr) munmap(pv->addr, pv->size);
    if (pv->fd >= 0) close(pv->fd);
#endif
    free(pv);
    self->h = NULL;
}

uint8_t shm_remove(const char *name) {
    if (!name) return 0;
#if P_WIN
    (void)name;                                        /* 内核对象随最后句柄关闭自动销毁 */
    return 1;
#else
    char nm[256];
    shm_posix_name(name, nm, sizeof(nm));
    return shm_unlink(nm) == 0 ? 1 : 0;
#endif
}
