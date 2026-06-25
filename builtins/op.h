/* op.h —— op.sc 语法机制的 C ABI 契约与默认运行时声明
 *
 * 角色：op.sc 是默认导入的「语言底层（语法层面）机制」声明模块；本头文件是其
 *       C 侧伴随头，声明这些机制需要的 C 结构体与运行时原型。scc 生成的每个 C
 *       单元都默认带入本头（经 platform.h），其实现由 builtins/op_impl.c 提供
 *       （编译器自动编译并链接，无需 inc）。
 *
 * 约定：
 *   - chain 不拥有元素：remove/pop/cut 等不释放元素指针本身
 *   - chain 元素为 sc 链表结构体（def T: ~ {}，首位有 void *_prev, *_next）
 */
#ifndef SC_OP_H
#define SC_OP_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 自动指针 T@（胖指针 + 引用图 + 释放点验证） ----------------
 * 见 builtins/auto_ptr.md。一次指针赋值是一条双向边，对两端各记一次账：
 *   in  = 入边数（被多少胖指针指向），由胖指针 tar 维护；
 *   out = 出边数（持有多少胖指针），由胖指针 own 维护。
 * 释放不变式：in==0 && out==0 才可释放（in>0 悬挂 / out>0 未清理）。
 *
 * 堆对象内存布局（T() 分配）：[ sc_ref 头 | pad | ===== T 实体 ===== ]
 *   头固定置于 malloc 块首，实体在 SC_REF_HDR 偏移处（统一过对齐，免去 per-type alignof）；
 *   故 (sc_ref*)((char*)obj - SC_REF_HDR) 即头，free 时还原块首。 */

typedef struct sc_ref {
    int32_t in;       /* 入边数（tar 维护；>0 释放=悬挂） */
    int32_t out;      /* 出边数（own 维护；>0 释放=未清理） */
    int32_t heap;     /* 1=堆对象（in→0&&out==0 可自动 free）/ 0=栈或全局（退域断言） */
    int32_t flags;    /* 标志位：bit0 SC_REF_ATOM=该对象 in/out 计数用原子 RMW（保持 16 字节） */
} sc_ref;

typedef struct sc_fat {
    void    *p;       /* 目标地址（首成员，可强转裸指针，&fat==&fat.p） */
    int32_t *tar;     /* 目标 sc_ref.in 地址（NULL=目标无头/不追踪） */
    int32_t *own;     /* 持有者 sc_ref.out 地址（哨兵见下；NULL=未初始化） */
} sc_fat;

/* 裸自动指针 @（类型擦除）：前 24B 与 sc_fat 同构（共同首序列，可 (sc_fat*) 复用 bind/unbind），
 * 尾随 dtor。dtor 在擦除点（T@/T() → @）由静态类型 T 填入（T_drop / NULL）；归零时读 dtor
 * 自析构，故通用容器无需知道具体类型即可正确释放。.p 存实体基址（同 T@，非 object@ 的 &_class），
 * 头由 tar 直接定位、回转 (T*).p 零偏移；不派发方法，故无 base 偏移问题。 */
typedef struct sc_afat {
    void    *p;
    int32_t *tar;
    int32_t *own;
    void   (*dtor)(void *);
} sc_afat;

#define SC_REF_HDR  16                  /* 堆对象头偏移（≥sizeof(sc_ref)，过 max_align 对齐） */
#define SC_REF_ATOM 1                   /* sc_ref.flags 位：对象引用计数用原子 RMW（T<atom>() 构造） */
#define SC_REF_CANARY 2                 /* sc_ref.flags 位：对象带越界 canary（--check=mem 注入头尾哨兵） */
#define SC_OWN_ROOT ((int32_t*)-1)      /* 栈/全局根指针：域退出自动拆 */
#define SC_OWN_RAW  ((int32_t*)-2)      /* 经裸 base / 显式转裸：不追踪 */
/* own 是否「真实持有者」（普通 out 地址）：排除 NULL/ROOT/RAW 哨兵
 * （哨兵 -1/-2 作无符号指针是最大地址，不能用 >0 判定） */
