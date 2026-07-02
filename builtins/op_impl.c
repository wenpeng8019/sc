/* op_impl.c —— op.sc 语法机制的默认运行时实现
 *
 * 编译器对每个工程都自动编译并链接本文件（op.sc 为默认导入模块，无需 inc）。
 * 契约见同目录 op.h。
 */
#define P_LOG_SYS_IMPL  /* 在本 TU 内展开 platform.h 的 P_log_sys 系统日志实现（print 用） */
#define P_MT_IMPL       /* 在本 TU 内展开 platform.h 的互斥/条件变量/线程层（内核 g_mu、线程 id 用） */
#include "platform.h"   /* builtins 跨平台基础头（编译时 -I builtins 根目录） */
#include "op.h"

#include "adt/adt.h"    /* dict：合并进本 TU 的 tok 运行时用其哈希表（g_toks intern） */

/* ---------------- 分配接口 —— 实现由 mem 提供 ----------------
 * sc_chunk/sc_chunk0/sc_refit/sc_recycle：确定性池化分配的协议接口（声明见 op.h），
 *   实现由 mem 提供（mem_impl.c）。op 单元恒隐式依赖 mem（编译器自动纳入单元图），
 *   故 mem 的定义必被链接；op 此处不再重复定义（避免与 mem 的 sc_chunk 重定义/签名冲突）。
 * sc_alloc/sc_realloc/sc_free：随 SC_POOL 切换的间接层。默认（未定义 SC_POOL）为 op.h
 *   里直通 libc 的宏，本文件无需定义；开启 -DSC_POOL 时编译下述函数，转发到 mem 池化三件套。 */
#include "mem/mem.h"
#ifdef SC_POOL
void *sc_alloc(size_t n)            { return sc_chunk(n); }
void *sc_realloc(void *p, size_t n) { return sc_refit(p, n); }
void  sc_free(void *p)              { sc_recycle(p); }
#endif

/* 异步内核的平台相关头（多路复用后端：epoll/kqueue/IOCP/poll、自管道、互斥）在
 * 文件末尾的「异步内核」段按平台 #ifdef 引入，使本文件在 POSIX 与 Windows 均可编译。 */

/* ---------------- 自动指针 T@ 运行时（见 op.h / builtins/auto_ptr.md） ----------------
 * 释放点验证 + 入边归零处理。绑定/解绑为 op.h 内联（热路径）；此处是其回调与显式检查。 */

void sc_ref_check(sc_ref *r, const char *who) {
    if (!r) return;
    if (r->in != 0)
        fprintf(stderr, "sc: 悬挂：%s 释放时仍被 %d 个引用指向\n", who ? who : "对象", r->in);
    if (r->out != 0)
        fprintf(stderr, "sc: 未清理：%s 释放时仍持有 %d 个出向引用\n", who ? who : "对象", r->out);
}

/* 裸 @ 还原 object@（--check=ref）：源 afat 须由 class 析构槽产出（dtor==objdrop），
 * 否则源非 class 类型，无 _class 派发槽，还原 object@ 无意义——中止以暴露误用。 */
sc_afat __sc_afat_objck(sc_afat a, void (*objdrop)(void *)) {
    if (a.dtor != objdrop) {
        fprintf(stderr, "sc: 裸 @ 还原 object@ 失败：源非 class 类型（无 _class 派发槽，object@ 无意义）\n");
        abort();
    }
    return a;
}

void sc_fat_on_zero(sc_fat *f) { sc_fat_on_zero_d(f, (void (*)(void *))0); }

/* 入边归零（in→0）：堆对象先调目标类型析构 dtor（清子成员 → out 递减），再判 out。
 *   dtor != NULL 且 heap → 先析构（栈值对象走退域 RAII drop，不在此重复析构，故 heap 守卫）。
 *   out != 0（析构后仍持出边）→ 报「未清理」，不释放（释放会令其子目标 in 计数失衡）。
 *   out == 0 && heap → 释放整块（--check=mem 经 canary 校验）。 */
void sc_fat_on_zero_d(sc_fat *f, void (*dtor)(void *)) {
    sc_ref *r = (sc_ref *)f->tar;          /* in 为 sc_ref 首成员，故 &ref->in == ref == 块首 */
    if (!r) return;
    if (dtor && r->heap) dtor(f->p);       /* 堆对象 in→0：先析构，让其清理子成员（out 递减） */
    if (r->out != 0) { sc_ref_check(r, "对象"); return; }  /* 析构后仍持出边 → 报未清理，不释放 */
    if (!r->heap) return;                  /* 栈/全局对象：不释放 */
    if (r->flags & SC_REF_CANARY) { sc_canary_free(r); return; }  /* --check=mem：校验头尾哨兵 */
    sc_free(r);                            /* 堆对象 in==0 && out==0 → 释放整块（含头；经分配间接层） */
}

/* --check=mem：ref 头堆对象带头尾 canary。块首 = (char*)r - SC_CANARY；
 * 头哨兵 {魔数, 实体字节数}，尾哨兵在实体之后。校验越界损坏后释放整块。 */
void sc_canary_free(sc_ref *r) {
    char     *block = (char *)r - SC_CANARY;
    uintptr_t *head = (uintptr_t *)block;          /* head[0]=魔数, head[1]=实体字节数 */
    uintptr_t  want = sc_canary_magic(block);
    uintptr_t  size = head[1];
    uintptr_t *tail = (uintptr_t *)((char *)r + SC_REF_HDR + size);
    if (head[0] != want)
        fprintf(stderr, "sc: 越界：对象头哨兵被破坏（下溢/野写），地址 %p\n", (void *)r);
    if (*tail != want)
        fprintf(stderr, "sc: 越界：对象尾哨兵被破坏（缓冲区上溢），地址 %p\n", (void *)r);
    free(block);
}

/* --check=mem 栈数组尾哨兵：anchor 派生逐字节模式（地址异或盐的字节循环），
 * 填入紧贴有效元素之后的 n 字节尾区；check 处逐字节比对，破坏即报越界（不中止，沿 sc_ref_check 约定）。 */
void sc_stack_canary_fill(unsigned char *p, size_t n, const void *anchor) {
    uintptr_t m = sc_canary_magic(anchor);
    for (size_t i = 0; i < n; i++)
        p[i] = (unsigned char)(m >> ((i % sizeof(uintptr_t)) * 8));
}

void sc_stack_canary_check(const unsigned char *p, size_t n, const void *anchor, const char *who) {
    uintptr_t m = sc_canary_magic(anchor);
    for (size_t i = 0; i < n; i++)
        if (p[i] != (unsigned char)(m >> ((i % sizeof(uintptr_t)) * 8))) {
            fprintf(stderr, "sc: 越界：栈数组 %s 尾哨兵被破坏（缓冲区上溢）\n", who ? who : "对象");
            return;
        }
}

/* --check=ptr 运行时守卫：解引用前的裸指针 nil 校验、已知维度数组下标的越界校验。
 * 命中即报 stderr 并 abort —— 解引用空指针 / 越界访问属未定义行为，放行只会让破坏继续扩散。 */
const void *__sc_ptr_check(const void *p, const char *who) {
    if (!p) {
        fprintf(stderr, "sc: 空指针解引用：%s\n", who ? who : "(未知位置)");
        abort();
    }
    return p;
}

long __sc_bound_check(long idx, long len, const char *who) {
    if (idx < 0 || idx >= len) {
        fprintf(stderr, "sc: 数组下标越界：%s（下标 %ld，长度 %ld）\n",
                who ? who : "(未知位置)", idx, len);
        abort();
    }
    return idx;
}

/* ---------------- chain：侵入式双向链表 ---------------- */
/* 元素首部内嵌 _prev/_next（sc 编译器注入），偏移固定在结构体最前面。
 * 约定：首元素 _prev = 尾元素（rear），尾元素 _next = NULL。 */

/* _prev/_next 固定在结构体最前面（void *_prev, *_next）*/
#define LPREV(it) (*(void **)((char *)(it) + 0))       /* _prev at offset 0 */
#define LNEXT(it) (*(void **)((char *)(it) + sizeof(void *))) /* _next at offset 8 */

/* 边界安全逻辑前驱：head 无前驱 → 返回 NULL（供编译器 prev(o) 内置导航调用）。
 * 约定 head._prev = rear（复用指针省一个 rear 字段），故裸 _prev 不能直接当前驱。
 * 判定：真前驱 p 满足 LNEXT(p) == it；head 的 _prev(=rear) 其 _next == NULL ≠ it → NULL。 */
void *sc_chain_prev(void *it) {
    if (!it) return NULL;
    void *p = LPREV(it);
    return (p && LNEXT(p) == it) ? p : NULL;
}

void sc_chain_append(sc_chain *_this, void *it) {
    if (!it) return;
    void *h = _this->head;
    if (!h) { _this->head = it; LPREV(it) = it; LNEXT(it) = NULL; return; }
    void *r = LPREV(h);        /* rear */
    LNEXT(r) = it;
    LPREV(it) = r;
    LNEXT(it) = NULL;
    LPREV(h) = it;             /* 首元素 _prev 始终指向新 rear */
}

void sc_chain_push(sc_chain *_this, void *it) {
    if (!it) return;
    void *h = _this->head;
    if (!h) { _this->head = it; LPREV(it) = it; LNEXT(it) = NULL; return; }
    LPREV(it) = LPREV(h);      /* 继承 rear */
    LNEXT(it) = h;
    LPREV(h) = it;
    _this->head = it;
}

void *sc_chain_pop(sc_chain *_this) {
    void *h = _this->head;
    if (!h) return NULL;
    void *n = LNEXT(h);
    if (n) { LPREV(n) = LPREV(h); _this->head = n; }
    else _this->head = NULL;
    LPREV(h) = LNEXT(h) = NULL;
    return h;
}

void sc_chain_before(sc_chain *_this, void *pos, void *it) {
    void *h = _this->head;
    if (!h || !pos || !it) return;
    if (pos == h) {            /* 等价 push */
        LPREV(it) = LPREV(h);
        LNEXT(it) = h;
        LPREV(h) = it;
        _this->head = it;
        return;
    }
    void *p = LPREV(pos);
    LNEXT(p) = it;
    LPREV(it) = p;
    LNEXT(it) = pos;
    LPREV(pos) = it;
}

void sc_chain_after(sc_chain *_this, void *pos, void *it) {
    void *h = _this->head;
    if (!h || !pos || !it) return;
    void *n = LNEXT(pos);
    LNEXT(pos) = it;
    LPREV(it) = pos;
    LNEXT(it) = n;
    if (n) LPREV(n) = it;
    else LPREV(h) = it;        /* it 成为新 rear */
}

void sc_chain_remove(sc_chain *_this, void *it) {
    void *h = _this->head;
    if (!h || !it) return;
    if (it == h) { sc_chain_pop(_this); return; }
    void *p = LPREV(it), *n = LNEXT(it);
    LNEXT(p) = n;
    if (n) LPREV(n) = p;
    else LPREV(h) = p;         /* it 原为 rear */
    LPREV(it) = LNEXT(it) = NULL;
}

void *sc_chain_first(sc_chain *_this) { return _this->head; }

void *sc_chain_last(sc_chain *_this) {
    void *h = _this->head;
    if (!h) return NULL;
    return LPREV(h);
}

void sc_chain_revert(sc_chain *_this) {
    void *h = _this->head;
    if (!h) return;
    void *r = LPREV(h);
    if (r == h) return;        /* 单元素 */
    void *cur = h;
    while (cur) {              /* 逐节点交换 prev/next */
        void *nx = LNEXT(cur);
        LNEXT(cur) = LPREV(cur);
        LPREV(cur) = nx;
        cur = nx;
    }
    _this->head = r;
    LPREV(r) = h;              /* 新首元素 _prev = 新 rear（原首元素） */
    LNEXT(h) = NULL;           /* 新 rear _next = NULL */
}

void sc_chain_append_to(sc_chain *_this, sc_chain *dst) {
    void *lh = _this->head;
    if (!lh) return;
    void *dh = dst->head;
    if (!dh) { dst->head = lh; _this->head = NULL; return; }
    void *dr = LPREV(dh), *lr = LPREV(lh);
    LNEXT(dr) = lh;
    LPREV(lh) = dr;
    LPREV(dh) = lr;            /* dst 新 rear = 原 self rear */
    _this->head = NULL;
}

void sc_chain_push_to(sc_chain *_this, sc_chain *dst) {
    void *lh = _this->head;
    if (!lh) return;
    void *dh = dst->head;
    if (!dh) { dst->head = lh; _this->head = NULL; return; }
    void *dr = LPREV(dh), *lr = LPREV(lh);
    LNEXT(lr) = dh;
    LPREV(dh) = lr;
    LPREV(lh) = dr;            /* 新首元素（self 首）继承 dst rear */
    dst->head = lh;
    _this->head = NULL;
}

void sc_chain_cut(sc_chain *_this, void *from, void *to, sc_chain *out) {
    void *h = _this->head;
    if (!h || !from || !to || !out) return;
    void *p = LPREV(from), *n = LNEXT(to);
    int fromHead = (from == h);
    /* 从原链摘除 [from..to] */
    if (fromHead && !n) _this->head = NULL;
    else if (fromHead) { LPREV(n) = p; _this->head = n; }      /* p 即原 rear */
    else if (!n) { LNEXT(p) = NULL; LPREV(h) = p; }            /* to 原为 rear */
    else { LNEXT(p) = n; LPREV(n) = p; }
    /* 构成 out */
    out->head = from;
    LPREV(from) = to;
    LNEXT(to) = NULL;
}

/* Simon Tatham 自底向上 O(n log n) 归并排序（utlist DL_SORT2 改写）。
 * 稳定、原地、不分配内存：每趟把长 insize 的相邻有序段两两归并，insize 倍增至全表有序。
 * cmp(a, b) <= 0 视 a 不大于 b（升序）；实参为元素节点首址（含注入 _prev/_next），sc 侧 (a: T&) 还原。
 * 算法仅沿 _next 前向游走，故中途 _prev 可暂不一致；每趟末按 chain 约定补正：head._prev = rear、rear._next = NULL。 */
