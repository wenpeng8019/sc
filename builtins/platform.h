/* platform.h —— sc 内置库的跨平台基础头（单头文件，参考摘取自 stdc）
 *
 * 角色：builtins 内其他内置模块的 C 实现（adt_impl.c / m_impl.c ...）
 *       统一经由本头文件实现跨平台，不直接散落 #ifdef。
 * 内容：常用标准 C 头（scc 生成的 C 统一由本头带入）、平台判定宏、
 *       平台基础头、路径分隔符、TLS、字节序、
 *       时钟（墙钟/单调/CPU 耗时）、微秒休眠、CPU 核数、原子操作。
 * 发行：与其他 builtins 资源一样内嵌进 scc 二进制并随用释放。
 */
#ifndef SC_PLATFORM_H
#define SC_PLATFORM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---------------- 平台判定 ---------------- */

/* defined(_WIN32) 也包含 defined(_WIN64) */
#if defined(_WIN32)
#define P_WIN 1
#else
#define P_WIN 0
#endif

/* __APPLE__ 说明是 Apple 平台; __MACH__ 说明内核是 Darwin */
#if defined(__APPLE__) && defined(__MACH__)
#define P_DARWIN 1
#else
#define P_DARWIN 0
#endif

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#define P_BSD 1
#else
#define P_BSD 0
#endif

/* Unix-like（macOS, BSD 等），__unix 是旧 gcc 兼容 */
#if defined(__unix__) || defined(__unix)
#define P_UNIX 1
#else
#define P_UNIX 0
#endif

#if defined(__linux__) || defined(__linux)
#define P_LINUX 1
#else
#define P_LINUX 0
#endif

/* ---------------- 路径分隔符 ---------------- */

#if P_POSIX_LIKE
#define P_SEP '/'
#else
#define P_SEP '\\'
#endif

#if P_WIN
#define P_IS_SEP(c) ((c) == '/' || (c) == '\\')
#else
#define P_IS_SEP(c) ((c) == '/')
#endif

#ifndef __FILE_NAME__
#define __FILE_NAME__ (strrchr(__FILE__, P_SEP) ? strrchr(__FILE__, P_SEP) + 1 : __FILE__)
#endif

/* ---------------- 平台基础头 ---------------- */

#if P_WIN
#   include <windows.h>
#endif
#if !P_WIN || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__)
#   include <unistd.h>
#   include <errno.h>
#endif

/* POSIX（Linux, macOS, BSD 等），该宏依赖 unistd.h */
#if defined(_POSIX_VERSION)
#define P_POSIX 1
#else
#define P_POSIX 0
#endif

/* MINGW 不是真正的 POSIX 环境，但提供部分 POSIX API 兼容实现 */
#if P_POSIX || defined(__MINGW32__) || defined(__MINGW64__)
#define P_POSIX_LIKE 1
#else
#define P_POSIX_LIKE 0
#endif

/* ---------------- 字节序 ---------------- */

#if P_WIN
#   ifndef LITTLE_ENDIAN
#   define LITTLE_ENDIAN 1234
#   endif
#   ifndef BIG_ENDIAN
#   define BIG_ENDIAN    4321
#   endif
#   ifndef BYTE_ORDER
#   define BYTE_ORDER    LITTLE_ENDIAN
#   endif
#elif P_DARWIN || P_BSD
#   include <machine/endian.h>
#else
#   include <endian.h>
#endif

/* ---------------- 线程局部存储 ---------------- */

#if defined(__cplusplus)
#define TLS thread_local
#elif defined(_MSC_VER)
#define TLS __declspec(thread)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define TLS _Thread_local
#elif defined(__GNUC__) || defined(__clang__)
#define TLS __thread
#else
#error "TLS not supported on this compiler"
#endif

/* ---------------- CPU 核数 ---------------- */

/* 逻辑核数（至少返回 1） */
static inline uint32_t P_ncpu(void) {
#if P_WIN
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors > 0 ? (uint32_t)si.dwNumberOfProcessors : 1;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (uint32_t)n : 1;
#endif
}

/* ---------------- 时间与时钟 ---------------- */

/* 统一时钟值类型。不能直接叫 clock：会与 libc 的 clock() 函数（time.h）重定义 */
#if P_POSIX_LIKE
typedef struct timespec P_clock;
#else
typedef struct { time_t tv_sec; long tv_nsec; } P_clock;
#endif

