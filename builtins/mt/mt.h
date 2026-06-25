/* mt.h —— sc 多线程支持标准的 C ABI 契约（与 builtins/mt/mt.sc 同步维护）
 *
 * 约定：
 *   - thread 与 run 线程创建（thread_run/thread_join）已下沉至语言内核（op.h/op_impl.c）
 *   - mutex：init/drop 配对；lock/unlock 配对
 *   - cond：init/drop 配对；wait 为 cond 方法（编译器生成 cond_wait 调用）
 *   - pool：init/drop 配对；run 语句第二参为 pool 时编译器生成 pool_run 调用
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
 * 见 builtins/op.h（默认带入）与 op_impl.c（始终链接）。pool 仍为本模块执行目标，
 * 其 pool_run 接收与 thread_run 同形的参数帧。 */

/* ---------------- pool：线程池（run 语句的另一种执行目标） ---------------- */

typedef struct pool {
    void *h;       /* 实现私有区指针（队列 + 同步原语 + 工作线程） */
} pool;

/* run 语句原语（pool 形态，对称 thread_run）：fn 为 rpc 实际函数，
 * params/psize 为装填好的参数结构体（拷贝入任务节点，调用点无需保活）。
 * 返回 1 成功 / 0 失败（池未初始化/已停/内存不足） */
uint8_t pool_run(pool *_this, void (*fn)(void *), const void *params, size_t psize);

void    pool_init(pool *_this, uint32_t n);  /* n 个工作线程；0 → CPU 逻辑核数 */
void    pool_join(pool *_this);              /* 屏障：等待全部已提交任务完成（后续仍可提交） */
void    pool_drop(pool *_this);              /* 析构：等任务完成 → 停工作线程 → 回收 */

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
