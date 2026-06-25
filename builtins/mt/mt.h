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

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* thread 类型与 run 线程创建（thread_run/thread_join）已下沉至语言内核：
 * 见 builtins/op.h（默认带入）与 op_impl.c（始终链接）。pool 亦为语言内核
 * 声明的接口协议（vtable，见 op.h）；本模块按「默认 pool」策略提供具名构造
 * default_pool(n)，填充 run/join/drop 指针并返回 pool&（犹如 io 的 file() 之于
 * com）。其入池帧（fn/params/psize）与 thread_run 同形。queue 亦为语言内核声明的
 * 接口协议（vtable，见 op.h）；本模块按「默认 FIFO」策略提供具名构造 default_queue
 * (host)，填充 post/pull/drop 指针并返回 queue&。 */

/* ---------------- pool：线程池协议的「默认」实现（构造入口） ---------------- */

struct pool;   /* op.h 定义完整 vtable；此处仅作返回类型的不完全声明 */

/* default_pool：构造一个「默认策略」线程池（FIFO 任务队列 + n 个固定工作线程），
 * 填充 op 层 pool 协议的 run/join/drop 指针并返回 pool&。
 *   - n：工作线程数；0 → CPU 逻辑核数
 *   - 返回 struct pool*（失败 NULL）；用完调 p->drop() 停池回收（含 pool 对象本身）
 * 将来可按其它策略（如 work-stealing / 优先级）另起 *_pool(n) 构造，均返回 pool&。 */
struct pool *default_pool(uint32_t n);

/* ---------------- queue：消息队列协议的「默认」实现（构造入口） ---------------- */

struct queue;  /* op.h 定义完整 vtable；此处仅作返回类型的不完全声明 */

/* default_queue：构造一个「默认 FIFO」消息队列，填充 op 层 queue 协议的
 * post/pull/drop 指针并返回 queue&。
 *   - host：宿主三态（pool&）——NULL 未绑/延迟、(struct pool*)-1 当前/主线程
 *     （自行跑 pull 循环消费）、&pool 线程池消费（自动消费接通见后续实现）。
 *     P2 阶段仅记录 host，三态功能上均为手动 pull；池自动消费为下一步。
 *   - 返回 struct queue*（失败 NULL）；用完调 q->drop() 解绑排空回收（含 queue 对象本身）
 * 将来可按其它策略（优先级 / 有界背压）另起 *_queue(host) 构造，均返回 queue&。 */
struct queue *default_queue(struct pool *host);

/* ---------------- mutex：互斥锁 ---------------- */

typedef struct mutex {
    void *h;       /* 平台锁句柄（实现私有） */
} mutex;

void    mutex_init(mutex *_this);
void    mutex_drop(mutex *_this);
void    mutex_lock(mutex *_this);
void    mutex_unlock(mutex *_this);
uint8_t mutex_try_lock(mutex *_this);     /* 1 成功 / 0 已被占用 */

/* ---------------- cond：条件变量 ---------------- */

typedef struct cond {
    void *h;       /* 平台条件变量句柄（实现私有） */
} cond;

void    cond_init(cond *_this);
void    cond_drop(cond *_this);
void    cond_one(cond *_this);            /* 唤醒一个等待者 */
void    cond_all(cond *_this);            /* 唤醒全部等待者 */

/* cond.wait 方法原语：调用前须已持有 m；nsec/sec 全 0 → 无限等待，
 * 否则为相对超时时长（sec 秒 + nsec 纳秒）。
 * 返回 0 被唤醒 / 1 超时 / -1 错误 */
int32_t cond_wait(cond *c, mutex *m, uint64_t nsec, uint64_t sec);

/* ---------------- barrier：屏障（N 方汇合） ---------------- */

typedef struct barrier {
    void *h;       /* 实现私有区指针（mutex + cond + 计数） */
} barrier;

void    barrier_init(barrier *_this, uint32_t n);  /* n 方汇合（0 视为 1） */
void    barrier_drop(barrier *_this);
uint8_t barrier_wait(barrier *_this);     /* 阻塞至全部到达；最后到达者返回 1，其余 0 */

#ifdef __cplusplus
}
#endif

#endif /* SC_M_H */
