/* async_impl.c —— sc 异步机制默认运行时（libuv 实现）
 *
 * 见 async.h 头注的线程安全模型。要点重述：
 *   - 全局事件循环 g_loop + 跨线程唤醒句柄 g_wake(uv_async_t) + 互斥锁 g_mu
 *     + 就绪队列(g_ready_head/tail) + 未决 future 计数 g_pending。
 *   - future_done 可被任意线程调用：锁内置 ready/result/递减 pending，
 *     若已有 waiter 则推入就绪队列；解锁后 uv_async_send 唤醒循环线程。
 *   - drain_cb（g_wake 回调，循环线程）排空就绪队列逐个 resume 等待者状态机；
 *     当 pending==0 且队列空时 uv_stop 让 async_loop(uv_run) 返回。
 *   - 因此所有状态机 resume 都在循环线程串行发生 → 生成码无需加锁。
 *
 * libuv 跨平台：Linux epoll / macOS·BSD kqueue / Windows IOCP。
 */
#include "async.h"

#include <uv.h>
#include <stdlib.h>
#include <string.h>

static uv_loop_t  g_loop;
static uv_async_t g_wake;
static uv_mutex_t g_mu;
static future    *g_ready_head;   /* 就绪队列头（单链，经 future.next 串联） */
static future    *g_ready_tail;   /* 就绪队列尾 */
static long       g_pending;      /* 已创建但尚未完成的 future 数（含 _ret 与叶子） */
static int        g_inited;

/* 语言自有的 id 派发回调（async_loop 传入）：完成的「带 id 且无协程等待者」future
 * 由循环线程交给它处理，像消息队列。返回<0 请求停循环。NULL=不派发（纯协程驱动）。 */
typedef int (*async_proc_t)(int id, future *f);
static async_proc_t g_proc;

/* 就绪队列（调用方持 g_mu） */
static void push_ready(future *f) {
    f->next = NULL;
    if (g_ready_tail) g_ready_tail->next = f;
    else              g_ready_head = f;
    g_ready_tail = f;
}
static future *pop_ready(void) {
    future *f = g_ready_head;
    if (f) {
        g_ready_head = (future *)f->next;
        if (!g_ready_head) g_ready_tail = NULL;
        f->next = NULL;
    }
    return f;
}

/* g_wake 回调：循环线程排空就绪队列；有协程等待者则 resume，否则按 id 派发。 */
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
                f->resume(f->frame);             /* 协程路径：恢复等待者状态机 */
            } else if (f->id >= 0 && g_proc) {
                int rc = g_proc(f->id, f);       /* 消息路径：按 id 派发处理 */
                free(f);                         /* 派发型 future 即消息，处理后回收 */
                if (rc < 0) { uv_stop(&g_loop); break; }
            } else if (f->id >= 0) {
                free(f);                         /* 有 id 但未设派发器：丢弃该消息 */
            }
            continue;
        }
        if (pend == 0 && empty) uv_stop(&g_loop);  /* 无未决且队列空 → 退出 uv_run */
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

void async_loop(void *proc) {
    if (!g_inited) return;
    g_proc = (async_proc_t)proc;   /* 按 id 派发回调（NULL=纯协程驱动） */
    uv_mutex_lock(&g_mu);
    int idle = (g_pending == 0 && g_ready_head == NULL);
    uv_mutex_unlock(&g_mu);
    if (idle) return;        /* 无未决且无待派发消息：避免空转阻塞（g_wake 常驻 ref） */
    uv_run(&g_loop, UV_RUN_DEFAULT);
}

void async_final(void) {
    if (!g_inited) return;
    uv_close((uv_handle_t *)&g_wake, NULL);
    uv_run(&g_loop, UV_RUN_NOWAIT);   /* 处理 close 回调 */
    uv_loop_close(&g_loop);
    uv_mutex_destroy(&g_mu);
    g_ready_head = g_ready_tail = NULL;
    g_pending = 0;
    g_inited = 0;
}

future *future_new(void) {
    future *f = (future *)calloc(1, sizeof(future));
    future_init(f);
    return f;
}

/* future()（伪类构造）后端：把已分配/清零的 future 登记到当前事件循环（pending +1）。
 * 由编译器为 future() 生成的 future__new 调用（malloc + memset 之后），以及内部
 * future_new 共用。要求 async_init 已建立 g_mu（future() 只在异步上下文中使用）。 */