/* 墙钟（系统实时时间，受调时影响），成功返回 0 */
static inline int P_time_now(P_clock* clk) {
#if P_WIN
    /* Win8+ 提供高精度版本，动态加载以兼容 Win7 */
    typedef VOID (WINAPI *GetPreciseFn)(LPFILETIME);
    static GetPreciseFn pPrecise = NULL;
    static int s_checked = 0;
    if (!s_checked) {
        HMODULE k32 = GetModuleHandleA("kernel32.dll");
        if (k32) pPrecise = (GetPreciseFn)GetProcAddress(k32, "GetSystemTimePreciseAsFileTime");
        s_checked = 1;
    }
    FILETIME ft;
    if (pPrecise) pPrecise(&ft);
    else GetSystemTimeAsFileTime(&ft);
    uint64_t t100 = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    t100 -= 116444736000000000ULL;   /* FILETIME 纪元 1601 → Unix 纪元 1970 */
    clk->tv_sec = (time_t)(t100 / 10000000ULL);
    clk->tv_nsec = (long)((t100 % 10000000ULL) * 100ULL);
    return 0;
#else
    return clock_gettime(CLOCK_REALTIME, clk) == 0 ? 0 : -1;
#endif
}

/* 单调时钟（不受系统时间调整影响，适合测时长），成功返回 0 */
static inline int P_clock_now(P_clock* clk) {
#if P_WIN
    static LARGE_INTEGER freq = {0};
    if (!freq.QuadPart && !QueryPerformanceFrequency(&freq)) return -1;
    LARGE_INTEGER ticks;
    if (!QueryPerformanceCounter(&ticks)) return -1;
    clk->tv_sec = (time_t)(ticks.QuadPart / freq.QuadPart);
    clk->tv_nsec = (long)((ticks.QuadPart % freq.QuadPart) * 1000000000LL / freq.QuadPart);
    return 0;
#else
    return clock_gettime(CLOCK_MONOTONIC, clk) == 0 ? 0 : -1;
#endif
}

/* CPU 耗时（用户态+内核态，是时长而非时刻），成功返回 0
 * process_or_thread：true = 本进程累计，false = 当前线程累计 */
static inline int P_cost_now(P_clock* clk, bool process_or_thread) {
#if P_WIN
    FILETIME c, e, k, u;
    BOOL ok = process_or_thread
        ? GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)
        : GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u);
    if (!ok) return -1;
    uint64_t t100 = (((uint64_t)k.dwHighDateTime << 32) | k.dwLowDateTime)
                  + (((uint64_t)u.dwHighDateTime << 32) | u.dwLowDateTime);
    /* 注意：CPU 时间是时长，不做 1601→1970 纪元偏移换算 */
    clk->tv_sec = (time_t)(t100 / 10000000ULL);
    clk->tv_nsec = (long)((t100 % 10000000ULL) * 100ULL);
    return 0;
#else
    /* macOS 10.12+/Linux/BSD 均支持 CPUTIME 时钟源 */
    return clock_gettime(process_or_thread ? CLOCK_PROCESS_CPUTIME_ID
                                           : CLOCK_THREAD_CPUTIME_ID, clk) == 0 ? 0 : -1;
#endif
}

/* 时钟值换算：_f 为浮点，无后缀为整数（四舍五入） */
#define clock_s_f(a)     ((a).tv_sec+(a).tv_nsec/1000000000.0)
#define clock_ms_f(a)    ((a).tv_sec*1000.0+(a).tv_nsec/1000000.0)
#define clock_us_f(a)    ((a).tv_sec*1000000.0+(a).tv_nsec/1000.0)
#define clock_s(a)       ((a).tv_sec+((a).tv_nsec+500000000)/1000000000)
#define clock_ms(a)      ((a).tv_sec*1000+((a).tv_nsec+500000)/1000000)
#define clock_us(a)      ((a).tv_sec*1000000+((a).tv_nsec+500)/1000)
/* 注意：不要统一转换为 ns 再计算，按 long 精度以 ns 表示的 s 最多只能表示 2s */
#define clock_s_diff(a,b)     (((a).tv_sec-(b).tv_sec)+((a).tv_nsec-(b).tv_nsec+((a).tv_nsec>=(b).tv_nsec?500000000:-500000000))/1000000000)
#define clock_ms_diff(a,b)    (((a).tv_sec-(b).tv_sec)*1000+((a).tv_nsec-(b).tv_nsec+((a).tv_nsec>=(b).tv_nsec?500000:-500000))/1000000)
#define clock_us_diff(a,b)    (((a).tv_sec-(b).tv_sec)*1000000+((a).tv_nsec-(b).tv_nsec+((a).tv_nsec>=(b).tv_nsec?500:-500))/1000)
#define clock_dec(a,b,v) {                      \
    (v).tv_sec = (a).tv_sec - (b).tv_sec;       \
    (v).tv_nsec = (a).tv_nsec - (b).tv_nsec;    \
    if ((v).tv_nsec < 0) {                      \
        (v).tv_sec -= 1;                        \
        (v).tv_nsec += 1000000000;              \
    }                                           \
}
#define clock_inc(a,b,v) {                      \
    (v).tv_sec = (a).tv_sec + (b).tv_sec;       \
    (v).tv_nsec = (a).tv_nsec + (b).tv_nsec;    \
    if ((v).tv_nsec >= 1000000000) {            \
        (v).tv_sec += 1;                        \
        (v).tv_nsec -= 1000000000;              \
    }                                           \
}
#define clock_gt(a,b)      ((a).tv_sec>(b).tv_sec || ((a).tv_sec==(b).tv_sec && (a).tv_nsec>(b).tv_nsec))
#define clock_ge(a,b)      ((a).tv_sec>(b).tv_sec || ((a).tv_sec==(b).tv_sec && (a).tv_nsec>=(b).tv_nsec))

