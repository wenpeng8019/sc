/* m_impl.c —— sc 多线程支持标准（m.h 契约）默认实现
 * 跨平台经由 builtins/platform.h：POSIX pthread / Windows 线程 API
 */
#include "m.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>

#if !P_WIN
#include <pthread.h>
#endif
#if P_LINUX
#include <sys/syscall.h>
#endif

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

/* 跨平台统一线程 id（参考 stdc：mach tid / gettid / GetCurrentThreadId） */
static uint64_t thd_current_id(void) {
#if P_WIN
    return (uint64_t)GetCurrentThreadId();
#elif P_DARWIN
    return (uint64_t)pthread_mach_thread_np(pthread_self());
#elif P_LINUX
    return (uint64_t)syscall(SYS_gettid);
#else
    return (uint64_t)(uintptr_t)pthread_self();
#endif
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

uint8_t thread_run(void (*fn)(void *), const void *params, size_t psize, thread **out) {
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
    HANDLE h = CreateThread(NULL, 0, thd_entry, t, 0, NULL);
    if (!h) { free(t); return 0; }
    if (out) { im->t = h; *out = t; }
    else CloseHandle(h);                /* detach：关闭句柄，线程自释放 */
#else
    pthread_t h;
    pthread_attr_t attr, *pattr = NULL;
    if (!out) {                         /* detach：创建即分离，入口结束自释放 */
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pattr = &attr;
    }
    int err = pthread_create(&h, pattr, thd_entry, t);
    if (pattr) pthread_attr_destroy(pattr);
    if (err) { free(t); return 0; }
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

void msleep_rpc(struct msleep *_p) { P_msleep(_p->ms); }

/* ---------------- mutex ---------------- */

#if P_WIN
typedef CRITICAL_SECTION mtx_state;
#else
typedef pthread_mutex_t  mtx_state;
#endif

void mutex_init(mutex *_this) {
    mtx_state *m = (mtx_state *)malloc(sizeof(mtx_state));
    if (m) {
#if P_WIN
        InitializeCriticalSection(m);
#else
        pthread_mutex_init(m, NULL);
#endif
    }
    _this->h = m;
}

void mutex_drop(mutex *_this) {
    mtx_state *m = (mtx_state *)_this->h;
    if (!m) return;
#if P_WIN
    DeleteCriticalSection(m);
#else
    pthread_mutex_destroy(m);
#endif
    free(m);
    _this->h = NULL;
}

void mutex_lock(mutex *_this) {
    mtx_state *m = (mtx_state *)_this->h;
    if (!m) return;
#if P_WIN
    EnterCriticalSection(m);
#else
    pthread_mutex_lock(m);
#endif
}

void mutex_unlock(mutex *_this) {
    mtx_state *m = (mtx_state *)_this->h;
    if (!m) return;
#if P_WIN
    LeaveCriticalSection(m);
#else
    pthread_mutex_unlock(m);
#endif
}

uint8_t mutex_try_lock(mutex *_this) {
    mtx_state *m = (mtx_state *)_this->h;
    if (!m) return 0;
#if P_WIN
    return TryEnterCriticalSection(m) ? 1 : 0;
#else
    return pthread_mutex_trylock(m) == 0 ? 1 : 0;
#endif
}
