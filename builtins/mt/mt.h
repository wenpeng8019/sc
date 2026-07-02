/* mt.h —— sc 多线程支持标准的 C ABI 契约（与 builtins/mt/mt.sc 同步维护）
 *
 * 约定：
 *   - thread 与 run 线程创建（thread_run/thread_join）已下沉至语言内核（op.h/op_impl.c）
 *   - mutex：init/drop 配对；lock/unlock 配对
 *   - cond：init/drop 配对；wait 为 cond 方法（编译器生成 cond_wait 调用）
 *   - pool：op 层接口协议（vtable，见 op.h），本模块经 default_pool(n) 具名构造填充
 *   - queue：op 层接口协议（vtable，见 op.h），本模块经 default_queue(host) 具名构造填充
 *   - 返回 uint8_t（sc 的 bool 类型）的函数：1 成功 / 0 失败
 */
#ifndef SC_M_H
#define SC_M_H

/* mt 内联持有平台锁/条件/屏障句柄（sc_mutex_t 等），须 opt-in 展开 platform.h 的
 * 互斥/条件/屏障/线程层，并把 <pthread.h> 带入所有 inc 本模块的单元（有意打破
 * 「普通单元不沾线程头」原则——用 mt 即认此代价，换取无堆分配的内联锁）。 */
#define P_MT_IMPL
#include "platform.h"   /* 提供 sc_mutex_t / sc_cond_t / sc_barrier_t + P_* 操作 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>    /* bool 返回值原型 */