void sc_chain_sort(sc_chain *_this, sc_chain_cmp cmp) {
    void *list = _this->head;
    if (!list || !cmp) return;
    int insize = 1, looping = 1;
    while (looping) {
        void *p = list;
        list = NULL;
        void *tail = NULL;
        int nmerges = 0;
        while (p) {
            nmerges++;
            void *q = p;
            int psize = 0;
            for (int i = 0; i < insize; i++) {   /* q 越过 p 段（长 insize）抵达后半段首 */
                psize++;
                q = LNEXT(q);
                if (!q) break;
            }
            int qsize = insize;
            while (psize > 0 || (qsize > 0 && q)) {   /* 归并 p 段与 q 段 */
                void *e;
                if (psize == 0)                  { e = q; q = LNEXT(q); qsize--; }
                else if (qsize == 0 || !q)       { e = p; p = LNEXT(p); psize--; }
                else if (cmp(p, q) <= 0)         { e = p; p = LNEXT(p); psize--; }
                else                             { e = q; q = LNEXT(q); qsize--; }
                if (tail) LNEXT(tail) = e;
                else      list = e;
                LPREV(e) = tail;
                tail = e;
            }
            p = q;   /* 下一对待归并段 */
        }
        LPREV(list) = tail;   /* head._prev = rear（chain 约定） */
        LNEXT(tail) = NULL;   /* rear._next = NULL */
        if (nmerges <= 1) looping = 0;   /* 单趟仅一次归并即全表有序 */
        insize *= 2;
    }
    _this->head = list;
}

/* ---------------- ioq：com 读写缓存队列（自膨胀循环缓冲）---------------- */
/* 槽统一为 void*（size 以 (void*)(uintptr_t) 编码）。_head/_tail 为单调递增的
 * 逻辑槽计数，物理下标取 % _cap。每项变长，靠首槽 tag/size 自身区分：
 *   io 缓冲项 : [size(!=0), buf]      —— 2 槽，pull 时执行 io（按 rq/wq 方向 read/write）
 *   完成回调项: [0, cb, data]         —— 3 槽，pull 时回调 ((void(*)(void*))cb)(data)
 * （无 libuv：pull 同步执行 io、空队列返回 NULL 不阻塞；真实异步驱动应自行延迟兑现。）*/

/* 确保至少 need 个空闲槽，不足则倍增扩容并把现存项搬到新缓冲首部。 */
static void sc_ioq_ensure(sc_ioq *q, uint32_t need) {
    uint32_t used = q->tail - q->head;
    if (q->cap && q->cap - used >= need) return;
    uint32_t ncap = q->cap ? q->cap : 8;
    while (ncap - used < need) ncap *= 2;
    void **nb = (void **)malloc((size_t)ncap * sizeof(void *));
    for (uint32_t i = 0; i < used; i++) nb[i] = q->_buf[(q->head + i) % q->cap];
    free(q->_buf);
    q->_buf  = nb;
    q->cap  = ncap;
    q->head = 0;
    q->tail = used;
}

/* 入队一段 io 缓冲项 [size, buf]（size 须 != 0）。 */
void sc_ioq_push(sc_ioq *_this, void *buf, int32_t size) {
    sc_ioq_ensure(_this, 2);
    _this->_buf[ _this->tail      % _this->cap] = (void *)(uintptr_t)(uint32_t)size;
    _this->_buf[(_this->tail + 1) % _this->cap] = buf;
    _this->tail += 2;
}

/* 入队一个完成回调项 [0, cb, data]。 */
void sc_ioq_notify(sc_ioq *_this, void *cb, void *data) {
    sc_ioq_ensure(_this, 3);
    _this->_buf[ _this->tail      % _this->cap] = (void *)(uintptr_t)0;
    _this->_buf[(_this->tail + 1) % _this->cap] = cb;
    _this->_buf[(_this->tail + 2) % _this->cap] = data;
    _this->tail += 3;
}

/* 取队首并执行 io：缓冲项按 rq/wq 方向调 com 的 read/write 并返回 buf；
 * 回调项调用 cb(data) 返回 NULL；空队列返回 NULL（无 libuv 不阻塞）。 */
void *sc_ioq_pull(sc_ioq *_this) {
    if (_this->head == _this->tail) return NULL;       /* 空 */
    uintptr_t tag = (uintptr_t)_this->_buf[_this->head % _this->cap];
    if (tag != 0) {                                      /* io 缓冲项 [size, buf] */
        void *buf = _this->_buf[(_this->head + 1) % _this->cap];
        _this->head += 2;
        sc_com *c = _this->com;
        uint32_t n = (uint32_t)tag;
        if (c) {
            if (c->rq == _this) { if (c->read)  c->read (c, buf, &n); }
            else                { if (c->write) c->write(c, buf, &n); }
        }
        return buf;
    }
    /* 完成回调项 [0, cb, data] */
    void *cb   = _this->_buf[(_this->head + 1) % _this->cap];
    void *data = _this->_buf[(_this->head + 2) % _this->cap];
    _this->head += 3;
    if (cb) ((void (*)(void *))cb)(data);
    return NULL;
}

/* ---------------- limit：com 一次有界读视图（框架确定的读流程）---------------- */
/* limit 的 data/ending 由用户实现，size/len 为属性。框架只负责确定不变的读循环，
 * 驱动 com >> s（s 为 com[...] 句柄）：反复用 com 的 read 读入 data()+len，按 size
 * 与 ending 判截止，回写 len。缓存/边界策略全在用户的 data/ending 里，框架不写死。
 *   ending==NULL：定长，读满 size 即 sc_eof；
 *   ending!=NULL：每次最多读 size 字节后回调 ending，>=0 命中（保留其值为 len）。
 * 返回 sc_eof（读完）/ 负数（不可恢复，中断）/ sc_again（同步上下文无事件循环，报错信号）。*/
int32_t sc_limit_read(sc_com *c, sc_limit *s) {
    if (!c || !s || !c->read) return -1;
    char *base = (char *)(s->data ? s->data(s) : NULL);
    if (!base) return -1;
    for (;;) {
        uint32_t chunk;
        if (s->ending) {
            chunk = s->size;                       /* 动态：每次最多读 size */
        } else {
            if (s->len >= s->size) return sc_eof;  /* 定长：读满即完成 */
            chunk = s->size - s->len;
        }
        if (chunk == 0) return sc_eof;
        int32_t r = c->read(c, base + s->len, &chunk);
        if (r < 0) return r;                       /* 不可恢复，中断 */
        if (r == sc_again) return sc_again;        /* 异步挂起：同步上下文由调用方报错 */
        s->len += chunk;
        if (s->ending) {                           /* 动态截止：回调判定（每对象方法指针） */
            int32_t m = s->ending(s);
            if (m >= 0) { s->len = (uint32_t)m; return sc_eof; }
        } else if (s->len >= s->size) {
            return sc_eof;                         /* 定长读满 */
        }
        if (r == sc_eof || chunk == 0) return sc_eof;  /* 设备已无更多数据 */
    }
}

/* ---------------- thread：线程（run 语句原语，语言内核） ----------------
 * 跨平台原语经 platform.h（POSIX pthread / Windows 线程 API）。run 依赖 thread，
 * 故其运行时下沉到此（op_impl.c 始终随工程编译链接，无需 inc）。
 *
 * run 联合实体单块布局：[thread][rpc 参数 psize][thd_impl]
 *   t + 1            → rpc 参数（与 codegen 约定：p + sizeof(thread)）
 *   t->h             → 实现私有区 thd_impl（同块尾部）
 * joinable：join 等待后整块释放；detach：线程入口结束后整块自释放 */
typedef struct {
#if P_WIN
    HANDLE     t;          /* 平台句柄：仅 joinable 使用（join 等待/关闭） */
#else
    pthread_t  t;
#endif
    void     (*fn)(void *); /* rpc 实际函数 */
    uint8_t    joinable;
} thd_impl;

/* 跨平台统一线程 id：复用 platform.h 的 P_thread_id（mach tid / gettid / GetCurrentThreadId） */
static uint64_t thd_current_id(void) {
    return P_thread_id();
}

