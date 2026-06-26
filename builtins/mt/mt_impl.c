/* mt_impl.c —— sc 多线程支持标准（mt.h 契约）默认实现
 * 跨平台经由 builtins/platform.h：POSIX pthread / Windows 线程 API
 */
#include "mt.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>

/* 线程/互斥/条件变量的跨平台原语均由 platform.h 提供（POSIX 下已含 pthread.h） */

/* ---------------- mutex ---------------- */

typedef sc_mutex_t mtx_state;

void mutex_init(mutex *_this) {
    mtx_state *m = (mtx_state *)malloc(sizeof(mtx_state));
    if (m) sc_mutex_init(m);
    _this->h = m;
}

void mutex_drop(mutex *_this) {
    mtx_state *m = (mtx_state *)_this->h;
    if (!m) return;
    sc_mutex_final(m);
    free(m);
    _this->h = NULL;
}

void mutex_lock(mutex *_this) {
    mtx_state *m = (mtx_state *)_this->h;
    if (!m) return;
    sc_mutex_lock(m);
}

void mutex_unlock(mutex *_this) {
    mtx_state *m = (mtx_state *)_this->h;
    if (!m) return;
    sc_mutex_unlock(m);
}

uint8_t mutex_try_lock(mutex *_this) {
    mtx_state *m = (mtx_state *)_this->h;
    if (!m) return 0;
    return (uint8_t)sc_mutex_try(m);
}

/* ---------------- cond ---------------- */

typedef sc_cond_t cnd_state;

void cond_init(cond *_this) {
    cnd_state *c = (cnd_state *)malloc(sizeof(cnd_state));
    if (c) sc_cond_init(c);
    _this->h = c;
}

void cond_drop(cond *_this) {
    cnd_state *c = (cnd_state *)_this->h;
    if (!c) return;
    sc_cond_final(c);
    free(c);
    _this->h = NULL;
}

void cond_one(cond *_this) {
    cnd_state *c = (cnd_state *)_this->h;
    if (!c) return;
    sc_cond_one(c);
}

void cond_all(cond *_this) {
    cnd_state *c = (cnd_state *)_this->h;
    if (!c) return;
    sc_cond_all(c);
}

/* wait 方法原语：nsec/sec 全 0 → 无限等待，否则相对超时。
 * 返回 0 被唤醒 / 1 超时 / -1 错误 */
int32_t cond_wait(cond *c, mutex *m, uint64_t nsec, uint64_t sec) {
    if (!c || !c->h || !m || !m->h) return -1;
    return (int32_t)sc_cond_wait((cnd_state *)c->h, (mtx_state *)m->h, nsec, sec);
}

/* ---------------- barrier ---------------- */
/* 直接复用 platform.h 的 sc_barrier_t（mutex + cond 自实现 N 方汇合）。 */

void barrier_init(barrier *_this, uint32_t n) {
    sc_barrier_t *b = (sc_barrier_t *)malloc(sizeof(sc_barrier_t));
    if (b) sc_barrier_init(b, n);
    _this->h = b;
}

void barrier_drop(barrier *_this) {
    sc_barrier_t *b = (sc_barrier_t *)_this->h;
    if (!b) return;
    sc_barrier_final(b);
    free(b);
    _this->h = NULL;
}

uint8_t barrier_wait(barrier *_this) {
    sc_barrier_t *b = (sc_barrier_t *)_this->h;
    if (!b) return 0;
    return (uint8_t)(sc_barrier_wait(b) ? 1 : 0);
}

/* ---------------- pool：线程池协议的「默认」实现 ---------------- */
/* pool 协议（vtable）由语言内核声明（op.h，默认带入）；本模块按「默认策略」用
 * default_pool(n) 具名构造——犹如 io 的 file() 之于 com：pol_state 首字段即
 * pool base，故 (pool*) 与 (pol_state*) 同址，方法直接 (pol_state*)_this 回取私有
 * 区。整块（含 pool 对象、同步原语、柔性 worker 句柄数组）一次分配，drop 整块回收。 */

/* 任务节点：联合分配 [pool_task][rpc 参数 psize]（与 run 的联合实体同哲学） */
typedef struct pool_task {
    struct pool_task *next;
    void            (*fn)(void *);   /* rpc 实际函数，参数紧随本节点 */
} pool_task;

/* 池盒子：首字段 pool base（协议 vtable）+ 单链 FIFO 队列 + 双条件变量（来活 / 全部完成） */
typedef struct {
    pool       base;       /* op 层 pool 协议（首字段：与 pol_state 同址，可互转） */
    mtx_state  mu;
    cnd_state  more;       /* 来活：post 唤醒空闲 worker */
    cnd_state  idle;       /* 全部完成：pending 归零唤醒 join 等待者 */
    pool_task *head, *tail;
    uint32_t   pending;    /* 排队 + 执行中的任务数 */
    uint32_t   nthr;
    uint8_t    shutdown;
#if P_WIN
    HANDLE     thr[1];     /* 柔性：与 pol_state 同块分配 nthr 个 */
#else
    pthread_t  thr[1];
#endif
} pol_state;

static void pol_lock(pol_state *p)   { sc_mutex_lock(&p->mu); }
static void pol_unlock(pol_state *p) { sc_mutex_unlock(&p->mu); }

