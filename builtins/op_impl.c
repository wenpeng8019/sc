/* op_impl.c —— op.sc 语法机制的默认运行时实现
 *
 * 编译器对每个工程都自动编译并链接本文件（op.sc 为默认导入模块，无需 inc）。
 * 契约见同目录 op.h。
 */
#include "op.h"
#include "platform.h"   /* builtins 跨平台基础头（编译时 -I builtins 根目录） */
#include <stdlib.h>     /* ioq 循环缓冲：malloc/free */
#include <stdint.h>     /* intptr_t（结果标量往返） */
#include <string.h>     /* memset */
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
    free(r);                               /* 堆对象 in==0 && out==0 → 释放整块（含头） */
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
void *chain_prev(void *it) {
    if (!it) return NULL;
    void *p = LPREV(it);
    return (p && LNEXT(p) == it) ? p : NULL;
}

void chain_append(chain *_this, void *it) {
    if (!it) return;
    void *h = _this->head;
    if (!h) { _this->head = it; LPREV(it) = it; LNEXT(it) = NULL; return; }
    void *r = LPREV(h);        /* rear */
    LNEXT(r) = it;
    LPREV(it) = r;
    LNEXT(it) = NULL;
    LPREV(h) = it;             /* 首元素 _prev 始终指向新 rear */
}

void chain_push(chain *_this, void *it) {
    if (!it) return;
    void *h = _this->head;
    if (!h) { _this->head = it; LPREV(it) = it; LNEXT(it) = NULL; return; }
    LPREV(it) = LPREV(h);      /* 继承 rear */
    LNEXT(it) = h;
    LPREV(h) = it;
    _this->head = it;
}

void *chain_pop(chain *_this) {
    void *h = _this->head;
    if (!h) return NULL;
    void *n = LNEXT(h);
    if (n) { LPREV(n) = LPREV(h); _this->head = n; }
    else _this->head = NULL;
    LPREV(h) = LNEXT(h) = NULL;
    return h;
}