#define SC_OWN_REAL(ow) ((ow) != (int32_t*)0 && (ow) != SC_OWN_ROOT && (ow) != SC_OWN_RAW)

/* 取持有者对象头：own 指向 sc_ref.out，回退 offsetof 得头首址；
 * tar 指向 sc_ref.in（首成员）故 (sc_ref*)tar 即头。原子位据此读 flags。 */
#define SC_TAR_HDR(tr) ((sc_ref *)(tr))
#define SC_OWN_HDR(ow) ((sc_ref *)((char *)(ow) - offsetof(sc_ref, out)))

/* 释放点验证：in!=0 悬挂 / out!=0 未清理（追踪构建附 who 源码定位） */
void sc_ref_check(sc_ref *r, const char *who);
/* 胖指针入边归零回调：（堆对象）先调析构 dtor 让其清理子成员 → out 递减，再判定：
 * out==0 → 自动 free；out>0 → 报「未清理出边」（dtor 未清干净）。
 *   sc_fat_on_zero   —— 无析构（dtor=NULL）路径，等价 sc_fat_on_zero_d(f, NULL)。
 *   sc_fat_on_zero_d —— 由解绑点按目标静态类型 T 传入 T 的析构（void(*)(void*)）；
 *                       仅堆对象调 dtor（栈值对象自有退域 RAII drop，避免重复析构）。 */
void sc_fat_on_zero(sc_fat *f);
void sc_fat_on_zero_d(sc_fat *f, void (*dtor)(void *));

/* ---------------- 分配间接层 sc_alloc / sc_realloc / sc_free ----------------
 * 语言内核生成的堆对象（T() / T@ / 堆专属 NAME&）与 adt 缓冲（string/list）的
 * 分配·重分配·释放统一经此三件套，便于整体切换分配器。
 *   默认（未定义 SC_POOL）：宏直通 libc malloc/realloc/free —— 零开销。
 *     全生命周期静态堆对象用 libc 即可，速度与资源利用率俱佳。
 *   启用池化（编译期 -DSC_POOL，需链接 builtins/mem）：转发到 mem 的
 *     chunk/refit/recycle（size-class 每线程私有堆，减碎片、支持跨线程回收）。
 * 注：--check=mem（canary）与 SC_POOL 互斥——canary 路径自带块算术，恒走 libc。 */
#ifndef SC_POOL
#  define sc_alloc(n)       malloc((size_t)(n))
#  define sc_realloc(p, n)  realloc((p), (size_t)(n))
#  define sc_free(p)        free(p)
#else
void *sc_alloc(size_t n);
void *sc_realloc(void *p, size_t n);
void  sc_free(void *p);
#endif

/* ---------------- --check=mem 越界 canary（头尾哨兵 + 地址派生魔数） ----------------
 * 仅在 --check=mem 构建下，T__new_ref 把 ref 头堆对象扩成：
 *   [ 头哨兵 SC_CANARY | sc_ref 头 SC_REF_HDR | T 实体 | 尾哨兵 SC_CANARY ]
 *   头哨兵区前 16 字节 = { uintptr_t 魔数, uintptr_t 实体字节数 }（魔数定位、size 供尾哨兵寻址）；
 *   尾哨兵首 uintptr_t = 同一魔数。魔数 = 块首地址 ^ SALT（每块各异，memcpy 定值不可伪造）。
 * sc_ref 头仍在 (char*)实体 - SC_REF_HDR（绑定路径不变）；释放经 sc_canary_free 校验头尾再放整块。 */
#define SC_CANARY      16                                   /* 头/尾哨兵区字节（过 max_align，保 body 对齐） */
#define SC_CANARY_SALT ((uintptr_t)0x5343414e41525921ULL)   /* 'SCANARY!' 地址异或盐 */
static inline uintptr_t sc_canary_magic(const void *block) { return (uintptr_t)block ^ SC_CANARY_SALT; }
/* 带 canary 的 ref 头堆对象释放：校验头/尾哨兵（越界损坏则报 stderr），再 free 整块（含哨兵区）。 */
void sc_canary_free(sc_ref *r);