#if P_WIN
static DWORD WINAPI thd_entry(LPVOID p) {
#else
static void *thd_entry(void *p) {
#endif
    sc_thread   *t  = (sc_thread *)p;
    thd_impl *im = (thd_impl *)t->h;
    t->id = thd_current_id();
    im->fn((void *)(t + 1));            /* 执行 rpc 实际函数（参数紧随 thread） */
    if (!im->joinable) sc_recycle(t);   /* detach：自释放整块 */
    return 0;
}

uint8_t sc_thread_run(void (*fn)(void *), const void *params, size_t psize, sc_thread **out,
                   uint32_t stack, uint8_t prio) {
    if (out) *out = NULL;
    if (!fn) return 0;
    sc_thread *t = (sc_thread *)sc_chunk(sizeof(sc_thread) + psize + sizeof(thd_impl));   /* rpc 参数联合块：确定性池化（恒走 mem chunk） */
    if (!t) return 0;
    t->id = 0;
    t->h = (char *)(t + 1) + psize;     /* 私有区位于参数之后（同块） */
    if (params && psize) memcpy(t + 1, params, psize);
    thd_impl *im = (thd_impl *)t->h;
    im->fn = fn;
    im->joinable = out ? 1 : 0;
#if P_WIN
    /* stack=0 → 默认栈；非 0 作为初始提交栈字节数 */
    HANDLE h = CreateThread(NULL, (SIZE_T)stack, thd_entry, t, 0, NULL);
    if (!h) { sc_recycle(t); return 0; }
    /* prio：最佳努力映射到 Windows 线程优先级（0=默认不调整） */
    if (prio) {
        int wp = prio < 64 ? THREAD_PRIORITY_BELOW_NORMAL
               : prio < 128 ? THREAD_PRIORITY_NORMAL
               : prio < 192 ? THREAD_PRIORITY_ABOVE_NORMAL
                            : THREAD_PRIORITY_HIGHEST;
        SetThreadPriority(h, wp);
    }
    if (out) { im->t = h; *out = t; }
    else CloseHandle(h);                /* detach：关闭句柄，线程自释放 */
#else
    pthread_t h;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (!out)                           /* detach：创建即分离，入口结束自释放 */
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (stack) {                        /* 设定栈大小（不低于平台下限） */
#ifdef PTHREAD_STACK_MIN
        size_t sz = stack < (uint32_t)PTHREAD_STACK_MIN
                    ? (size_t)PTHREAD_STACK_MIN : (size_t)stack;
#else
        size_t sz = (size_t)stack;
#endif
        pthread_attr_setstacksize(&attr, sz);
    }
    int err = pthread_create(&h, &attr, thd_entry, t);
    pthread_attr_destroy(&attr);
    if (err) { sc_recycle(t); return 0; }
    /* prio：最佳努力（多数平台 SCHED_OTHER 不支持线程优先级，失败即忽略） */
    if (prio) {
        struct sched_param sp;
        memset(&sp, 0, sizeof(sp));
        sp.sched_priority = (int)prio;
        pthread_setschedparam(h, SCHED_RR, &sp);
    }
    if (out) { im->t = h; *out = t; }   /* 仅 joinable 记录句柄（入口不读它） */
#endif
    return 1;
}

void sc_thread_join(sc_thread *_this) {
    if (!_this || !_this->h) return;
    thd_impl *im = (thd_impl *)_this->h;
    if (!im->joinable) return;          /* 防误用：detach 线程不可 join */
#if P_WIN
    WaitForSingleObject(im->t, INFINITE);
    CloseHandle(im->t);
#else
    pthread_join(im->t, NULL);
#endif
    sc_recycle(_this);                  /* 回收联合实体（thread + 参数 + 私有区；确定性池化） */
}

/* ---------------- print：日志输出（语言关键字） ----------------
 * print：C printf 风格日志输出。print 属语言内核，故其运行时下沉到此
 * （op_impl.c 始终随工程编译链接，无需 inc）。
 *   - 首参 chn：u1 级别/通道（「级别就是通道」）。0=普通 stdout（默认，无着色）；
 *     1..6 = F/E/W/I/D/V（见 op.h enum / op.sc def log），按级别着色 stdout
 *   - 输出：一行文本（仅 tty 着色；I=状态用默认色；自动补换行；单次 fprintf 防撕裂）
 *   - 级别过滤：环境变量 SC_LOG=F/E/W/I/D/V（默认 D；更详尽的级别被丢弃），首次读取
 *   - 系统日志：环境变量 SC_LOG_SYS 非空 → 额外经 platform.h 的 P_log_sys 写系统日志
 *     （Win=OutputDebugString / macOS=os_log / Linux=syslog / Android=logcat ...） */

/* 级别字符：1=F 2=E 3=W 4=I 5=D 6=V（顺序对齐 op.sc def log 与 stdc log_level_e） */
static const char SC_LV_CHARS[] = "FEWIDV";
#define SC_LV_DEF 5 /* D：默认过滤阈值（V 更详尽，默认丢弃） */

/* 单行日志缓冲字节数：编译期可覆盖（-DSC_PRINT_BUF=4096），超长截断 */
#ifndef SC_PRINT_BUF
#define SC_PRINT_BUF 2048
#endif

static int sc_log_level(void) {
    static int s_level = 0;
    if (!s_level) {
        s_level = SC_LV_DEF;
        const char *e = getenv("SC_LOG");
        if (e && *e) {
            const char *p = strchr(SC_LV_CHARS, *e);
            if (p) s_level = (int)(p - SC_LV_CHARS) + 1;
        }
    }
    return s_level;
}

/* 级别 → ANSI 前景色（tty 时用）；I(状态)=默认色（空串） */
static const char *sc_lv_color(int lv) {
    switch (lv) {
        case 1: return P_ANSI_PURPLE;  /* F 致命 */
        case 2: return P_ANSI_RED;     /* E 错误 */
        case 3: return P_ANSI_YELLOW;  /* W 警告 */
        case 5: return P_ANSI_CYAN;    /* D 调试 */
        case 6: return P_ANSI_GRAY;    /* V 详尽 */
        default: return "";            /* I 状态：默认色 */
    }
}

/* stdout 是否着色：一次探测（tty 才着色，重定向/管道输出纯文本）；Windows 顺带启用 VT */
static int sc_use_color(void) {
    static int s = -1;
    if (s < 0) {
        s = P_isatty(stdout) ? 1 : 0;
#if P_WIN
        if (s) {
            HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
            DWORD m = 0;
            if (GetConsoleMode(h, &m))
                SetConsoleMode(h, m | 0x0004u /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */);
        }
#endif
    }
    return s;
}

/* 系统日志镜像开关：环境变量 SC_LOG_SYS 非空 → print 额外写系统日志（一次读取） */
static int sc_log_sys_on(void) {
    static int s = -1;
    if (s < 0) { const char *e = getenv("SC_LOG_SYS"); s = (e && *e) ? 1 : 0; }
    return s;
}

void sc_print(uint8_t chn, const char *fmt, ...) {
    if (!fmt) return;

    const int lv = (int)chn;                 /* 0=普通 stdout；1..6=F/E/W/I/D/V */
    if (lv >= 1 && lv <= 6 && lv > sc_log_level()) return;   /* 级别过滤 */

    char text[SC_PRINT_BUF];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    if (n < 0) return;
    if (n >= (int)sizeof(text)) n = (int)sizeof(text) - 1;
    /* 去掉用户尾部换行（统一由本函数补一个），便于着色包裹与系统日志单行 */
    while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r')) text[--n] = '\0';

    /* stdout：按级别着色（单次 fprintf，多线程行内不撕裂） */
    if (lv >= 1 && lv <= 6 && sc_use_color()) {
        const char *c = sc_lv_color(lv);
        if (*c) fprintf(stdout, "%s%s%s\n", c, text, P_ANSI_RESET);
        else    fprintf(stdout, "%s\n", text);
    } else {
        fprintf(stdout, "%s\n", text);
    }

    /* 可选：镜像到系统日志（跨平台自适应，实现在 platform.h） */
    if (lv >= 1 && lv <= 6 && sc_log_sys_on())
        P_log_sys(lv, NULL, text);
}

/* ============================================================================
 * 异步内核（语言自有机制：双多路复用后端）
 *
 * future 是语言自身的内核机制，连同 async_init/loop/final、future_*、async_io、
 * com_*_async 全部实现在此（op_impl.c 始终随工程编译链接）。叶子原语生态（delay
 * 及 tcp/udp/... 等）属“异步功能库”，实现在 async 模块（inc async.sc），经本层
 * 暴露的钩子接入：op_timer_arm（基础定时器）、op_uv_loop（uv 事件循环句柄）。
 *
 * 多路复用后端在编译期二选一：
 *   · 默认（无 SCC_WITH_UV）：语言自有 O(1) 就绪通知内核。按平台选最优后端——
 *       Linux=epoll / macOS·BSD=kqueue / Windows=IOCP / 其它 POSIX=poll(兜底)；
 *       跨线程唤醒 POSIX 用自管道、Windows 用 PostQueuedCompletionStatus；
 *       互斥 POSIX 用 pthread、Windows 用 CRITICAL_SECTION。单事件循环线程协作式，
 *       future_done 任意线程安全。com io 就绪句柄常驻注册进 mux，避免每轮重建（O(1)）。
 *   · -DSCC_WITH_UV：libuv 后端（epoll/kqueue/IOCP）。op 层事件循环即 uv_loop，
 *     跨线程唤醒用 uv_async，定时/网络等叶子原语可直接挂 op_uv_loop() 上的 uv 句柄。
 *
 * 二者公开符号一致；生成码与 async 模块对后端无感。
 * ==========================================================================*/

/* ---- 后端无关的共享状态与原语 ---- */
static sc_future *g_ready_head;             /* 就绪队列头（经 future.next 串联） */
static sc_future *g_ready_tail;             /* 就绪队列尾 */
static long    g_pending;                /* 已创建未完成 future 计数 */
static int     g_inited;                 /* async_init 是否已建立 */
static int   (*g_proc)(int id, sc_future *f);   /* async_loop 传入的 id 派发回调（返回<0 停） */

static void push_ready(sc_future *f) {      /* 调用方持锁 */
    f->next = NULL;
    if (g_ready_tail) g_ready_tail->next = f;
    else              g_ready_head = f;
    g_ready_tail = f;
}
static sc_future *pop_ready(void) {         /* 调用方持锁 */
    sc_future *f = g_ready_head;
    if (f) {
        g_ready_head = (sc_future *)f->next;
        if (!g_ready_head) g_ready_tail = NULL;
        f->next = NULL;
    }
    return f;
}

uint8_t sc_future_ready(sc_future *_this) { return (uint8_t)(_this ? _this->ready : 0); }
void   *sc_future_get(sc_future *_this)   { return _this ? _this->result : NULL; }
void   *sc_future_ctx(sc_future *_this)   { return _this ? _this->ctx : NULL; }

sc_future *sc_future_new(void) {
    sc_future *f = (sc_future *)calloc(1, sizeof(sc_future));
    sc_future_init(f);
    return f;
}

/* ---------------- session：rpc 延迟应答会话 TLS（见 op.h；后端中立） ----------------
 * 当前正在执行的 rpc 调用会话 + 是否被「裸 async」领取，按线程隔离（消费者各自一份）。
 * 纯本线程读写、无跨线程共享，故无需加锁；身份对象由实现模块（mt）在调用方栈构造，
 * op 内核只透传指针。begin 在执行 rpc 体前调用、current 由体内裸 async 取用并置领取标记、
 * taken 在体返回后查询以决定即时应答（未领取）抑或延迟应答（已领取，等将来 done 兑现）。
 * 与异步后端（poll/libuv）无关，故置于后端条件编译之外。 */
static TLS sc_session *g_cur_session;
static TLS int      g_cur_session_taken;

void sc_op_session_begin(sc_session *s) { g_cur_session = s; g_cur_session_taken = 0; }
sc_session *sc_op_session_current(void) { g_cur_session_taken = 1; return g_cur_session; }
int sc_op_session_taken(void) { return g_cur_session_taken; }

#ifdef SCC_WITH_UV
/* ===================== 后端 B：libuv（-DSCC_WITH_UV） ===================== */
#include <uv.h>

static uv_loop_t  g_loop;
static uv_async_t g_wake;
static uv_mutex_t g_mu;

/* g_wake 回调：循环线程排空就绪队列；协程帧 resume，否则按 id 派发。 */
static void drain_cb(uv_async_t *h) {
    (void)h;
    for (;;) {
        uv_mutex_lock(&g_mu);
        sc_future *f = pop_ready();
        long pend = g_pending;
        int  empty = (g_ready_head == NULL);
        uv_mutex_unlock(&g_mu);
        if (f) {
            if (f->frame && f->resume) {
                f->resume(f->frame);
            } else if (f->id >= 0 && g_proc) {
                int rc = g_proc(f->id, f);
                free(f);
                if (rc < 0) { uv_stop(&g_loop); break; }
            } else if (f->id >= 0) {
                free(f);
            }
            continue;
        }
        if (pend == 0 && empty) uv_stop(&g_loop);
        break;
    }
}

void sc_async_init(void) {
    if (g_inited) return;
    uv_loop_init(&g_loop);
    uv_async_init(&g_loop, &g_wake, drain_cb);
    uv_mutex_init(&g_mu);
    g_ready_head = g_ready_tail = NULL;
    g_pending = 0;
    g_proc = NULL;
    g_inited = 1;
}

void sc_async_final(void) {
    if (!g_inited) return;
    uv_close((uv_handle_t *)&g_wake, NULL);
    uv_run(&g_loop, UV_RUN_NOWAIT);
    uv_loop_close(&g_loop);
    uv_mutex_destroy(&g_mu);
    g_ready_head = g_ready_tail = NULL;
    g_pending = 0;
    g_inited = 0;
}

void sc_future_init(sc_future *_this) {
    _this->id  = -1;
    _this->ctx = NULL;
    uv_mutex_lock(&g_mu);
    g_pending++;
    uv_mutex_unlock(&g_mu);
}

void sc_future_done(sc_future *f, void *result) {
    int  enqueue;
    long remaining;
    uv_mutex_lock(&g_mu);
    f->result = result;
    f->ready  = 1;
    enqueue = (f->frame != NULL) || (f->id >= 0);
    if (enqueue) push_ready(f);
    g_pending--;
    remaining = g_pending;
    uv_mutex_unlock(&g_mu);
    if (enqueue || remaining == 0) uv_async_send(&g_wake);
}

uint8_t sc_future_await(sc_future *f, void *frame, void (*resume)(void *)) {
    uint8_t r;
    uv_mutex_lock(&g_mu);
    f->frame  = frame;
    f->resume = resume;
    r = (uint8_t)f->ready;
    uv_mutex_unlock(&g_mu);
    return r;
}

static void uv_drive(void) {
    uv_mutex_lock(&g_mu);
    int idle = (g_pending == 0 && g_ready_head == NULL);
    uv_mutex_unlock(&g_mu);
    if (idle) return;                 /* 无未决：避免空转阻塞（g_wake 常驻 ref） */
    uv_run(&g_loop, UV_RUN_DEFAULT);
}

void sc_async_loop(void *proc) {
    if (!g_inited) return;
    g_proc = (int (*)(int, sc_future *))proc;
    uv_drive();
}

void sc_async_io(void) {
    if (!g_inited) return;
    uv_drive();
}

/* op 层暴露给 async 叶子原语生态的 uv 事件循环句柄（poll 后端返回 NULL）。 */
void *sc_op_uv_loop(void) { return g_inited ? (void *)&g_loop : NULL; }

/* 基础定时器原语（uv_timer 实现）：async 模块的 delay 等在其上构建。 */
typedef struct uv_timer_req { uv_timer_t timer; sc_future *fut; } uv_timer_req;
static void op_timer_closed(uv_handle_t *h) { free(h->data); }
static void op_timer_fired(uv_timer_t *t) {
    uv_timer_req *r = (uv_timer_req *)t->data;
    sc_future *f = r->fut;
    sc_future_done(f, NULL);
    uv_close((uv_handle_t *)t, op_timer_closed);
}
void sc_op_timer_arm(sc_future *f, uint32_t ms) {
    uv_timer_req *r = (uv_timer_req *)calloc(1, sizeof(uv_timer_req));
    r->fut = f;
    uv_timer_init(&g_loop, &r->timer);
    r->timer.data = r;
    uv_timer_start(&r->timer, op_timer_fired, ms, 0);
}

/* com 异步收发（uv 后端）：可多路复用（readable/writable 给句柄）→ uv_poll；
 * 否则即时收发并兑现。 */
typedef struct uv_io_req {
    uv_poll_t poll;
    sc_com      *c;
    int       dir;          /* 0=读 1=写 */
    void     *buf;
    uint32_t  size;
    sc_future   *fut;
} uv_io_req;

static void uv_io_complete(uv_io_req *r) {
    uint32_t n = r->size;
    if (r->dir == 0) { if (r->c->read)  r->c->read(r->c, r->buf, &n); }
    else             { if (r->c->write) r->c->write(r->c, r->buf, &n); }
    sc_future_done(r->fut, (void *)(intptr_t)n);
}
static void uv_io_closed(uv_handle_t *h) { free(h->data); }
static void uv_io_cb(uv_poll_t *p, int st, int ev) {
    (void)st; (void)ev;
    uv_io_req *r = (uv_io_req *)p->data;
    uv_poll_stop(p);
    uv_io_complete(r);
    uv_close((uv_handle_t *)p, uv_io_closed);
}

static sc_future *uv_com_io(sc_com *c, int dir, void *buf, uint32_t size) {
    sc_future *f = sc_future_new();
    int32_t (*probe)(sc_com *, void **) = (dir == 0) ? c->readable : c->writable;
    void   *id = NULL;
    if (probe) probe(c, &id);
    if (probe && id != NULL) {                  /* 多路复用句柄 → uv_poll */
        uv_io_req *r = (uv_io_req *)calloc(1, sizeof(uv_io_req));
        r->c = c; r->dir = dir; r->buf = buf; r->size = size; r->fut = f;
        uv_poll_init(&g_loop, &r->poll, (int)(intptr_t)id);
        r->poll.data = r;
        uv_poll_start(&r->poll, (dir == 0) ? UV_READABLE : UV_WRITABLE, uv_io_cb);
        return f;
    }
    uint32_t n = size;                          /* 非多路复用：即时收发 */
    if (dir == 0) { if (c->read)  c->read(c, buf, &n); }
    else          { if (c->write) c->write(c, buf, &n); }
    sc_future_done(f, (void *)(intptr_t)n);
    return f;
}

sc_future *sc_com_read_async(sc_com *c, void *data, uint32_t size) { return uv_com_io(c, 0, data, size); }
sc_future *sc_com_write_async(sc_com *c, void *buf,  uint32_t size) { return uv_com_io(c, 1, buf,  size); }

/* 【E】com[...] 句柄异步有界读（uv 后端）：可多路复用 → uv_poll，回调里跑 limit_read，
 * 遇 sc_again 继续 poll、命中/出错则兑现；非多路复用 → 即时跑完。 */
typedef struct uv_limit_req {
    uv_poll_t poll;
    sc_com      *c;
    sc_limit    *lim;
    sc_future   *fut;
} uv_limit_req;

static void uv_limit_done(uv_limit_req *r, int32_t rc) {
    int32_t res = (rc < 0) ? rc : (int32_t)r->lim->len;
    sc_future_done(r->fut, (void *)(intptr_t)res);
}
static void uv_limit_closed(uv_handle_t *h) { free(h->data); }
static void uv_limit_cb(uv_poll_t *p, int st, int ev) {
    (void)st; (void)ev;
    uv_limit_req *r = (uv_limit_req *)p->data;
    int32_t rc = limit_read(r->c, r->lim);
    if (rc == sc_again) return;                 /* 未满：继续等待下次就绪 */
    uv_poll_stop(p);
    uv_limit_done(r, rc);
    uv_close((uv_handle_t *)p, uv_limit_closed);
}

sc_future *sc_com_limit_read_async(sc_com *c, sc_limit *s) {
    sc_future *f = sc_future_new();
    void   *id = NULL;
    if (c->readable) c->readable(c, &id);
    if (c->readable && id != NULL) {            /* 多路复用句柄 → uv_poll 驱动 */
        uv_limit_req *r = (uv_limit_req *)calloc(1, sizeof(uv_limit_req));
        r->c = c; r->lim = s; r->fut = f;
        uv_poll_init(&g_loop, &r->poll, (int)(intptr_t)id);
        r->poll.data = r;
        uv_poll_start(&r->poll, UV_READABLE, uv_limit_cb);
        return f;
    }
    int32_t rc = limit_read(c, s);              /* 非多路复用：即时跑完 */
    int32_t res = (rc < 0) ? rc : (int32_t)s->len;
    sc_future_done(f, (void *)(intptr_t)res);
    return f;
}

#else
/* ===================== 后端 A：语言自有 O(1) 多路复用内核 =====================
 * 平台后端：Linux=epoll / macOS·BSD=kqueue / Windows=IOCP / 其它 POSIX=poll。
 * 统一抽象成 mux_*（多路复用器）+ 跨平台互斥/唤醒/时钟，run_loop 等上层逻辑共享。 */

/* ---- 平台后端选择 ---- */
#if P_LINUX
#  define SC_MUX_EPOLL  1
#elif P_DARWIN || P_BSD
#  define SC_MUX_KQUEUE 1
#elif P_WIN
#  define SC_MUX_IOCP   1
#else
#  define SC_MUX_POLL   1
#endif

/* ---- 平台头（互斥/线程基础原语由 platform.h 提供，此处仅引多路复用后端头） ---- */
#if P_WIN
#  include <winsock2.h>
#else
#  include <fcntl.h>
#  if defined(SC_MUX_EPOLL)
#    include <sys/epoll.h>
#  elif defined(SC_MUX_KQUEUE)
#    include <sys/event.h>
#    include <sys/time.h>
#  else
#    include <poll.h>
#  endif
#endif

/* ---- 多路复用句柄类型（Windows=SOCKET，POSIX=fd） ---- */
#if P_WIN
typedef SOCKET SC_FD;
#  define SC_FD_NONE (INVALID_SOCKET)
#else
typedef int SC_FD;
#  define SC_FD_NONE (-1)
#endif

static sc_mutex_t g_mu;                  /* 就绪队列 / g_pending 的跨线程互斥 */

static uint64_t now_ms(void) {
    P_clock c;
    P_clock_now(&c);                    /* CLOCK_MONOTONIC */
    return (uint64_t)c.tv_sec * 1000ull + (uint64_t)c.tv_nsec / 1000000ull;
}

/* delay 定时器节点（单调时钟截止） */
typedef struct timer_node {
    struct timer_node *next;
    uint64_t           deadline_ms;
    sc_future            *fut;
} timer_node;
static timer_node *g_timers;            /* 仅循环线程访问 */

/* com 异步 io 请求节点（com_*_async 登记，事件循环驱动）。
 * 句柄就绪后常驻注册进 mux，避免每轮重建（O(1)）；非多路复用（无句柄）走重探轮询。 */
typedef struct io_req {
    struct io_req *next;
    sc_com           *c;
    int            dir;                 /* 0=读 1=写 */
    void          *buf;
    uint32_t       size;
    sc_limit         *lim;                 /* 非 NULL=【E】com[...] 句柄有界读（框架读循环） */
    sc_future        *fut;
    SC_FD          fd;                  /* 多路复用句柄（SC_FD_NONE=未取得/不支持） */
    int            registered;          /* 已注册进 mux（句柄常驻监听） */
    int            armed;               /* 本轮判定就绪，待执行 io */
    int            needs_poll;          /* 无句柄的 again：每轮重探（小超时轮询） */
} io_req;
static io_req *g_io_reqs;               /* 仅循环线程访问 */

/* ============================ 多路复用器 mux_* ============================
 * mux_open/close：建立/销毁后端 + 跨线程唤醒通道。
 * wake：任意线程唤醒阻塞中的事件循环。
 * mux_arm/disarm：把就绪句柄常驻注册/注销（udata=对应 io_req）。
 * mux_wait：等待至多 timeout_ms；唤醒事件被消费，句柄就绪则置对应 io_req->armed。 */

#if defined(SC_MUX_EPOLL)
/* ---------------------------- Linux：epoll ---------------------------- */
static int g_ep = -1;
static int g_wake_pipe[2] = { -1, -1 };  /* 自管道：data.ptr=NULL 标记唤醒事件 */

static int mux_open(void) {
    g_ep = epoll_create1(0);
    if (g_ep < 0) return -1;
    if (pipe(g_wake_pipe) != 0) { g_wake_pipe[0] = g_wake_pipe[1] = -1; return -1; }
    for (int i = 0; i < 2; i++) {
        int fl = fcntl(g_wake_pipe[i], F_GETFL, 0);
        if (fl >= 0) fcntl(g_wake_pipe[i], F_SETFL, fl | O_NONBLOCK);
    }
    struct epoll_event ev;
    memset(&ev, 0, sizeof ev);
    ev.events   = EPOLLIN;
    ev.data.ptr = NULL;                  /* 唤醒哨兵 */
    epoll_ctl(g_ep, EPOLL_CTL_ADD, g_wake_pipe[0], &ev);
    return 0;
}
static void mux_close(void) {
    for (int i = 0; i < 2; i++) { if (g_wake_pipe[i] >= 0) close(g_wake_pipe[i]); g_wake_pipe[i] = -1; }
    if (g_ep >= 0) close(g_ep);
    g_ep = -1;
}
static void wake(void) {
    if (g_wake_pipe[1] < 0) return;
    char b = 1; ssize_t r;
    do { r = write(g_wake_pipe[1], &b, 1); } while (r < 0 && errno == EINTR);
    (void)r;
}
static void drain_wake(void) {
    char buf[64];
    while (read(g_wake_pipe[0], buf, sizeof buf) > 0) { /* 丢弃 */ }
}
static void mux_arm(SC_FD fd, int dir, io_req *r) {
    struct epoll_event ev;
    memset(&ev, 0, sizeof ev);
    ev.events   = (dir == 0) ? EPOLLIN : EPOLLOUT;
    ev.data.ptr = r;
    epoll_ctl(g_ep, EPOLL_CTL_ADD, fd, &ev);
}
static void mux_disarm(SC_FD fd, int dir) {
    (void)dir;
    epoll_ctl(g_ep, EPOLL_CTL_DEL, fd, NULL);
}
static void mux_wait(int timeout_ms) {
    struct epoll_event evs[64];
    int n = epoll_wait(g_ep, evs, 64, timeout_ms);
    for (int i = 0; i < n; i++) {
        if (evs[i].data.ptr == NULL) drain_wake();
        else ((io_req *)evs[i].data.ptr)->armed = 1;
    }
}

#elif defined(SC_MUX_KQUEUE)
/* -------------------------- macOS·BSD：kqueue -------------------------- */
static int g_kq = -1;
static int g_wake_pipe[2] = { -1, -1 };  /* 自管道：udata=NULL 标记唤醒事件 */

static int mux_open(void) {
    g_kq = kqueue();
    if (g_kq < 0) return -1;
    if (pipe(g_wake_pipe) != 0) { g_wake_pipe[0] = g_wake_pipe[1] = -1; return -1; }
    for (int i = 0; i < 2; i++) {
        int fl = fcntl(g_wake_pipe[i], F_GETFL, 0);
        if (fl >= 0) fcntl(g_wake_pipe[i], F_SETFL, fl | O_NONBLOCK);
    }
    struct kevent kev;
    EV_SET(&kev, g_wake_pipe[0], EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL); /* 唤醒哨兵 */
    kevent(g_kq, &kev, 1, NULL, 0, NULL);
    return 0;
}
static void mux_close(void) {
    for (int i = 0; i < 2; i++) { if (g_wake_pipe[i] >= 0) close(g_wake_pipe[i]); g_wake_pipe[i] = -1; }
    if (g_kq >= 0) close(g_kq);
    g_kq = -1;
}
static void wake(void) {
    if (g_wake_pipe[1] < 0) return;
    char b = 1; ssize_t r;
    do { r = write(g_wake_pipe[1], &b, 1); } while (r < 0 && errno == EINTR);
    (void)r;
}
static void drain_wake(void) {
    char buf[64];
    while (read(g_wake_pipe[0], buf, sizeof buf) > 0) { /* 丢弃 */ }
}
static void mux_arm(SC_FD fd, int dir, io_req *r) {
    struct kevent kev;
    EV_SET(&kev, fd, (dir == 0) ? EVFILT_READ : EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, r);
    kevent(g_kq, &kev, 1, NULL, 0, NULL);
}
static void mux_disarm(SC_FD fd, int dir) {
    struct kevent kev;
    EV_SET(&kev, fd, (dir == 0) ? EVFILT_READ : EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(g_kq, &kev, 1, NULL, 0, NULL);
}
static void mux_wait(int timeout_ms) {
    struct kevent evs[64];
    struct timespec ts, *pts = NULL;
    if (timeout_ms >= 0) {
        ts.tv_sec  = timeout_ms / 1000;
        ts.tv_nsec = (long)(timeout_ms % 1000) * 1000000L;
        pts = &ts;
    }
    int n = kevent(g_kq, NULL, 0, evs, 64, pts);
    for (int i = 0; i < n; i++) {
        if (evs[i].udata == NULL) drain_wake();
        else ((io_req *)evs[i].udata)->armed = 1;
    }
}

#elif defined(SC_MUX_IOCP)
/* ------------------------------ Windows：IOCP ------------------------------
 * IOCP 为“完成”模型，与本框架 com.readable/writable 的“就绪”契约不同：故 IOCP 在此
 * 承载事件循环核心——跨线程唤醒（PostQueuedCompletionStatus）与定时器等待
 * （GetQueuedCompletionStatus 超时）。com io 句柄的“就绪”探测用 winsock select()
 * 每轮 0 超时非阻塞判定（真·IOCP 就绪需未公开的 AFD 轮询，超出范围）。 */
static HANDLE g_iocp = NULL;
static ULONG_PTR const WAKE_KEY = 1;

static int mux_open(void) {
    g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    return g_iocp ? 0 : -1;
}
static void mux_close(void) {
    if (g_iocp) CloseHandle(g_iocp);
    g_iocp = NULL;
}
static void wake(void) {
    if (g_iocp) PostQueuedCompletionStatus(g_iocp, 0, WAKE_KEY, NULL);
}
static void mux_arm(SC_FD fd, int dir, io_req *r)  { (void)fd; (void)dir; (void)r; }   /* select 模型：无常驻注册 */
static void mux_disarm(SC_FD fd, int dir)          { (void)fd; (void)dir; }
static void mux_wait(int timeout_ms) {
    /* 先用 select 0 超时非阻塞判定 com 句柄就绪 */
    fd_set rfds, wfds;
    FD_ZERO(&rfds); FD_ZERO(&wfds);
    int has_fd = 0;
    for (io_req *r = g_io_reqs; r; r = r->next) {
        if (!r->registered || r->fd == SC_FD_NONE) continue;
        if (r->dir == 0) FD_SET(r->fd, &rfds); else FD_SET(r->fd, &wfds);
        has_fd = 1;
    }
    if (has_fd) {
        struct timeval tv = { 0, 0 };
        if (select(0, &rfds, &wfds, NULL, &tv) > 0) {
            for (io_req *r = g_io_reqs; r; r = r->next) {
                if (!r->registered || r->fd == SC_FD_NONE) continue;
                if ((r->dir == 0 && FD_ISSET(r->fd, &rfds)) ||
                    (r->dir == 1 && FD_ISSET(r->fd, &wfds)))
                    r->armed = 1;
            }
        }
    }
    /* 再阻塞在 IOCP 上等定时器/唤醒（有 fd 待轮询时用 1ms 轮询步进） */
    DWORD to = (timeout_ms < 0) ? INFINITE : (DWORD)timeout_ms;
    if (has_fd && (timeout_ms < 0 || timeout_ms > 1)) to = 1;
    DWORD bytes; ULONG_PTR key; LPOVERLAPPED ov;
    GetQueuedCompletionStatus(g_iocp, &bytes, &key, &ov, to);  /* 唤醒/超时即返回 */
}

#else
/* --------------------------- 其它 POSIX：poll(兜底) --------------------------- */
static int g_wake_pipe[2] = { -1, -1 };

static int mux_open(void) {
    if (pipe(g_wake_pipe) != 0) { g_wake_pipe[0] = g_wake_pipe[1] = -1; return -1; }
    for (int i = 0; i < 2; i++) {
        int fl = fcntl(g_wake_pipe[i], F_GETFL, 0);
        if (fl >= 0) fcntl(g_wake_pipe[i], F_SETFL, fl | O_NONBLOCK);
    }
    return 0;
}
static void mux_close(void) {
    for (int i = 0; i < 2; i++) { if (g_wake_pipe[i] >= 0) close(g_wake_pipe[i]); g_wake_pipe[i] = -1; }
}
static void wake(void) {
    if (g_wake_pipe[1] < 0) return;
    char b = 1; ssize_t r;
    do { r = write(g_wake_pipe[1], &b, 1); } while (r < 0 && errno == EINTR);
    (void)r;
}
static void drain_wake(void) {
    char buf[64];
    while (read(g_wake_pipe[0], buf, sizeof buf) > 0) { /* 丢弃 */ }
}
static void mux_arm(SC_FD fd, int dir, io_req *r)  { (void)fd; (void)dir; (void)r; }   /* poll 无状态：靠 io_req->fd 每轮重建 */
static void mux_disarm(SC_FD fd, int dir)          { (void)fd; (void)dir; }
static void mux_wait(int timeout_ms) {
    int nreq = 0;
    for (io_req *r = g_io_reqs; r; r = r->next)
        if (r->registered && r->fd >= 0) nreq++;
    struct pollfd *pfds = (struct pollfd *)malloc(sizeof(struct pollfd) * (size_t)(nreq + 1));
    pfds[0].fd = g_wake_pipe[0]; pfds[0].events = POLLIN; pfds[0].revents = 0;
    int nfd = 1;
    for (io_req *r = g_io_reqs; r; r = r->next) {
        if (!(r->registered && r->fd >= 0)) continue;
        pfds[nfd].fd = r->fd;
        pfds[nfd].events = (r->dir == 0) ? POLLIN : POLLOUT;
        pfds[nfd].revents = 0;
        nfd++;
    }
    int pr = poll(pfds, (nfds_t)nfd, timeout_ms);
    if (pr < 0 && errno != EINTR) { free(pfds); return; }
    if (pfds[0].revents & POLLIN) drain_wake();
    int idx = 1;
    for (io_req *r = g_io_reqs; r; r = r->next) {
        if (!(r->registered && r->fd >= 0)) continue;
        if (pfds[idx].revents & (POLLIN | POLLOUT | POLLERR | POLLHUP)) r->armed = 1;
        idx++;
    }
    free(pfds);
}
#endif /* mux 后端 */

/* ============================ 后端无关上层逻辑 ============================ */
void sc_async_init(void) {
    if (g_inited) return;
    P_mutex_init(&g_mu);
    g_ready_head = g_ready_tail = NULL;
    g_pending = 0;
    g_proc    = NULL;
    g_timers  = NULL;
    g_io_reqs = NULL;
    mux_open();
    g_inited = 1;
}

void sc_async_final(void) {
    if (!g_inited) return;
    mux_close();
    while (g_timers)  { timer_node *t = g_timers;  g_timers  = t->next; free(t); }
    while (g_io_reqs) { io_req     *r = g_io_reqs; g_io_reqs = r->next; free(r); }
    P_mutex_final(&g_mu);
    g_ready_head = g_ready_tail = NULL;
    g_pending = 0;
    g_inited  = 0;
}

void sc_future_init(sc_future *_this) {
    _this->id  = -1;         /* 默认无标签：仅协程 await 用（0 是合法 id，不能作哨兵） */
    _this->ctx = NULL;       /* 默认无上下文；future<ID>(ctx) 由构造辅助回填 */
    P_mutex_lock(&g_mu);
    g_pending++;
    P_mutex_unlock(&g_mu);
}

void sc_future_done(sc_future *f, void *result) {
    int  enqueue;
    long remaining;
    P_mutex_lock(&g_mu);
    f->result = result;
    f->ready  = 1;
    enqueue = (f->frame != NULL) || (f->id >= 0);   /* 协程等待者 / 带 id 待派发 → 入队 */
    if (enqueue) push_ready(f);
    g_pending--;
    remaining = g_pending;
    P_mutex_unlock(&g_mu);
    if (enqueue || remaining == 0) wake();           /* 唤醒去 resume/派发，或去判退出 */
}

uint8_t sc_future_await(sc_future *f, void *frame, void (*resume)(void *)) {
    uint8_t r;
    P_mutex_lock(&g_mu);
    f->frame  = frame;
    f->resume = resume;
    r = (uint8_t)f->ready;
    P_mutex_unlock(&g_mu);
    return r;
}

/* 排空就绪队列：协程帧 resume / 带 id 无等待者经 g_proc 派发。返回 1=请求停循环。 */
static int drain_ready(void) {
    for (;;) {
        P_mutex_lock(&g_mu);
        sc_future *f = pop_ready();
        P_mutex_unlock(&g_mu);
        if (!f) return 0;
        if (f->frame && f->resume) {
            f->resume(f->frame);
        } else if (f->id >= 0 && g_proc) {
            int rc = g_proc(f->id, f);
            free(f);
            if (rc < 0) return 1;
        } else if (f->id >= 0) {
            free(f);
        }
    }
}

static void fire_timers(uint64_t now) {
    timer_node **pp = &g_timers;
    while (*pp) {
        timer_node *t = *pp;
        if (t->deadline_ms <= now) {
            *pp = t->next;
            sc_future *f = t->fut;
            free(t);
            sc_future_done(f, NULL);
        } else {
            pp = &t->next;
        }
    }
}

/* 处理新登记/无句柄的 io_req：探测就绪句柄并常驻注册进 mux；非多路复用走重探。
 * 回填 *p_any_armed（有立即可执行的 io）/ *p_any_needs_poll（有 again 待轮询）。 */
static void process_new_reqs(int *p_any_armed, int *p_any_needs_poll) {
    int any_armed = 0, any_needs_poll = 0;
    for (io_req *r = g_io_reqs; r; r = r->next) {
        if (r->registered) continue;             /* 已常驻监听：等 mux_wait 置 armed */
        if (r->armed) { any_armed = 1; continue; }
        int32_t (*probe)(sc_com *, void **) =
            (r->dir == 0) ? r->c->readable : r->c->writable;
        if (!probe) { r->armed = 1; any_armed = 1; continue; }   /* 不探测=立即可操作 */
        void   *id = NULL;
        int32_t rc = probe(r->c, &id);
        if (id != NULL) {                        /* 多路复用句柄 → 常驻注册 */
            r->fd = (SC_FD)(intptr_t)id;
            mux_arm(r->fd, r->dir, r);
            r->registered = 1;
        } else if (rc < 0) {                     /* 出错：兑现（按 io 返回处理） */
            r->armed = 1; any_armed = 1;
        } else if (rc == 1) {                    /* 就绪 */
            r->armed = 1; any_armed = 1;
        } else {                                 /* 0=again，无句柄 → 重探轮询 */
            any_needs_poll = 1;
        }
    }
    *p_any_armed = any_armed;
    *p_any_needs_poll = any_needs_poll;
}

/* 执行本轮就绪的 io：注销 mux、收发、摘表、兑现 future。 */
static void execute_ready(void) {
    io_req **pp = &g_io_reqs;
    while (*pp) {
        io_req *r = *pp;
        if (r->armed) {
            if (r->registered && r->fd != SC_FD_NONE) { mux_disarm(r->fd, r->dir); r->registered = 0; }
            if (r->lim) {
                /* 【E】框架确定读循环：遇 again 重置等待（process_new_reqs 重探/重注册），
                 * 命中 ending/定长或出错才兑现（结果=limit.len，错误为负码）。 */
                int32_t rc = sc_limit_read(r->c, r->lim);
                if (rc == sc_again) { r->armed = 0; pp = &r->next; continue; }
                *pp = r->next;
                sc_future *f = r->fut;
                int32_t res = (rc < 0) ? rc : (int32_t)r->lim->len;
                free(r);
                sc_future_done(f, (void *)(intptr_t)res);
                continue;
            }
            uint32_t n = r->size;
            if (r->dir == 0) { if (r->c->read)  r->c->read(r->c, r->buf, &n); }
            else             { if (r->c->write) r->c->write(r->c, r->buf, &n); }
            *pp = r->next;
            sc_future *f = r->fut;
            free(r);
            sc_future_done(f, (void *)(intptr_t)n);   /* 结果=收发字节数 */
        } else {
            pp = &r->next;
        }
    }
}

static void run_loop(void) {
    if (!g_inited) return;
    for (;;) {
        if (drain_ready()) break;                    /* 派发器请求停 */
        P_mutex_lock(&g_mu);
        int done = (g_pending == 0 && g_ready_head == NULL);
        P_mutex_unlock(&g_mu);
        if (done) break;

        int any_armed = 0, any_needs_poll = 0;
        process_new_reqs(&any_armed, &any_needs_poll);

        int timeout = -1;
        if (g_timers) {
            uint64_t now = now_ms();
            uint64_t nearest = 0; int have = 0;
            for (timer_node *t = g_timers; t; t = t->next)
                if (!have || t->deadline_ms < nearest) { nearest = t->deadline_ms; have = 1; }
            if (have) timeout = (int)((nearest > now) ? (nearest - now) : 0);
        }
        if (any_armed) timeout = 0;                  /* 有立即就绪：不阻塞 */
        else if (any_needs_poll && (timeout < 0 || timeout > 1)) timeout = 1;

        mux_wait(timeout);
        fire_timers(now_ms());
        execute_ready();
    }
}

void sc_async_loop(void *proc) {
    if (!g_inited) return;
    g_proc = (int (*)(int, sc_future *))proc;   /* 按 id 派发回调（NULL=纯协程驱动） */
    run_loop();
}

void sc_async_io(void) {
    if (!g_inited) return;
    run_loop();
}

/* op 层暴露给 async 叶子原语生态的钩子（自有后端无 uv 循环）。 */
void *sc_op_uv_loop(void) { return NULL; }

/* 基础定时器原语（事件循环超时驱动）：async 模块的 delay 等在其上构建。 */
void sc_op_timer_arm(sc_future *f, uint32_t ms) {
    timer_node *t = (timer_node *)calloc(1, sizeof(timer_node));
    t->deadline_ms = now_ms() + (uint64_t)ms;
    t->fut  = f;
    t->next = g_timers;
    g_timers = t;
    wake();              /* 缩短下一次等待超时，及时按新截止重算 */
}

/* com 异步收发桥接：登记进活动表，由事件循环驱动。 */
sc_future *sc_com_read_async(sc_com *c, void *data, uint32_t size) {
    sc_future *f = sc_future_new();
    io_req *r = (io_req *)calloc(1, sizeof(io_req));
    r->c = c; r->dir = 0; r->buf = data; r->size = size; r->fut = f; r->fd = SC_FD_NONE;
    r->next = g_io_reqs;
    g_io_reqs = r;
    wake();
    return f;
}

sc_future *sc_com_write_async(sc_com *c, void *buf, uint32_t size) {
    sc_future *f = sc_future_new();
    io_req *r = (io_req *)calloc(1, sizeof(io_req));
    r->c = c; r->dir = 1; r->buf = buf; r->size = size; r->fut = f; r->fd = SC_FD_NONE;
    r->next = g_io_reqs;
    g_io_reqs = r;
    wake();
    return f;
}

/* 【E】com[...] 句柄异步有界读：登记 lim 请求，循环就绪后跑框架读流程 limit_read。 */
sc_future *sc_com_limit_read_async(sc_com *c, sc_limit *s) {
    sc_future *f = sc_future_new();
    io_req *r = (io_req *)calloc(1, sizeof(io_req));
    r->c = c; r->dir = 0; r->lim = s; r->fut = f; r->fd = SC_FD_NONE;
    r->next = g_io_reqs;
    g_io_reqs = r;
    wake();
    return f;
}
#endif /* SCC_WITH_UV */

/* tok_impl.c —— 分布式 token（tok / dep / form 机制）的默认运行时实现 v2
 *
 * 契约见同目录 tok.h（C ABI）与 op.sc（@def token 协议，方法 get/set）。
 * 经 op→tok 隐式依赖随工程始终编译链接（tok/dep/form 为语言关键字，恒可用）；
 * tok→adt 隐式依赖使 g_toks 哈希表（adt dict）随工程链接。
 *
 * v2 相对 v1 的增强（对齐 c_prototype.h/.c 的 C_Form/Enforce/Depend/input/output）：
 *   1. form 触发「就绪(ready)」依赖更新：token 形成（form）是区别于「变更(changed)」的
 *      特殊状态，首次满足门逻辑时以 TOK_ALL_READY/TOK_ANY_READY 唤起 follow
 *      （对齐 C_ALL_READY=-2 / C_ANY_READY=-1）。
 *   2. MT：id 以 '/' 开头的 token 进入多线程模式，细粒度无锁同步（对齐 '/' 前缀 MT 约定）：
 *      · 值：每-token 序列锁（seqlock）——读无锁乐观重试，写者经 seq 自旋独占；
 *      · 依赖门：每-dep 自旋锁，仅护门计数（armed/remain/all），临界区纳秒级；
 *      · follow 回调一律在释放所有锁后调用——任一线程任一时刻零持锁跑用户码，
 *        根除跨 token 锁序死锁；独立 token 子图真并行，无全局串行点。
 *      约束：combine 须纯（仅 base/input→value，不得 set/get 其他 token，它在写者独占下运行）；
 *      跨 token 副作用一律写在 follow（锁外运行）。
 *   3. g_toks 全局表改用 adt 哈希（dict，字符串键拷贝 → 裸 token*），O(1) 均摊 intern，
 *      取代 v1 线性查找。
 *   4. token 的依赖表 deps 动态增长（可增长数组），取代 v1 定长 deps[16]。
 *   5. form 前挂起 action：未 form 的 form 候选其 set 入挂起队列，form 时按 combine
 *      回放后再触发就绪——pending 动态数组与依赖表结构分离（对齐 c_prototype 的
 *      「form 前挂起 action + 依赖结构优化」）。
 *
 * 句柄、依赖记录均为进程生命周期对象（模块域静态），不单独回收。 */


/* follow 的 acting 动作码（对齐 c_prototype.h）：
 *   TOK_ALL_READY  (-2)：与门首次全部就绪（form 触发）
 *   TOK_ALL_CHANGED(-3)：与门全部已变更（set 触发）
 *   TOK_ANY_READY  (-1)：或门首次任一就绪 / 任一变更（acting 退化）
 *   TOK_BACK       (-4)：反向遍历（back t）——this->active==TOK_BACK 走反向计算
 *   TOK_LOOP       (-5)：受控反馈环迭代（token_loop_run）——this->active==TOK_LOOP 走一轮环体计算
 *   idx >= 0        ：或门，本次变更的依赖项下标 */
#define TOK_ALL_READY   (-2)
#define TOK_ALL_CHANGED (-3)
#define TOK_ANY_READY   (-1)
#define TOK_BACK        (-4)
#define TOK_LOOP        (-5)

enum { TOK_NEW = 0, TOK_READY = 1 };  /* 生命状态：NEW=未就绪（待 form） / READY=已被 form 激活 */

/* MT 同步：细粒度无锁。值用每-token seqlock（t->seq），依赖门用每-dep 自旋锁（d->glock），
 * follow 一律锁外调用（见各函数）。所有锁状态字段 calloc 零值即初始（seq 偶=可读、glock 空闲），
 * 无需运行时 init；非 MT token 全程不触原子（零开销）。下方为 seqlock / 门锁的原子助手。 */

typedef struct tok_dep {
    sc_token        **ts;      /* 句柄数组（注册时拷贝）：[0..n) 为触发源，[n..ntot) 为 map 目标 */
    int            n;       /* 触发源数：门/武装/反挂仅覆盖 [0..n)，目标不触发本 dep */
    int            ntot;    /* 传给 follow 的总句柄数（含 map 目标）；非 map 时 ntot==n */
    int            all;     /* 当前门逻辑：1=与门 / 0=或门（follow 返回值更新） */
    int            remain;  /* 与门：本轮尚未武装(arm)的依赖项数；归零即触发 */
    unsigned char *armed;   /* 每依赖项本轮是否已武装（n 字节；与门去重计数用） */
    int            mt;      /* 任一成员（含目标）为 MT 则整条 dep 走自旋门锁（glock） */
    int            glock;   /* 门状态自旋锁（0=空闲 / 1=持有）：仅护 all/remain/armed */
    sc_token_follow   follow;
    void          *ctx;
} tok_dep;

/* form 前挂起队列（懒分配）：未 form 的 token 收到 set 才创建本结构挂到 t->pending；
 * form-first（先 form 再 set，如 dnn 训练）全程 pending==NULL，零分配零占用。池化分配。 */
typedef struct tok_pending {
    sc_thin *vals;          /* 挂起的 set 输入值（可增长，池化） */
    int32_t *tags;          /* 对应挂起输入的 tag */
    int      n, cap;
} tok_pending;

/* back 反向调度缓存（懒构建 + graph-epoch 失效刷新）：token_back 的「反向可达 dep/节点
 * 有序表」只依赖静态图结构，与值无关——故按 sink token 缓存，建图后恒命中，零重算零分配。
 * 仅当 g_graph_epoch 变化（注册了新 dep / 新挂 exec）才重建。池化分配。 */
typedef struct tok_back_cache {
    uint32_t epoch;         /* 构建时的 g_graph_epoch；!= 当前则重建 */
    int      mode;          /* 1=边反向（list 为 tok_dep*，按 rank 降序）/ 2=节点 drain（list 为 token*，按 depth 降序） */
    int      n, cap;        /* 有序表长 / 容量 */
    void   **list;          /* 有序调度表（tok_dep*[] 或 token*[]，池化） */
} tok_back_cache;

struct sc_token {
    /* —— 8 字节成员先排（消除与 4 字节字段交错产生的填充）—— */
    char         *id;        /* 字符串唯一键 */
    sc_token_combine combine;   /* 非空=有 combine 合成体；空=enforce 直赋。与就绪无关：就绪唯凭 form */
    tok_dep     **deps;      /* 动态：引用本 token 的依赖关系（可增长数组） */
    tok_dep     **producers; /* 动态：以本 token 为 map 目标的依赖关系（反向邻接：目标←dep，供 back 回溯上游） */
    void         *ctx;       /* 节点私有上下文（侧车）：form 绑定，follow/exec 经 token_ctx 取用（拉取式流水线的队列+状态机+kernel，或推送式的观察统计等） */
    sc_token_exec    exec;      /* 节点处理钩子（form 绑定）：拉取(back)模式反拓扑唤起、推送(set)模式值变更落定后锁外唤起；dep 只管路由、combine 须纯，节点处理/副作用归此 */
    tok_pending  *pending;   /* form 前挂起队列（懒分配，池化）：NULL=无挂起；form-first 全程 NULL */
    tok_back_cache *back;    /* 反向调度缓存（懒构建，池化）：NULL=未建；按 graph-epoch 刷新 */
    sc_thin       value;     /* 当前值（@ 类型擦除自描述胖指针；32B = 4 指针） */
    /* —— 4 字节成员（紧凑收尾）—— */
    int32_t       tag;       /* 当前值随附标签（最近一次 set/form 的 tag） */
    int           state;     /* TOK_NEW / TOK_READY */
    int           ndeps, capdeps;
    int           nprod, capprod;
    int           depth;     /* dep…map 依赖图深度（源=0；编译期烘焙的常量，token_set_depth 写入，token_depth 读） */
    int           scc_id;    /* dep loop 受控反馈簇编号（编译期 Tarjan 烘焙；token_set_scc 写，token_scc 读） */
    int           scc_size;  /* 所属 SCC 簇大小（>1 或含自环 = 反馈簇；0/1 = 非反馈，不迭代） */
    int           critical;  /* 关键路径标志（dep…map 最长链上；编译期烘焙，token_set_crit 写，token_critical 读） */
    int           slack;     /* 松弛余量（可深多少跳而不拖慢全局；0=关键；token_slack 读） */
    int           fanin;     /* 扇入度：被多少上游 map 依赖（编译期烘焙，token_set_degree 写，token_fanin 读） */
    int           fanout;    /* 扇出度：驱动多少下游 map 目标（枢纽识别；token_fanout 读） */
    int           reach;     /* 可达下游数：变更后须重算的 token 总数（脏标记影响范围；token_set_reach 写，token_reach 读） */
    int           batch_width; /* 同波次并行宽度：与本 token 同深度的 token 数（拓扑分批；token_set_batch 写，token_batch_width 读） */
    int           checkpoint; /* 支配检查点标志：是否为缓存边界咽喉（编译期烘焙，token_set_dom 写，token_checkpoint 读） */
    int           dom_size;  /* 支配子树规模：缓存可覆盖的下游 token 数（token_dom_size 读） */
    int           mt;        /* MT token（id 以 '/' 开头）：值经 seqlock 同步；0=非 MT，零开销 */
    uint32_t      seq;       /* seqlock 序列号：偶=稳定可读 / 奇=写者占用中（兼写者自旋互斥） */
};

/* ---- 全局 token 表：adt 哈希（拷贝字符串键 → 裸 token*） ---- */
static sc_dict    g_toks;          /* key_size=-1：dict 自持 id 拷贝 */
static sc_mutex_t g_toks_mtx;      /* 守护 bind 的 intern 并发 */
static int        g_toks_init = 0; /* 惰性构造（首次 bind 单线程，模块 init 阶段） */

/* 依赖图世代计数：任一拓扑变更（注册 dep / 挂 exec）即自增，使各 token 的 back 调度缓存失效。
 * 建图在模块 init 单线程完成；运行期（训练循环）拓扑不变 → epoch 稳定 → back 缓存恒命中。 */
static uint32_t   g_graph_epoch = 0;

/* dep loop 全局注册表：所有 token_depend_loop 登记于此，token_loop_run 按 SCC 簇筛选驱动迭代。
 * 注册在模块 init 单线程（建图先于任何迭代），故无并发；运行期只读遍历。 */
static tok_dep  **g_loop_deps = NULL;
static int        g_nloop = 0, g_caploop = 0;

static void toks_ensure(void) {
    if (!g_toks_init) {            /* 模块 init 单线程：首次 bind 时构造，无并发竞争 */
        sc_dict_init(&g_toks, -1);
        P_mutex_init(&g_toks_mtx);
        g_toks_init = 1;
    }
}

/* 把裸 token* 包成 sc_thin：tar=NULL 不计数、own=SC_OWN_RAW 不记 out、dtor=NULL 不析构，
 * 纯当裸指针存取，dict 的 retain/release 对其为 no-op（零 ARC 干扰）。 */
static sc_thin tok_afat(sc_token *t) {
    sc_thin f;
    f.p = t; f.tar = NULL; f.dtor = NULL;
    return f;
}

/* 空 @（无值）：sender 恒空、未 form 的 form 主初值。纯结构零值，无 ARC 干扰。 */
static sc_thin tok_empty_afat(void) {
    sc_thin f;
    f.p = NULL; f.tar = NULL; f.dtor = NULL;
    return f;
}

/* modified 哨兵（对齐 c_prototype 的 g_ccModified）：强制刷新传播的特殊 @ 值。
 * 取一不与任何真实标量值/堆指针冲突的唯一地址作 .p（本进程唯一），任一侧出现即判「不等」。 */
static char g_tok_modified_marker;
sc_thin sc_tok_modified(void) {
    sc_thin f;
    f.p = (void *)&g_tok_modified_marker; f.tar = NULL; f.dtor = NULL;
    return f;
}

/* @ 值相等判定（对齐 c_prototype 的 C_equal）：按 .p 比对——标量经 (x:@) 装箱即值入 .p，
 * 故比 .p 对标量即值相等；堆对象则为指针同一性。modified 哨兵任一侧出现恒判「不等」→ 强制传播。 */
static int tok_afat_equal(sc_thin a, sc_thin b) {
    if (a.p == (void *)&g_tok_modified_marker || b.p == (void *)&g_tok_modified_marker) return 0;
    return a.p == b.p;
}

/* 调用 form 候选的 combine：据基值 base、输入 input、tag 打包上下文（sender 恒空）算新值。
 * base 由调用方供给——非 MT 直接传 t->value（裸读）；MT 须传原子读出的当前值（写者独占下
 * 经 sc_get_ord 读，避免裸读跨线程原子写的数据竞争 / 编译器缓存导致的陈旧 base 破坏单调性）。 */
static sc_thin tok_run_combine(sc_token *t, sc_thin base, sc_thin input, int32_t tag) {
    sc_tok_in self;
    self.sender = tok_empty_afat();
    self.base   = base;
    self.input  = input;
    self.tag    = tag;
    return t->combine(&self);
}

static char *tok_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)sc_chunk(n);          /* 池化：id 进程生命周期，随建图一次性分配 */
    if (p) memcpy(p, s, n);
    return p;
}