/* worker 循环：取任务 → 解锁执行 → pending 递减，归零唤醒 join */
#if P_WIN
static DWORD WINAPI pol_worker(LPVOID arg) {
#else
static void *pol_worker(void *arg) {
#endif
    pol_state *p = (pol_state *)arg;
    for (;;) {
        pol_lock(p);
        while (!p->head && !p->shutdown)
            sc_cond_wait(&p->more, &p->mu, 0, 0);   /* 无限等待来活 */
        pool_task *t = p->head;
        if (!t) { pol_unlock(p); break; }      /* shutdown 且队列已空 */
        p->head = t->next;
        if (!p->head) p->tail = NULL;
        pol_unlock(p);

        t->fn((void *)(t + 1));                /* 参数紧随任务节点 */
        free(t);

        pol_lock(p);
        if (--p->pending == 0)
            sc_cond_all(&p->idle);
        pol_unlock(p);
    }
    return 0;
}

/* run 方法（协议指针，对称 thread_run）：装填好的 rpc 参数入队。
 * 返回 1 成功 / 0 失败（池已停 / 无 worker / 内存不足） */
static uint8_t pol_run(pool *_this, void (*fn)(void *), const void *params, size_t psize) {
    pol_state *p = (pol_state *)_this;
    if (!p || !fn) return 0;
    pool_task *t = (pool_task *)malloc(sizeof(pool_task) + psize);
    if (!t) return 0;
    t->next = NULL;
    t->fn = fn;
    if (params && psize) memcpy(t + 1, params, psize);
    pol_lock(p);
    if (p->shutdown || p->nthr == 0) { pol_unlock(p); free(t); return 0; }
    if (p->tail) p->tail->next = t; else p->head = t;
    p->tail = t;
    p->pending++;
    sc_cond_one(&p->more);
    pol_unlock(p);
    return 1;
}

/* join 方法：屏障，等待全部已提交任务完成（之后 pool 仍可继续提交） */
static void pol_join(pool *_this) {
    pol_state *p = (pol_state *)_this;
    if (!p) return;
    pol_lock(p);
    while (p->pending > 0)
        sc_cond_wait(&p->idle, &p->mu, 0, 0);   /* 无限等全部完成 */
    pol_unlock(p);
}

/* drop 方法：等已提交任务全部完成 → 停 worker → 回收整块（含 pool 对象本身） */
static void pol_drop(pool *_this) {
    pol_state *p = (pol_state *)_this;
    if (!p) return;
    pol_lock(p);
    p->shutdown = 1;
    sc_cond_all(&p->more);
    pol_unlock(p);
    for (uint32_t i = 0; i < p->nthr; i++) {
#if P_WIN
        WaitForSingleObject(p->thr[i], INFINITE);
        CloseHandle(p->thr[i]);
#else
        pthread_join(p->thr[i], NULL);
#endif
    }
    sc_mutex_final(&p->mu);
    sc_cond_final(&p->more);
    sc_cond_final(&p->idle);
    free(p);                /* 整块回收（pool 对象与盒子同生命周期） */
}

/* default_pool：「默认策略」线程池构造（填充协议 vtable，返回 pool&）。
 *   - n：工作线程数；0 → CPU 逻辑核数
 *   - 返回 &box->base（失败 NULL）；用完调 p->drop() 停池回收 */
struct pool *default_pool(uint32_t n) {
    if (n == 0) n = P_ncpu();
    pol_state *p = (pol_state *)malloc(sizeof(pol_state) + (n - 1) * sizeof(p->thr[0]));
    if (!p) return NULL;
    memset(p, 0, sizeof(*p));
    p->base.h    = p;        /* 私有区即本盒子（方法实际经 (pol_state*)_this 回取） */
    p->base.run  = pol_run;
    p->base.join = pol_join;
    p->base.drop = pol_drop;
    sc_mutex_init(&p->mu);
    sc_cond_init(&p->more);
    sc_cond_init(&p->idle);
    for (uint32_t i = 0; i < n; i++) {
#if P_WIN
        p->thr[i] = CreateThread(NULL, 0, pol_worker, p, 0, NULL);
        if (!p->thr[i]) break;
#else
        if (pthread_create(&p->thr[i], NULL, pol_worker, p) != 0) break;
#endif
        p->nthr++;
    }
    return &p->base;
}

/* ---------------- queue + port：消息队列协议的「PORT 单收件箱」实现 ---------------- */
/* queue 协议（vtable）由语言内核声明（op.h，默认带入）；本模块按 thread_queue.c 的
 * 「每线程 PORT」模型实现——port 是每线程的单一收件箱中枢，多个 queue 归并进同一 port，
 * 消息在 port 邮箱里按到达顺序（同优先级 FIFO）排成单链，从而获得跨 queue 的全局时序。
 *   - port（sc_port）：每线程一个（TLS g_port 惰性堆分配）。其地址即该线程的稳定唯一身份。
 *     字段：单收件箱 box（ready 按 prio 有序 + delaying 按 deadline 有序）、recv 条件变量
 *     （消费者等来活）、send 条件变量（R2 替代死等）、waiting（本线程当前阻塞 sync 的队列，
 *     环检测用）、substituting（R2 铁律）、attached（绑定到本 port 的队列链，线程退出自动 detach）。
 *   - queue（que_state）：首字段 queue base（vtable）+ consumer（谁 pull 我，首次 pull 惰性 attach）
 *     + staging（attach 前的暂存收件箱，首次 attach 时整块插入 port 收件箱首部，优先处理历史）
 *     + count（归属本队列的在途消息数，detach 清理用）+ host（宿主三态）。
 *   - 宿主三态（host）：NULL 未绑/延迟、(pool*)-1 当前/主线程（自跑 pull 循环）、&pool
 *     线程池消费——host 为真实 pool 时，post 直接经 pool 协议指针转交池（pool->run），由
 *     池的工作线程并发执行（不入 port，无 attach）。
 *   - 全局唯一互斥 g_mutex（仿参考 g_mutex）保护所有 port 收件箱 + queue 暂存 + consumer/waiting
 *     关系图，彻底消除锁序环；各 port 的 recv/send 条件变量均在 g_mutex 上等待。
 *     调用方阻塞/唤醒用每调用私有的应答描述符（叶子锁，不与 g_mutex 嵌套）。 */

/* 消息节点：联合分配 [pmsg][rpc 参数 psize]（与 pool 任务节点同哲学）。
 * receiver=目标队列（detach 清理 + 归属判定）；prio=优先级（ready 按其有序）；
 * delayed/deadline=延迟项（投递带 delay_ms>0 则计算绝对到期时刻入 delaying 有序链）。*/
typedef struct pmsg {
    struct pmsg      *next;
    struct que_state *receiver;    /* 目标队列（归属/清理） */
    void            (*fn)(void *); /* rpc 实际函数，参数紧随本节点 */
    int32_t           prio;        /* 优先级（高者先被消费，0=默认） */
    uint8_t           delayed;     /* 是否延迟项（在 delaying 链上） */
    struct timespec   deadline;    /* 延迟到期绝对时刻（单调钟；delayed=1 时有效） */
} pmsg;

/* 收件箱：ready 单链（按 prio 有序，head=最高优先级）+ delaying 单链（按 deadline 升序）。
 * port 与 queue（暂存）各内嵌一个，复用同一组插入/提升助手。 */
typedef struct inbox {
    pmsg *head, *tail;             /* ready 链：head=队头（最高优先级，先消费） */
    pmsg *delaying;                /* 延迟链：按 deadline 升序，pull 到期提升进 ready */
} inbox;

/* 端口：每线程一个收件箱中枢（TLS g_port 惰性堆分配，地址即线程身份）。 */
typedef struct sc_port {
    inbox             box;         /* 单收件箱（聚合所有 attached queue 的消息） */
    cnd_state         recv;        /* 消费者等来活（在 g_mutex 上等待） */
    cnd_state         send;        /* R2：替代死等（本轮先建字段，R2 启用） */
    struct que_state *waiting;     /* 本线程当前阻塞 sync 的队列（NULL=未阻塞；环检测用） */
    uint8_t           substituting;/* R2：铁律（本轮先建字段，R2 启用） */
    struct que_state *attached;    /* attached 队列链（线程退出自动 detach；经 anext 串） */
} sc_port;

/* 队列盒子：首字段 queue base（协议 vtable）+ consumer（谁 pull）+ staging（attach 前暂存）。 */
typedef struct que_state {
    queue            base;         /* op 层 queue 协议（首字段：与 que_state 同址，可互转） */
    inbox            staging;      /* attach 前暂存收件箱（首次 attach 整块插 port 首部） */
    uint32_t         count;        /* 归属本队列的在途消息数（detach 清理用） */
    uint8_t          closed;       /* drop 置位：拒收新消息 */
    struct pool     *host;         /* 宿主绑定：NULL/(pool*)-1/&pool */
    struct sc_port  *consumer;     /* 消费此队列的 port（首次 pull 惰性 attach；NULL=未绑） */
    struct que_state *anext;       /* attached 链 next（挂在 consumer->attached 上） */
} que_state;

static sc_mutex_t  g_mutex;        /* 全局唯一锁：保护所有 port 收件箱 + queue 暂存 + 关系图 */
static TLS sc_port *g_port;        /* 每线程 port 指针（惰性堆分配；地址=线程身份） */

/* ---- 线程退出 TLS 析构：消费线程退出时自动 detach 全部 attached queue + 释放 port ----
 * sc 寿命模型：queue 是 main 拥有的耐久堆对象，port 是消费线程的临时中枢。消费线程
 * （run 语句起的）pull 完即退出，须在退出那刻把它 attach 的队列解绑（consumer 置空，
 * 之后 main 的 q->drop() 见 consumer==NULL → 只释 queue 自身，无 UAF）并回收 port。
 * 经 pthread_key 析构器（POSIX）/ FlsAlloc 回调（Windows）在线程退出时触发。 */
static void port_release(sc_port *p);

#if P_WIN
static DWORD g_port_fls = FLS_OUT_OF_INDEXES;
static void WINAPI mt_port_dtor(void *p) { if (p) port_release((sc_port *)p); }
#else
static pthread_key_t g_port_key;
static void mt_port_dtor(void *p) { if (p) port_release((sc_port *)p); }
#endif

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor)) static void mt_init(void) {
    sc_mutex_init(&g_mutex);
#if P_WIN
    g_port_fls = FlsAlloc(mt_port_dtor);
#else
    pthread_key_create(&g_port_key, mt_port_dtor);
#endif
}
static inline void mt_ensure(void) {}
#else
/* MSVC 回退：启动期单线程惰性 init（首个 queue 在主线程创建于派生消费线程之前）。 */
static int g_mt_inited = 0;
static inline void mt_ensure(void) {
    if (!g_mt_inited) {
        sc_mutex_init(&g_mutex);
        g_port_fls = FlsAlloc(mt_port_dtor);
        g_mt_inited = 1;
    }
}
#endif