/* --check=mem 栈数组尾哨兵：函数内一维栈数组 `T buf[N]` 超额分配 SC_CANARY_ELEMS(T) 个尾元素，
 * 声明后 fill 填入地址派生模式，退域/return/break 处 check 校验尾区未被越界写破坏（报 stderr）。
 * 尾区紧贴有效元素，溢出先撞尾哨兵、就地拦截、不波及邻接对象；魔数随 buf 地址变化不可伪造。 */
#define SC_CANARY_ELEMS(T) ((SC_CANARY + sizeof(T) - 1) / sizeof(T))   /* 覆盖 SC_CANARY 字节所需的 T 元素数 */
/* 多维栈数组：仅外层维度超额若干「行」以覆盖 SC_CANARY 字节，INNER=内层各维元素积（一维取 1）。 */
#define SC_CANARY_OUTER(T, INNER) ((SC_CANARY + (INNER) * sizeof(T) - 1) / ((INNER) * sizeof(T)))
void sc_stack_canary_fill(unsigned char *p, size_t n, const void *anchor);
void sc_stack_canary_check(const unsigned char *p, size_t n, const void *anchor, const char *who);

/* --check=ptr 运行时指针/下标守卫：在解引用（*p / p->m）与指针下标处对裸指针做 nil 校验，
 * 在编译期已知维度的栈数组下标处做越界校验。命中即报 stderr 并 abort（致命，防 UB 继续扩散）。
 *   · __sc_ptr_check：p==NULL 时报「空指针解引用」并 abort，否则原样返回 p。
 *   · __sc_bound_check：idx<0 || idx>=len 时报「数组下标越界」并 abort，否则返回 idx。
 * SC_PTRCHK 用 __typeof__ 保型且仅求值一次（__typeof__ 操作数不求值，gcc/clang 通用）。 */
const void *__sc_ptr_check(const void *p, const char *who);
long        __sc_bound_check(long idx, long len, const char *who);
#define SC_PTRCHK(p, who)      ((__typeof__(p))__sc_ptr_check((const void *)(p), (who)))
#define SC_BOUNDCHK(i, n, who) (__sc_bound_check((long)(i), (long)(n), (who)))

/* 绑定一条边：目标.in++、持有者.out++（哨兵 own 跳过 out 记账）。
 * 原子性逐对象判定：读对象头 flags 的 SC_REF_ATOM 位选原子/普通 RMW（混合图天然正确）。 */
static inline void sc_fat_bind(sc_fat *f, void *tgt, sc_ref *tr, int32_t *ow) {
    f->p = tgt;
    f->tar = tr ? &tr->in : (int32_t *)0;
    f->own = ow;
    if (f->tar) {
        if (SC_TAR_HDR(f->tar)->flags & SC_REF_ATOM)
            __atomic_fetch_add(f->tar, 1, __ATOMIC_SEQ_CST);
        else (*f->tar)++;
    }
    if (SC_OWN_REAL(f->own)) {
        if (SC_OWN_HDR(f->own)->flags & SC_REF_ATOM)
            __atomic_fetch_add(f->own, 1, __ATOMIC_SEQ_CST);
        else (*f->own)++;
    }
}
/* 解绑一条边：目标.in--（触 0 → sc_fat_on_zero_d，传入目标类型析构）、持有者.out--；清空指针。
 * own 字段解绑后保留（供重绑复用），故 own-- 仅在「确有活动边」（f->p 非空）时执行，
 * 否则对已 = nil 的成员再次解绑会把 owner.out 误减为负。
 * dtor 为目标静态类型 T 的析构 T_drop（无则 NULL），由编译器在解绑点按静态类型填入。 */
static inline void sc_fat_unbind_d(sc_fat *f, void (*dtor)(void *)) {
    if (f->tar) {
        int32_t nv;
        if (SC_TAR_HDR(f->tar)->flags & SC_REF_ATOM)
            nv = __atomic_sub_fetch(f->tar, 1, __ATOMIC_SEQ_CST);
        else nv = --(*f->tar);
        if (nv == 0) sc_fat_on_zero_d(f, dtor);
    }
    if (SC_OWN_REAL(f->own) && f->p) {
        if (SC_OWN_HDR(f->own)->flags & SC_REF_ATOM)
            __atomic_fetch_sub(f->own, 1, __ATOMIC_SEQ_CST);
        else (*f->own)--;
    }
    f->p = (void *)0; f->tar = (int32_t *)0;
}
static inline void sc_fat_unbind(sc_fat *f) { sc_fat_unbind_d(f, (void (*)(void *))0); }