/* ---- MT 值单元：seqlock（序列锁）——读无锁、写者经 seq 自旋独占（仅 MT token 走此路径） ----
 * t->seq 偶=稳定可读 / 奇=写者占用中。写者 CAS 偶→奇取得独占（兼写者互斥），改值后 +2 置回偶
 * （release 发布）；读者乐观重试：读 seq→读各字段→复读 seq，奇或前后不等则重试。值各字段以原子
 * （seq_cst）逐项读写——TSan 干净、无撕裂（x86 上 seq_cst 读即 plain mov，读路径近乎零成本）。 */
static void tok_store_value(sc_token *t, sc_thin v, int32_t tag) {
    sc_set_ord(&t->value.p,    v.p);
    sc_set_ord(&t->value.tar,  v.tar);
    sc_set_ord(&t->value.dtor, v.dtor);
    sc_set_ord(&t->tag, tag);
}
static sc_thin tok_load_value(sc_token *t) {
    sc_thin v;
    for (;;) {
        uint32_t s1 = sc_get_ord(&t->seq);
        if (s1 & 1u) continue;                 /* 写者占用中：重试 */
        v.p    = sc_get_ord(&t->value.p);
        v.tar  = sc_get_ord(&t->value.tar);
        v.dtor = sc_get_ord(&t->value.dtor);
        uint32_t s2 = sc_get_ord(&t->seq);
        if (s1 == s2) return v;                /* 期间无写者：快照一致 */
    }
}
static uint32_t tok_write_begin(sc_token *t) {    /* CAS 偶→奇：取得写者独占（兼互斥） */
    for (;;) {
        uint32_t s = sc_get(&t->seq) & ~1u;    /* 期望偶 */
        uint32_t exp = s;
        if (sc_test_and_set_ord(&t->seq, &exp, s + 1u)) return s;
        /* 失败：他写者占用，自旋重试 */
    }
}
static void tok_write_end(sc_token *t, uint32_t s) {
    sc_set_ord(&t->seq, s + 2u);               /* 偶：发布新值 */
}
/* 写者独占下读当前值：经 sc_get_ord 原子逐项读（无需 seqlock 重试——本线程即唯一写者）。
 * 供 combine 取 base：与其他写者经 seq 互斥、与读者经原子读相容，TSan 干净。 */