/* 单调时钟快照（秒/毫秒/微秒） */
static inline uint64_t P_tick_s(void)  { P_clock _clk; P_clock_now(&_clk); return (uint64_t)clock_s(_clk); }
static inline uint64_t P_tick_ms(void) { P_clock _clk; P_clock_now(&_clk); return (uint64_t)clock_ms(_clk); }
static inline uint64_t P_tick_us(void) { P_clock _clk; P_clock_now(&_clk); return (uint64_t)clock_us(_clk); }

#define tick_diff(now, nlast)  ((now)>(nlast) ? (now)-(nlast) : 0)

/* ---------------- 休眠 ---------------- */

/* 微秒休眠。注意：Windows 的 Sleep 实际精度只有毫秒精度，
 * 不足 1ms 的部分向上取整为 1ms（避免 Sleep(0) 变成让出时间片不休眠）。 */
static inline void P_usleep(uint64_t us) {
#if P_WIN
    Sleep((DWORD)((us + 999ULL) / 1000ULL));
#else
    struct timespec ts = { (time_t)(us / 1000000ULL), (long)(us % 1000000ULL) * 1000L };
    nanosleep(&ts, NULL);
#endif
}

//------------------  原子操作  ------------------------------------------------
// 优先使用 C11 stdatomic.h，否则使用平台特定实现
// 注意：所有 P_inc/P_and/P_or/P_xor 等操作返回新值（操作后的值）
// P_get_and_xxx 操作返回旧值（操作前的值）
// P_test_and_set 返回 bool（true 表示成功）；失败时 *pTestVar 更新为实际旧值
// 注意：C11/MSVC 分支的返回新值宏中 v 会求值两次，v 不得带副作用
//-----------------------------------------------------------------------------

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
// C11 atomics
#   include <stdatomic.h>

/* 用 __typeof__ 而非 typeof：typeof 是 GCC/Clang 扩展，C23 前非标准，
 * 会触发 Clang -Wlanguage-extension-token 警告；__typeof__ 是双下划线形式，
 * 同样受 GCC/Clang 支持但不触发该警告。 */
#define P_get(pVar) atomic_load_explicit((_Atomic __typeof__(*pVar)*)pVar, memory_order_relaxed)
#define P_set(pVar, v) atomic_store_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_relaxed)
#define P_get_acq(pVar) atomic_load_explicit((_Atomic __typeof__(*pVar)*)pVar, memory_order_acquire)
#define P_set_rel(pVar, v) atomic_store_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_release)
#define P_get_ord(pVar) atomic_load_explicit((_Atomic __typeof__(*pVar)*)pVar, memory_order_seq_cst)
#define P_set_ord(pVar, v) atomic_store_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_seq_cst)

#define P_get_and_set(pVar, v) atomic_exchange_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_relaxed)
#define P_get_and_set_dbl(pVar, v) atomic_exchange_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acq_rel)
#define P_get_and_set_acq(pVar, v) atomic_exchange_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acquire)
#define P_get_and_set_rel(pVar, v) atomic_exchange_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_release)
#define P_get_and_set_ord(pVar, v) atomic_exchange_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_seq_cst)