/* 取本线程 port（惰性堆分配并登记 TLS 析构器）。无需持 g_mutex（仅本线程 TLS）。 */
static sc_port *port_self(void) {
    sc_port *p = g_port;
    if (!p) {
        mt_ensure();
        p = (sc_port *)calloc(1, sizeof(sc_port));
        if (!p) return NULL;
        sc_cond_init(&p->recv);
        sc_cond_init(&p->send);
        g_port = p;
#if P_WIN
        FlsSetValue(g_port_fls, p);    /* 线程退出触发 mt_port_dtor */
#else
        pthread_setspecific(g_port_key, p);
#endif
    }
    return p;
}

/* ready 链按优先级有序插入：head=最高优先级；同优先级稳定（FIFO，插在同级之后）。 */
static void inbox_ready_insert(inbox *b, pmsg *m) {
    pmsg *prev = NULL, *cur = b->head;
    while (cur && cur->prio >= m->prio) { prev = cur; cur = cur->next; }
    m->next = cur;
    if (prev) prev->next = m; else b->head = m;
    if (!cur) b->tail = m;          /* 插到尾部 */
}

/* delaying 链按 deadline 升序插入；同时刻稳定（插在相等之后）。 */
static void inbox_delay_insert(inbox *b, pmsg *m) {
    pmsg *prev = NULL, *cur = b->delaying;
    while (cur && !clock_gt(cur->deadline, m->deadline)) { prev = cur; cur = cur->next; }
    m->next = cur;
    if (prev) prev->next = m; else b->delaying = m;
}

