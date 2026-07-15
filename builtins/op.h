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

#include "platform.h"   /* 跨平台原子 RMW（sc_get_and_inc_ord / sc_inc_ord 等，三平台分支）；
                          * platform.h 末尾回带 op.h，靠 SC_OP_H/SC_PLATFORM_H 双护栏消解循环 */

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

/* 瘦自动指针 T*（真瘦，24B）：前 16B（p,tar）与 sc_fat/sc_afat 同构，尾随 dtor。
 * 只统计目标入边 tar->in，不带/不统计持有者出边 own——故不参与「未清出边」校验。
 * dtor 由静态类型 T 填（T_drop / NULL，裸 @^ 在擦除点填），归零时自析构故通用容器无需知 T。
 * 与 sc_fat（24B {p,tar,own}）/ sc_afat（32B {p,tar,own,dtor}）互转：拷 p/tar，各按己方记账。 */
typedef struct sc_thin {
    void    *p;
    int32_t *tar;
    void   (*dtor)(void *);
} sc_thin;

#define SC_REF_HDR  16                  /* 堆对象头偏移（≥sizeof(sc_ref)，过 max_align 对齐） */
#define SC_REF_ATOM 1                   /* sc_ref.flags 位：对象引用计数用原子 RMW（T<atom>() 构造） */
#define SC_REF_CANARY 2                 /* sc_ref.flags 位：对象带越界 canary（--check=mem 注入头尾哨兵） */
#define SC_REF_RAW    4                 /* sc_ref.flags 位：T<raw>() 裸分配（sc_alloc/libc），释放走 sc_free */
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

/* ---------------- 自动指针风味互转（sc_fat / sc_afat / sc_thin） ----------------
 * 替代 codegen 旧用的 GNU 语句表达式 ({ T _t = e; (R){...}; })，使生成代码在 MSVC 等
 * 不支持 ({...}) 扩展的编译器上同样可编译。操作数 e 经实参一次求值（与语句表达式
 * 「临时绑定后取字段」语义等同）。含 off 的变体按运行时 dtor 是否等于 objdrop
 * （object 风味判定）补/减 _class 偏移；objdrop 为各单元静态 sc_obj_drop（按实参传入，
 * 避免本头依赖该 per-unit 静态符号）。 */
static inline sc_thin sc_fat_as_thin(sc_fat e, void (*d)(void *)) {
    sc_thin r; r.p = e.p; r.tar = e.tar; r.dtor = d; return r;
}
static inline sc_thin sc_fat_as_thin_addoff(sc_fat e, size_t off, void (*d)(void *)) {
    sc_thin r; r.p = (void *)((char *)e.p + off); r.tar = e.tar; r.dtor = d; return r;
}
static inline sc_fat sc_thin_as_fat(sc_thin t) {
    sc_fat r; r.p = t.p; r.tar = t.tar; r.own = SC_OWN_RAW; return r;
}
static inline sc_fat sc_thin_as_fat_suboff(sc_thin t, size_t off, void (*objdrop)(void *)) {
    sc_fat r; r.p = (t.dtor == objdrop) ? (void *)((char *)t.p - off) : t.p;
    r.tar = t.tar; r.own = SC_OWN_RAW; return r;
}
static inline sc_afat sc_fat_as_afat(sc_fat e, void (*d)(void *)) {
    sc_afat r; r.p = e.p; r.tar = e.tar; r.own = e.own; r.dtor = d; return r;
}
static inline sc_afat sc_fat_as_afat_addoff(sc_fat e, size_t off, void (*d)(void *)) {
    sc_afat r; r.p = (void *)((char *)e.p + off); r.tar = e.tar; r.own = e.own; r.dtor = d; return r;
}
static inline sc_fat sc_fat_suboff(sc_fat e, size_t off) {
    sc_fat r; r.p = (void *)((char *)e.p - off); r.tar = e.tar; r.own = e.own; return r;
}
static inline sc_fat sc_afat_as_fat(sc_afat a) {
    sc_fat r; r.p = a.p; r.tar = a.tar; r.own = a.own; return r;
}
static inline sc_fat sc_afat_as_fat_suboff(sc_afat a, size_t off, void (*objdrop)(void *)) {
    sc_fat r; r.p = (a.dtor == objdrop) ? (void *)((char *)a.p - off) : a.p;
    r.tar = a.tar; r.own = a.own; return r;
}
/* 裸 @ 还原 object@ 的 --check=ref 守卫：源非 object 风味（dtor!=objdrop）则报错退出，
 * 否则原样返回（供 sc_afat_as_fat 取字段）。实现见 op_impl.c。 */
sc_afat __sc_afat_objck(sc_afat a, void (*objdrop)(void *));

/* ---------------- 分配间接层 sc_alloc / sc_realloc / sc_free ----------------
 * 语言内核生成的堆对象（T() / T@ / 堆专属 NAME&）与 adt 缓冲（string/list）的
 * 分配·重分配·释放统一经此三件套，便于整体切换分配器。
 *   默认（未定义 SC_POOL）：宏直通 libc malloc/realloc/free —— 零开销。
 *     全生命周期静态堆对象用 libc 即可，速度与资源利用率俱佳。
 *   启用池化（编译期 -DSC_POOL，需链接 builtins/mem）：转发到 mem 的
 *     chunk/refit/recycle（size-class 每线程私有堆，减碎片、支持跨线程回收）。
 * 注：--check=mem（canary）与 SC_POOL 互斥——canary 路径自带块算术，恒走 libc。
 *   胖 T@ 默认经 sc_chunk 池化（自带 MEM_DEBUG 尾金丝雀，--check=mem 联动开启）；
 *   仅 T<raw>() 退化经本间接层（sc_alloc）分配、sc_free 释放。 */
#ifndef SC_POOL
#  define sc_alloc(n)       malloc((size_t)(n))
#  define sc_realloc(p, n)  realloc((p), (size_t)(n))
#  define sc_free(p)        free(p)
#else
void *sc_alloc(size_t n);
void *sc_realloc(void *p, size_t n);
void  sc_free(void *p);
#endif

/* ---------------- 堆专属对象的安全析构 ----------------
 * sc_ptr_drop：对一个普通堆指针执行「用户 drop（若有）+ sc_free」，nil 是空操作。
 * sc_ptr_drop_slot：在执行前把调用方的指针槽位置为 NULL，再析构对象；用于编译器
 * 发出的 p->drop()，因此显式析构、T@1 退域析构和条件分支可以安全汇合，不会二次释放。
 * slot 通过 memcpy 读写，避免把任意 T** 强转为 void** 后违反 C 的严格别名规则。 */
