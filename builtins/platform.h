/* platform.h —— sc 内置库的跨平台基础头（单头文件，参考摘取自 stdc）
 *
 * 角色：builtins 内其他内置模块的 C 实现（adt_impl.c / m_impl.c ...）
 *       统一经由本头文件实现跨平台，不直接散落 #ifdef。
 * 内容：平台判定宏、平台基础头、路径分隔符、TLS、字节序、
 *       单调时钟、毫秒休眠。
 * 发行：与其他 builtins 资源一样内嵌进 scc 二进制并随用释放。
 */
#ifndef SC_PLATFORM_H
#define SC_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
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

/* ---------------- 单调时钟 ---------------- */

#if P_POSIX_LIKE
typedef struct timespec P_clock;
#else
typedef struct { time_t tv_sec; long tv_nsec; } P_clock;
#endif

/* 单调时钟（不受系统时间调整影响），成功返回 0 */
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
    return clock_gettime(CLOCK_MONOTONIC, clk);
#endif
}

#define P_clock_ms(a) ((uint64_t)(a).tv_sec * 1000u + (uint64_t)(a).tv_nsec / 1000000u)
#define P_clock_us(a) ((uint64_t)(a).tv_sec * 1000000u + (uint64_t)(a).tv_nsec / 1000u)

/* ---------------- 休眠 ---------------- */

static inline void P_msleep(uint32_t ms) {
#if P_WIN
    Sleep(ms);
#else
    struct timespec ts = { (time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L };
    nanosleep(&ts, NULL);
#endif
}

#endif /* SC_PLATFORM_H */
