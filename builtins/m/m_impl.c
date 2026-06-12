/* m_impl.c —— sc 多线程支持标准（m.h 契约）默认实现
 * 跨平台经由 builtins/platform.h：POSIX pthread / Windows 线程 API
 */
#include "m.h"
#include "platform.h"
#include <stdlib.h>

#if !P_WIN
#include <pthread.h>
#endif

/* ---------------- thread ---------------- */

/* 句柄状态：平台线程对象 + 入口包装上下文 */
typedef struct {
#if P_WIN
    HANDLE     t;
#else
    pthread_t  t;
#endif
    thread_fn *fn;
    void      *arg;
} thd_state;

#if P_WIN
static DWORD WINAPI thd_entry(LPVOID p) {
    thd_state *s = (thd_state *)p;
    s->fn(s->arg);
    return 0;
}
#else
static void *thd_entry(void *p) {
    thd_state *s = (thd_state *)p;
    s->fn(s->arg);
    return NULL;
}
#endif

void thread_init(thread *_this) { _this->h = NULL; }

uint8_t thread_start(thread *_this, thread_fn *f, void *arg) {
    if (_this->h || !f) return 0;   /* 已启动未回收 / 无入口 */
    thd_state *s = (thd_state *)malloc(sizeof(thd_state));
    if (!s) return 0;
    s->fn = f;
    s->arg = arg;
#if P_WIN
    s->t = CreateThread(NULL, 0, thd_entry, s, 0, NULL);
    if (!s->t) { free(s); return 0; }
#else
    if (pthread_create(&s->t, NULL, thd_entry, s) != 0) { free(s); return 0; }
#endif
    _this->h = s;
    return 1;
}

void thread_join(thread *_this) {
    thd_state *s = (thd_state *)_this->h;
    if (!s) return;
#if P_WIN
    WaitForSingleObject(s->t, INFINITE);
    CloseHandle(s->t);
#else
    pthread_join(s->t, NULL);
#endif
    free(s);
    _this->h = NULL;
}

void thread_drop(thread *_this) {
    thd_state *s = (thd_state *)_this->h;
    if (!s) return;
#if P_WIN
    CloseHandle(s->t);   /* 关闭句柄，线程继续运行 */
#else
    pthread_detach(s->t);
#endif
    free(s);
    _this->h = NULL;
}

void thread_sleep(thread *_this, uint32_t ms) {
    (void)_this;   /* 休眠当前线程，与接收者实例无关 */
    P_msleep(ms);
}

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