/* 把 delaying 链中所有已到期（deadline <= now）的项提升进 ready（按 prio 重新有序）。 */
static void inbox_promote(inbox *b) {
    if (!b->delaying) return;
    struct timespec now;
    P_clock_now(&now);
    while (b->delaying) {
        pmsg *d = b->delaying;
        if (clock_gt(d->deadline, now)) break;   /* 未到期，后续更晚，停 */
        b->delaying = d->next;
        d->delayed = 0;
        inbox_ready_insert(b, d);
    }
}

/* 循环死锁替代判定（须持 g_mutex）：from 端口拟向 tq 队列 sync，是否应改为本地替代执行？
 *   - tq->consumer == from：向自己消费的队列 sync（自替代）。
 *   - 否则沿 consumer→waiting→consumer 链行走：若某环节回到 from → 存在循环互锁。
 * 走的是「每节点至多一条出边（waiting）」的函数图；不变量（替代使图保持无环）下必终止；
 * 仍加防御步数上限，意外成环时保守返回 0（退化为正常投递，至多死锁而非检测器卡死）。 */
static int port_should_substitute(que_state *tq, sc_port *from) {
    if (tq->consumer == from) return 1;            /* 自替代 */
    sc_port *c = tq->consumer;
    int guard = 4096;                              /* 防御：远超任何合理线程数 */
    while (c && guard-- > 0) {
        que_state *w = c->waiting;                 /* c 线程当前阻塞 sync 的队列 */
        if (!w) return 0;                          /* c 未阻塞 → 无循环 */
        if (w->consumer == from) return 1;         /* 循环回到自己 → 替代 */
        c = w->consumer;
    }
    return 0;
}


/* post 方法（协议指针，对称 pool.run）：rpc 调用整体打包成消息入队。
 * prio：优先级（高者先被消费，0=默认）；delay_ms：延迟毫秒（>0 则到期才可被 pull）。
 * 宿主为真实线程池（host 非 NULL 且非 (pool*)-1）时，直接转交池消费（经 pool 协议
 * 指针 host->run 派发，不入 port，无 attach）——此路径忽略 prio/delay（池自调度）。
 * 否则按 PORT 模型投递：
 *   - 已 attach（consumer 非空）：插入 consumer port 的单收件箱（delay_ms>0 入 delaying
 *     有序链，否则按 prio 入 ready），唤醒该 port 的消费者（recv）。
 *   - 未 attach（consumer 空）：暂存到本队列 staging 收件箱（首次 pull/attach 时整块
 *     插入 port 首部，优先处理历史）。
 * 返回 1 成功 / 0 失败（队列已关闭 / 内存不足） */
static uint8_t que_post(queue *_this, void (*fn)(void *), const void *params, size_t psize,
                        int32_t prio, int64_t delay_ms) {
    que_state *q = (que_state *)_this;
    if (!q || !fn) return 0;
    /* 宿主=线程池：投递即提交给池（pool->run 自行 memcpy 参数），忽略 prio/delay。
     * host 只在构造时设定、之后不改，故此处免锁读取安全。 */
    {
        struct pool *host = q->host;
        if (host && host != (struct pool *)-1)
            return host->run(host, fn, params, psize);
    }
    pmsg *m = (pmsg *)malloc(sizeof(pmsg) + psize);
    if (!m) return 0;
    m->next = NULL;
    m->receiver = q;
    m->fn = fn;
    m->prio = prio;
    m->delayed = 0;
    if (params && psize) memcpy(m + 1, params, psize);

    mt_ensure();
    sc_mutex_lock(&g_mutex);
    if (q->closed) { sc_mutex_unlock(&g_mutex); free(m); return 0; }

    if (delay_ms > 0) {
        struct timespec now, rel;
        P_clock_now(&now);
        rel.tv_sec  = (time_t)(delay_ms / 1000);
        rel.tv_nsec = (long)((delay_ms % 1000) * 1000000L);
        clock_inc(now, rel, m->deadline);     /* 绝对到期时刻（单调钟） */
        m->delayed = 1;
    }

    sc_port *cp = q->consumer;
    if (cp) {
        /* 已 attach：入 consumer port 单收件箱，唤醒其消费者 */
        if (m->delayed) inbox_delay_insert(&cp->box, m);
        else            inbox_ready_insert(&cp->box, m);
        q->count++;
        sc_cond_one(&cp->recv);
    } else {
        /* 未 attach：暂存本队列 staging（首次 attach flush 进 port 首部） */
        if (m->delayed) inbox_delay_insert(&q->staging, m);
        else            inbox_ready_insert(&q->staging, m);
        q->count++;
    }
    sc_mutex_unlock(&g_mutex);
    return 1;
}

/* sync 方法（协议指针）：阻塞带回复——把 rpc 调用投递给某消费者（另一线程 pull 或池
 * 工作线程）执行，阻塞至执行完成，结果回填 caller 的 params 首字段（返回槽 _）。
 *   - timeout_ms<=0（无限等）：复用 post 路径，把 caller 的 (fn, params 指针, &reply) 打成
 *     trampoline 投递，消费者执行 q_sync_run——在 caller 的 params 缓冲上实跑 fn 后置位、唤醒。
 *     零分配；caller 阻塞期间 params 与 reply 栈保活。
 *   - timeout_ms>0（有限超时，P5c）：走 q_sync_box 堆盒子 + 引用计数路径（见下），执行方只碰
 *     盒子堆缓冲，caller 超时 abort 不致 UAF。返回 1=超时。
 *   - prio/delay_ms 透传给 post（优先级排序 / 延迟投递）。
 *   - 返回 0 成功 / 1 超时 / -1 投递失败（队列已关闭等）。
 *   - 循环死锁替代（向自己消费的队列、或循环互锁）由外层 que_sync 包装拦截、改本地执行（P5d）。 */