#define P_inc(pVar, v) (atomic_fetch_add_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_relaxed) + (v))
#define P_inc_dbl(pVar, v) (atomic_fetch_add_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acq_rel) + (v))
#define P_inc_acq(pVar, v) (atomic_fetch_add_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acquire) + (v))
#define P_inc_rel(pVar, v) (atomic_fetch_add_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_release) + (v))
#define P_inc_ord(pVar, v) (atomic_fetch_add_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_seq_cst) + (v))
#define P_and(pVar, v) (atomic_fetch_and_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_relaxed) & (v))
#define P_and_dbl(pVar, v) (atomic_fetch_and_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acq_rel) & (v))
#define P_and_acq(pVar, v) (atomic_fetch_and_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acquire) & (v))
#define P_and_rel(pVar, v) (atomic_fetch_and_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_release) & (v))
#define P_and_ord(pVar, v) (atomic_fetch_and_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_seq_cst) & (v))
#define P_or(pVar, v) (atomic_fetch_or_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_relaxed) | (v))
#define P_or_dbl(pVar, v) (atomic_fetch_or_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acq_rel) | (v))
#define P_or_acq(pVar, v) (atomic_fetch_or_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acquire) | (v))
#define P_or_rel(pVar, v) (atomic_fetch_or_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_release) | (v))
#define P_or_ord(pVar, v) (atomic_fetch_or_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_seq_cst) | (v))
#define P_xor(pVar, v) (atomic_fetch_xor_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_relaxed) ^ (v))
#define P_xor_dbl(pVar, v) (atomic_fetch_xor_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acq_rel) ^ (v))
#define P_xor_acq(pVar, v) (atomic_fetch_xor_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acquire) ^ (v))
#define P_xor_rel(pVar, v) (atomic_fetch_xor_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_release) ^ (v))
#define P_xor_ord(pVar, v) (atomic_fetch_xor_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_seq_cst) ^ (v))

#define P_get_and_inc(pVar, v) atomic_fetch_add_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_relaxed)
#define P_get_and_inc_dbl(pVar, v) atomic_fetch_add_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acq_rel)
#define P_get_and_inc_acq(pVar, v) atomic_fetch_add_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acquire)
#define P_get_and_inc_rel(pVar, v) atomic_fetch_add_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_release)
#define P_get_and_inc_ord(pVar, v) atomic_fetch_add_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_seq_cst)
#define P_get_and_and(pVar, v) atomic_fetch_and_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_relaxed)
#define P_get_and_and_dbl(pVar, v) atomic_fetch_and_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acq_rel)
#define P_get_and_and_acq(pVar, v) atomic_fetch_and_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acquire)
#define P_get_and_and_rel(pVar, v) atomic_fetch_and_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_release)
#define P_get_and_and_ord(pVar, v) atomic_fetch_and_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_seq_cst)
#define P_get_and_or(pVar, v) atomic_fetch_or_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_relaxed)
#define P_get_and_or_dbl(pVar, v) atomic_fetch_or_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acq_rel)
#define P_get_and_or_acq(pVar, v) atomic_fetch_or_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acquire)
#define P_get_and_or_rel(pVar, v) atomic_fetch_or_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_release)
#define P_get_and_or_ord(pVar, v) atomic_fetch_or_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_seq_cst)
#define P_get_and_xor(pVar, v) atomic_fetch_xor_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_relaxed)
#define P_get_and_xor_dbl(pVar, v) atomic_fetch_xor_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acq_rel)
#define P_get_and_xor_acq(pVar, v) atomic_fetch_xor_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acquire)
#define P_get_and_xor_rel(pVar, v) atomic_fetch_xor_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_release)
#define P_get_and_xor_ord(pVar, v) atomic_fetch_xor_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_seq_cst)

#define P_test_and_set(pVar, pTestVar, v) atomic_compare_exchange_strong_explicit((_Atomic __typeof__(*pVar)*)pVar, pTestVar, v, memory_order_relaxed, memory_order_relaxed)
#define P_test_and_set_acq(pVar, pTestVar, v) atomic_compare_exchange_strong_explicit((_Atomic __typeof__(*pVar)*)pVar, pTestVar, v, memory_order_acquire, memory_order_relaxed)
#define P_test_and_set_rel(pVar, pTestVar, v) atomic_compare_exchange_strong_explicit((_Atomic __typeof__(*pVar)*)pVar, pTestVar, v, memory_order_release, memory_order_relaxed)
#define P_test_and_set_dbl(pVar, pTestVar, v) atomic_compare_exchange_strong_explicit((_Atomic __typeof__(*pVar)*)pVar, pTestVar, v, memory_order_acq_rel, memory_order_relaxed)
#define P_test_and_set_ord(pVar, pTestVar, v) atomic_compare_exchange_strong_explicit((_Atomic __typeof__(*pVar)*)pVar, pTestVar, v, memory_order_seq_cst, memory_order_relaxed)

/* 注：以下 _or_acq 变体的失败序强于成功序，C11 禁止该组合（C17 起放宽）；
 * gcc/clang 实际均接受，严格 C11 环境下请用成功序 ≥ 失败序的变体 */