static sc_thin tok_value_excl(sc_token *t) {
    sc_thin v;
    v.p    = sc_get_ord(&t->value.p);
    v.tar  = sc_get_ord(&t->value.tar);
    v.dtor = sc_get_ord(&t->value.dtor);
    return v;
}

/* ---- 依赖门自旋锁：仅护 d->armed/remain/all（纳秒级临界区，绝不跨 follow） ----
 * 仅 MT dep（d->mt）生效；非 MT 为 no-op。门锁恒为一次只持一把的叶子锁，不嵌套任何锁。 */
static void dep_lock(tok_dep *d) {
    if (!d->mt) return;
    for (;;) { int exp = 0; if (sc_test_and_set_acq(&d->glock, &exp, 1)) return; }
}
static void dep_unlock(tok_dep *d) {
    if (!d->mt) return;
    sc_set_rel(&d->glock, 0);
}

sc_token *sc_token_bind(const char *id, sc_token_combine combine) {
    toks_ensure();
    P_mutex_lock(&g_toks_mtx);
    sc_token *t = (sc_token *)sc_dict_get(&g_toks, id).p;     /* 哈希 O(1) intern 查找 */
    if (t) {                                          /* 已存在：幂等返回，按需补挂 combine */
        if (combine && !t->combine) t->combine = combine;
        P_mutex_unlock(&g_toks_mtx);
        return t;
    }
    t = (sc_token *)sc_chunk0(sizeof(sc_token));           /* 池化清零：token 句柄进程生命周期 */
    t->id = tok_strdup(id);
    t->combine = combine;
    t->state = TOK_NEW;                               /* 对齐 c_prototype：bind 仅取共享壳，未就绪；唯 form 升 READY */
    t->mt = (id[0] == '/');                           /* MT token：值经 seqlock 同步，依赖门走自旋锁 */
    sc_dict_put(&g_toks, id, tok_afat(t));
    P_mutex_unlock(&g_toks_mtx);
    return t;
}