typedef struct q_sync {
    mtx_state mu;
    cnd_state done_cv;
    uint8_t   done;
} q_sync;

typedef struct q_sync_tramp {
    void   (*fn)(void *);   /* 真正的 rpc worker */
    void    *params;        /* caller 的 rpc 参数缓冲（执行结果回填其首字段） */
    q_sync  *reply;         /* caller 栈上的应答描述符 */
} q_sync_tramp;

/* 消费者侧蹦床：在 caller 缓冲上实跑 rpc，再置位并唤醒 caller。 */
static void q_sync_run(void *arg) {
    q_sync_tramp *t = (q_sync_tramp *)arg;
    t->fn(t->params);                       /* 在 caller 的 params 上执行（写入返回槽 _） */
    sc_mutex_lock(&t->reply->mu);
    t->reply->done = 1;
    sc_cond_one(&t->reply->done_cv);
    sc_mutex_unlock(&t->reply->mu);
}

/* ---- P5c 有限超时 sync：堆盒子 + 引用计数 ----
 * 无限等路径（timeout_ms<=0）下，sync 描述符与结果缓冲在 caller 栈上、靠 caller 阻塞期间
 * 保活；但有限超时下 caller 会 abort 解栈，执行方再碰其栈即 UAF。故超时路径改为堆分配
 * q_sync_box：盒子自带结果缓冲（堆），执行方只在盒子堆内存上跑 rpc，绝不碰 caller 栈。
 * 引用计数（初 2 = caller + 在途消息）协同释放：whoever 最后撒手（refs→0）回收。
 *   state: 0=pending / 1=done(执行方已完成) / 2=abandoned(caller 超时弃用)。 */
typedef struct q_sync_box {
    mtx_state   mu;
    cnd_state   done_cv;
    int32_t     state;      /* 0 pending / 1 done / 2 abandoned */
    int32_t     refs;       /* 引用计数：初 2（caller + 在途消息）；归 0 回收 */
    void      (*fn)(void *);/* 真正的 rpc worker */
    /* rpc 参数/结果缓冲紧随本结构体之后：[q_sync_box][params psize] */
} q_sync_box;

/* refs 归 0 后的真正回收（须在 mu 解锁之后调用，因为要 final 掉 mu/cv）。 */
static void q_sync_box_free(q_sync_box *b) {
    sc_mutex_final(&b->mu);
    sc_cond_final(&b->done_cv);
    free(b);
}

/* 消费者侧蹦床：在盒子堆缓冲上实跑 rpc，再据 caller 是否已弃用决定唤醒/回收。 */
static void q_sync_box_run(void *arg) {
    q_sync_box *b = *(q_sync_box **)arg;     /* post 拷贝的是 box 指针 */
    b->fn((void *)(b + 1));                  /* 在盒子堆缓冲上执行（写返回槽 _）——永不碰 caller 栈 */
    sc_mutex_lock(&b->mu);
    if (b->state != 2) {                     /* caller 未弃用：置完成并唤醒 */
        b->state = 1;
        sc_cond_one(&b->done_cv);
    }
    int32_t r = --b->refs;                   /* 撤销在途消息持有的引用 */
    sc_mutex_unlock(&b->mu);
    if (r == 0) q_sync_box_free(b);          /* caller 已先弃用撒手 → 末位回收 */
}

static int32_t que_sync_core(queue *_this, void (*fn)(void *), void *params, size_t psize,
                             int32_t prio, int64_t delay_ms, int64_t timeout_ms) {
    que_state *q = (que_state *)_this;
    if (!q || !fn) return -1;

    /* ---- 有限超时路径（P5c）：堆盒子 + 引用计数 ---- */
    if (timeout_ms > 0) {
        q_sync_box *b = (q_sync_box *)malloc(sizeof(q_sync_box) + psize);
        if (!b) return -1;
        sc_mutex_init(&b->mu);
        sc_cond_init(&b->done_cv);
        b->state = 0;
        b->refs  = 2;                        /* caller + 在途消息 */
        b->fn    = fn;
        if (params && psize) memcpy(b + 1, params, psize);
        /* 投递盒子指针（post 拷贝指针；参数已在盒子堆缓冲里）；prio/delay 透传 */
        uint8_t ok = q->base.post(&q->base, q_sync_box_run, &b, sizeof(b), prio, delay_ms);
        if (!ok) { q_sync_box_free(b); return -1; }

        /* 限时等待完成：用绝对截止重算剩余，鲁棒于虚假唤醒 */
        struct timespec deadline, now, rel;
        P_clock_now(&now);
        rel.tv_sec  = (time_t)(timeout_ms / 1000);
        rel.tv_nsec = (long)((timeout_ms % 1000) * 1000000L);
        clock_inc(now, rel, deadline);
        int32_t ret;
        sc_mutex_lock(&b->mu);
        for (;;) {
            if (b->state == 1) { ret = 0; break; }       /* 已完成 */
            P_clock_now(&now);
            if (clock_ge(now, deadline)) {               /* 超时 */
                if (b->state == 1) { ret = 0; break; }   /* 末刻 race 重查 */
                b->state = 2;                            /* 弃用：执行方将自行回收 */
                ret = 1;
                break;
            }
            clock_dec(deadline, now, rel);
            sc_cond_wait(&b->done_cv, &b->mu, (uint64_t)rel.tv_nsec, (uint64_t)rel.tv_sec);
        }
        if (ret == 0 && params && psize)
            memcpy(params, b + 1, psize);                /* 成功：盒子结果缓冲拷回 caller（含返回槽 _） */
        int32_t r = --b->refs;                           /* 撤销 caller 持有的引用 */
        sc_mutex_unlock(&b->mu);
        if (r == 0) q_sync_box_free(b);                  /* 执行方已先撒手 → 末位回收 */
        return ret;
    }

    /* ---- 无限等路径（timeout_ms<=0）：caller-stack 描述符，零分配（不变） ---- */
    q_sync rep;
    sc_mutex_init(&rep.mu);
    sc_cond_init(&rep.done_cv);
    rep.done = 0;
    q_sync_tramp tr = { fn, params, &rep };
    /* 经 post 投递蹦床（FIFO 入队或转交池）——post 拷趝 tr（含指向 caller 缓冲/应答的指针） */
    uint8_t ok = q->base.post(&q->base, q_sync_run, &tr, sizeof(tr), prio, delay_ms);
    if (!ok) {
        sc_mutex_final(&rep.mu);
        sc_cond_final(&rep.done_cv);
        return -1;
    }
    sc_mutex_lock(&rep.mu);
    while (!rep.done)
        sc_cond_wait(&rep.done_cv, &rep.mu, 0, 0);   /* 无限阻塞等回复 */
    sc_mutex_unlock(&rep.mu);
    sc_mutex_final(&rep.mu);
    sc_cond_final(&rep.done_cv);
    return 0;
}

