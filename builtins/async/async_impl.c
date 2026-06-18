/* async_impl.c —— 异步功能库的叶子原语实现（仅 inc async.sc 时编译链接）
 *
 * 语言底层异步机制（future/async_*、事件循环、com_*_async/async_io）属语言自有内核，
 * 实现在 builtins/op_impl.c（始终随工程链接），并在编译期二选一多路复用后端：
 * 默认 POSIX poll（零依赖）/ -DSCC_WITH_UV 时 libuv（uv_loop）。
 *
 * 本文件承载"异步功能库"——基于 op 层暴露钩子构建的叶子异步原语生态：
 *   · 默认后端：经 op_timer_arm（op 层基础定时器）实现 delay 等可移植原语；
 *   · -DSCC_WITH_UV：可直接挂 op_uv_loop() 上的 libuv 句柄，接入 libuv 现成生态
 *     （uv_timer/uv_tcp/uv_udp/uv_getaddrinfo/uv_fs ... 网络·文件·DNS·子进程等）。
 *
 * 用法：inc async.sc —— 引入 delay 等叶子原语声明并链接本实现。
 */
#include "async.h"
#include "../op.h"

#ifdef SCC_WITH_UV
#include <uv.h>
#include <stdlib.h>
#endif

/* delay —— ms 毫秒定时器叶子原语，立即返回未就绪 future。
 *   默认后端：经 op 层基础定时器 op_timer_arm（poll 超时驱动）兑现；
 *   -DSCC_WITH_UV：直接用 libuv 现成定时器 uv_timer 挂在 op 层事件循环上。 */
#ifdef SCC_WITH_UV

typedef struct delay_req { uv_timer_t timer; future *fut; } delay_req;
static void delay_closed(uv_handle_t *h) { free(h->data); }
static void delay_fired(uv_timer_t *t) {
    delay_req *r = (delay_req *)t->data;
    future_done(r->fut, (void *)0);
    uv_close((uv_handle_t *)t, delay_closed);
}
future *delay(uint32_t ms) {
    uv_loop_t *loop = (uv_loop_t *)op_uv_loop();
    future    *f = future_new();
    if (!loop) { future_done(f, (void *)0); return f; }   /* 兜底：无循环即时兑现 */
    delay_req *r = (delay_req *)calloc(1, sizeof(delay_req));
    r->fut = f;
    uv_timer_init(loop, &r->timer);
    r->timer.data = r;
    uv_timer_start(&r->timer, delay_fired, ms, 0);
    return f;
}

#else

future *delay(uint32_t ms) {
    future *f = future_new();
    op_timer_arm(f, ms);     /* op 层基础定时器（poll 单调时钟截止表） */
    return f;
}

#endif /* SCC_WITH_UV */