sc_thin sc_token_get(sc_token *t) {
    if (!t) return tok_empty_afat();
    if (!t->mt) return t->value;        /* 非 MT 快路径：裸读 */
    return tok_load_value(t);           /* MT：seqlock 无锁读 */
}

/* 武装与门依赖项 idx：首次置位则 remain--；返回 remain 是否归零（与门达成）。 */
static int dep_arm(tok_dep *d, int idx) {
    if (idx < 0 || idx >= d->n || d->armed[idx]) return 0;
    d->armed[idx] = 1;
    return (--d->remain == 0);
}
static void dep_reset(tok_dep *d) {            /* 门达成后重置：下一轮需全部重新武装 */
    if (d->n) memset(d->armed, 0, (size_t)d->n);
    d->remain = d->n;
}

/* 触发 t 的依赖级联：ready=1 为就绪事件（form 首次），ready=0 为变更事件（set）。
 *   与门：依赖项逐一武装，全部到位（remain 归零）才唤起 follow（ALL_READY / ALL_CHANGED）；
 *   或门：每事件即唤起 follow（ANY_READY / 变更项下标）。
 * follow 返回值更新下次门逻辑（非 0=与门 / 0=或门）。
 * 并发：门状态读改在 dep 自旋锁内完成；follow 一律在锁外调用（调用方亦已释放写者锁）——
 *   任一线程任一时刻零持锁跑用户码，根除跨 token 锁序死锁。 */