/* sync 方法（协议指针）—— 薄包装：先做循环死锁替代检测（P5d），否则转 que_sync_core。
 *   - 替代命中（向自己消费的队列、或沿 consumer→waiting 链回到自己）：在当前线程直接
 *     执行 fn(params)（写回返回槽 _），立即返回 0——打破循环，无需投递/阻塞。无限等与
 *     有限超时两路同此（命中替代即同步成功）。
 *   - 未命中：发布「本线程正阻塞 sync 于 q」（from(port)->waiting=q，置于 g_mutex 保护下，
 *     使后续可能形成循环的他方能检测到），再走 core 投递+阻塞；返回后清 waiting。
 *   - 锁序：图操作与 core 投递均用全局唯一 g_mutex（core 经 post 入 port 收件箱），无锁序环；
 *     调用方阻塞用每调用私有应答描述符（叶子锁，不与 g_mutex 嵌套）。
 *   - 池宿主队列：consumer 恒 NULL（池 worker 不 que_pull）→ 走链无命中 → 照常 core 投递
 *     转交池（替代对池天然禁用）。 */
static int32_t que_sync(queue *_this, void (*fn)(void *), void *params, size_t psize,
                        int32_t prio, int64_t delay_ms, int64_t timeout_ms) {
    que_state *q = (que_state *)_this;
    if (!q || !fn) return -1;

    sc_port *from = port_self();
    if (!from) return -1;
    sc_mutex_lock(&g_mutex);
    if (port_should_substitute(q, from)) {
        sc_mutex_unlock(&g_mutex);
        fn(params);                          /* 本线程直接执行（写回返回槽 _），打破循环 */
        return 0;
    }
    from->waiting = q;                        /* 发布：本线程正阻塞 sync 于 q（供他方循环检测） */
    sc_mutex_unlock(&g_mutex);

    int32_t ret = que_sync_core(_this, fn, params, psize, prio, delay_ms, timeout_ms);

    sc_mutex_lock(&g_mutex);
    from->waiting = NULL;                     /* 清除：已返回（成功/超时/失败均清） */
    sc_mutex_unlock(&g_mutex);
    return ret;
}

/* async 方法（协议指针）：非阻塞带回复——把 rpc 调用投递给某消费者（另一线程 pull 或池
 * 工作线程）执行，立即返回 promise&（mt-future 句柄），消费者执行完后兑现，调用方经
 * p->wait() 阻塞取结果。与 sync 不同：async 不阻塞，故参数缓冲与返回槽不能放调用方栈，
 * 改由 promise 堆拥有（promise_box 联合分配 [promise_box][rpc 参数缓冲]）。
 *   - 参数缓冲拷入 box，投递蹦床只携带 box 指针（post 拷贝该指针）。
 *   - 消费者跑 q_async_run：在 box 缓冲上实跑 rpc（写返回槽 _）→ 类型擦除结果首 8 字节
 *     存入 box->result → 置位并唤醒 wait。
 *   - 返回 promise&（失败 NULL）；调用方须先 p->wait() 取结果再 p->drop()（消费者兑现前
 *     drop 会 UAF——引用计数化安全释放待后续）。同线程对自己消费的队列 async+wait 会死锁。 */
typedef struct promise_box {
    promise     base;       /* op 层 promise 协议（首字段：与 promise_box 同址，可互转） */
    mtx_state   mu;
    cnd_state   done_cv;
    uint8_t     done;       /* 消费者兑现后置 1 */
    void       *result;     /* 类型擦除结果（返回槽首 sizeof(void*) 字节） */
    void      (*fn)(void *);/* 真正的 rpc worker */
    /* rpc 参数缓冲紧随本结构体之后：[promise_box][参数 max(psize, sizeof(void*))] */
} promise_box;

static uint8_t promise_ready(promise *_this) {
    promise_box *b = (promise_box *)_this;
    if (!b) return 0;
    sc_mutex_lock(&b->mu);
    uint8_t d = b->done;
    sc_mutex_unlock(&b->mu);
    return d;
}

static void *promise_wait(promise *_this) {
    promise_box *b = (promise_box *)_this;
    if (!b) return NULL;
    sc_mutex_lock(&b->mu);
    while (!b->done)
        sc_cond_wait(&b->done_cv, &b->mu, 0, 0);      /* 无限阻塞等兑现（超时待后续） */
    void *r = b->result;
    sc_mutex_unlock(&b->mu);
    return r;
}

static void promise_drop(promise *_this) {
    promise_box *b = (promise_box *)_this;
    if (!b) return;
    sc_mutex_final(&b->mu);
    sc_cond_final(&b->done_cv);
    free(b);                /* 整块回收（promise 对象 + 堆参数缓冲同生命周期） */
}