#ifdef __cplusplus
extern "C" {
#endif

/* thread 类型与 run 线程创建（thread_run/thread_join）已下沉至语言内核：
 * 见 builtins/op.h（默认带入）与 op_impl.c（始终链接）。pool 亦为语言内核
 * 声明的接口协议（vtable，见 op.h）；本模块按「默认 pool」策略提供具名构造
 * default_pool(n)，填充 run/join/drop 指针并返回 pool&（犹如 io 的 file() 之于
 * com）。其入池帧（fn/params/psize）与 thread_run 同形。queue 亦为语言内核声明的
 * 接口协议（vtable，见 op.h）；本模块按「默认 FIFO」策略提供具名构造 default_queue
 * (host)，填充 post/sync/async/pull/drop 指针并返回 queue&。async 投递构造并返回 promise&
 * （mt-future，op 层接口协议，见 op.h），由消费者执行完后兑现、调用方 p->wait() 阻塞取。 */

/* ---------------- pool：线程池协议的「默认」实现（构造入口） ---------------- */

struct sc_pool;   /* op.h 定义完整 vtable；此处仅作返回类型的不完全声明 */

/* default_pool：构造一个「默认策略」线程池（FIFO 任务队列 + n 个固定工作线程），
 * 填充 op 层 pool 协议的 run/join/drop 指针并返回 pool&。
 *   - n：工作线程数；0 → CPU 逻辑核数
 *   - 返回 struct sc_pool*（失败 NULL）；用完调 p->drop() 停池回收（含 pool 对象本身）
 * 将来可按其它策略（如 work-stealing / 优先级）另起 *_pool(n) 构造，均返回 pool&。 */
struct sc_pool *sc_default_pool(uint32_t n);

/* ---------------- sem_pool：pool 协议的「信号量限流」策略实现（构造入口） ---------------- */

/* sem_pool：构造一个「信号量限流」池——与 default_pool / drain_pool 并列，同返回 pool&、
 * 同凭 run 投递。它是有界并发的「一次性 worker 派发器」：run = 任务入队 + 计数自增，永不
 * 阻塞；调度按「剩余信号量」(n - 在跑) 启动线程，每个 worker 只跑一个任务即退（线程结束、
 * 不自循环）；腾出槽位时把下一个排队任务交给新线程。填充 op 层 pool 协议的 run/join/drop。
 *   - n：并发上限（在跑 worker 数封顶）；0 → CPU 逻辑核数
 *   - 返回 struct sc_pool*（失败 NULL）；构造时不启线程，首个 run 才按需起；
 *     用完调 sp->drop() 停池回收（含 pool 对象本身）
 * 与 default_pool 之别：default_pool 是 n 个常驻线程复用、空闲驻留；sem_pool 无常驻线程，
 * 按需起、一任务一线程、用完即弃（线程级隔离、空闲零驻留，代价是每任务一次线程创建）。
 * 与 drain_pool 之别：drain_pool 反复重跑 fn 排空池外工作源（需世代代检）；sem_pool 的 fn
 * 只跑一次、任务由池自有队列承载（不变式「在跑<上限 ⟹ 队列空」即足，无须代检）。 */
struct sc_pool *sc_sem_pool(uint32_t n);

/* ---------------- drain_pool：pool 协议的「按需自调度」策略实现（构造入口） ---------------- */

/* drain_pool：构造一个「按需自调度」池——与 default_pool 并列，同返回 pool&、同凭 run 投递，
 * 仅策略相反：无内部任务队列，worker 反复跑投递的工作单元 rpc 直到一轮无新投递即退；
 * run = 通知有新活 + 按需激活一个 worker（上限 n）。填充 op 层 pool 协议的 run/join/drop。
 *   - n：worker 上限；0 → CPU 逻辑核数
 *   - 返回 struct pool*（失败 NULL）；构造时不启 worker，首个 run<dp> 才按需激活；
 *     用完调 dp->drop() 停池回收（含 pool 对象本身）
 * 工作单元 rpc 自身应循环排空至「本视角无活」后返回；drain_pool 经世代代检（running 计数 +
 * 工作世代经池内部锁守护）保证末个 worker 不在尚有活时退出（详见 op.h / mt_impl.c）。 */
struct sc_pool *sc_drain_pool(uint32_t n);

/* ---------------- queue：消息队列协议的「默认」实现（构造入口） ---------------- */

struct sc_queue;  /* op.h 定义完整 vtable；此处仅作返回类型的不完全声明 */

/* default_queue：构造一个「默认 FIFO」消息队列，填充 op 层 queue 协议的
 * post/pull/drop 指针并返回 queue&。
 *   - host：宿主三态（pool&）——NULL 未绑/延迟、(struct sc_pool*)-1 当前/主线程
 *     （自行跑 pull 循环消费）、&pool 线程池消费（post 自动转交池，用 p->join()
 *     作屏障，无需 pull；q、p 各自 drop）。
 *   - 返回 struct sc_queue*（失败 NULL）；用完调 q->drop() 解绑排空回收（含 queue 对象本身）
 * 将来可按其它策略（优先级 / 有界背压）另起 *_queue(host) 构造，均返回 queue&。 */
struct sc_queue *sc_default_queue(struct sc_pool *host);

/* ---------------- mutex：互斥锁 ---------------- */

typedef struct sc_mutex {
    sc_mutex_t h;       /* 平台锁句柄（实现私有） */
} sc_mutex;

void    sc_mutex_init(sc_mutex *_this);
void    sc_mutex_drop(sc_mutex *_this);
void    sc_mutex_lock(sc_mutex *_this);
void    sc_mutex_unlock(sc_mutex *_this);
bool    sc_mutex_try_lock(sc_mutex *_this);     /* 1 成功 / 0 已被占用 */

/* ---------------- cond：条件变量 ---------------- */

typedef struct sc_cond {
    sc_cond_t h;       /* 平台条件变量句柄（实现私有） */
} sc_cond;

void    sc_cond_init(sc_cond *_this);
void    sc_cond_drop(sc_cond *_this);
void    sc_cond_one(sc_cond *_this);            /* 唤醒一个等待者 */
void    sc_cond_all(sc_cond *_this);            /* 唤醒全部等待者 */

/* sc_cond.wait 方法原语：调用前须已持有 m；nsec/sec 全 0 → 无限等待，
 * 否则为相对超时时长（sec 秒 + nsec 纳秒）。
 * 返回 0 被唤醒 / 1 超时 / -1 错误 */
int32_t sc_cond_wait(sc_cond *c, sc_mutex *m, uint64_t nsec, uint64_t sec);

/* ---------------- barrier：屏障（N 方汇合） ---------------- */

typedef struct sc_barrier {
    sc_barrier_t h;
} sc_barrier;

void    sc_barrier_init(sc_barrier *_this, uint32_t n);  /* n 方汇合（0 视为 1） */
void    sc_barrier_drop(sc_barrier *_this);
bool    sc_barrier_wait(sc_barrier *_this);     /* 阻塞至全部到达；最后到达者返回 1，其余 0 */

#ifdef __cplusplus
}
#endif

#endif /* SC_M_H */