/* 裸自动指针 @ 绑定/解绑：复用 sc_fat 记账（前 24B 同构），dtor 单独随句柄存取。
 *   bind：擦除点由静态类型 T 填 dtor（T_drop / NULL）；
 *   unbind：归零时以句柄自带 dtor 析构（通用容器无需知道 T）。 */
static inline void sc_afat_bind(sc_afat *f, void *tgt, sc_ref *tr, int32_t *ow,
                                void (*dtor)(void *)) {
    sc_fat_bind((sc_fat *)f, tgt, tr, ow);
    f->dtor = dtor;
}
static inline void sc_afat_unbind(sc_afat *f) {
    sc_fat_unbind_d((sc_fat *)f, f->dtor);
}

/* ---------------- chain：侵入式双向链表 ----------------
 * 元素为 sc 链表结构体（def T: ~ {}，首位有 void *_prev, *_next）
 * 首元素 _prev = 尾元素（rear），尾元素 _next = NULL；不拥有元素
 * 导航：用内置 base(o), prev(o), next(o) 函数访问首真实成员和邻接节点 */

typedef struct chain {
    void    *head;     /* 首元素（空链为 NULL） */
} chain;

typedef int32_t (*chain_cmp)(void *a, void *b);       /* sort 比较回调：实参为元素节点首址（含注入 _prev/_next），sc 侧 (a: T&) 还原 */

void *chain_prev(void *it);                           /* 边界安全逻辑前驱：head→NULL（内置 prev(o) 后端） */
void  chain_append(chain *_this, void *it);           /* 队尾 */
void  chain_push(chain *_this, void *it);             /* 队首 */
void *chain_pop(chain *_this);                        /* 移除并返回首元素 */
void  chain_before(chain *_this, void *pos, void *it);
void  chain_after(chain *_this, void *pos, void *it);
void  chain_remove(chain *_this, void *it);
void *chain_first(chain *_this);
void *chain_last(chain *_this);
void  chain_revert(chain *_this);
void  chain_append_to(chain *_this, chain *dst);     /* 自身清空 */
void  chain_push_to(chain *_this, chain *dst);       /* 自身清空 */
void  chain_cut(chain *_this, void *from, void *to, chain *out);
void  chain_sort(chain *_this, chain_cmp cmp);       /* Simon Tatham 自底向上 O(n log n) 归并排序（稳定，原地不分配） */

/* ---------------- thread：线程（run 语句原语，语言内核） ----------------
 * thread 是 run 语句创建的线程实体。run 依赖它，故属语言内核（op.sc 默认导入、
 * op.h 默认带入每个 C 单元、op_impl.c 始终链接）——无需 inc。
 *   - 线程由 run 语句创建：编译器生成 thread_run 调用，单次
 *     malloc(sizeof(thread) + psize + 实现私有区)，rpc 参数 memcpy 到 thread
 *     对象紧随位置（t + 1）
 *   - out 非空 → joinable：*out 接收 thread*，须 thread_join 等待并回收（整块释放）
 *     out 为空 → detach：线程结束后自释放
 *   - id 由新线程自身填写（跨平台统一 tid），创建后立即读取可能尚未写入
 *   - h 为实现私有区指针（指向同块尾部），调用方不直接访问
 * pool 执行目标（pool_run）属多线程模块（mt.h，inc mt.sc）。 */

typedef struct thread {
    uint64_t id;   /* 跨平台统一线程 id（线程启动后由其自身填写） */
    void *h;       /* 实现私有区指针（同块分配） */
} thread;

/* run 语句原语：fn 为 rpc 实际函数（void name_rpc(struct name*)），
 * params/psize 为装填好的参数结构体；out 为空即 detach 自释放。
 * stack 为栈字节数（0=平台默认），prio 为优先级（0=默认，1..255 最佳努力映射）。
 * 返回 1 成功 / 0 失败（失败时 *out 置 NULL） */