#define P_test_and_set_or_acq(pVar, pTestVar, v) atomic_compare_exchange_strong_explicit((_Atomic __typeof__(*pVar)*)pVar, pTestVar, v, memory_order_relaxed, memory_order_acquire)
#define P_test_and_set_acq_or_acq(pVar, pTestVar, v) atomic_compare_exchange_strong_explicit((_Atomic __typeof__(*pVar)*)pVar, pTestVar, v, memory_order_acquire, memory_order_acquire)
#define P_test_and_set_rel_or_acq(pVar, pTestVar, v) atomic_compare_exchange_strong_explicit((_Atomic __typeof__(*pVar)*)pVar, pTestVar, v, memory_order_release, memory_order_acquire)
#define P_test_and_set_dbl_or_acq(pVar, pTestVar, v) atomic_compare_exchange_strong_explicit((_Atomic __typeof__(*pVar)*)pVar, pTestVar, v, memory_order_acq_rel, memory_order_acquire)
#define P_test_and_set_ord_or_acq(pVar, pTestVar, v) atomic_compare_exchange_strong_explicit((_Atomic __typeof__(*pVar)*)pVar, pTestVar, v, memory_order_seq_cst, memory_order_acquire)

#elif P_WIN && !defined(__GNUC__)
// Windows MSVC (Interlocked* 操作默认有 full barrier，无法实现弱内存序)
// 限制：本分支统一按 32 位 LONG 操作，仅适用于 32 位整型；
//        64 位变量需 Interlocked*64 系列，未封装（MinGW 走 __GNUC__ 分支不受限）。
//        P_get/P_get_acq 为普通读（x86 对齐读天然原子且自带 acquire）；
//        P_get_ord 用 InterlockedOr(p, 0) 获得 full barrier 语义。

#define P_get(pVar) (*(pVar))
#define P_set(pVar, v) InterlockedExchange((volatile LONG*)(pVar), (LONG)(v))
#define P_get_acq(pVar) (*(pVar))
#define P_set_rel(pVar, v) InterlockedExchange((volatile LONG*)(pVar), (LONG)(v))
#define P_get_ord(pVar) InterlockedOr((volatile LONG*)(pVar), 0)
#define P_set_ord(pVar, v) InterlockedExchange((volatile LONG*)(pVar), (LONG)(v))

#define P_get_and_set(pVar, v) InterlockedExchange((volatile LONG*)(pVar), (LONG)(v))
#define P_get_and_set_dbl(pVar, v) InterlockedExchange((volatile LONG*)(pVar), (LONG)(v))
#define P_get_and_set_acq(pVar, v) InterlockedExchange((volatile LONG*)(pVar), (LONG)(v))
#define P_get_and_set_rel(pVar, v) InterlockedExchange((volatile LONG*)(pVar), (LONG)(v))
#define P_get_and_set_ord(pVar, v) InterlockedExchange((volatile LONG*)(pVar), (LONG)(v))

// 返回新值：InterlockedExchangeAdd 返回旧值，需要 + v
#define P_inc(pVar, v) (InterlockedExchangeAdd((volatile LONG*)(pVar), (LONG)(v)) + (v))
#define P_inc_dbl(pVar, v) (InterlockedExchangeAdd((volatile LONG*)(pVar), (LONG)(v)) + (v))
#define P_inc_acq(pVar, v) (InterlockedExchangeAdd((volatile LONG*)(pVar), (LONG)(v)) + (v))
#define P_inc_rel(pVar, v) (InterlockedExchangeAdd((volatile LONG*)(pVar), (LONG)(v)) + (v))
#define P_inc_ord(pVar, v) (InterlockedExchangeAdd((volatile LONG*)(pVar), (LONG)(v)) + (v))
// 返回新值：InterlockedAnd 返回旧值，需要 & v
#define P_and(pVar, v) (InterlockedAnd((volatile LONG*)(pVar), (LONG)(v)) & (v))
#define P_and_dbl(pVar, v) (InterlockedAnd((volatile LONG*)(pVar), (LONG)(v)) & (v))
#define P_and_acq(pVar, v) (InterlockedAnd((volatile LONG*)(pVar), (LONG)(v)) & (v))
#define P_and_rel(pVar, v) (InterlockedAnd((volatile LONG*)(pVar), (LONG)(v)) & (v))
#define P_and_ord(pVar, v) (InterlockedAnd((volatile LONG*)(pVar), (LONG)(v)) & (v))
#define P_or(pVar, v) (InterlockedOr((volatile LONG*)(pVar), (LONG)(v)) | (v))
#define P_or_dbl(pVar, v) (InterlockedOr((volatile LONG*)(pVar), (LONG)(v)) | (v))
#define P_or_acq(pVar, v) (InterlockedOr((volatile LONG*)(pVar), (LONG)(v)) | (v))
#define P_or_rel(pVar, v) (InterlockedOr((volatile LONG*)(pVar), (LONG)(v)) | (v))
#define P_or_ord(pVar, v) (InterlockedOr((volatile LONG*)(pVar), (LONG)(v)) | (v))
#define P_xor(pVar, v) (InterlockedXor((volatile LONG*)(pVar), (LONG)(v)) ^ (v))
#define P_xor_dbl(pVar, v) (InterlockedXor((volatile LONG*)(pVar), (LONG)(v)) ^ (v))
#define P_xor_acq(pVar, v) (InterlockedXor((volatile LONG*)(pVar), (LONG)(v)) ^ (v))
#define P_xor_rel(pVar, v) (InterlockedXor((volatile LONG*)(pVar), (LONG)(v)) ^ (v))
#define P_xor_ord(pVar, v) (InterlockedXor((volatile LONG*)(pVar), (LONG)(v)) ^ (v))

