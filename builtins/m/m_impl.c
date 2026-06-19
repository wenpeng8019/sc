/* m_impl.c —— sc 多线程支持标准（m.h 契约）默认实现
 * 跨平台经由 builtins/platform.h：POSIX pthread / Windows 线程 API
 */
#include "m.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>

/* 线程/互斥/条件变量的跨平台原语均由 platform.h 提供（POSIX 下已含 pthread.h）；
 * 本模块仅保留自有的单块线程 ABI（[thread][参数][thd_impl]）。 */

/* ---------------- thread ---------------- */

/* run 联合实体单块布局：[thread][rpc 参数 psize][thd_impl]
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

/* wait 语句原语：nsec/sec 全 0 → 无限等待，否则相对超时。
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

/* ---------------- pool ---------------- */

/* 任务节点：联合分配 [pool_task][rpc 参数 psize]（与 run 的联合实体同哲学） */
typedef struct pool_task {
    struct pool_task *next;
    void            (*fn)(void *);   /* rpc 实际函数，参数紧随本节点 */
} pool_task;

/* 实现私有区：单链 FIFO 队列 + 双条件变量（来活 / 全部完成） */
typedef struct {
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

void pool_init(pool *_this, uint32_t n) {
    _this->h = NULL;
    if (n == 0) n = P_ncpu();
    pol_state *p = (pol_state *)malloc(sizeof(pol_state) + (n - 1) * sizeof(p->thr[0]));
    if (!p) return;
    memset(p, 0, sizeof(*p));
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
    _this->h = p;
}

/* run 语句原语（pool 形态，对称 thread_run）：装填好的 rpc 参数入队。
 * 返回 1 成功 / 0 失败（池未初始化/已停/内存不足） */
uint8_t pool_run(pool *_this, void (*fn)(void *), const void *params, size_t psize) {
    pol_state *p = _this ? (pol_state *)_this->h : NULL;
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

/* 屏障：等待全部已提交任务完成（之后 pool 仍可继续提交） */
void pool_join(pool *_this) {
    pol_state *p = _this ? (pol_state *)_this->h : NULL;
    if (!p) return;
    pol_lock(p);
    while (p->pending > 0)
        sc_cond_wait(&p->idle, &p->mu, 0, 0);   /* 无限等全部完成 */
    pol_unlock(p);
}

/* 析构：等已提交任务全部完成 → 停 worker → 回收（不丢任务，语义可预期） */
void pool_drop(pool *_this) {
    pol_state *p = _this ? (pol_state *)_this->h : NULL;
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
    free(p);
    _this->h = NULL;
}