uint8_t thread_run(void (*fn)(void *), const void *params, size_t psize, thread **out,
                   uint32_t stack, uint8_t prio);

void    thread_join(thread *_this);   /* 等待结束并回收（含 thread 对象本身） */

/* ---------------- future / async：单线程协作式异步机制 ----------------
 * 语言底层机制：future 类型 + async/await/done 关键字 + async_init/loop/final。
 * future 是异步结果句柄（类型擦除：result 为 void*）。含 await 的 rpc 被编译为
 * 状态机；async 把 rpc 调用登记进当前线程事件循环并返回 future&；done 标记 future
 * 就绪并唤醒等待者。
 *
 * 声明在此（默认带入每个 C 单元），其运行时实现亦属语言自有异步内核——见
 * builtins/op_impl.c（始终随工程编译链接，POSIX poll + 自管道 + pthread，不依赖
 * libuv）。故 future/async/await/done 无需 inc 任何模块即可用；async.sc 只额外提供
 * 叶子原语声明（如 delay）。
 *
 * 可 await 契约：任何自定义异步原语在"完成时"调 future_done 即可接入 await。
 * 线程安全：future_done 可被任意线程调用；运行时在锁内置位并经事件循环唤醒，
 *           所有状态机 resume 只在循环线程串行发生 → 生成的状态机代码无需加锁。 */

typedef struct future {
    int    ready;            /* 0=未就绪, 1=已就绪 */
    void  *result;           /* 类型擦除结果（标量经 intptr_t 往返） */
    void  *frame;            /* 等待者状态机帧（NULL=无人等待） */
    void (*resume)(void *);  /* 等待者恢复入口（状态机函数） */
    void  *next;             /* 就绪队列链接（运行时内部） */
    int    id;               /* future<ID> 事件 id（>=0=可派发；-1=无标签，仅协程 await 用） */
    void  *ctx;              /* future<ID>(ctx) 用户上下文：发起时挂、派发时经 future_ctx 取回 */
} future;

/* future 方法（op.sc 内 @def future 的成员函数，C 侧实现） */
void     future_init(future *_this);    /* future()：登记到当前事件循环（pending +1） */
uint8_t  future_ready(future *_this);   /* f.ready()：是否已就绪 */
void    *future_get(future *_this);     /* f.get()：取结果（调用点 : T& 强转还原） */
void    *future_ctx(future *_this);     /* f.ctx()：取发起时挂载的用户上下文（无则 NULL） */

/* 当前线程事件循环生命周期 */
void     async_init(void);              /* 建立事件循环 */
void     async_loop(void *proc);        /* 驱动事件循环至全部异步完成；proc=按 id 派发回调
                                         * （int (*)(int id, future *f)，返回<0 停循环），NULL=纯协程驱动 */
void     async_final(void);             /* 销毁事件循环 */

/* 运行时内部原语（编译器生成码与可 await 契约使用） */
future  *future_new(void);              /* 造未就绪 future（pending +1）：async 启动器内部用 */
void     future_done(future *f, void *result);  /* done 关键字：置就绪 + 唤醒（任意线程安全） */
/* await 握手（生成的状态机调用）：登记本帧为 waiter，返回是否已就绪。
 * 1=已就绪（不让出、直接续跑）；0=未就绪（保存状态后让出）。 */
uint8_t  future_await(future *f, void *frame, void (*resume)(void *));

/* op 层暴露给"异步功能库"（async 模块叶子原语生态）的钩子：
 *   op_timer_arm —— 基础定时器：在 ms 毫秒后兑现 f（done）。两后端各自实现
 *                   （poll=单调时钟截止表；libuv=uv_timer）。delay 即在其上构建。
 *   op_uv_loop   —— 取 op 层事件循环的 uv_loop_t*（仅 -DSCC_WITH_UV 时非 NULL）；
 *                   供网络/文件等 libuv 叶子原语直接挂句柄。poll 后端返回 NULL。 */
void     op_timer_arm(future *f, uint32_t ms);
void    *op_uv_loop(void);

