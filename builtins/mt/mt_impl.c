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

/* ---------------- queue：消息队列协议的「默认 FIFO」实现 ---------------- */
/* queue 协议（vtable）由语言内核声明（op.h，默认带入）；本模块按「默认 FIFO」策略用
 * default_queue(host) 具名构造——同 pool：que_state 首字段即 queue base，故 (queue*)
 * 与 (que_state*) 同址，方法直接 (que_state*)_this 回取私有区。消息节点 [q_msg][params]
 * 联合分配，参数拷贝入节点，投递点无需保活。
 * 宿主三态（host）：NULL 未绑/延迟、(pool*)-1 当前/主线程（自跑 pull 循环）、&pool
 * 线程池消费——host 为真实 pool 时，post 直接经 pool 协议指针转交池（pool->run），由
 * 池的工作线程并发执行，无需手动 pull（用 pool->join() 作屏障）；NULL/(pool*)-1 入本
 * 队列 FIFO，由当前/主线程手动 pull 消费。 */

/* 消息节点：联合分配 [q_msg][rpc 参数 psize]（与 pool 任务节点同哲学） */
typedef struct q_msg {
    struct q_msg *next;
    void        (*fn)(void *);   /* rpc 实际函数，参数紧随本节点 */
} q_msg;

/* 队列盒子：首字段 queue base（协议 vtable）+ 单链 FIFO + 条件变量（来活） */
typedef struct {
    queue        base;     /* op 层 queue 协议（首字段：与 que_state 同址，可互转） */
    mtx_state    mu;
    cnd_state    more;     /* 来活：post 唤醒阻塞的 pull */
    q_msg       *head, *tail;
    uint32_t     pending;  /* 队列中待处理消息数 */
    uint8_t      closed;   /* drop 置位：拒收新消息、唤醒 pull 退出 */
    struct pool *host;     /* 宿主绑定：NULL/(pool*)-1/&pool（P2 仅记录） */
} que_state;

static void que_lock(que_state *q)   { sc_mutex_lock(&q->mu); }
static void que_unlock(que_state *q) { sc_mutex_unlock(&q->mu); }

/* post 方法（协议指针，对称 pool.run）：rpc 调用整体打包成消息入队。
 * 宿主为真实线程池（host 非 NULL 且非 (pool*)-1）时，直接转交池消费（经 pool 协议
 * 指针 host->run 派发，不入本队列 FIFO），由池工作线程并发执行。
 * 返回 1 成功 / 0 失败（队列已关闭 / 内存不足） */
static uint8_t que_post(queue *_this, void (*fn)(void *), const void *params, size_t psize) {
    que_state *q = (que_state *)_this;
    if (!q || !fn) return 0;
    /* 宿主=线程池：投递即提交给池（pool->run 自行 memcpy 参数），由池工作线程消费。
     * host 只在构造时设定、之后不改，故此处免锁读取安全。 */
    {
        struct pool *host = q->host;
        if (host && host != (struct pool *)-1)
            return host->run(host, fn, params, psize);
    }
    q_msg *m = (q_msg *)malloc(sizeof(q_msg) + psize);
    if (!m) return 0;
    m->next = NULL;
    m->fn = fn;
    if (params && psize) memcpy(m + 1, params, psize);
    que_lock(q);
    if (q->closed) { que_unlock(q); free(m); return 0; }
    if (q->tail) q->tail->next = m; else q->head = m;
    q->tail = m;
    q->pending++;
    sc_cond_one(&q->more);
    que_unlock(q);
    return 1;
}

/* sync 方法（协议指针）：阻塞带回复——把 rpc 调用投递给某消费者（另一线程 pull 或池
 * 工作线程）执行，阻塞至执行完成，结果回填 caller 的 params 首字段（返回槽 _）。
 * 复用 post 路径：把 caller 的 (fn, params 指针, &reply) 打成 trampoline 投递，消费者
 * 执行 q_sync_run——在 caller 的 params 缓冲上实跑 fn 后置位 reply、唤醒 caller。
 *   - params 是 caller 栈上的 rpc 参数结构体；caller 阻塞期间保证其与 reply 存活。
 *   - post 会拷贝 trampoline，故 trampoline 自身栈生命周期无关。
 *   - 返回 0 成功 / -1 投递失败（队列已关闭等）。
 *   - 同线程对自己消费的队列 sync 会死锁（须由别的消费者执行）；超时/优先级/死锁替代待后续。 */
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