void chain_before(chain *_this, void *pos, void *it) {
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

void chain_after(chain *_this, void *pos, void *it) {
    void *h = _this->head;
    if (!h || !pos || !it) return;
    void *n = LNEXT(pos);
    LNEXT(pos) = it;
    LPREV(it) = pos;
    LNEXT(it) = n;
    if (n) LPREV(n) = it;
    else LPREV(h) = it;        /* it 成为新 rear */
}

void chain_remove(chain *_this, void *it) {
    void *h = _this->head;
    if (!h || !it) return;
    if (it == h) { chain_pop(_this); return; }
    void *p = LPREV(it), *n = LNEXT(it);
    LNEXT(p) = n;
    if (n) LPREV(n) = p;
    else LPREV(h) = p;         /* it 原为 rear */
    LPREV(it) = LNEXT(it) = NULL;
}

void *chain_first(chain *_this) { return _this->head; }

void *chain_last(chain *_this) {
    void *h = _this->head;
    if (!h) return NULL;
    return LPREV(h);
}

void chain_revert(chain *_this) {
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

void chain_append_to(chain *_this, chain *dst) {
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

void chain_push_to(chain *_this, chain *dst) {
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

void chain_cut(chain *_this, void *from, void *to, chain *out) {
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

/* ---------------- ioq：com 读写缓存队列（自膨胀循环缓冲）---------------- */
/* 槽统一为 void*（size 以 (void*)(uintptr_t) 编码）。_head/_tail 为单调递增的
 * 逻辑槽计数，物理下标取 % _cap。每项变长，靠首槽 tag/size 自身区分：
 *   io 缓冲项 : [size(!=0), buf]      —— 2 槽，pull 时执行 io（按 rq/wq 方向 read/write）
 *   完成回调项: [0, cb, data]         —— 3 槽，pull 时回调 ((void(*)(void*))cb)(data)
 * （无 libuv：pull 同步执行 io、空队列返回 NULL 不阻塞；真实异步驱动应自行延迟兑现。）*/

/* 确保至少 need 个空闲槽，不足则倍增扩容并把现存项搬到新缓冲首部。 */
static void ioq_ensure(ioq *q, uint32_t need) {
    uint32_t used = q->_tail - q->_head;
    if (q->_cap && q->_cap - used >= need) return;
    uint32_t ncap = q->_cap ? q->_cap : 8;
    while (ncap - used < need) ncap *= 2;
    void **nb = (void **)malloc((size_t)ncap * sizeof(void *));
    for (uint32_t i = 0; i < used; i++) nb[i] = q->_buf[(q->_head + i) % q->_cap];
    free(q->_buf);
    q->_buf  = nb;
    q->_cap  = ncap;
    q->_head = 0;
    q->_tail = used;
}

/* 入队一段 io 缓冲项 [size, buf]（size 须 != 0）。 */
void ioq_push(ioq *_this, void *buf, int32_t size) {
    ioq_ensure(_this, 2);
    _this->_buf[ _this->_tail      % _this->_cap] = (void *)(uintptr_t)(uint32_t)size;
    _this->_buf[(_this->_tail + 1) % _this->_cap] = buf;
    _this->_tail += 2;
}

/* 入队一个完成回调项 [0, cb, data]。 */
void ioq_notify(ioq *_this, void *cb, void *data) {
    ioq_ensure(_this, 3);
    _this->_buf[ _this->_tail      % _this->_cap] = (void *)(uintptr_t)0;
    _this->_buf[(_this->_tail + 1) % _this->_cap] = cb;
    _this->_buf[(_this->_tail + 2) % _this->_cap] = data;
    _this->_tail += 3;
}

/* 取队首并执行 io：缓冲项按 rq/wq 方向调 com 的 read/write 并返回 buf；
 * 回调项调用 cb(data) 返回 NULL；空队列返回 NULL（无 libuv 不阻塞）。 */
void *ioq_pull(ioq *_this) {
    if (_this->_head == _this->_tail) return NULL;       /* 空 */
    uintptr_t tag = (uintptr_t)_this->_buf[_this->_head % _this->_cap];
    if (tag != 0) {                                      /* io 缓冲项 [size, buf] */
        void *buf = _this->_buf[(_this->_head + 1) % _this->_cap];
        _this->_head += 2;
        com *c = _this->com;
        uint32_t n = (uint32_t)tag;
        if (c) {
            if (c->rq == _this) { if (c->read)  c->read (c, buf, &n); }
            else                { if (c->write) c->write(c, buf, &n); }
        }
        return buf;
    }
    /* 完成回调项 [0, cb, data] */
    void *cb   = _this->_buf[(_this->_head + 1) % _this->_cap];
    void *data = _this->_buf[(_this->_head + 2) % _this->_cap];
    _this->_head += 3;
    if (cb) ((void (*)(void *))cb)(data);
    return NULL;
}

/* ---------------- limit：com 一次有界读视图（框架确定的读流程）---------------- */
/* limit 的 data/ending 由用户实现，size/len 为属性。框架只负责确定不变的读循环，
 * 驱动 com >> s（s 为 com[...] 句柄）：反复用 com 的 read 读入 data()+len，按 size
 * 与 ending 判截止，回写 len。缓存/边界策略全在用户的 data/ending 里，框架不写死。
 *   ending==NULL：定长，读满 size 即 IO_EOF；
 *   ending!=NULL：每次最多读 size 字节后回调 ending，>=0 命中（保留其值为 len）。
 * 返回 IO_EOF（读完）/ 负数（不可恢复，中断）/ IO_AGAIN（同步上下文无事件循环，报错信号）。*/
int32_t limit_read(com *c, limit *s) {
    if (!c || !s || !c->read) return -1;
    char *base = (char *)(s->data ? s->data(s) : NULL);
    if (!base) return -1;
    for (;;) {
        uint32_t chunk;
        if (s->ending) {
            chunk = s->size;                       /* 动态：每次最多读 size */
        } else {
            if (s->len >= s->size) return IO_EOF;  /* 定长：读满即完成 */
            chunk = s->size - s->len;
        }
        if (chunk == 0) return IO_EOF;
        int32_t r = c->read(c, base + s->len, &chunk);
        if (r < 0) return r;                       /* 不可恢复，中断 */
        if (r == IO_AGAIN) return IO_AGAIN;        /* 异步挂起：同步上下文由调用方报错 */
        s->len += chunk;
        if (s->ending) {                           /* 动态截止：回调判定（每对象方法指针） */
            int32_t m = s->ending(s);
            if (m >= 0) { s->len = (uint32_t)m; return IO_EOF; }
        } else if (s->len >= s->size) {
            return IO_EOF;                         /* 定长读满 */
        }
        if (r == IO_EOF || chunk == 0) return IO_EOF;  /* 设备已无更多数据 */
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

/* 跨平台统一线程 id：复用 platform.h 的 sc_thread_id（mach tid / gettid / GetCurrentThreadId） */
static uint64_t thd_current_id(void) {
    return sc_thread_id();
}

#if P_WIN
static DWORD WINAPI thd_entry(LPVOID p) {
#else
static void *thd_entry(void *p) {
#endif
    thread   *t  = (thread *)p;
    thd_impl *im = (thd_impl *)t->h;
    t->id = thd_current_id();
    im->fn((void *)(t + 1));            /* 执行 rpc 实际函数（参数紧随 thread） */
    if (!im->joinable) free(t);         /* detach：自释放整块 */
    return 0;
}

uint8_t thread_run(void (*fn)(void *), const void *params, size_t psize, thread **out,
                   uint32_t stack, uint8_t prio) {
    if (out) *out = NULL;
    if (!fn) return 0;
    thread *t = (thread *)malloc(sizeof(thread) + psize + sizeof(thd_impl));
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
    if (!h) { free(t); return 0; }
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
    if (err) { free(t); return 0; }
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

void thread_join(thread *_this) {
    if (!_this || !_this->h) return;
    thd_impl *im = (thd_impl *)_this->h;
    if (!im->joinable) return;          /* 防误用：detach 线程不可 join */
#if P_WIN
    WaitForSingleObject(im->t, INFINITE);
    CloseHandle(im->t);
#else
    pthread_join(im->t, NULL);
#endif
    free(_this);                        /* 回收联合实体（thread + 参数 + 私有区） */
}

/* ---------------- print：日志输出（语言关键字） ----------------
 * print：C printf 风格日志输出。print 属语言内核，故其运行时下沉到此
 * （op_impl.c 始终随工程编译链接，无需 inc）。
 *   - 首参 chn：u1 日志通道（透传），chn!=0 时在行首附加通道标记
 *   - fmt 前缀 "X:"（X ∈ FEWIDV）指定级别，无前缀默认 D
 *   - 输出 stdout：HH:MM:SS.mmm L| 文本（自动补换行）
 *   - 级别过滤：环境变量 SC_LOG=F/E/W/I/D/V（默认 D），首次调用时读取 */

/* 级别：1=F 致命 2=E 错误 3=W 警告 4=I 状态 5=D 调试 6=V 详尽 */
static const char SC_LV_CHARS[] = "FEWIDV";
#define SC_LV_DEF 5 /* D */

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

void print(uint8_t chn, const char *fmt, ...) {
    if (!fmt) return;

    /* "X:" 级别前缀 */
    int lv = SC_LV_DEF;
    if (*fmt && fmt[1] == ':') {
        const char *p = strchr(SC_LV_CHARS, *fmt);
        if (p) {
            lv = (int)(p - SC_LV_CHARS) + 1;
            fmt += 2;
            if (*fmt == ' ') fmt++;   /* 忽略 1 个且只忽略 1 个空格（允许多空格缩进） */
        }
    }
    if (lv > sc_log_level()) return;

    /* 时间戳 HH:MM:SS.mmm（本地时间） */
    P_clock now;
    char ts[16] = "--:--:--.---";
    if (P_time_now(&now) == 0) {
        struct tm tmv;
#if P_WIN
        time_t sec = now.tv_sec;
        localtime_s(&tmv, &sec);
#else
        time_t sec = now.tv_sec;
        localtime_r(&sec, &tmv);
#endif
        snprintf(ts, sizeof(ts), "%02d:%02d:%02d.%03ld",
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec, now.tv_nsec / 1000000L);
    }

    char line[2048];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if (n < 0) return;
    if (n >= (int)sizeof(line)) n = (int)sizeof(line) - 1;

    /* 单次 fprintf 输出整行（多线程下行内不撕裂），自动补换行 */
    const char *nl = (n > 0 && line[n - 1] == '\n') ? "" : "\n";
    if (chn)
        fprintf(stdout, "%s %c|%u| %s%s", ts, SC_LV_CHARS[lv - 1], (unsigned)chn, line, nl);
    else
        fprintf(stdout, "%s %c| %s%s", ts, SC_LV_CHARS[lv - 1], line, nl);
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
static future *g_ready_head;             /* 就绪队列头（经 future.next 串联） */
static future *g_ready_tail;             /* 就绪队列尾 */
static long    g_pending;                /* 已创建未完成 future 计数 */
static int     g_inited;                 /* async_init 是否已建立 */
static int   (*g_proc)(int id, future *f);   /* async_loop 传入的 id 派发回调（返回<0 停） */

static void push_ready(future *f) {      /* 调用方持锁 */
    f->next = NULL;
    if (g_ready_tail) g_ready_tail->next = f;
    else              g_ready_head = f;
    g_ready_tail = f;
}
static future *pop_ready(void) {         /* 调用方持锁 */
    future *f = g_ready_head;
    if (f) {
        g_ready_head = (future *)f->next;
        if (!g_ready_head) g_ready_tail = NULL;
        f->next = NULL;
    }
    return f;
}

uint8_t future_ready(future *_this) { return (uint8_t)(_this ? _this->ready : 0); }
void   *future_get(future *_this)   { return _this ? _this->result : NULL; }
void   *future_ctx(future *_this)   { return _this ? _this->ctx : NULL; }

future *future_new(void) {
    future *f = (future *)calloc(1, sizeof(future));
    future_init(f);
    return f;
}

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
        future *f = pop_ready();
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

void async_init(void) {
    if (g_inited) return;
    uv_loop_init(&g_loop);
    uv_async_init(&g_loop, &g_wake, drain_cb);
    uv_mutex_init(&g_mu);
    g_ready_head = g_ready_tail = NULL;
    g_pending = 0;
    g_proc = NULL;
    g_inited = 1;
}

void async_final(void) {
    if (!g_inited) return;
    uv_close((uv_handle_t *)&g_wake, NULL);
    uv_run(&g_loop, UV_RUN_NOWAIT);
    uv_loop_close(&g_loop);
    uv_mutex_destroy(&g_mu);
    g_ready_head = g_ready_tail = NULL;
    g_pending = 0;
    g_inited = 0;
}

void future_init(future *_this) {
    _this->id  = -1;
    _this->ctx = NULL;
    uv_mutex_lock(&g_mu);
    g_pending++;
    uv_mutex_unlock(&g_mu);
}

void future_done(future *f, void *result) {
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

uint8_t future_await(future *f, void *frame, void (*resume)(void *)) {
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

void async_loop(void *proc) {
    if (!g_inited) return;
    g_proc = (int (*)(int, future *))proc;
    uv_drive();
}

void async_io(void) {
    if (!g_inited) return;
    uv_drive();
}

/* op 层暴露给 async 叶子原语生态的 uv 事件循环句柄（poll 后端返回 NULL）。 */
void *op_uv_loop(void) { return g_inited ? (void *)&g_loop : NULL; }

/* 基础定时器原语（uv_timer 实现）：async 模块的 delay 等在其上构建。 */
typedef struct uv_timer_req { uv_timer_t timer; future *fut; } uv_timer_req;
static void op_timer_closed(uv_handle_t *h) { free(h->data); }
static void op_timer_fired(uv_timer_t *t) {
    uv_timer_req *r = (uv_timer_req *)t->data;
    future *f = r->fut;
    future_done(f, NULL);
    uv_close((uv_handle_t *)t, op_timer_closed);
}
void op_timer_arm(future *f, uint32_t ms) {
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
    com      *c;
    int       dir;          /* 0=读 1=写 */
    void     *buf;
    uint32_t  size;
    future   *fut;
} uv_io_req;

static void uv_io_complete(uv_io_req *r) {
    uint32_t n = r->size;
    if (r->dir == 0) { if (r->c->read)  r->c->read(r->c, r->buf, &n); }
    else             { if (r->c->write) r->c->write(r->c, r->buf, &n); }
    future_done(r->fut, (void *)(intptr_t)n);
}
static void uv_io_closed(uv_handle_t *h) { free(h->data); }
static void uv_io_cb(uv_poll_t *p, int st, int ev) {
    (void)st; (void)ev;
    uv_io_req *r = (uv_io_req *)p->data;
    uv_poll_stop(p);
    uv_io_complete(r);
    uv_close((uv_handle_t *)p, uv_io_closed);
}

static future *uv_com_io(com *c, int dir, void *buf, uint32_t size) {
    future *f = future_new();
    int32_t (*probe)(com *, void **) = (dir == 0) ? c->readable : c->writable;
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
    future_done(f, (void *)(intptr_t)n);
    return f;
}

future *com_read_async(com *c, void *data, uint32_t size) { return uv_com_io(c, 0, data, size); }
future *com_write_async(com *c, void *buf,  uint32_t size) { return uv_com_io(c, 1, buf,  size); }

/* 【E】com[...] 句柄异步有界读（uv 后端）：可多路复用 → uv_poll，回调里跑 limit_read，
 * 遇 IO_AGAIN 继续 poll、命中/出错则兑现；非多路复用 → 即时跑完。 */
typedef struct uv_limit_req {
    uv_poll_t poll;
    com      *c;
    limit    *lim;
    future   *fut;
} uv_limit_req;

static void uv_limit_done(uv_limit_req *r, int32_t rc) {
    int32_t res = (rc < 0) ? rc : (int32_t)r->lim->len;
    future_done(r->fut, (void *)(intptr_t)res);
}
static void uv_limit_closed(uv_handle_t *h) { free(h->data); }
static void uv_limit_cb(uv_poll_t *p, int st, int ev) {
    (void)st; (void)ev;
    uv_limit_req *r = (uv_limit_req *)p->data;
    int32_t rc = limit_read(r->c, r->lim);
    if (rc == IO_AGAIN) return;                 /* 未满：继续等待下次就绪 */
    uv_poll_stop(p);
    uv_limit_done(r, rc);
    uv_close((uv_handle_t *)p, uv_limit_closed);
}

future *com_limit_read_async(com *c, limit *s) {
    future *f = future_new();
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
    future_done(f, (void *)(intptr_t)res);
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
    future            *fut;
} timer_node;
static timer_node *g_timers;            /* 仅循环线程访问 */

/* com 异步 io 请求节点（com_*_async 登记，事件循环驱动）。
 * 句柄就绪后常驻注册进 mux，避免每轮重建（O(1)）；非多路复用（无句柄）走重探轮询。 */
typedef struct io_req {
    struct io_req *next;
    com           *c;
    int            dir;                 /* 0=读 1=写 */
    void          *buf;
    uint32_t       size;
    limit         *lim;                 /* 非 NULL=【E】com[...] 句柄有界读（框架读循环） */
    future        *fut;
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
void async_init(void) {
    if (g_inited) return;
    sc_mutex_init(&g_mu);
    g_ready_head = g_ready_tail = NULL;
    g_pending = 0;
    g_proc    = NULL;
    g_timers  = NULL;
    g_io_reqs = NULL;
    mux_open();
    g_inited = 1;
}

void async_final(void) {
    if (!g_inited) return;
    mux_close();
    while (g_timers)  { timer_node *t = g_timers;  g_timers  = t->next; free(t); }
    while (g_io_reqs) { io_req     *r = g_io_reqs; g_io_reqs = r->next; free(r); }
    sc_mutex_final(&g_mu);
    g_ready_head = g_ready_tail = NULL;
    g_pending = 0;
    g_inited  = 0;
}

void future_init(future *_this) {
    _this->id  = -1;         /* 默认无标签：仅协程 await 用（0 是合法 id，不能作哨兵） */
    _this->ctx = NULL;       /* 默认无上下文；future<ID>(ctx) 由构造辅助回填 */
    sc_mutex_lock(&g_mu);
    g_pending++;
    sc_mutex_unlock(&g_mu);
}

void future_done(future *f, void *result) {
    int  enqueue;
    long remaining;
    sc_mutex_lock(&g_mu);
    f->result = result;
    f->ready  = 1;
    enqueue = (f->frame != NULL) || (f->id >= 0);   /* 协程等待者 / 带 id 待派发 → 入队 */
    if (enqueue) push_ready(f);
    g_pending--;
    remaining = g_pending;
    sc_mutex_unlock(&g_mu);
    if (enqueue || remaining == 0) wake();           /* 唤醒去 resume/派发，或去判退出 */
}

uint8_t future_await(future *f, void *frame, void (*resume)(void *)) {
    uint8_t r;
    sc_mutex_lock(&g_mu);
    f->frame  = frame;
    f->resume = resume;
    r = (uint8_t)f->ready;
    sc_mutex_unlock(&g_mu);
    return r;
}

/* 排空就绪队列：协程帧 resume / 带 id 无等待者经 g_proc 派发。返回 1=请求停循环。 */
static int drain_ready(void) {
    for (;;) {
        sc_mutex_lock(&g_mu);
        future *f = pop_ready();
        sc_mutex_unlock(&g_mu);
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
            future *f = t->fut;
            free(t);
            future_done(f, NULL);
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
        int32_t (*probe)(com *, void **) =
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
                int32_t rc = limit_read(r->c, r->lim);
                if (rc == IO_AGAIN) { r->armed = 0; pp = &r->next; continue; }
                *pp = r->next;
                future *f = r->fut;
                int32_t res = (rc < 0) ? rc : (int32_t)r->lim->len;
                free(r);
                future_done(f, (void *)(intptr_t)res);
                continue;
            }
            uint32_t n = r->size;
            if (r->dir == 0) { if (r->c->read)  r->c->read(r->c, r->buf, &n); }
            else             { if (r->c->write) r->c->write(r->c, r->buf, &n); }
            *pp = r->next;
            future *f = r->fut;
            free(r);
            future_done(f, (void *)(intptr_t)n);   /* 结果=收发字节数 */
        } else {
            pp = &r->next;
        }
    }
}

static void run_loop(void) {
    if (!g_inited) return;
    for (;;) {
        if (drain_ready()) break;                    /* 派发器请求停 */
        sc_mutex_lock(&g_mu);
        int done = (g_pending == 0 && g_ready_head == NULL);
        sc_mutex_unlock(&g_mu);
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

void async_loop(void *proc) {
    if (!g_inited) return;
    g_proc = (int (*)(int, future *))proc;   /* 按 id 派发回调（NULL=纯协程驱动） */
    run_loop();
}

void async_io(void) {
    if (!g_inited) return;
    run_loop();
}

/* op 层暴露给 async 叶子原语生态的钩子（自有后端无 uv 循环）。 */
void *op_uv_loop(void) { return NULL; }

/* 基础定时器原语（事件循环超时驱动）：async 模块的 delay 等在其上构建。 */
void op_timer_arm(future *f, uint32_t ms) {
    timer_node *t = (timer_node *)calloc(1, sizeof(timer_node));
    t->deadline_ms = now_ms() + (uint64_t)ms;
    t->fut  = f;
    t->next = g_timers;
    g_timers = t;
    wake();              /* 缩短下一次等待超时，及时按新截止重算 */
}

/* com 异步收发桥接：登记进活动表，由事件循环驱动。 */
future *com_read_async(com *c, void *data, uint32_t size) {
    future *f = future_new();
    io_req *r = (io_req *)calloc(1, sizeof(io_req));
    r->c = c; r->dir = 0; r->buf = data; r->size = size; r->fut = f; r->fd = SC_FD_NONE;
    r->next = g_io_reqs;
    g_io_reqs = r;
    wake();
    return f;
}

future *com_write_async(com *c, void *buf, uint32_t size) {
    future *f = future_new();
    io_req *r = (io_req *)calloc(1, sizeof(io_req));
    r->c = c; r->dir = 1; r->buf = buf; r->size = size; r->fut = f; r->fd = SC_FD_NONE;
    r->next = g_io_reqs;
    g_io_reqs = r;
    wake();
    return f;
}

/* 【E】com[...] 句柄异步有界读：登记 lim 请求，循环就绪后跑框架读流程 limit_read。 */
future *com_limit_read_async(com *c, limit *s) {
    future *f = future_new();
    io_req *r = (io_req *)calloc(1, sizeof(io_req));
    r->c = c; r->dir = 0; r->lim = s; r->fut = f; r->fd = SC_FD_NONE;
    r->next = g_io_reqs;
    g_io_reqs = r;
    wake();
    return f;
}
#endif /* SCC_WITH_UV */