/* ---------------- com / limit / ioq：设备通讯机制 ----------------
 * com 是机制框架：具体 io 依赖设备，由 com 的 read/write/error 每对象方法指针
 * （MethodPtr，C 侧为结构体函数指针字段）实现——非成员函数（伪类无派生）。
 * limit 是 com 的分身/切片，充当一次 read 的截止边界视图（com 默认 endless io）。
 * ioq 是读写缓存队列（自动膨胀的循环缓冲），com 提供 rq/wq（非 NULL）即支持异步 io。
 *
 * 声明在 op.sc（默认导入）；本头提供 C ABI 结构体与方法原型。limit、ioq 的方法
 * （limit_xxx、ioq_xxx）与通用收发框架的运行时实现属后续阶段（op_impl.c，不依赖 libuv）；
 * 具体设备 io 实现属可选模块（inc com.sc）。未调用即不链接。 */

typedef struct com   com;
typedef struct limit limit;
typedef struct ioq   ioq;

/* ioq：com 读写缓存队列（循环缓冲）
 * item 为连续一组值，依首值判类型：[size, buf] size≠0=io 缓冲（pull 执行 io）；
 *                                  [0, callback] size=0=io 完成回调地址。 */
struct ioq {
    struct com *com;         /* 所属 com */
    void   **_buf;           /* 循环缓冲存储（运行时分配，自动膨胀） */
    uint32_t _cap;           /* 容量（槽数） */
    uint32_t _head;          /* 队首索引 */
    uint32_t _tail;          /* 队尾索引 */
};

void  ioq_push(ioq *_this, void *buf, int32_t size);        /* 入队一段 io 缓冲 */
void  ioq_notify(ioq *_this, void *cb, void *data);         /* 入队一个完成回调（cb 为函数地址） */
void *ioq_pull(ioq *_this);                                 /* 取队首并执行 io（空则阻塞） */

/* limit：com 一次有界读视图（com 的分身/切片；首位为分身回指 _self，与注入对齐）。
 * 最小内核：框架只驱动确定的读流程，缓存/边界策略全在用户实现里。
 *   size   —— ending=NULL：定长大小；否则每次最多读取的 chunk（读满 size 触发 ending）
 *   len    —— 框架回写：已累计读取的字节计数
 *   data() —— MethodPtr（用户实现）：返回缓冲基址，框架以 data()+len 为写入起点
 *   ending —— MethodPtr（用户实现，默认 NULL）：动态截止判定。框架以本 limit 为接收者回调
 *              int32_t (*)(struct limit *_this)，>=0 命中（值=保留长度）/<0 继续；
 *              基址/已读长度经 _this 自取（_this->data(_this) / _this->len）。 */
struct limit {
    struct com *_self;       /* 分身回指本体 com（分身机制注入） */
    uint32_t size;           /* ending=NULL:定长大小；否则每次最大 chunk */
    uint32_t len;            /* 框架回写：累计读取计数 */
    void*    (*data)(struct limit *_this);   /* 返回缓冲基址（用户实现，MethodPtr） */
    int32_t  (*ending)(struct limit *_this); /* 动态截止（用户实现，MethodPtr，默认 NULL） */
};

/* read/write 返回码（与 op.sc 的 def io 对应）：
 *   <0      不可恢复错误，中断；  0  成功可继续；
 *   IO_AGAIN 异步挂起等待；       IO_EOF 读完（后续读按空完成）。 */
enum { IO_AGAIN = 1, IO_EOF = 2 };

/* 框架同步读流程：用 com 的 read 接口反复读入 limit 缓冲，按 size/ending 判截止，
 * 回写 limit.len。返回 IO_EOF（读完）/ 负数（不可恢复）/ IO_AGAIN（同步上下文报错信号）。
 * 驱动 com >> s（s 为 com[...] 句柄）；异步形态见下方 com_limit_read_async。 */
int32_t limit_read(struct com *_this, struct limit *s);

/* com：设备通讯端点。字段顺序与 op.sc 的 @def com 一致。
 * alloc/free/read/write/error 为每对象方法指针（首参为隐藏接收者 com*）。 */
