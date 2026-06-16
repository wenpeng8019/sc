/* m.h —— sc 多线程支持标准的 C ABI 契约（与 builtins/m/m.sc 同步维护）
 *
 * 约定：
 *   - 线程由 run 语句创建：编译器生成 thread_run 调用，
 *     单次 malloc(sizeof(thread) + psize + 实现私有区)，
 *     rpc 参数 memcpy 到 thread 对象紧随位置（t + 1）
 *   - out 非空 → joinable：*out 接收 thread*，须 thread_join 等待并回收（整块释放）
 *     out 为空 → detach：线程结束后自释放
 *   - id 由新线程自身填写（跨平台统一 tid），创建后立即读取可能尚未写入
 *   - h 为实现私有区指针（指向同块尾部），调用方不直接访问
 *   - mutex：init/drop 配对；lock/unlock 配对
 *   - cond：init/drop 配对；wait 语句由编译器生成 cond_wait 调用
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

/* ---------------- thread：线程（run 语句原语） ---------------- */

typedef struct thread {
    uint64_t id;   /* 跨平台统一线程 id（线程启动后由其自身填写） */
    void *h;       /* 实现私有区指针（同块分配） */
} thread;

/* run 语句原语：fn 为 rpc 实际函数（void name_rpc(struct name*)），
 * params/psize 为装填好的参数结构体；out 为空即 detach 自释放。
 * stack 为栈字节数（0=平台默认），prio 为优先级（0=默认，1..255 最佳努力映射）。
 * 返回 1 成功 / 0 失败（失败时 *out 置 NULL） */
uint8_t thread_run(void (*fn)(void *), const void *params, size_t psize, thread **out,
                   uint32_t stack, uint8_t prio);

void    thread_join(thread *_this);   /* 等待结束并回收（含 thread 对象本身） */

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

/* wait 语句原语：调用前须已持有 m；nsec/sec 全 0 → 无限等待，
 * 否则为相对超时时长（sec 秒 + nsec 纳秒）。
 * 返回 0 被唤醒 / 1 超时 / -1 错误 */
int32_t cond_wait(cond *c, mutex *m, uint64_t nsec, uint64_t sec);

#ifdef __cplusplus
}
#endif

#endif /* SC_M_H */