static void tok_fire(sc_token *t, int ready) {
    for (int i = 0; i < t->ndeps; i++) {
        tok_dep *d = t->deps[i];
        int idx = -1;
        for (int j = 0; j < d->n; j++) if (d->ts[j] == t) { idx = j; break; }
        int fire = 0, acting = 0;
        dep_lock(d);
        if (d->all) {                                  /* 与门 */
            if (dep_arm(d, idx)) {                      /* 全部依赖项到位 */
                fire = 1;
                acting = ready ? TOK_ALL_READY : TOK_ALL_CHANGED;
                dep_reset(d);                          /* 重置进入下一轮（变更门） */
            }
        } else {                                       /* 或门：任一事件即触发 */
            fire = 1;
            acting = ready ? TOK_ANY_READY : (idx >= 0 ? idx : TOK_ANY_READY);
        }
        dep_unlock(d);
        if (!fire) continue;
        int next = d->follow ? d->follow(d->ts, d->ntot, acting, d->ctx) : 0;  /* 锁外回调（传全句柄数，含 map 目标） */
        dep_lock(d);
        int prev = d->all;
        d->all = next ? 1 : 0;
        if (!prev && d->all) dep_reset(d);             /* 或门→与门：重新计数 */
        dep_unlock(d);
    }
}

/* form 前挂起一条 set 输入：懒分配 tok_pending（首次挂起才创建，池化）。
 * form-first（先 form 后 set）全程不触此路径，t->pending 恒 NULL，零分配零占用。 */
static void tok_pending_push(sc_token *t, sc_thin v, int32_t tag) {
    tok_pending *q = t->pending;
    if (!q) { q = (tok_pending *)sc_chunk0(sizeof(tok_pending)); t->pending = q; }
    if (q->n == q->cap) {
        q->cap = q->cap ? q->cap * 2 : 4;
        q->vals = (sc_thin *)sc_refit(q->vals, (size_t)q->cap * sizeof(sc_thin));
        q->tags = (int32_t *)sc_refit(q->tags, (size_t)q->cap * sizeof(int32_t));
    }
    q->vals[q->n] = v;
    q->tags[q->n] = tag;
    q->n++;
}
/* form 时丢弃挂起队列（回放后调用）：释放池化缓冲并清空指针。 */
static void tok_pending_drop(sc_token *t) {
    tok_pending *q = t->pending;
    if (!q) return;
    sc_recycle(q->vals); sc_recycle(q->tags); sc_recycle(q);
    t->pending = NULL;
}

/* set 内核：force=0 记忆化（同值抑制，对齐 c_prototype 的 C_input）；force=1 脉冲（绕过抑制,
 *   同值也落值并强制传播）。未 form 一律入挂起队列（脉冲亦然，待 form 回放）。 */
static void tok_set_impl(sc_token *t, sc_thin v, int32_t tag, int force) {
    if (!t) return;
    if (!t->mt) {                                      /* 非 MT 快路径：无锁无原子 */
        if (t->state == TOK_NEW) {                      /* 未 form（含 enforce 未被主 form）：入挂起队列，不落值不传播 */
            tok_pending_push(t, v, tag);
            return;
        }
        sc_thin oldv = t->value;
        sc_thin newv = t->combine ? tok_run_combine(t, oldv, v, tag) : v;
        if (!force && tok_afat_equal(newv, oldv)) return;  /* 新值==原值：未变更，不落值不传播（脉冲跳过此抑制） */
        t->value = newv;
        t->tag = tag;
        if (t->exec) t->exec(t, t->ctx);               /* 锁外：节点处理钩子（值落定后、向下游传播前） */
        tok_fire(t, 0);
        return;
    }
    /* MT：写者经 seq 取得独占（兼互斥），改值后发布，再锁外传播 */
    uint32_t s = tok_write_begin(t);
    if (t->state == TOK_NEW) {                         /* 未 form：入挂起队列（token 私有，仅写者触碰） */
        tok_pending_push(t, v, tag);
        tok_write_end(t, s);
        return;
    }
    sc_thin oldv = tok_value_excl(t);                            /* MT：base 经原子读（写者独占下） */
    sc_thin nv = t->combine ? tok_run_combine(t, oldv, v, tag) : v;
    if (!force && tok_afat_equal(nv, oldv)) { tok_write_end(t, s); return; } /* 未变更：不发布不传播（脉冲跳过） */
    tok_store_value(t, nv, tag);
    tok_write_end(t, s);
    if (t->exec) t->exec(t, t->ctx);                   /* 锁外：节点处理钩子（已发布、传播前） */
    tok_fire(t, 0);                                    /* 锁外：变更事件传播 */
}

void sc_token_set(sc_token *t, sc_thin v, int32_t tag) {
    tok_set_impl(t, v, tag, 0);                        /* 记忆化：同值抑制（默认） */
}

void sc_token_pulse(sc_token *t, sc_thin v, int32_t tag) {
    tok_set_impl(t, v, tag, 1);                        /* 脉冲：绕过相等抑制，同值也强制传播（拉取流水线/迭代） */
}

void sc_token_form(sc_token *t, sc_thin v, int32_t tag, void *ctx, sc_token_exec exec) {
    if (!t) return;
    if (ctx) t->ctx = ctx;                             /* 绑定节点私有上下文（侧车）：form = 灌值 + 升格 + 挂侧车 + 挂钩子 */
    if (exec) { t->exec = exec; g_graph_epoch++; }     /* 挂节点处理钩子；新挂 exec 改变 back 调度模式 → 失效 back 缓存 */
    if (!t->mt) {                                      /* 非 MT 快路径 */
        t->value = v;
        t->tag = tag;
        t->state = TOK_READY;
        if (t->pending) {                              /* 回放 form 前挂起的 action（按 combine 合并） */
            tok_pending *q = t->pending;
            for (int i = 0; i < q->n; i++) {
                t->value = t->combine ? tok_run_combine(t, t->value, q->vals[i], q->tags[i])
                                      : q->vals[i];
                t->tag = q->tags[i];
            }
            tok_pending_drop(t);
        }
        tok_fire(t, 1);
        return;
    }
    /* MT：写者独占灌初值 + 回放挂起，发布后锁外触发就绪 */
    uint32_t s = tok_write_begin(t);
    t->state = TOK_READY;                              /* 升格 form 主（写者独占下） */
    tok_store_value(t, v, tag);                        /* 灌初值（原子发布；seq 仍奇，读者重试） */
    if (t->pending) {                                  /* 回放挂起：每步以当前值为 base 经 combine 合并 */
        tok_pending *q = t->pending;
        for (int i = 0; i < q->n; i++) {
            sc_thin nv = t->combine ? tok_run_combine(t, tok_value_excl(t), q->vals[i], q->tags[i])
                                    : q->vals[i];
            tok_store_value(t, nv, q->tags[i]);
        }
        tok_pending_drop(t);
    }
    tok_write_end(t, s);
    tok_fire(t, 1);                                    /* 锁外：就绪事件触发依赖门 */
}

/* t.ctx()：取节点私有上下文（form 绑定的侧车；未绑定=NULL）。follow/exec 体内经此取节点状态。
 * 仅读裸指针：ctx 由 form 单线程绑定（建图/启动期），运行期只读，无并发写。 */
void *sc_token_ctx(sc_token *t) { return t ? t->ctx : NULL; }


static void tok_depend_impl(sc_token **ts, int ntrig, int ntot, int all, sc_token_follow follow, void *ctx) {
    g_graph_epoch++;                                  /* 拓扑变更：注册新 dep 即失效所有 back 调度缓存 */
    tok_dep *d = (tok_dep *)sc_chunk0(sizeof(tok_dep));
    d->ts = (sc_token **)sc_chunk((size_t)(ntot ? ntot : 1) * sizeof(sc_token *));
    memcpy(d->ts, ts, (size_t)ntot * sizeof(sc_token *));
    d->n = ntrig;
    d->ntot = ntot;
    d->all = all;
    d->follow = follow;
    d->ctx = ctx;
    d->armed = (unsigned char *)sc_chunk0((size_t)(ntrig ? ntrig : 1));
    d->remain = ntrig;
    for (int i = 0; i < ntot; i++)                     /* 先定门：任一成员（含 map 目标）MT 则整条 dep 走全局锁 */
        if (ts[i] && ts[i]->mt) { d->mt = 1; break; }
    for (int i = 0; i < ntot; i++) {                   /* 升格：MT dep 的所有成员（含目标）一并 MT，跨线程读写受串行化 */
        sc_token *t = ts[i];
        if (!t) continue;
        if (d->mt) t->mt = 1;
    }
    for (int i = 0; i < ntrig; i++) {                  /* 反挂仅触发源：map 目标被 follow 写入但不触发本 dep（杜绝自环回灌） */
        sc_token *t = ts[i];
        if (!t) continue;
        if (t->ndeps == t->capdeps) {
            t->capdeps = t->capdeps ? t->capdeps * 2 : 4;
            t->deps = (tok_dep **)sc_refit(t->deps, (size_t)t->capdeps * sizeof(tok_dep *));
        }
        t->deps[t->ndeps++] = d;
    }
    for (int i = ntrig; i < ntot; i++) {               /* 反向邻接：map 目标记下产出它的 dep（目标←dep，供 back 回溯上游） */
        sc_token *t = ts[i];
        if (!t) continue;
        if (t->nprod == t->capprod) {
            t->capprod = t->capprod ? t->capprod * 2 : 4;
            t->producers = (tok_dep **)sc_refit(t->producers, (size_t)t->capprod * sizeof(tok_dep *));
        }
        t->producers[t->nprod++] = d;
    }
    /* 注册即就绪：触发源中已就绪者预先武装；满足门逻辑则立即触发 ready
     *（对齐 c_prototype C_Depend：注册时已 form 的依赖项即计入门逻辑）。
     * 注册本在模块 init 单线程（deps 数组建图先于任何并发 fire）；门状态读改仍走 dep 锁、
     * follow 锁外调用，与运行期 fire 同构。 */
    int fire = 0, acting = 0;
    dep_lock(d);
    if (all) {                                         /* 与门：预武装已就绪源 */
        for (int i = 0; i < ntrig; i++)
            if (ts[i] && ts[i]->state == TOK_READY) { d->armed[i] = 1; d->remain--; }
        if (ntrig > 0 && d->remain == 0) {             /* 注册时已全部就绪 → 立即 ALL_READY */
            fire = 1; acting = TOK_ALL_READY;
            dep_reset(d);
        }
    } else {                                           /* 或门：任一已就绪 → 立即 ANY_READY */
        for (int i = 0; i < ntrig; i++)
            if (ts[i] && ts[i]->state == TOK_READY) { fire = 1; acting = TOK_ANY_READY; break; }
    }
    dep_unlock(d);
    if (fire) {
        int next = follow ? follow(d->ts, ntot, acting, ctx) : 0;  /* 锁外回调（传全句柄数，含 map 目标） */
        dep_lock(d);
        int prev = d->all;
        d->all = next ? 1 : 0;
        if (!prev && d->all) dep_reset(d);
        dep_unlock(d);
    }
}

void sc_token_depend(sc_token **ts, int n, int all, sc_token_follow follow, void *ctx) {
    tok_depend_impl(ts, n, n, all, follow, ctx);       /* 非 map：触发源即全部句柄 */
}

void sc_token_depend_map(sc_token **ts, int nsrc, int ntgt, int all, sc_token_follow follow, void *ctx) {
    tok_depend_impl(ts, nsrc, nsrc + ntgt, all, follow, ctx);  /* map：ts = 源(nsrc) ++ 目标(ntgt) */
}

/* 烘焙：编译期对 dep…map 图算好的深度，注册时以常量写入句柄（lightmap 式预计算，运行时只读）。 */
void sc_token_set_depth(sc_token *t, int depth) {
    if (t) t->depth = depth;
}

/* t.depth()：读依赖图深度（源=0；O(1) 常量，无图遍历）。 */
int sc_token_depth(sc_token *t) {
    return t ? t->depth : 0;
}

/* 烘焙：编译期对 dep…map DAG 算好的关键路径标志 + 松弛，注册时以常量写入句柄。 */
void sc_token_set_crit(sc_token *t, int critical, int slack) {
    if (t) { t->critical = critical; t->slack = slack; }
}

/* t.critical()：该 token 是否在关键路径（最长链）上（O(1)；加长它即拖慢整条流水线）。 */
int sc_token_critical(sc_token *t) {
    return t ? t->critical : 0;
}

/* t.slack()：松弛余量（可深多少跳而不拖慢全局；0=关键点）。 */
int sc_token_slack(sc_token *t) {
    return t ? t->slack : 0;
}

/* 烘焙：编译期对 dep…map 图算好的扇入/扇出度，注册时以常量写入句柄（枢纽识别）。 */
void sc_token_set_degree(sc_token *t, int fanin, int fanout) {
    if (t) { t->fanin = fanin; t->fanout = fanout; }
}

/* t.fanin()：扇入度（被多少上游 map 依赖；O(1) 常量）。 */
int sc_token_fanin(sc_token *t) {
    return t ? t->fanin : 0;
}

/* t.fanout()：扇出度（驱动多少下游 map 目标；高=枢纽 / 广播源）。 */
int sc_token_fanout(sc_token *t) {
    return t ? t->fanout : 0;
}

/* 烘焙：编译期算好的可达下游数（脏标记影响范围），注册时以常量写入句柄。 */
void sc_token_set_reach(sc_token *t, int reach) {
    if (t) t->reach = reach;
}

/* t.reach()：变更本 token 后须重算的下游 token 总数（失效爆炸半径；O(1)）。 */
int sc_token_reach(sc_token *t) {
    return t ? t->reach : 0;
}

/* 烘焙：编译期算好的拓扑波次并行宽度，注册时以常量写入句柄（接 MT 调度）。 */
void sc_token_set_batch(sc_token *t, int width) {
    if (t) t->batch_width = width;
}

