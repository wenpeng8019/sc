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