static inline void sc_ptr_drop(void *p, void (*drop)(void *)) {
    if (!p) return;
    if (drop) drop(p);
    sc_free(p);
}
static inline void sc_ptr_drop_slot(void *slot, void (*drop)(void *)) {
    if (!slot) return;
    void *p = NULL;
    memcpy(&p, slot, sizeof(p));
    if (!p) return;
    void *nil = NULL;
    memcpy(slot, &nil, sizeof(nil));
    sc_ptr_drop(p, drop);
}

/* ---------------- 确定性池化分配 sc_chunk / sc_chunk0 / sc_refit / sc_recycle ----------------
 * 与 sc_alloc/sc_free（随 SC_POOL 在 libc↔mem 间切换）不同，本组恒走 mem 池
 * （chunk/chunk0/refit/recycle）——不受 SC_POOL 开关影响，始终池化。完整对应
 * malloc/calloc/realloc/free 四件套，故除「短命高频」联合节点外，亦可承载需「恒池化」的
 * 长生命周期可增长 / 须清零结构（如 tok 运行时的句柄、依赖记录、动态邻接表）：这类对象
 * 数量多、尺寸小、进程生命周期，走池化分配减碎片、免散落 libc 调用。
 * 典型短命用例：rpc 传参的联合节点（run 线程 / pool 任务 / queue 消息 / promise 各自
 * [节点][rpc 参数]，每次调用一份、随即回收）。
 * 实现见 op_impl.c（转发 mem chunk/chunk0/refit/recycle）；op 单元恒隐式依赖 mem
 * （编译器自动纳入单元图，无需 inc mem.sc）。与 adt 的 list 段式存储直接走 mem 同哲学。 */
void *sc_chunk(uint64_t n);            /* 池化 malloc：size==0 视为 1；失败 NULL */
void *sc_chunk0(uint64_t n);           /* 池化 calloc：分配并清零；失败 NULL */
void *sc_refit(void *p, uint64_t n);   /* 池化 realloc：保留内容；失败 NULL 且原块有效 */
void  sc_recycle(void *p);           /* 池化 free：sc_recycle(NULL) 安全空操作 */

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
 * SC_PTRCHK 用 P_TYPEOF 保型且仅求值一次（typeof 操作数不求值；P_TYPEOF 见 platform.h，跨平台）。 */
const void *__sc_ptr_check(const void *p, const char *who);
long        __sc_bound_check(long idx, long len, const char *who);
#define SC_PTRCHK(p, who)      ((P_TYPEOF(p))__sc_ptr_check((const void *)(p), (who)))
#define SC_BOUNDCHK(i, n, who) (__sc_bound_check((long)(i), (long)(n), (who)))

/* 绑定一条边：目标.in++、持有者.out++（哨兵 own 跳过 out 记账）。
 * 原子性逐对象判定：读对象头 flags 的 SC_REF_ATOM 位选原子/普通 RMW（混合图天然正确）。 */