// 返回旧值
#define P_get_and_inc(pVar, v) InterlockedExchangeAdd((volatile LONG*)(pVar), (LONG)(v))
#define P_get_and_inc_dbl(pVar, v) InterlockedExchangeAdd((volatile LONG*)(pVar), (LONG)(v))
#define P_get_and_inc_acq(pVar, v) InterlockedExchangeAdd((volatile LONG*)(pVar), (LONG)(v))
#define P_get_and_inc_rel(pVar, v) InterlockedExchangeAdd((volatile LONG*)(pVar), (LONG)(v))
#define P_get_and_inc_ord(pVar, v) InterlockedExchangeAdd((volatile LONG*)(pVar), (LONG)(v))
#define P_get_and_and(pVar, v) InterlockedAnd((volatile LONG*)(pVar), (LONG)(v))
#define P_get_and_and_dbl(pVar, v) InterlockedAnd((volatile LONG*)(pVar), (LONG)(v))
#define P_get_and_and_acq(pVar, v) InterlockedAnd((volatile LONG*)(pVar), (LONG)(v))
#define P_get_and_and_rel(pVar, v) InterlockedAnd((volatile LONG*)(pVar), (LONG)(v))
#define P_get_and_and_ord(pVar, v) InterlockedAnd((volatile LONG*)(pVar), (LONG)(v))
#define P_get_and_or(pVar, v) InterlockedOr((volatile LONG*)(pVar), (LONG)(v))
#define P_get_and_or_dbl(pVar, v) InterlockedOr((volatile LONG*)(pVar), (LONG)(v))
#define P_get_and_or_acq(pVar, v) InterlockedOr((volatile LONG*)(pVar), (LONG)(v))
#define P_get_and_or_rel(pVar, v) InterlockedOr((volatile LONG*)(pVar), (LONG)(v))
#define P_get_and_or_ord(pVar, v) InterlockedOr((volatile LONG*)(pVar), (LONG)(v))
#define P_get_and_xor(pVar, v) InterlockedXor((volatile LONG*)(pVar), (LONG)(v))
#define P_get_and_xor_dbl(pVar, v) InterlockedXor((volatile LONG*)(pVar), (LONG)(v))
#define P_get_and_xor_acq(pVar, v) InterlockedXor((volatile LONG*)(pVar), (LONG)(v))
#define P_get_and_xor_rel(pVar, v) InterlockedXor((volatile LONG*)(pVar), (LONG)(v))
#define P_get_and_xor_ord(pVar, v) InterlockedXor((volatile LONG*)(pVar), (LONG)(v))

// 返回 bool：InterlockedCompareExchange 返回旧值，比较是否等于 expected
static inline bool P_test_and_set_impl(volatile LONG* pVar, LONG* pTestVar, LONG v) {
    LONG old = InterlockedCompareExchange(pVar, v, *pTestVar);
    if (old == *pTestVar) return true;
    *pTestVar = old;
    return false;
}
#define P_test_and_set(pVar, pTestVar, v) P_test_and_set_impl((volatile LONG*)(pVar), (LONG*)(pTestVar), (LONG)(v))
#define P_test_and_set_acq(pVar, pTestVar, v) P_test_and_set_impl((volatile LONG*)(pVar), (LONG*)(pTestVar), (LONG)(v))
#define P_test_and_set_rel(pVar, pTestVar, v) P_test_and_set_impl((volatile LONG*)(pVar), (LONG*)(pTestVar), (LONG)(v))
#define P_test_and_set_dbl(pVar, pTestVar, v) P_test_and_set_impl((volatile LONG*)(pVar), (LONG*)(pTestVar), (LONG)(v))
#define P_test_and_set_ord(pVar, pTestVar, v) P_test_and_set_impl((volatile LONG*)(pVar), (LONG*)(pTestVar), (LONG)(v))