struct com {
    void    *dev;            /* 设备句柄（设备相关） */
    struct ioq *rq;          /* 读队列（NULL=不支持异步读） */
    struct ioq *wq;          /* 写队列（NULL=不支持异步写） */
    struct limit *(*alloc)(struct com *_this, uint32_t size, void *ending);  /* 分身构造 → limit& */
    void   (*free)(struct com *_this, struct limit *s);                     /* 分身析构 */
    int32_t (*read)(struct com *_this, void *data, uint32_t *size);         /* 设备读 */
    int32_t (*write)(struct com *_this, void *buf, uint32_t *size);         /* 设备写 */
    int32_t (*error)(struct com *_this);                                    /* 错误回调 */
    int32_t (*readable)(struct com *_this, void **id);  /* 读就绪查询：*id=可监听句柄（nil=不支持多路复用，转看返回值） */
    int32_t (*writable)(struct com *_this, void **id);  /* 写就绪查询（语义对称，见 op.sc 契约） */
    int32_t (*close)(struct com *_this);                /* 关闭设备：释放底层资源（nil=无需关闭，OS 回收） */
};

/* com 异步收发桥接：rpc 体内 com >> v / com << v 由编译器整合 await 时生成对其的调用，
 * 各产出一个 future（io 完成时兑现）。实现属语言自有异步内核（op_impl.c，始终链接）：
 * 把请求登记进活动表，由 async_loop/async_io 的 poll 循环在设备就绪时驱动收发并兑现。 */
future *com_read_async (struct com *_this, void *data, uint32_t size);   /* 异步读 → future */
future *com_write_async(struct com *_this, void *buf,  uint32_t size);   /* 异步写 → future */

/* com[...] 句柄异步有界读（【E】）：以 limit_read 的框架确定读循环驱动 com >> s，遇
 * IO_AGAIN 挂起、待设备就绪后续读，直至 ending/定长命中再 future_done 兑现（结果=
 * limit.len；出错则为负返回码）。实现属语言自有异步内核（op_impl.c，始终链接）。 */
future *com_limit_read_async(struct com *_this, struct limit *s);

/* com 设备 io 就绪事件循环（与 async_loop 正交）：据 com.readable/writable 探测就绪
 * （多路复用句柄 → poll；否则轮询返回值），就绪后执行 io 并 future_done 兑现。
 * 多路复用后端为语言自有 POSIX poll 实现（op_impl.c，不依赖 libuv）。详见 op.sc。 */
void     async_io(void);

/* ---------------- print：日志输出（语言关键字） ----------------
 * print 关键字 → 编译器生成 print 调用（首参为 u1 通道 chn，默认 0）。
 * print 属语言内核：声明在此（默认带入每个 C 单元），运行时实现在 op_impl.c
 * （始终随工程编译链接）——无需 inc。
 *   - chn：日志通道（透传），chn==0 为默认通道；F/E/W/I/D/V 级别与通道正交
 *   - 特例：<chn> 为 string 变量时编译器改生成 string_printf（追加进该串，不走本函数）
 *   - fmt 前缀 "X:"（X ∈ FEWIDV）指定日志级别，无前缀默认 D（调试）
 *   - 输出 stdout：HH:MM:SS.mmm L| 文本（chn!=0 时加 通道标记；自动补换行）
 *   - 级别过滤：环境变量 SC_LOG=F/E/W/I/D/V（默认 D），首次调用时读取 */
void print(uint8_t chn, const char *fmt, ...);

/* ---------------- stringify：JSON 格式化关键字选项 ----------------
 * stringify<选项>(值[, 缓存, 大小]) 由编译器按静态类型生成格式化器（写入独立
 * stringify.h，依赖 adt string）。生成的格式化器签名携带 stringify_t 选项参数，
 * 调用点据 stringify<key:val> 构造之。
 *   - compact:1 → 紧凑单行 {"x":3,"y":4}
 *   - 默认（compact:0）→ 多行美化（2 空格逐层缩进） */
typedef struct stringify {
    uint8_t compact;                /* 1 → 紧凑单行；0 → 多行美化（默认） */
} stringify_t;

#ifdef __cplusplus
}
#endif

#endif /* SC_OP_H */