static inline void sc_fat_bind(sc_fat *f, void *tgt, sc_ref *tr, int32_t *ow) {
    f->p = tgt;
    f->tar = tr ? &tr->in : (int32_t *)0;
    f->own = ow;
    if (f->tar) {
        if (SC_TAR_HDR(f->tar)->flags & SC_REF_ATOM)
            sc_get_and_inc_ord(f->tar, 1);            /* 跨平台原子 fetch_add（新值不取，弃返回） */
        else (*f->tar)++;
    }
    if (SC_OWN_REAL(f->own)) {
        if (SC_OWN_HDR(f->own)->flags & SC_REF_ATOM)
            sc_get_and_inc_ord(f->own, 1);           /* 跨平台原子 fetch_add */
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
            nv = sc_inc_ord(f->tar, -1);             /* 跨平台原子 sub_fetch（取新值判 0） */
        else nv = --(*f->tar);
        if (nv == 0) sc_fat_on_zero_d(f, dtor);
    }
    if (SC_OWN_REAL(f->own) && f->p) {
        if (SC_OWN_HDR(f->own)->flags & SC_REF_ATOM)
            sc_get_and_inc_ord(f->own, -1);          /* 跨平台原子 fetch_sub（弃返回） */
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

/* 瘦自动指针 T@^ 绑定/解绑：只统计目标入边 tar->in，不碰 own。
 *   bind：tar->in++（原子位感知）+ 存 p/dtor；
 *   unbind：tar->in--（触 0 → sc_fat_on_zero_d，自带 dtor 析构）；清空。
 * 前 16B（p,tar）与 sc_fat 同构，故复用 SC_TAR_HDR / sc_fat_on_zero_d 走目标头。 */
static inline void sc_thin_bind(sc_thin *t, void *tgt, sc_ref *tr, void (*dtor)(void *)) {
    t->p = tgt;
    t->tar = tr ? &tr->in : (int32_t *)0;
    t->dtor = dtor;
    if (t->tar) {
        if (SC_TAR_HDR(t->tar)->flags & SC_REF_ATOM)
            sc_get_and_inc_ord(t->tar, 1);
        else (*t->tar)++;
    }
}
static inline void sc_thin_unbind(sc_thin *t) {
    if (t->tar) {
        int32_t nv;
        if (SC_TAR_HDR(t->tar)->flags & SC_REF_ATOM)
            nv = sc_inc_ord(t->tar, -1);
        else nv = --(*t->tar);
        if (nv == 0) sc_fat_on_zero_d((sc_fat *)t, t->dtor);
    }
    t->p = (void *)0; t->tar = (int32_t *)0;
}

/* ---------------- clock：时钟（op.sc @def clock 的 C 契约，语言内核） ----------------
 * 内联持有平台时间值句柄 clk_t（platform.h「时间与时钟」段，timespec 封装，值语义、
 * 无堆分配）。全部方法为 static inline 薄封装，直转 P_time_now / P_clock_now /
 * P_cost_now 采集与 clock_* 换算宏——零调用开销；跨 Win/POSIX 由平台层负责。
 * 因 op.h 经 platform.h 带入每个 C 单元且 clk_t 先于此处定义，内联无链接/原型冲突。 */
typedef struct sc_clock {
    clk_t h;                        /* 平台时间值句柄（内联，实现私有） */
} sc_clock;

static inline void sc_clock_now (sc_clock *_this)             { P_time_now(&_this->h); }         /* 墙钟 */
static inline void sc_clock_mono(sc_clock *_this)             { P_clock_now(&_this->h); }        /* 单调钟 */
static inline void sc_clock_cost(sc_clock *_this, bool proc)  { P_cost_now(&_this->h, proc); }   /* CPU 耗时 */

static inline uint64_t sc_clock_s (sc_clock *_this)  { return (uint64_t)clock_s(_this->h); }
static inline uint64_t sc_clock_ms(sc_clock *_this)  { return (uint64_t)clock_ms(_this->h); }
static inline uint64_t sc_clock_us(sc_clock *_this)  { return (uint64_t)clock_us(_this->h); }
static inline double sc_clock_s_f (sc_clock *_this)  { return clock_s_f(_this->h); }
static inline double sc_clock_ms_f(sc_clock *_this)  { return clock_ms_f(_this->h); }
static inline double sc_clock_us_f(sc_clock *_this)  { return clock_us_f(_this->h); }

static inline sc_clock sc_clock_diff(sc_clock *_this, sc_clock other) {
    sc_clock r; clock_dec(_this->h, other.h, r.h); return r;   /* self - other */
}
static inline bool sc_clock_gt(sc_clock *_this, sc_clock other) { return clock_gt(_this->h, other.h); }
static inline bool sc_clock_ge(sc_clock *_this, sc_clock other) { return clock_ge(_this->h, other.h); }

/* ---------------- 随机数（系统 CSPRNG；实现见 op_impl.c，平台层 platform.h P_RAND_IMPL） ----------------
 * op 单元恒被链接（默认导入），全库共用同一份随机源：mac/BSD arc4random_buf、Windows
 * RtlGenRandom、Linux getrandom→/dev/urandom——均一次填满整块、无「种子」概念（系统源全部
 * 不可用时才逐字节降级 rand()）。os/crypto 等模块转调此层，不再各自展开平台实现。 */
void     sc_rand_bytes(void *buf, size_t n);   /* 用 CSPRNG 填满 n 字节；n==0 空操作 */
uint32_t sc_rand32(void);                       /* 32 位随机整数（非零） */
uint64_t sc_rand64(void);                       /* 64 位随机整数（非零） */

/* ---------------- chain：侵入式双向链表 ----------------
 * 元素为 sc 链表结构体（def T: ~ {}，首位有 void *_prev, *_next）
 * 首元素 _prev = 尾元素（rear），尾元素 _next = NULL；不拥有元素
 * 导航：用内置 base(o), prev(o), next(o) 函数访问首真实成员和邻接节点 */

typedef struct sc_chain {
    void    *head;     /* 首元素（空链为 NULL） */
} sc_chain;

typedef int32_t (*sc_chain_cmp)(void *a, void *b);       /* sort 比较回调：实参为元素节点首址（含注入 _prev/_next），sc 侧 (a: T&) 还原 */

void *sc_chain_prev(void *it);                           /* 边界安全逻辑前驱：head→NULL（内置 prev(o) 后端） */
void  sc_chain_append(sc_chain *_this, void *it);           /* 队尾 */
void  sc_chain_push(sc_chain *_this, void *it);             /* 队首 */
void *sc_chain_pop(sc_chain *_this);                        /* 移除并返回首元素 */
void  sc_chain_before(sc_chain *_this, void *pos, void *it);
void  sc_chain_after(sc_chain *_this, void *pos, void *it);
void  sc_chain_remove(sc_chain *_this, void *it);
void *sc_chain_first(sc_chain *_this);
void *sc_chain_last(sc_chain *_this);
void  sc_chain_revert(sc_chain *_this);
void  sc_chain_append_to(sc_chain *_this, sc_chain *dst);     /* 自身清空 */
void  sc_chain_push_to(sc_chain *_this, sc_chain *dst);       /* 自身清空 */
void  sc_chain_cut(sc_chain *_this, void *from, void *to, sc_chain *out);
void  sc_chain_sort(sc_chain *_this, sc_chain_cmp cmp);       /* Simon Tatham 自底向上 O(n log n) 归并排序（稳定，原地不分配） */

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
 * pool 是 run 语句入池目标，亦属 op 层接口协议（见下方 struct pool；构造与
 *   工作线程实现属 mt，inc mt.sc 经 default_pool 取得 pool&）。 */

typedef struct sc_thread {
    uint64_t id;   /* 跨平台统一线程 id（线程启动后由其自身填写） */
    void *h;       /* 实现私有区指针（同块分配） */
} sc_thread;

/* run 语句原语：fn 为 rpc 实际函数（void name_rpc(struct name*)），
 * params/psize 为装填好的参数结构体；out 为空即 detach 自释放。
 * stack 为栈字节数（0=平台默认），prio 为优先级（0=默认，1..255 最佳努力映射）。
 * 返回 1 成功 / 0 失败（失败时 *out 置 NULL） */
uint8_t sc_thread_run(void (*fn)(void *), const void *params, size_t psize, sc_thread **out,
                   uint32_t stack, uint8_t prio);

void    sc_thread_join(sc_thread *_this);   /* 等待结束并回收（含 thread 对象本身） */

/* ---------------- pool：线程池接口协议（run 语句入池目标，op 层声明） ----------------
 * pool 是 op 层定义的「线程池接口协议」——仿 com：成员均为「每对象方法指针」构成的
 * vtable（无具体实现）。由实现模块（mt）按不同策略提供具名构造函数填充指针并返回
 * pool&（如 default_pool(n)，犹如 com 的 file()）。如此语言内核（run 语句）经协议
 * 指针派发，零 emit mt 符号——彻底解耦 mt 模块与语言。
 *   - run ：把一个 rpc 任务入池排队执行（run work(args), p → p->run(p, fn, &参数, sizeof)）
 *   - join：屏障，等全部已提交任务完成（后续仍可提交）
 *   - drop：析构，等任务完成 → 停工作线程 → 回收（含 pool 对象本身）
 * 任务节点延续联合分配哲学：[节点][rpc 参数]，参数拷贝入节点，调用点无需保活。
 * 两种内置策略（mt_impl.c，均返回 pool&、均凭 run 投递）：
 *   - default_pool(n)：常驻 worker 消费内部 FIFO 任务队列——run = 入队，fn 执行一次；
 *   - drain_pool(n)  ：按需自调度——无任务队列，worker 反复跑投递的工作单元 fn 直到一轮
 *     无新投递即退；run = 通知有新活 + 按需激活一个 worker（上限 n，经世代代检防丢唤醒）。
 * run 的入池帧与 sc_thread_run 同形。 */
typedef struct sc_pool {
    void   *h;       /* 实现私有区指针（队列 + 同步原语 + 工作线程，实现私有） */
    uint8_t (*run)(struct sc_pool *_this, void (*fn)(void *), const void *params, size_t psize);
    void    (*join)(struct sc_pool *_this);
    void    (*drop)(struct sc_pool *_this);
} sc_pool;

/* ---------------- queue：消息队列接口协议（rpc 投递目标，op 层声明） ----------------
 * queue 是 op 层定义的「消息队列接口协议」——仿 com/pool：成员均为「每对象方法指针」
 * 构成的 vtable（无具体实现）。由实现模块（mt）按策略提供具名构造函数填充指针并返回
 * queue&（如 default_queue(host)，犹如 com 的 file()）。如此语言内核（<< 投递）经协议
 * 指针派发，零 emit mt 符号——彻底解耦 mt 模块与语言。
 *   - post：把一个 rpc 任务整体打包投入队列（q << work(args) → q->post(q, fn, &参数, sizeof, 0, 0)）
 *   - sync：阻塞带回复——把 rpc 调用投递给队列，阻塞至某消费者（另一线程 pull / 池
 *           工作线程）执行完成，结果回填 params 首字段（返回槽 _）；sync work(args), q
 *           → q->sync(q, work_rpc, &参数, sizeof(参数), prio, delay_ms, timeout_ms)。
 *           timeout_ms<=0 无限阻塞等回复；>0 有限超时（P5c）：超时未果则放弃并返回。
 *           返回 0 成功 / 1 超时 / -1 队列已关闭或投递失败。可选状态出参由调用点接收返回码。
 *           有限超时走堆盒子+引用计数路径（执行方只碰盒子堆内存，调用方超时 abort 不 UAF）。
 *           同线程 sync 到自身会死锁（需别的消费者）；循环死锁替代待后续。
 *   - async：非阻塞带回复——把 rpc 调用投递给队列，立即返回 promise&（mt-future 句柄）；
 *           消费者执行完成后兑现 promise，调用方经 p->wait() 阻塞取结果（或 p->ready() 轮询）。
 *           async work(args), q → q->async(q, work_rpc, &参数, sizeof(参数), prio, delay_ms)。参数缓冲堆
 *           分配并由 promise 拥有（drop 回收）；返回 NULL=投递失败。
 *   - prio/delay_ms（post/sync/async 共有）：prio=优先级（高者先被消费，0=默认）；
 *           delay_ms=延迟毫秒（>0 则到期后才可被 pull，0=立即）。二者仅作用于 FIFO-pull 消费
 *           路径（host=nil/自 pull）；池宿主路径忽略（池自调度）。
 *   - pull：从队列取一条消息在当前线程执行；timeout_ms <0 无限等 / 0 立即返回 / >0 毫秒超时；
 *           返回 1 处理了一条 / 0 超时且队列空 / -1 队列已关闭（且排空）
 *   - drop：析构，解绑宿主 → 排空残留消息 → 回收（含 queue 对象本身）
 * 宿主三态（构造时绑定，host 为 pool&）：nil=未绑/延迟、(pool*)-1=当前/主线程（自行跑
 *   pull 循环消费）、&pool=线程池消费（池工作线程持续 pull）。
 * 消息节点延续联合分配哲学：[节点][rpc 参数]，参数拷贝入节点，投递点无需保活。
 * 默认实现见 mt_impl.c 的 default_queue(host)；投递帧与 sc_thread_run/sc_pool.run 同形。 */
struct sc_promise;  /* 前置声明：queue.async 返回 promise&（mt-future，定义见下） */
typedef struct sc_queue {
    void   *h;       /* 实现私有区指针（FIFO 消息队列 + 延迟链 + 同步原语 + 宿主绑定，实现私有） */
    uint8_t (*post)(struct sc_queue *_this, void (*fn)(void *), const void *params, size_t psize, int32_t prio, int64_t delay_ms);
    int32_t (*sync)(struct sc_queue *_this, void (*fn)(void *), void *params, size_t psize, int32_t prio, int64_t delay_ms, int64_t timeout_ms);
    struct sc_promise *(*async)(struct sc_queue *_this, void (*fn)(void *), const void *params, size_t psize, int32_t prio, int64_t delay_ms);
    int32_t (*pull)(struct sc_queue *_this, int64_t timeout_ms);
    void    (*drop)(struct sc_queue *_this);
} sc_queue;

/* ---------------- promise：mt-future（async 投递的结果句柄，op 层声明） ----------------
 * promise 是 op 层定义的「异步结果句柄接口协议」——仿 sc_queue/sc_pool：成员均为「每对象方法
 * 指针」构成的 vtable（无具体实现），由实现模块（mt）的 sc_queue.async 具名构造并返回
 * sc_promise&。与 libuv future（单线程协作、绑事件循环）不同，sc_promise 是线程世界的阻塞型
 * 未来：内部 mutex+cond，消费者（另一线程 pull / 池工作线程）执行完 rpc 后兑现，调用方
 * p->wait() 阻塞取结果。如此语言内核（async 投递）经协议指针派发、零 emit mt 符号。
 *   - ready：非阻塞轮询，是否已完成（bool）
 *   - wait：阻塞至完成，返回结果——类型擦除为 void*（返回槽首 8 字节），调用点 : T 还原
 *           （标量 p->wait(): i4，指针 p->wait(): char&，与 future.get() 同语义）
 *   - drop：释放（含堆参数缓冲与 promise 对象本身）
 * 生命周期：参数缓冲与返回槽由 promise 堆拥有，async 投递后调用方无需保活；须先 wait()
 *   取结果再 drop()——消费者兑现前 drop 会 UAF（引用计数化的安全释放待后续阶段）。
 * 默认实现见 mt_impl.c（sc_queue.async 构造 sc_promise_box）。 */
typedef struct sc_promise {
    void   *h;       /* 实现私有区指针（同步原语 + 堆参数缓冲 + 结果，实现私有） */
    uint8_t (*ready)(struct sc_promise *_this);  /* 非阻塞轮询：是否已完成 */
    void   *(*wait)(struct sc_promise *_this);   /* 阻塞至完成，返回结果（类型擦除 void*，调用点 : T 还原） */
    void    (*drop)(struct sc_promise *_this);   /* 释放（含堆参数缓冲与 promise 对象本身） */
} sc_promise;

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

typedef struct sc_future {
    int    ready;            /* 0=未就绪, 1=已就绪 */
    void  *result;           /* 类型擦除结果（标量经 intptr_t 往返） */
    void  *frame;            /* 等待者状态机帧（NULL=无人等待） */
    void (*resume)(void *);  /* 等待者恢复入口（状态机函数） */
    void  *next;             /* 就绪队列链接（运行时内部） */
    int    id;               /* future<ID> 事件 id（>=0=可派发；-1=无标签，仅协程 await 用） */
    void  *ctx;              /* future<ID>(ctx) 用户上下文：发起时挂、派发时经 future_ctx 取回 */
} sc_future;

/* future 方法（op.sc 内 @def future 的成员函数，C 侧实现） */
void     sc_future_init(sc_future *_this);    /* future()：登记到当前事件循环（pending +1） */
uint8_t  sc_future_ready(sc_future *_this);   /* f.ready()：是否已就绪 */
void    *sc_future_get(sc_future *_this);     /* f.get()：取结果（调用点 : T& 强转还原） */
void    *sc_future_ctx(sc_future *_this);     /* f.ctx()：取发起时挂载的用户上下文（无则 NULL） */

/* 当前线程事件循环生命周期 */
void     sc_async_init(void);           /* 建立事件循环 */
void     sc_async_loop(void *proc);     /* 驱动事件循环至全部异步完成；proc=按 id 派发回调
                                         * （int (*)(int id, sc_future *f)，返回<0 停循环），NULL=纯协程驱动 */
void     sc_async_final(void);          /* 销毁事件循环 */

/* 运行时内部原语（编译器生成码与可 await 契约使用） */
sc_future  *sc_future_new(void);              /* 造未就绪 sc_future（pending +1）：async 启动器内部用 */
void     sc_future_done(sc_future *f, void *result);  /* done 关键字：置就绪 + 唤醒（任意线程安全） */
/* await 握手（生成的状态机调用）：登记本帧为 waiter，返回是否已就绪。
 * 1=已就绪（不让出、直接续跑）；0=未就绪（保存状态后让出）。 */
uint8_t  sc_future_await(sc_future *f, void *frame, void (*resume)(void *));

/* ---------------- deferred：rpc 延迟应答句柄（接口协议） ----------------
 * 与 sc_future（fnc 单线程异步）对称的「rpc 延迟应答」机制：sync 驱动的 rpc 体内裸 `async`
 * 取出当前待应答调用（求值为 deferred&），rpc 体 return 不再自动应答；之后（任意线程、任意
 * 时刻）`done s, result` 兑现——把结果写回调用方返回槽并唤醒其阻塞，与 `done sc_future` 同形。
 *
 * 是 op 层「接口协议」对象（仿 sc_queue/sc_promise）：respond 为每对象方法指针，由消息队列实现
 * 模块（mt，inc mt.sc）填充——故语言内核（done 兑现）经协议指针派发、零 emit mt 符号。
 * respond(s, src, n)：从 src 拷 n 字节到调用方返回槽（偏移 0，与即时应答原地写 _ 对齐）并
 * 唤醒；n==0/src==NULL=无结果（rpc 无返回值）。
 *
 * 待应答调用身份（当前正在执行的 rpc 调用）由 op 内核 TLS 维护（op_deferred_begin 在消费者
 * 执行 rpc 体前设置，裸 `async` 经 op_deferred_current 取出并标记「已领取」=转延迟应答）。
 * sc_deferred 句柄本身由实现模块在调用方栈上构造（随调用方阻塞存活），op 内核只透传其指针。 */
typedef struct sc_deferred {
    void  *h;                                              /* 实现私有区指针（实现私有） */
    void (*respond)(struct sc_deferred *_this, void *src, size_t n);  /* done 兑现：写回返回槽 + 唤醒 */
} sc_deferred;

/* 当前 rpc 调用的延迟应答句柄 TLS（op 内核维护，消息队列实现模块在执行 rpc 体前后调用）：
 *   op_deferred_begin(s) —— 执行 rpc 体前置当前待应答调用为 s、清「已领取」标记；
 *   sc_op_deferred_current() —— 裸 `async` 取当前待应答调用，并标记「已领取」（转延迟应答）；
 *   sc_op_deferred_taken()  —— rpc 体返回后查询是否被领取（决定即时应答 vs 延迟应答）。 */
void     sc_op_deferred_begin(sc_deferred *s);
sc_deferred *sc_op_deferred_current(void);
int      sc_op_deferred_taken(void);

/* op 层暴露给"异步功能库"（async 模块叶子原语生态）的钩子：
 *   op_timer_arm —— 基础定时器：在 ms 毫秒后兑现 f（done）。两后端各自实现
 *                   （poll=单调时钟截止表；libuv=uv_timer）。delay 即在其上构建。
 *   op_uv_loop   —— 取 op 层事件循环的 uv_loop_t*（仅 -DSCC_WITH_UV 时非 NULL）；
 *                   供网络/文件等 libuv 叶子原语直接挂句柄。poll 后端返回 NULL。 */
void     sc_op_timer_arm(sc_future *f, uint32_t ms);
void    *sc_op_uv_loop(void);

/* ---------------- tok / dep / form：分布式 token 依赖机制（C ABI 契约） ----------------
 * 句柄类型 token 的 sc 侧协议（@def token，方法 get/set）声明在 op.sc（语言内核机制，
 * 默认导入，供编译器识别方法分派）；本段 C ABI 与运行时实现已合并进 op（原独立 tok 模块
 * 已并入 op.h/op_impl.c）。token / token_* 原型随 op.h 进入每个 C 单元（恒可用）。
 *
 * tok 是「分布式 token」：跨进程以字符串 id 唯一标识的共享量，值为类型擦除 @（sc_thin）。
 *   · tok t: "id"        声明一个 token 句柄（enforce 纯从）；
 *   · tok t: "id"<换行+缩进体>  声明并挂 combine 回调（form 候选：缩进体即 combine）；
 *   · form t, v          初始化 form token：灌初值并升格为 form 主；
 *   · dep all/any: ...   声明 token 间依赖关系（follow 回调，all=与门 / any=或门）。
 * 均为模块域静态对象，注册延迟到模块 init（编译器生成 sc_token_bind / sc_token_depend）。 */

typedef struct sc_token sc_token;

/* combine 上下文（this）：form 候选 combine 体的唯一形参 sc_tok_in&。
 *   值均为 @（sc_thin，类型擦除自描述胖指针，体内 (e:T@) 还原典型对象）：
 *     sender —— 发送者（当前由 set 触发时恒为空 @，预留扩展）；
 *     base   —— 当前值（form 主上一轮结果）；
 *     input  —— 本次 set 输入；
 *     tag    —— set 随附的整型标签（t.set(v, tag) 透传，体内分流用）。
 *   combine 返回新值（@）。 */
typedef struct sc_tok_in {
    sc_thin sender;
    sc_thin base;
    sc_thin input;
    int32_t tag;
} sc_tok_in;

/* follow 上下文（this）：dep follow 体的唯一形参 sc_dep_in&。
 *   toks   —— 依赖项句柄数组（token**，下标 [i] 取第 i 项 token&）；
 *   count  —— 依赖项个数；
 *   active —— 本次触发动作码（见下 acting）：负数为门事件码，>=0 为或门变更项下标。
 *   ctx    —— 注册时透传的用户上下文（dep 关系的私有边状态；无则空 &）。
 *   dep 的 a:"id" 局部名糖由编译器注入 `var a: token& = this->toks[i]`。 */
typedef struct sc_dep_in {
    sc_token  **toks;
    int32_t  count;
    int32_t  active;
    void    *ctx;
} sc_dep_in;

/* combine 回调：form 候选据上下文 this（base/input/sender/tag）算出新值（@ 擦除）。 */
typedef sc_thin (*sc_token_combine)(sc_tok_in *self);
/* follow 回调：依赖项 ts[0..n) 之一就绪/变更时触发；返回下次门逻辑（非 0=与门 all / 0=或门 any）。
 *   ctx=注册时透传的用户上下文。acting=触发动作（对齐 c_prototype.h）：
 *     -2 (ALL_READY)  ：与门首次全部就绪（form 触发）；
 *     -3 (ALL_CHANGED)：与门全部已变更（set 触发）；
 *     -1 (ANY_READY)  ：或门首次任一就绪 / 任一变更（acting 退化）；
 *     -4 (BACK)       ：反向遍历（back t）——this->active==-4 即走反向计算（读目标写源）；
 *                       BACK 下返回值另作「中止反向遍历」信号：非 0=停止本轮 back 扫描
 *                       （drain 协作层用：认领并处理一节点后中止重扫），0=继续遍历（如反向传播）；
 *     idx >= 0        ：或门，本次变更的依赖项下标。
 *   编译器生成的蹦床把本签名打包为 sc_dep_in& 后调用合成的 follow 体。 */
typedef int (*sc_token_follow)(sc_token **ts, int n, int acting, void *ctx);
/* exec 钩子：节点（token）私有的处理回调，统一拉取/推送两模式（模式由谁驱动决定，非钩子属性）：
 *   · 拉取（back sink）：按反拓扑序对每个注册了 exec 的节点唤起 exec(t, t->ctx)——从侧车（ctx）
 *     出队一帧、跑节点 kernel、t.set 产出（触发前向路由），返回非 0 = 「已认领并处理一节点」即
 *     请求中止本轮 back 扫描。
 *   · 推送（token_set）：值变更落定后、于锁外（combine 临界区已退出）、向下游 dep 传播之前唤起
 *     exec(t, t->ctx)——节点级副作用/观察点（sink 产出、统计、日志、外部推送）；仅在值变更时唤起。
 *   与 combine/follow 正交：combine 须纯（锁内只算值），dep 只管前向路由，节点处理/副作用归 exec
 *   （锁外，MT 安全）。一个节点只会被其一种模式驱动（取决于模板用 back 还是 set）。 */
typedef int (*sc_token_exec)(sc_token *t, void *ctx);

sc_token    *sc_token_bind(const char *id, sc_token_combine combine);  /* create-or-get：按 id intern 句柄，combine 非空则挂为 form 候选 */
sc_thin     sc_token_get(sc_token *t);                                /* t.get()：取当前值（@，调用点 (e:T@) 还原） */
void        sc_token_set(sc_token *t, sc_thin v, int32_t tag);        /* t.set(v, tag)：设值（随附 tag）；唯新值≠原值才落值并触发依赖级联（记忆化/去抖） */
void        sc_token_pulse(sc_token *t, sc_thin v, int32_t tag);      /* t.pulse(v, tag)：脉冲设值——绕过相等抑制，即便同值也落值并强制传播（拉取流水线/迭代：每次 set 皆事件） */
sc_thin     sc_tok_modified(void);                                 /* modified 哨兵 @：combine 体内 return tok_modified() → 强制刷新传播（即使值未变） */
void        sc_token_form(sc_token *t, sc_thin v, int32_t tag, void *ctx, sc_token_exec exec);  /* form t, v[, ctx[, exec]]：灌初值并升格为 form 主；ctx 非空则绑定节点私有上下文（侧车），exec 非空则挂节点处理钩子 */
void       *sc_token_ctx(sc_token *t);                                /* t.ctx()：取本 token 的私有上下文（form 绑定的侧车；未绑定=空 &） */
void        sc_token_depend(sc_token **ts, int n, int all, sc_token_follow follow, void *ctx);  /* dep：注册依赖关系（all=与门/或门） */
void        sc_token_depend_map(sc_token **ts, int nsrc, int ntgt, int all, sc_token_follow follow, void *ctx);  /* dep…map：源(nsrc)→目标(ntgt) 显式图边；ts=源++目标，仅源触发 */
void        sc_token_set_depth(sc_token *t, int depth);               /* 烘焙：编译期算好的依赖图深度写入句柄（注册时调用，常量入参） */
int         sc_token_depth(sc_token *t);                              /* t.depth()：读依赖图深度（源=0；O(1)，无图遍历） */
void        sc_token_set_crit(sc_token *t, int critical, int slack);  /* 烘焙：编译期算好的关键路径标志 + 松弛写入句柄 */
int         sc_token_critical(sc_token *t);                           /* t.critical()：是否在关键路径（最长链）上（O(1)） */
int         sc_token_slack(sc_token *t);                              /* t.slack()：松弛余量（可深多少跳而不拖慢全局；0=关键） */
void        sc_token_set_degree(sc_token *t, int fanin, int fanout);  /* 烘焙：编译期算好的扇入/扇出度写入句柄（枢纽识别） */
int         sc_token_fanin(sc_token *t);                              /* t.fanin()：扇入度（被多少上游 map 依赖；O(1)） */
int         sc_token_fanout(sc_token *t);                             /* t.fanout()：扇出度（驱动多少下游 map 目标；高=枢纽） */
void        sc_token_set_reach(sc_token *t, int reach);               /* 烘焙：编译期算好的可达下游数写入句柄（脏标记影响范围） */
int         sc_token_reach(sc_token *t);                              /* t.reach()：变更后须重算的下游 token 总数（失效爆炸半径；O(1)） */
void        sc_token_set_batch(sc_token *t, int width);               /* 烘焙：编译期算好的拓扑波次并行宽度写入句柄（接 MT 调度） */
int         sc_token_batch(sc_token *t);                              /* t.batch()：拓扑波次编号（=depth；同波可并行；O(1)） */
int         sc_token_batch_width(sc_token *t);                        /* t.batch_width()：本波次并行宽度（同深度可并行 token 数） */
void        sc_token_set_dom(sc_token *t, int checkpoint, int dom_size); /* 烘焙：编译期支配树算好的检查点标志 + 支配子树规模写入句柄 */
int         sc_token_checkpoint(sc_token *t);                         /* t.checkpoint()：是否为支配咽喉（缓存边界；O(1)） */
int         sc_token_dom_size(sc_token *t);                           /* t.dom_size()：支配子树规模（缓存可覆盖的下游 token 数） */
void        sc_token_back(sc_token *t, sc_thin seed, int32_t tag);    /* back t[, seed]：反向遍历（反向传播 / drain 骨架）——自 t 沿反向邻接收上游 dep，按深度降序（反拓扑）以 acting=TOK_BACK 唤起 follow；follow 返回非 0 即提前中止本轮遍历（drain 拉取）；seed 非空先灌入 t */
void        sc_token_depend_loop(sc_token **ts, int nsrc, int ntgt, int all, sc_token_follow follow, void *ctx);  /* dep loop：受控反馈环——源不反挂（不自动级联），登记全局 loop 表，由 token_loop_run 按 SCC 簇驱动 */
void        sc_token_set_scc(sc_token *t, int scc_id, int scc_size); /* 烘焙：编译期 Tarjan 算好的 SCC 反馈簇划分写入句柄（注册时调用，常量入参） */
int         sc_token_scc(sc_token *t);                                /* t.scc()：读受控反馈簇编号（O(1)；非反馈/未烘焙=0） */
int         sc_token_scc_size(sc_token *t);                           /* 所属反馈簇大小（>1 或含自环=反馈簇；0/1=非反馈） */
int         sc_token_loop_run(sc_token *t, int max);                  /* t.loop_run(max)：驱动 t 所在 SCC 反馈簇迭代至多 max 轮（acting=TOK_LOOP），返回实际轮数 */

/* ---------------- com / limit / ioq：设备通讯机制 ----------------
 * com 是机制框架：具体 io 依赖设备，由 com 的 read/write/error 每对象方法指针
 * （MethodPtr，C 侧为结构体函数指针字段）实现——非成员函数（伪类无派生）。
 * limit 是 com 的分身/切片，充当一次 read 的截止边界视图（com 默认 endless io）。
 * ioq 是读写缓存队列（自动膨胀的循环缓冲），com 提供 rq/wq（非 NULL）即支持异步 io。
 *
 * 声明在 op.sc（默认导入）；本头提供 C ABI 结构体与方法原型。limit、ioq 的方法
 * （limit_xxx、ioq_xxx）与通用收发框架的运行时实现属后续阶段（op_impl.c，不依赖 libuv）；
 * 具体设备 io 实现属可选模块（inc com.sc）。未调用即不链接。 */

typedef struct sc_com   sc_com;
typedef struct sc_limit sc_limit;
typedef struct sc_ioq   sc_ioq;

/* ioq：com 读写缓存队列（循环缓冲）
 * item 为连续一组值，依首值判类型：[size, buf] size≠0=io 缓冲（pull 执行 io）；
 *                                  [0, callback] size=0=io 完成回调地址。 */
struct sc_ioq {
    struct sc_com *com;         /* 所属 com */
    void   **_buf;           /* 循环缓冲存储（运行时分配，自动膨胀） */
    uint32_t cap;           /* 容量（槽数） */
    uint32_t head;          /* 队首索引 */
    uint32_t tail;          /* 队尾索引 */
};

void  sc_ioq_push(sc_ioq *_this, void *buf, int32_t size);        /* 入队一段 io 缓冲 */
void  sc_ioq_notify(sc_ioq *_this, void *cb, void *data);         /* 入队一个完成回调（cb 为函数地址） */
void *sc_ioq_pull(sc_ioq *_this);                                 /* 取队首并执行 io（空则阻塞） */

/* limit：com 一次有界读视图（com 的分身/切片；首位为分身回指 _self，与注入对齐）。
 * 最小内核：框架只驱动确定的读流程，缓存/边界策略全在用户实现里。
 *   size   —— ending=NULL：定长大小；否则每次最多读取的 chunk（读满 size 触发 ending）
 *   len    —— 框架回写：已累计读取的字节计数
 *   data() —— MethodPtr（用户实现）：返回缓冲基址，框架以 data()+len 为写入起点
 *   ending —— MethodPtr（用户实现，默认 NULL）：动态截止判定。框架以本 limit 为接收者回调
 *              int32_t (*)(struct sc_limit *_this)，>=0 命中（值=保留长度）/<0 继续；
 *              基址/已读长度经 _this 自取（_this->data(_this) / _this->len）。 */
struct sc_limit {
    struct sc_com *_self;       /* 分身回指本体 com（分身机制注入） */
    uint32_t size;           /* ending=NULL:定长大小；否则每次最大 chunk */
    uint32_t len;            /* 框架回写：累计读取计数 */
    void*    (*data)(struct sc_limit *_this);   /* 返回缓冲基址（用户实现，MethodPtr） */
    int32_t  (*ending)(struct sc_limit *_this); /* 动态截止（用户实现，MethodPtr，默认 NULL） */
};

/* read/write 返回码（与 op.sc 的 def io 对应）：
 *   <0      不可恢复错误，中断；  0  成功可继续；
 *   again 异步挂起等待；       eof 读完（后续读按空完成）。
 * op.sc 的 `def io: [again=1, eof=2]` 经 codegen 落为 sc_again / sc_eof（命名域名），
 * 本头据此提供同名定义。 */
enum { sc_again = 1, sc_eof = 2 };

/* 框架同步读流程：用 com 的 read 接口反复读入 limit 缓冲，按 size/ending 判截止，
 * 回写 limit.len。返回 sc_eof（读完）/ 负数（不可恢复）/ sc_again（同步上下文报错信号）。
 * 驱动 com >> s（s 为 com[...] 句柄）；异步形态见下方 com_limit_read_async。 */
int32_t sc_limit_read(struct sc_com *_this, struct sc_limit *s);

/* com：设备通讯端点。字段顺序与 op.sc 的 @def com 一致。
 * alloc/free/read/write/error 为每对象方法指针（首参为隐藏接收者 com*）。 */
struct sc_com {
    void    *dev;            /* 设备句柄（设备相关） */
    struct sc_ioq *rq;          /* 读队列（NULL=不支持异步读） */
    struct sc_ioq *wq;          /* 写队列（NULL=不支持异步写） */
    struct sc_limit *(*alloc)(struct sc_com *_this, uint32_t size, void *ending);  /* 分身构造 → limit& */
    void   (*free)(struct sc_com *_this, struct sc_limit *s);                     /* 分身析构 */
    int32_t (*read)(struct sc_com *_this, void *data, uint32_t *size);         /* 设备读 */
    int32_t (*write)(struct sc_com *_this, void *buf, uint32_t *size);         /* 设备写 */
    int32_t (*error)(struct sc_com *_this);                                    /* 错误回调 */
    int32_t (*readable)(struct sc_com *_this, void **id);  /* 读就绪查询：*id=可监听句柄（nil=不支持多路复用，转看返回值） */
    int32_t (*writable)(struct sc_com *_this, void **id);  /* 写就绪查询（语义对称，见 op.sc 契约） */
    int32_t (*close)(struct sc_com *_this);                /* 关闭设备：释放底层资源（nil=无需关闭，OS 回收） */
    /* seek：随机寻址（仅可寻址设备实现，如 file/stream；tcp/ssl/ssh 等流式设备为 NULL）。
     * whence：0=SEEK_SET(从头绝对) / 1=SEEK_CUR(相对当前) / 2=SEEK_END(相对尾部)；
     * 返回寻址后的绝对位置(>=0) / <0 出错或不支持。seek(0, 1) 即取当前位置。 */
    int64_t (*seek)(struct sc_com *_this, int64_t off, int32_t whence);
    /* take：转移句柄 s 的数据缓冲所有权给调用方（零拷贝），返回缓冲基址（NULL=不支持/无缓冲）。
     * 取走后 s 的 data() 缓冲被摘除，free(s) 不再回收它——调用方自负释放（sc 池缓冲用
     * mem.sc 的 recycle）。仅内部分配缓冲的可寻址设备（file/stream）实现；流式设备为 NULL。
     * 内建 file/stream 的 com[0] 读全部缓冲多分配 1 字节 NUL，故 take 出的缓冲天然以 \0 结尾
     * （文本读可直接当 C 字符串；数据长度仍见 s->len）。 */
    void   *(*take)(struct sc_com *_this, struct sc_limit *s);
};

/* com 异步收发桥接：rpc 体内 com >> v / com << v 由编译器整合 await 时生成对其的调用，
 * 各产出一个 future（io 完成时兑现）。实现属语言自有异步内核（op_impl.c，始终链接）：
 * 把请求登记进活动表，由 async_loop/async_io 的 poll 循环在设备就绪时驱动收发并兑现。 */
sc_future *sc_com_read_async (struct sc_com *_this, void *data, uint32_t size);   /* 异步读 → future */
sc_future *sc_com_write_async(struct sc_com *_this, void *buf,  uint32_t size);   /* 异步写 → future */

/* com[...] 句柄异步有界读（【E】）：以 limit_read 的框架确定读循环驱动 com >> s，遇
 * sc_agian 挂起、待设备就绪后续读，直至 ending/定长命中再 future_done 兑现（结果=
 * limit.len；出错则为负返回码）。实现属语言自有异步内核（op_impl.c，始终链接）。 */
sc_future *sc_com_limit_read_async(struct sc_com *_this, struct sc_limit *s);

/* com 设备 io 就绪事件循环（与 async_loop 正交）：据 com.readable/writable 探测就绪
 * （多路复用句柄 → poll；否则轮询返回值），就绪后执行 io 并 future_done 兑现。
 * 多路复用后端为语言自有 POSIX poll 实现（op_impl.c，不依赖 libuv）。详见 op.sc。 */
void     sc_async_io(void);

/* ---------------- print：日志输出（语言关键字） ----------------
 * print 关键字 → 编译器生成 print 调用（首参为 u1 级别/通道 chn，默认 0）。
 * print 属语言内核：声明在此（默认带入每个 C 单元），运行时实现在 op_impl.c
 * （始终随工程编译链接）——无需 inc。
 *   - chn：级别/通道（「级别就是通道」）。0=普通 stdout（默认，无着色）；
 *          1..6=F/E/W/I/D/V，按级别着色 stdout（仅 tty）并可镜像系统日志
 *   - flush：非 0 时输出后立即 fflush(stdout)（语句形式末项符号 '.' 生成 flush=1）；
 *            0=默认（依 libc 缓冲策略）。string 通道追加不经本函数，无 flush 概念
 *   - 下列裸枚举常量 F/E/W/I/D/V 供 print<级别> 生成的 print((uint8_t)(级别), ...)
 *     编译（与 op.sc 的 def log 对齐）；数值即通道号
 *   - 特例：<chn> 为 string 变量时编译器改生成 string_printf（追加进该串，不走本函数）
 *   - 级别过滤：环境变量 SC_LOG=F/E/W/I/D/V（默认 D），首次调用时读取
 *   - 系统日志：环境变量 SC_LOG_SYS 非空 → 额外写系统日志（见 platform.h P_log_sys） */
enum { sc_F = 1, sc_E = 2, sc_W = 3, sc_I = 4, sc_D = 5, sc_V = 6 };
void sc_print(uint8_t chn, int flush, const char *fmt, ...);

/* ---------------- stringify：JSON 格式化关键字选项 ----------------
 * stringify<选项>(值[, 缓存, 大小]) 由编译器按静态类型生成格式化器（写入独立
 * stringify.h，依赖 adt string）。生成的格式化器签名携带 stringify_t 选项参数，
 * 调用点据 stringify<key:val> 构造之。
 *   - compact:1 → 紧凑单行 {"x":3,"y":4}
 *   - 默认（compact:0）→ 多行美化（2 空格逐层缩进） */
typedef struct sc_stringify {
    uint8_t compact;                /* 1 → 紧凑单行；0 → 多行美化（默认） */
} sc_stringify_t;

#ifdef __cplusplus
}
#endif

#endif /* SC_OP_H */