#define P_test_and_set_or_acq(pVar, pTestVar, v) P_test_and_set_impl((volatile LONG*)(pVar), (LONG*)(pTestVar), (LONG)(v))
#define P_test_and_set_acq_or_acq(pVar, pTestVar, v) P_test_and_set_impl((volatile LONG*)(pVar), (LONG*)(pTestVar), (LONG)(v))
#define P_test_and_set_rel_or_acq(pVar, pTestVar, v) P_test_and_set_impl((volatile LONG*)(pVar), (LONG*)(pTestVar), (LONG)(v))
#define P_test_and_set_dbl_or_acq(pVar, pTestVar, v) P_test_and_set_impl((volatile LONG*)(pVar), (LONG*)(pTestVar), (LONG)(v))
#define P_test_and_set_ord_or_acq(pVar, pTestVar, v) P_test_and_set_impl((volatile LONG*)(pVar), (LONG*)(pTestVar), (LONG)(v))

#elif defined(__GNUC__)

#define P_get(pVar) __atomic_load_n(pVar, __ATOMIC_RELAXED)
#define P_set(pVar, v) __atomic_store_n(pVar, v, __ATOMIC_RELAXED)
#define P_get_acq(pVar) __atomic_load_n(pVar, __ATOMIC_ACQUIRE)
#define P_set_rel(pVar, v) __atomic_store_n(pVar, v, __ATOMIC_RELEASE)
#define P_get_ord(pVar) __atomic_load_n(pVar, __ATOMIC_SEQ_CST)
#define P_set_ord(pVar, v) __atomic_store_n(pVar, v, __ATOMIC_SEQ_CST)

#define P_get_and_set(pVar, v) __atomic_exchange_n(pVar, v, __ATOMIC_RELAXED)
#define P_get_and_set_dbl(pVar, v) __atomic_exchange_n(pVar, v, __ATOMIC_ACQ_REL)
#define P_get_and_set_acq(pVar, v) __atomic_exchange_n(pVar, v, __ATOMIC_ACQUIRE)
#define P_get_and_set_rel(pVar, v) __atomic_exchange_n(pVar, v, __ATOMIC_RELEASE)
#define P_get_and_set_ord(pVar, v) __atomic_exchange_n(pVar, v, __ATOMIC_SEQ_CST)

#define P_inc(pVar, v) __atomic_add_fetch(pVar, v, __ATOMIC_RELAXED)
#define P_inc_dbl(pVar, v) __atomic_add_fetch(pVar, v, __ATOMIC_ACQ_REL)
#define P_inc_acq(pVar, v) __atomic_add_fetch(pVar, v, __ATOMIC_ACQUIRE)
#define P_inc_rel(pVar, v) __atomic_add_fetch(pVar, v, __ATOMIC_RELEASE)
#define P_inc_ord(pVar, v) __atomic_add_fetch(pVar, v, __ATOMIC_SEQ_CST)
#define P_and(pVar, v) __atomic_and_fetch(pVar, v, __ATOMIC_RELAXED)
#define P_and_dbl(pVar, v) __atomic_and_fetch(pVar, v, __ATOMIC_ACQ_REL)
#define P_and_acq(pVar, v) __atomic_and_fetch(pVar, v, __ATOMIC_ACQUIRE)
#define P_and_rel(pVar, v) __atomic_and_fetch(pVar, v, __ATOMIC_RELEASE)
#define P_and_ord(pVar, v) __atomic_and_fetch(pVar, v, __ATOMIC_SEQ_CST)
#define P_or(pVar, v) __atomic_or_fetch(pVar, v, __ATOMIC_RELAXED)
#define P_or_dbl(pVar, v) __atomic_or_fetch(pVar, v, __ATOMIC_ACQ_REL)
#define P_or_acq(pVar, v) __atomic_or_fetch(pVar, v, __ATOMIC_ACQUIRE)
#define P_or_rel(pVar, v) __atomic_or_fetch(pVar, v, __ATOMIC_RELEASE)
#define P_or_ord(pVar, v) __atomic_or_fetch(pVar, v, __ATOMIC_SEQ_CST)
#define P_xor(pVar, v) __atomic_xor_fetch(pVar, v, __ATOMIC_RELAXED)
#define P_xor_dbl(pVar, v) __atomic_xor_fetch(pVar, v, __ATOMIC_ACQ_REL)
#define P_xor_acq(pVar, v) __atomic_xor_fetch(pVar, v, __ATOMIC_ACQUIRE)
#define P_xor_rel(pVar, v) __atomic_xor_fetch(pVar, v, __ATOMIC_RELEASE)
#define P_xor_ord(pVar, v) __atomic_xor_fetch(pVar, v, __ATOMIC_SEQ_CST)