static int32_t que_sync(queue *_this, void (*fn)(void *), void *params) {
    que_state *q = (que_state *)_this;
    if (!q || !fn) return -1;
    q_sync rep;
    sc_mutex_init(&rep.mu);
    sc_cond_init(&rep.done_cv);
    rep.done = 0;
    q_sync_tramp tr = { fn, params, &rep };
    /* 经 post 投递蹦床（FIFO 入队或转交池）——post 拷贝 tr（含指向 caller 缓冲/应答的指针） */
    uint8_t ok = q->base.post(&q->base, q_sync_run, &tr, sizeof(tr));
    if (!ok) {
        sc_mutex_final(&rep.mu);
        sc_cond_final(&rep.done_cv);
        return -1;
    }
    sc_mutex_lock(&rep.mu);
    while (!rep.done)
        sc_cond_wait(&rep.done_cv, &rep.mu, 0, 0);   /* 无限阻塞等回复（超时待后续） */
    sc_mutex_unlock(&rep.mu);
    sc_mutex_final(&rep.mu);
    sc_cond_final(&rep.done_cv);
    return 0;
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

static struct promise *que_async(queue *_this, void (*fn)(void *), const void *params, size_t psize) {
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
    /* 经 post 投递蹦床（FIFO 入队或转交池）——post 拷贝 box 指针（参数已在 box 堆缓冲里） */
    uint8_t ok = q->base.post(&q->base, q_async_run, &b, sizeof(b));
    if (!ok) {
        sc_mutex_final(&b->mu);
        sc_cond_final(&b->done_cv);
        free(b);
        return NULL;
    }
    return &b->base;
}

/* pull 方法：取队首消息在当前线程执行。timeout_ms <0 无限等 / 0 立即 / >0 毫秒。
 * 返回 1 处理了一条 / 0 超时且队列空 / -1 队列已关闭（且排空）。 */
static int32_t que_pull(queue *_this, int64_t timeout_ms) {
    que_state *q = (que_state *)_this;
    if (!q) return -1;
    que_lock(q);
    while (!q->head && !q->closed) {
        if (timeout_ms == 0) { que_unlock(q); return 0; }       /* 立即返回 */
        if (timeout_ms < 0) {
            sc_cond_wait(&q->more, &q->mu, 0, 0);               /* 无限等待来活 */
        } else {
            uint64_t sec  = (uint64_t)timeout_ms / 1000ULL;
            uint64_t nsec = ((uint64_t)timeout_ms % 1000ULL) * 1000000ULL;
            int r = sc_cond_wait(&q->more, &q->mu, nsec, sec);
            /* 超时且仍空才返 0；若恰在超时边界来活，留待 while 复检后照常取出 */
            if (r == 1 && !q->head && !q->closed) { que_unlock(q); return 0; }
        }
    }
    q_msg *m = q->head;
    if (!m) { que_unlock(q); return -1; }                       /* 已关闭且排空 */
    q->head = m->next;
    if (!q->head) q->tail = NULL;
    q->pending--;
    que_unlock(q);

    m->fn((void *)(m + 1));                                     /* 参数紧随消息节点 */
    free(m);
    return 1;
}

/* drop 方法：解绑宿主 → 排空残留消息（释放，不执行）→ 唤醒 pull 退出 → 回收整块。 */
static void que_drop(queue *_this) {
    que_state *q = (que_state *)_this;
    if (!q) return;
    que_lock(q);
    q->closed = 1;
    q_msg *m = q->head;                  /* 排空：丢弃未处理消息（投递为不带回复的 fire-and-forget） */
    while (m) { q_msg *n = m->next; free(m); m = n; }
    q->head = q->tail = NULL;
    q->pending = 0;
    sc_cond_all(&q->more);               /* 唤醒所有阻塞 pull，令其见 closed 退出 */
    que_unlock(q);
    sc_mutex_final(&q->mu);
    sc_cond_final(&q->more);
    free(q);                             /* 整块回收（queue 对象与盒子同生命周期） */
}

/* default_queue：「默认 FIFO」消息队列构造（填充协议 vtable，返回 queue&）。
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
    sc_mutex_init(&q->mu);
    sc_cond_init(&q->more);
    q->host = host;          /* &pool：post 自动转交池消费；NULL/(pool*)-1：手动 pull */
    return &q->base;
}
