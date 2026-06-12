/* m.h —— sc 多线程支持标准的 C ABI 契约（与 builtins/m/m.sc 同步维护）
 *
 * 约定：
 *   - h 为实现私有的平台句柄（实现负责分配/释放），调用方不直接访问
 *   - 返回 uint8_t（sc 的 b 类型）的函数：1 成功 / 0 失败
 *   - thread：start 后必须 join（回收）或 drop（detach 释放）二选一
 *   - mutex：init/drop 配对；lock/unlock 配对
 */
#ifndef SC_M_H
#define SC_M_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- thread：线程 ---------------- */

typedef void thread_fn(void *arg);    /* 线程入口函数类型 */

typedef struct thread {
    void *h;       /* 平台线程句柄（实现私有） */
} thread;

void    thread_init(thread *_this);                              /* 构造为空 */
uint8_t thread_start(thread *_this, thread_fn *f, void *arg);    /* 启动线程 */
void    thread_join(thread *_this);                              /* 等待结束并回收 */
void    thread_drop(thread *_this);                              /* 未 join 则 detach 后释放 */
void    thread_sleep(thread *_this, uint32_t ms);                /* 当前线程休眠 */

/* ---------------- mutex：互斥锁 ---------------- */

typedef struct mutex {
    void *h;       /* 平台锁句柄（实现私有） */
} mutex;

void    mutex_init(mutex *_this);
void    mutex_drop(mutex *_this);
void    mutex_lock(mutex *_this);
void    mutex_unlock(mutex *_this);
uint8_t mutex_try_lock(mutex *_this);     /* 1 成功 / 0 已被占用 */

#ifdef __cplusplus
}
#endif

#endif /* SC_M_H */