#define P_get_and_inc(pVar, v) __atomic_fetch_add(pVar, v, __ATOMIC_RELAXED)
#define P_get_and_inc_dbl(pVar, v) __atomic_fetch_add(pVar, v, __ATOMIC_ACQ_REL)
#define P_get_and_inc_acq(pVar, v) __atomic_fetch_add(pVar, v, __ATOMIC_ACQUIRE)
#define P_get_and_inc_rel(pVar, v) __atomic_fetch_add(pVar, v, __ATOMIC_RELEASE)
#define P_get_and_inc_ord(pVar, v) __atomic_fetch_add(pVar, v, __ATOMIC_SEQ_CST)
#define P_get_and_and(pVar, v) __atomic_fetch_and(pVar, v, __ATOMIC_RELAXED)
#define P_get_and_and_dbl(pVar, v) __atomic_fetch_and(pVar, v, __ATOMIC_ACQ_REL)
#define P_get_and_and_acq(pVar, v) __atomic_fetch_and(pVar, v, __ATOMIC_ACQUIRE)
#define P_get_and_and_rel(pVar, v) __atomic_fetch_and(pVar, v, __ATOMIC_RELEASE)
#define P_get_and_and_ord(pVar, v) __atomic_fetch_and(pVar, v, __ATOMIC_SEQ_CST)
#define P_get_and_or(pVar, v) __atomic_fetch_or(pVar, v, __ATOMIC_RELAXED)
#define P_get_and_or_dbl(pVar, v) __atomic_fetch_or(pVar, v, __ATOMIC_ACQ_REL)
#define P_get_and_or_acq(pVar, v) __atomic_fetch_or(pVar, v, __ATOMIC_ACQUIRE)
#define P_get_and_or_rel(pVar, v) __atomic_fetch_or(pVar, v, __ATOMIC_RELEASE)
#define P_get_and_or_ord(pVar, v) __atomic_fetch_or(pVar, v, __ATOMIC_SEQ_CST)
#define P_get_and_xor(pVar, v) __atomic_fetch_xor(pVar, v, __ATOMIC_RELAXED)
#define P_get_and_xor_dbl(pVar, v) __atomic_fetch_xor(pVar, v, __ATOMIC_ACQ_REL)
#define P_get_and_xor_acq(pVar, v) __atomic_fetch_xor(pVar, v, __ATOMIC_ACQUIRE)
#define P_get_and_xor_rel(pVar, v) __atomic_fetch_xor(pVar, v, __ATOMIC_RELEASE)
#define P_get_and_xor_ord(pVar, v) __atomic_fetch_xor(pVar, v, __ATOMIC_SEQ_CST)

#define P_test_and_set(pVar, pTestVar, v) __atomic_compare_exchange_n(pVar, pTestVar, v, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)

#define P_test_and_set_acq(pVar, pTestVar, v) __atomic_compare_exchange_n(pVar, pTestVar, v, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
#define P_test_and_set_rel(pVar, pTestVar, v) __atomic_compare_exchange_n(pVar, pTestVar, v, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED)
#define P_test_and_set_dbl(pVar, pTestVar, v) __atomic_compare_exchange_n(pVar, pTestVar, v, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)
#define P_test_and_set_ord(pVar, pTestVar, v) __atomic_compare_exchange_n(pVar, pTestVar, v, false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED)

#define P_test_and_set_or_acq(pVar, pTestVar, v) __atomic_compare_exchange_n(pVar, pTestVar, v, false, __ATOMIC_RELAXED, __ATOMIC_ACQUIRE)
#define P_test_and_set_acq_or_acq(pVar, pTestVar, v) __atomic_compare_exchange_n(pVar, pTestVar, v, false, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE)
#define P_test_and_set_rel_or_acq(pVar, pTestVar, v) __atomic_compare_exchange_n(pVar, pTestVar, v, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)
#define P_test_and_set_dbl_or_acq(pVar, pTestVar, v) __atomic_compare_exchange_n(pVar, pTestVar, v, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)
#define P_test_and_set_ord_or_acq(pVar, pTestVar, v) __atomic_compare_exchange_n(pVar, pTestVar, v, false, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE)

#else
#error "Unsupported platform"
#endif

#endif /* SC_PLATFORM_H */