/* t.batch()：拓扑波次编号（= depth；同波 token 可并行触发；O(1)）。 */
int sc_token_batch(sc_token *t) {
    return t ? t->depth : 0;
}

/* t.batch_width()：本波次并行宽度（与本 token 同深度、可并行的 token 数）。 */
int sc_token_batch_width(sc_token *t) {
    return t ? t->batch_width : 0;
}

/* 烘焙：编译期支配树算好的检查点标志 + 支配子树规模，注册时以常量写入句柄（缓存边界）。 */
void sc_token_set_dom(sc_token *t, int checkpoint, int dom_size) {
    if (t) { t->checkpoint = checkpoint; t->dom_size = dom_size; }
}

/* t.checkpoint()：是否为支配咽喉（缓存边界——在此缓存可覆盖整个支配子树；O(1)）。 */
int sc_token_checkpoint(sc_token *t) {
    return t ? t->checkpoint : 0;
}

/* t.dom_size()：支配子树规模（本检查点缓存可覆盖的下游 token 数）。 */
int sc_token_dom_size(sc_token *t) {
    return t ? t->dom_size : 0;
}

/* dep 的目标侧深度：取其所有 map 目标（ts[n..ntot)）深度最大者；无目标退化取触发源深度。
 * 反向遍历据此排序——目标越深者越靠近输出，反拓扑序中越先行。 */
static int dep_back_rank(tok_dep *d) {
    int md = -1;
    for (int i = d->n; i < d->ntot; i++)
        if (d->ts[i] && d->ts[i]->depth > md) md = d->ts[i]->depth;
    if (md < 0)                                  /* 无 map 目标：退化用触发源深度 */
        for (int i = 0; i < d->n; i++)
            if (d->ts[i] && d->ts[i]->depth > md) md = d->ts[i]->depth;
    return md;
}

/* back 调度构建：自 sink t 沿 producers[] 反向 BFS，按图是否注册节点 exec 自动分派两种语义，
 * 把唤起序列烘进 t->back（tok_back_cache）。BFS 临时去重表（seen/work/deps）走池化短命缓冲
 * （sc_chunk/sc_refit/sc_recycle），构建末即回收——稳态训练循环只在首轮（或拓扑变更后）构建一次，
 * 此后 token_back 纯读缓存、零分配零去重零排序。
 *   mode 2（drain：图中有节点 exec）：缓存「按 depth 降序的注册 exec 节点」列表。
 *   mode 1（边反向：无任何 exec，如梯度反传）：缓存「按 dep_back_rank 降序的可达 dep」列表。 */
static tok_back_cache *tok_back_build(sc_token *t) {
    tok_back_cache *bc = t->back;
    if (!bc) bc = (tok_back_cache *)sc_chunk0(sizeof(tok_back_cache)); /* 句柄进程生命周期 */
    int capseen = 8, nseen = 0, capwork = 8, nwork = 0, capdep = 8, ndep = 0;
    sc_token   **seen = (sc_token **)sc_chunk((size_t)capseen * sizeof(sc_token *));   /* 短命：构建末回收 */
    sc_token   **work = (sc_token **)sc_chunk((size_t)capwork * sizeof(sc_token *));
    tok_dep **deps = (tok_dep **)sc_chunk((size_t)capdep * sizeof(tok_dep *));
    seen[nseen++] = t;
    work[nwork++] = t;
    while (nwork) {
        sc_token *x = work[--nwork];
        for (int i = 0; i < x->nprod; i++) {
            tok_dep *d = x->producers[i];
            int dup = 0;
            for (int k = 0; k < ndep; k++) if (deps[k] == d) { dup = 1; break; }
            if (!dup) {
                if (ndep == capdep) { capdep *= 2;
                    deps = (tok_dep **)sc_refit(deps, (size_t)capdep * sizeof(tok_dep *)); }
                deps[ndep++] = d;
            }
            for (int j = 0; j < d->n; j++) {     /* d 的触发源即上游，继续向上展开 */
                sc_token *u = d->ts[j];
                if (!u) continue;
                int vis = 0;
                for (int k = 0; k < nseen; k++) if (seen[k] == u) { vis = 1; break; }
                if (vis) continue;
                if (nseen == capseen) { capseen *= 2;
                    seen = (sc_token **)sc_refit(seen, (size_t)capseen * sizeof(sc_token *)); }
                seen[nseen++] = u;
                if (nwork == capwork) { capwork *= 2;
                    work = (sc_token **)sc_refit(work, (size_t)capwork * sizeof(sc_token *)); }
                work[nwork++] = u;
            }
        }
    }
    int has_exec = 0;
    for (int i = 0; i < nseen; i++) if (seen[i] && seen[i]->exec) { has_exec = 1; break; }
    if (has_exec) {                              /* mode 2：节点 drain，缓存 exec 节点（depth 降序） */
        for (int i = 1; i < nseen; i++) {        /* 插入排序按 depth 降序（规模小） */
            sc_token *x = seen[i]; int d = x->depth; int j = i - 1;
            while (j >= 0 && seen[j]->depth < d) { seen[j + 1] = seen[j]; j--; }
            seen[j + 1] = x;
        }
        int cnt = 0;
        for (int i = 0; i < nseen; i++) if (seen[i] && seen[i]->exec) cnt++;
        if (bc->cap < cnt) { bc->cap = cnt ? cnt : 1;
            bc->list = (void **)sc_refit(bc->list, (size_t)bc->cap * sizeof(void *)); }
        bc->n = 0;
        for (int i = 0; i < nseen; i++) if (seen[i] && seen[i]->exec) bc->list[bc->n++] = seen[i];
        bc->mode = 2;
    } else {                                     /* mode 1：边反向，缓存 dep（dep_back_rank 降序） */
        for (int i = 1; i < ndep; i++) {
            tok_dep *d = deps[i]; int r = dep_back_rank(d); int j = i - 1;
            while (j >= 0 && dep_back_rank(deps[j]) < r) { deps[j + 1] = deps[j]; j--; }
            deps[j + 1] = d;
        }
        if (bc->cap < ndep) { bc->cap = ndep ? ndep : 1;
            bc->list = (void **)sc_refit(bc->list, (size_t)bc->cap * sizeof(void *)); }
        bc->n = ndep;
        for (int i = 0; i < ndep; i++) bc->list[i] = deps[i];
        bc->mode = 1;
    }
    sc_recycle(seen); sc_recycle(work); sc_recycle(deps);
    bc->epoch = g_graph_epoch;                   /* 末置 epoch：缓存就绪标记（发布前最后一步） */
    return bc;
}

static int g_back_lock = 0;                      /* back 缓存构建锁：仅首构建/拓扑变更后争用 */

/* back t[, seed]：反向遍历（反向传播骨架）。自输出 token t 出发，沿 producers[] 反向邻接
 * 收集全部上游 dep（去重），按 dep_back_rank 降序（反拓扑：靠近输出者先行）依次以
 * acting=TOK_BACK 唤起其 follow——follow 体内 this->active==TOK_BACK 即走反向计算（读目标
 * 写源；梯度数学由用户体负责，多 dep 写同一源的累积经 form combine(sum) 完成）。
 * seed 非空则先灌入 t（梯度种子，如 loss.backward(1)），不触发前向级联。
 * 编译期已保证图为 DAG（环检测），反向 BFS 必终止。
 *
 * 调度缓存：唤起序列烘进 t->back，按 g_graph_epoch 命中——拓扑稳定（训练循环 / drain 运行期）
 *   下仅首轮构建，此后纯读缓存、零分配零去重零排序。构建经全局锁双检：稳态（epoch 命中）走无锁
 *   快路径，仅首构建/拓扑变更后入锁（release 发布 t->back，快路径 acquire 读，多 worker 并发 back
 *   同一 sink 安全）。拓扑变更（dep 注册 / 新挂 exec）经 g_graph_epoch++ 自动失效缓存。
 *
 * 提前中止（break）：follow/exec 返回非 0 即停止本轮反向遍历——供「drain」式协作层用：worker
 *   自 sink back，最深可认领节点的钩子认领并处理后返回非 0 中止扫描，worker 再发起下一轮
 *   back 重扫，天然实现「最近未处理优先」的拉取式流水线排空。返回 0（如反向传播）则全程遍历，
 *   行为不变（向后兼容）。 */
void sc_token_back(sc_token *t, sc_thin seed, int32_t tag) {
    if (!t) return;
    if (seed.p) {                                /* 种子非空：灌起点值（不触发前向 deps） */
        if (!t->mt) { t->value = seed; t->tag = tag; }
        else { uint32_t s = tok_write_begin(t); tok_store_value(t, seed, tag); tok_write_end(t, s); }
    }
    tok_back_cache *bc = (tok_back_cache *)sc_get_ord(&t->back);
    if (!bc || bc->epoch != g_graph_epoch) {     /* 缓存缺失/失效：双检入锁构建（稳态不入此） */
        for (;;) { int e = 0; if (sc_test_and_set_acq(&g_back_lock, &e, 1)) break; }
        bc = (tok_back_cache *)sc_get_ord(&t->back);
        if (!bc || bc->epoch != g_graph_epoch) {
            bc = tok_back_build(t);
            sc_set_rel(&t->back, bc);             /* release 发布：快路径 acquire 读必见已构建完整缓存 */
        }
        sc_set_rel(&g_back_lock, 0);
    }
    if (bc->mode == 2) {                          /* drain：按 depth 降序唤起注册 exec 节点 */
        for (int i = 0; i < bc->n; i++) {
            sc_token *x = (sc_token *)bc->list[i];
            if (x && x->exec && x->exec(x, x->ctx))
                break;                           /* 已认领并处理一节点 → 中止本轮 back 扫描 */
        }
    } else {                                      /* 边反向：按反拓扑序唤起 follow（active=TOK_BACK） */
        for (int i = 0; i < bc->n; i++) {
            tok_dep *d = (tok_dep *)bc->list[i];
            if (d->follow && d->follow(d->ts, d->ntot, TOK_BACK, d->ctx))
                break;                           /* follow 返回非 0 = 请求中止反向遍历 */
        }
    }
}

/* 烘焙：编译期 Tarjan 算好的 SCC 反馈簇划分，注册时以常量写入句柄（lightmap 式预计算）。 */
void sc_token_set_scc(sc_token *t, int scc_id, int scc_size) {
    if (t) { t->scc_id = scc_id; t->scc_size = scc_size; }
}

/* t.scc()：读受控反馈簇编号（O(1) 常量；非反馈/未烘焙为 0）。 */
int sc_token_scc(sc_token *t) {
    return t ? t->scc_id : 0;
}

/* 所属反馈簇大小（>1 或含自环 = 反馈簇；0/1 = 非反馈）。 */
int sc_token_scc_size(sc_token *t) {
    return t ? t->scc_size : 0;
}

/* dep loop：受控反馈环注册。与 map 不同——触发源**不反挂** deps[]（杜绝 set 自动级联导致
 * 无限环），仅登记到全局 loop dep 列表，由显式 token_loop_run 按 SCC 簇驱动迭代；目标侧仍建
 * 反向邻接 producers[]（供 back / 查询）。SCC 簇划分由编译期 Tarjan 烘焙（sc_token_set_scc）。 */
void sc_token_depend_loop(sc_token **ts, int nsrc, int ntgt, int all, sc_token_follow follow, void *ctx) {
    int ntot = nsrc + ntgt;
    g_graph_epoch++;                             /* 拓扑变更：注册 loop dep 即失效所有 back 调度缓存 */
    tok_dep *d = (tok_dep *)sc_chunk0(sizeof(tok_dep));
    d->ts = (sc_token **)sc_chunk((size_t)(ntot ? ntot : 1) * sizeof(sc_token *));
    memcpy(d->ts, ts, (size_t)ntot * sizeof(sc_token *));
    d->n = nsrc;
    d->ntot = ntot;
    d->all = all;
    d->follow = follow;
    d->ctx = ctx;
    d->armed = (unsigned char *)sc_chunk0((size_t)(nsrc ? nsrc : 1));
    d->remain = nsrc;
    for (int i = nsrc; i < ntot; i++) {          /* 反向邻接：目标记下产出它的 dep（供 back / 查询） */
        sc_token *t = ts[i];
        if (!t) continue;
        if (t->nprod == t->capprod) {
            t->capprod = t->capprod ? t->capprod * 2 : 4;
            t->producers = (tok_dep **)sc_refit(t->producers, (size_t)t->capprod * sizeof(tok_dep *));
        }
        t->producers[t->nprod++] = d;
    }
    if (g_nloop == g_caploop) {                  /* 登记全局 loop 列表（token_loop_run 据 SCC 簇筛选驱动） */
        g_caploop = g_caploop ? g_caploop * 2 : 8;
        g_loop_deps = (tok_dep **)sc_refit(g_loop_deps, (size_t)g_caploop * sizeof(tok_dep *));
    }
    g_loop_deps[g_nloop++] = d;
}

/* t.loop_run(max)：驱动 t 所在 SCC 反馈簇迭代至多 max 轮。每轮对簇内每条 loop dep 以
 * acting=TOK_LOOP 唤起 follow（环体读旧值→算新值→sc_token_set 写回，下轮读到新值）。
 * 返回实际迭代轮数；非反馈簇（scc_size<=1）返 0。一期无 eps 收敛判据，跑满 max 轮。 */
int sc_token_loop_run(sc_token *t, int max) {
    if (!t || t->scc_size <= 1 || max <= 0) return 0;  /* 非反馈簇：无环可迭代 */
    int scc = t->scc_id;
    int iter = 0;
    for (; iter < max; iter++) {
        for (int i = 0; i < g_nloop; i++) {
            tok_dep *d = g_loop_deps[i];
            int in = 0;                          /* 该 dep 属 t 的反馈簇？任一成员同簇即是 */
            for (int j = 0; j < d->ntot; j++)
                if (d->ts[j] && d->ts[j]->scc_size > 1 && d->ts[j]->scc_id == scc) { in = 1; break; }
            if (in && d->follow) d->follow(d->ts, d->ntot, TOK_LOOP, d->ctx);
        }
    }
    return iter;
}