/* 消费者侧蹦床：在 box 的堆缓冲上实跑 rpc，擦除结果后置位并唤醒 wait。 */
static void q_async_run(void *arg) {
    promise_box *b = *(promise_box **)arg;            /* post 拷贝的是 box 指针 */
    void *buf = (void *)(b + 1);                      /* 参数缓冲紧随 box */
    b->fn(buf);                                       /* 在堆缓冲上执行（写入返回槽 _） */
    memcpy(&b->result, buf, sizeof(void *));          /* 类型擦除：返回槽首 8 字节（buf ≥8 已保证） */
    sc_mutex_lock(&b->mu);
    b->done = 1;
    sc_cond_one(&b->done_cv);
    sc_mutex_unlock(&b->mu);
}

static struct promise *que_async(queue *_this, void (*fn)(void *), const void *params, size_t psize,
                                 int32_t prio, int64_t delay_ms) {
    que_state *q = (que_state *)_this;
    if (!q || !fn) return NULL;
    /* 缓冲至少 sizeof(void*)：q_async_run 擦除结果时读首 8 字节，避免越界。 */
    size_t bufsz = psize < sizeof(void *) ? sizeof(void *) : psize;
    promise_box *b = (promise_box *)malloc(sizeof(promise_box) + bufsz);
    if (!b) return NULL;
    memset(b, 0, sizeof(promise_box) + bufsz);
    b->base.h     = b;       /* 私有区即本盒子（方法经 (promise_box*)_this 回取） */
    b->base.ready = promise_ready;
    b->base.wait  = promise_wait;
    b->base.drop  = promise_drop;
    sc_mutex_init(&b->mu);
    sc_cond_init(&b->done_cv);
    b->done   = 0;
    b->result = NULL;
    b->fn     = fn;
    if (params && psize) memcpy(b + 1, params, psize);
    /* 经 post 投递蹦床（FIFO 入队或转交池）——post 拷贝 box 指针（参数已在 box 堆缓冲里），
     * prio/delay 透传给 post（优先级排序 / 延迟投递）。 */
    uint8_t ok = q->base.post(&q->base, q_async_run, &b, sizeof(b), prio, delay_ms);
    if (!ok) {
        sc_mutex_final(&b->mu);
        sc_cond_final(&b->done_cv);
        free(b);
        return NULL;
    }
    return &b->base;
}

/* pull 方法：从本线程 port 单收件箱取队首消息在当前线程执行。timeout_ms <0 无限等 / 0 立即 / >0 毫秒。
 * 返回 1 处理了一条 / 0 超时且收件箱空 / -1 队列已关闭（且本队列排空）。
 *   - 首次对某队列 pull：惰性 attach——把该队列绑到本线程 port（consumer=port），并把队列
 *     staging 暂存消息整块插入 port 收件箱首部（优先处理历史），之后该队列消息直接进 port。
 *   - 单收件箱聚合：port 收件箱汇集所有 attach 到本 port 的队列的消息，pull 取队头（不限发起队列）
 *     在本线程执行——故对任一 attach 的队列调 pull 都驱动同一 port 邮箱（跨队列全局时序）。
 * P5：先把 delaying 中已到期项提升进 ready（按 prio），再取 ready 队头（最高优先级）；
 * ready 空时按 min(pull 截止, delaying 头到期时刻) 定时等，醒来重算（鲁棒于虚假唤醒）。 */
static int32_t que_pull(queue *_this, int64_t timeout_ms) {
    que_state *q = (que_state *)_this;
    if (!q) return -1;

    sc_port *port = port_self();
    if (!port) return -1;

    sc_mutex_lock(&g_mutex);

    /* 惰性 attach：首次 pull 把队列绑到本线程 port，并 flush staging 历史进 port 首部 */
    if (q->consumer != port) {
        q->consumer = port;
        q->anext = port->attached;
        port->attached = q;
        /* staging.ready 整块前插 port 收件箱首部（保历史内部序，优先于 port 既有消息） */
        if (q->staging.head) {
            q->staging.tail->next = port->box.head;
            port->box.head = q->staging.head;
            if (!port->box.tail) port->box.tail = q->staging.tail;
            q->staging.head = q->staging.tail = NULL;
        }
        /* staging.delaying 按 deadline 并入 port delaying */
        pmsg *d = q->staging.delaying;
        while (d) { pmsg *n = d->next; inbox_delay_insert(&port->box, d); d = n; }
        q->staging.delaying = NULL;
    }

    /* pull 自身的绝对截止（仅 timeout_ms>0 时有意义） */
    struct timespec pull_deadline;
    uint8_t have_pull_deadline = 0;
    if (timeout_ms > 0) {
        struct timespec now, rel;
        P_clock_now(&now);
        rel.tv_sec  = (time_t)(timeout_ms / 1000);
        rel.tv_nsec = (long)((timeout_ms % 1000) * 1000000L);
        clock_inc(now, rel, pull_deadline);
        have_pull_deadline = 1;
    }

    for (;;) {
        inbox_promote(&port->box);              /* 已到期延迟项提升进 ready */
        if (port->box.head) break;              /* ready 有货 → 取队头 */
        if (q->closed) { sc_mutex_unlock(&g_mutex); return -1; }  /* 本队列已关闭且排空 */
        if (timeout_ms == 0) { sc_mutex_unlock(&g_mutex); return 0; }  /* 立即模式 */

        /* 本轮等待时限 = min(pull 截止, delaying 头到期时刻)，取较早者 */
        struct timespec now, wake;
        uint8_t have_wake = 0;
        P_clock_now(&now);
        if (port->box.delaying) { wake = port->box.delaying->deadline; have_wake = 1; }
        if (have_pull_deadline && (!have_wake || clock_gt(wake, pull_deadline))) {
            wake = pull_deadline; have_wake = 1;
        }

        if (!have_wake) {
            sc_cond_wait(&port->recv, &g_mutex, 0, 0);          /* 无限等来活 */
        } else if (clock_gt(wake, now)) {
            struct timespec rel;
            clock_dec(wake, now, rel);                          /* 相对时限 = wake - now（>0） */
            sc_cond_wait(&port->recv, &g_mutex, (uint64_t)rel.tv_nsec, (uint64_t)rel.tv_sec);
        }
        /* 醒来：若是 pull 自身超时且仍无可取货，返 0；否则回到循环顶重算（虚假唤醒亦安全） */
        if (have_pull_deadline) {
            struct timespec now2;
            P_clock_now(&now2);
            if (clock_ge(now2, pull_deadline)) {
                inbox_promote(&port->box);
                if (!port->box.head && !q->closed) { sc_mutex_unlock(&g_mutex); return 0; }
            }
        }
    }

    pmsg *m = port->box.head;
    port->box.head = m->next;
    if (!port->box.head) port->box.tail = NULL;
    if (m->receiver) m->receiver->count--;
    sc_mutex_unlock(&g_mutex);

    m->fn((void *)(m + 1));                                     /* 参数紧随消息节点 */
    free(m);
    return 1;
}