void future_init(future *_this) {
    _this->id = -1;          /* 默认无标签：仅协程 await 用（0 是合法 id，不能作哨兵） */
    _this->ctx = NULL;       /* 默认无上下文；future<ID>(ctx) 由构造辅助回填 */
    uv_mutex_lock(&g_mu);
    g_pending++;
    uv_mutex_unlock(&g_mu);
}

/* 任意线程可调：置就绪 + 唤醒等待者（经就绪队列 + uv_async_send）。
 * 与 await 登记 waiter 在 g_mu 下握手，避免丢唤醒：
 *   - done 先于 await：ready=1 但 frame==NULL，不入队；await 后续见 ready 直接续跑。
 *   - await 先于 done：frame 已设，done 见 frame!=NULL → 入队 + 唤醒。 */
void future_done(future *f, void *result) {
    int  enqueue;
    long remaining;
    uv_mutex_lock(&g_mu);
    f->result = result;
    f->ready  = 1;
    /* 协程等待者 → 入队待 resume；或带 id 的无等待者 → 入队待 id 派发。 */
    enqueue = (f->frame != NULL) || (f->id >= 0);
    if (enqueue) push_ready(f);
    g_pending--;
    remaining = g_pending;
    uv_mutex_unlock(&g_mu);
    /* 有待处理项 → 唤醒去 resume/派发；或全部完成 → 唤醒去 uv_stop。 */
    if (enqueue || remaining == 0) uv_async_send(&g_wake);
}

uint8_t future_ready(future *_this) { return (uint8_t)(_this ? _this->ready : 0); }
void   *future_get(future *_this)   { return _this ? _this->result : NULL; }
void   *future_ctx(future *_this)   { return _this ? _this->ctx : NULL; }

/* await 握手（循环线程，由生成的状态机调用）：在 g_mu 下登记本帧为 waiter，
 * 并原子读取就绪位。返回 1=已就绪（生成码不让出、直接续跑）；0=未就绪（让出）。
 * 与 future_done 在同一把锁下交接，杜绝“登记前已完成”导致的丢唤醒。 */
uint8_t future_await(future *f, void *frame, void (*resume)(void *)) {
    uint8_t r;
    uv_mutex_lock(&g_mu);
    f->frame  = frame;
    f->resume = resume;
    r = (uint8_t)f->ready;
    uv_mutex_unlock(&g_mu);
    return r;
}

/* ---------------- delay：libuv 默认叶子异步原语（uv_timer） ---------------- */

typedef struct delay_req {
    uv_timer_t timer;
    future    *fut;
} delay_req;

static void delay_closed(uv_handle_t *h) {
    free(h->data);   /* h->data == delay_req（timer 内嵌于其首部） */
}
static void delay_on_timer(uv_timer_t *t) {
    delay_req *r = (delay_req *)t->data;
    future    *f = r->fut;
    future_done(f, NULL);                       /* 兑现：循环线程，经就绪队列 */
    uv_close((uv_handle_t *)t, delay_closed);   /* 回收定时器与 req */
}

future *delay(uint32_t ms) {
    delay_req *r = (delay_req *)calloc(1, sizeof(delay_req));
    r->fut = future_new();
    uv_timer_init(&g_loop, &r->timer);
    r->timer.data = r;
    uv_timer_start(&r->timer, delay_on_timer, ms, 0);
    return r->fut;
}

/* ---------------- com 异步收发桥接（临时立即完成驱动） ----------------
 * 把一次同步 io 包装为「已就绪」future，使 rpc 内 com >> v / com << v 的 await
 * 整合可端到端验证。真实异步驱动应改为：入队 rq/wq、io 完成回调里 future_done
 * 延迟兑现（见 op_impl.c 的 ioq 循环缓冲）。当前实现立即调用 read/write 并兑现。 */
future *com_read_async(com *c, void *data, uint32_t size) {
    future  *f = future_new();
    uint32_t n = size;
    if (c && c->read) c->read(c, data, &n);
    future_done(f, (void *)(intptr_t)n);            /* 立即兑现，结果=收发字节数 */
    return f;
}

future *com_write_async(com *c, void *buf, uint32_t size) {
    future  *f = future_new();
    uint32_t n = size;
    if (c && c->write) c->write(c, buf, &n);
    future_done(f, (void *)(intptr_t)n);
    return f;
}