/* port_release：线程退出 TLS 析构触发——把本 port 所有 attached 队列解绑（consumer 置空，
 * 令 main 后续 q->drop() 见 consumer==NULL 安全回收），释放 port 收件箱残留消息与 port 自身。
 * 正确程序里消费线程退出前已 pull 干净（收件箱空），此处仅做兜底回收。
 * 注：残留同步消息（被阻塞调用方）此处仅释放不唤醒——R2 引入 shadow session 后以 CLOSED 唤醒。 */
static void port_release(sc_port *p) {
    sc_mutex_lock(&g_mutex);
    /* 解绑所有 attached 队列（consumer 置空、清在途计数） */
    que_state *q = p->attached;
    while (q) {
        que_state *nq = q->anext;
        q->consumer = NULL;
        q->anext = NULL;
        q->count = 0;
        q = nq;
    }
    p->attached = NULL;
    /* 释放收件箱残留消息（ready + delaying） */
    pmsg *m = p->box.head;
    while (m) { pmsg *n = m->next; free(m); m = n; }
    m = p->box.delaying;
    while (m) { pmsg *n = m->next; free(m); m = n; }
    p->box.head = p->box.tail = p->box.delaying = NULL;
    sc_mutex_unlock(&g_mutex);
    sc_cond_final(&p->recv);
    sc_cond_final(&p->send);
    free(p);
    if (g_port == p) g_port = NULL;
}

/* drop 方法：关闭队列 → 若已 attach 则从 port 解绑并清其在 port 收件箱的残留 → 排空 staging
 * 暂存 → 回收 queue 整块。约定 drop 在消费线程退出（join）之后调用：此时 consumer 已被
 * port_release 置空，仅需清 staging 并释放本对象。 */
static void que_drop(queue *_this) {
    que_state *q = (que_state *)_this;
    if (!q) return;
    sc_mutex_lock(&g_mutex);
    q->closed = 1;
    /* 兜底：若仍 attach（异常路径，正常 drop 在 join 后 consumer 已空）则从 port 解绑并清残留 */
    sc_port *cp = q->consumer;
    if (cp) {
        /* 从 attached 链摘除 */
        que_state **pp = &cp->attached;
        while (*pp) { if (*pp == q) { *pp = q->anext; break; } pp = &(*pp)->anext; }
        q->anext = NULL;
        /* 清 port 收件箱中归属本队列的消息（ready） */
        pmsg **rp = &cp->box.head; pmsg *prev = NULL;
        for (pmsg *t = cp->box.head; t; ) {
            pmsg *nx = t->next;
            if (t->receiver == q) { if (prev) prev->next = nx; else cp->box.head = nx; free(t); }
            else { prev = t; }
            t = nx;
        }
        cp->box.tail = prev;
        (void)rp;
        /* 清 port 收件箱 delaying 中归属本队列的消息 */
        prev = NULL;
        for (pmsg *t = cp->box.delaying; t; ) {
            pmsg *nx = t->next;
            if (t->receiver == q) { if (prev) prev->next = nx; else cp->box.delaying = nx; free(t); }
            else { prev = t; }
            t = nx;
        }
        q->consumer = NULL;
    }
    /* 排空 staging 暂存（未 attach 时投递的历史；正常已在 attach 时 flush 入 port） */
    pmsg *m = q->staging.head;
    while (m) { pmsg *n = m->next; free(m); m = n; }
    m = q->staging.delaying;
    while (m) { pmsg *n = m->next; free(m); m = n; }
    q->staging.head = q->staging.tail = q->staging.delaying = NULL;
    q->count = 0;
    sc_mutex_unlock(&g_mutex);
    free(q);                             /* 整块回收（queue 对象与盒子同生命周期） */
}

/* default_queue：「PORT 单收件箱」消息队列构造（填充协议 vtable，返回 queue&）。
 *   - host：宿主三态——NULL 未绑/延迟、(pool*)-1 当前/主线程（手动 pull）、&pool
 *     线程池（post 自动转交池消费，用 pool->join() 作屏障，无需 pull）
 *   - 返回 &q->base（失败 NULL）；用完调 q->drop() 解绑排空回收（池宿主须另行 drop 池）*/
struct queue *default_queue(struct pool *host) {
    que_state *q = (que_state *)malloc(sizeof(que_state));
    if (!q) return NULL;
    memset(q, 0, sizeof(*q));
    q->base.h    = q;        /* 私有区即本盒子（方法实际经 (que_state*)_this 回取） */
    q->base.post = que_post;
    q->base.sync = que_sync;
    q->base.async = que_async;
    q->base.pull = que_pull;
    q->base.drop = que_drop;
    q->host = host;          /* &pool：post 自动转交池消费；NULL/(pool*)-1：手动 pull */
    return &q->base;
}
