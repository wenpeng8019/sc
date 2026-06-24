/* platform.h —— sc 内置库的跨平台基础头（单头文件，参考摘取自 stdc）
 *
 * 角色：builtins 内其他内置模块的 C 实现（adt_impl.c / m_impl.c ...）
 *       统一经由本头文件实现跨平台，不直接散落 #ifdef。
 * 内容：常用标准 C 头（scc 生成的 C 统一由本头带入）、平台判定宏、
 *       平台基础头、路径分隔符、TLS、字节序、
 *       时钟（墙钟/单调/CPU 耗时）、微秒休眠、CPU 核数、原子操作、
 *       互斥/条件变量/线程 id（跨平台 pthread ↔ Win32）。
 * 发行：与其他 builtins 资源一样内嵌进 scc 二进制并随用释放。
 */
#ifndef SC_PLATFORM_H
#define SC_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>     /* assert：ret 调用语法糖 !! func() 的失败中止 */
#include <inttypes.h>   /* PRId64 / PRIu64：64 位整数 printf 说明符跨平台适配（print 关键字用） */

typedef struct { void* p; uint32_t sz; uint32_t off; } ptr;

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
#if !P_WIN
#   include <pthread.h>          /* 互斥/条件变量/线程：POSIX 后端 */
#   if P_LINUX
#       include <sys/syscall.h>  /* SYS_gettid（线程 id） */
#   endif
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

///////////////////////////////////////////////////////////////////////////////
// 内存对齐
///////////////////////////////////////////////////////////////////////////////

// 返回类型的对齐要求（字节数）
#if defined(__cplusplus)
#   define ALIGN_OF(type)   alignof(type)
#elif __STDC_VERSION__ >= 201112L
#   define ALIGN_OF(type)   _Alignof(type)
#elif defined(_MSC_VER)
#   define ALIGN_OF(type)   __alignof(type)
#else
#   define ALIGN_OF(type)   __alignof__(type)
#endif

// 平台最大对齐要求（malloc/calloc 保证的对齐）
// + C11: stddef.h 定义了 max_align_t
// + 旧标准：手动定义为包含所有最大对齐类型的 union
#if __STDC_VERSION__ >= 201112L
    // C11 already defines max_align_t in stddef.h (included above)
#   define MAX_ALIGN        ALIGN_OF(max_align_t)
#elif defined(__cplusplus) && __cplusplus >= 201103L
    // C++11 has std::max_align_t
#   include <cstddef>
    typedef std::max_align_t max_align_t;
#   define MAX_ALIGN        ALIGN_OF(max_align_t)
#else
    // Pre-C11: define manually
    typedef union {
        long long ll;
        long double ld;
        void *p;
        double d;
    } max_align_t;
#   define MAX_ALIGN        ALIGN_OF(max_align_t)
#endif

// 声明对齐属性
// + 用法: ALIGN_AS(16) uint8_t buf[64];
// + 如果直接使用对象类型（如结构体）定义，则默认会对齐为类型的自然对齐，即无需显式通过 ALIGN_AS 指定对齐
#if defined(_MSC_VER)
#   define ALIGN_AS(n)      __declspec(align(n))
#else
#   define ALIGN_AS(n)      __attribute__((aligned(n)))
#endif

// 检查指针是否按指定字节数对齐
// + 用法: if (IS_ALIGNED(ptr, 4)) { ... }
#define IS_ALIGNED(ptr, n)  (((uintptr_t)(ptr) & ((n) - 1)) == 0)

// 主机字节序的未对齐安全读写（不做字节序转换，仅解决对齐问题）
// + 前提：数据字节序与本机一致（本机存、本机取）
// sc_read_x/sc_write_x: 通过指针读写，sc_read_s(&result, bytes) / sc_write_s(bytes, value)
// sc_get_x: 返回值版本，uint16_t val = sc_get_s(bytes)
// + memcpy 会被编译器优化为单条 load/store 指令
static inline uint16_t sc_get_s(const void *src)  { uint16_t v; memcpy(&v, src, 2); return v; }
static inline uint32_t sc_get_l(const void *src)  { uint32_t v; memcpy(&v, src, 4); return v; }
static inline uint64_t sc_get_ll(const void *src) { uint64_t v; memcpy(&v, src, 8); return v; }
#define sc_read_s(sp, bytes)   memcpy((sp), (bytes), 2)
#define sc_read_l(lp, bytes)   memcpy((lp), (bytes), 4)
#define sc_read_ll(llp, bytes) memcpy((llp), (bytes), 8)
#define sc_write_s(bytes, s)   memcpy((bytes), &(s), 2)
#define sc_write_l(bytes, l)   memcpy((bytes), &(l), 4)
#define sc_write_ll(bytes, ll) memcpy((bytes), &(ll), 8)

///////////////////////////////////////////////////////////////////////////////
// 字节序
///////////////////////////////////////////////////////////////////////////////

#if P_WIN
#   define LITTLE_ENDIAN   1234
#   define BIG_ENDIAN      4321
#   define BYTE_ORDER      LITTLE_ENDIAN
    // 参考 winsock2.h 条件判断逻辑
    // + Windows: SDK 在 Windows 8+ 或定义 INCL_EXTRA_HTON_FUNCTIONS 时提供 htonll/ntohll
#   if !defined(INCL_EXTRA_HTON_FUNCTIONS) && \
       (!defined(NTDDI_VERSION) || NTDDI_VERSION < 0x06020000) /* NTDDI_WIN8 */
    static inline uint64_t htonll(uint64_t x) {
        return (((uint64_t)htonl((uint32_t)(x & 0xFFFFFFFFULL))) << 32) | (uint64_t)htonl((uint32_t)(x >> 32));
    }
    static inline uint64_t ntohll(uint64_t x) { return htonll(x); }
#   endif
#elif P_DARWIN
#   include <machine/endian.h>
#   include <libkern/OSByteOrder.h>
#   ifndef htonll
#       define htonll(x) OSSwapHostToBigInt64(x)
#   endif
#   ifndef ntohll
#       define ntohll(x) OSSwapBigToHostInt64(x)
#   endif
#elif P_LINUX || P_BSD
#   include <endian.h>
#   ifndef htonll
#       define htonll(x) htobe64(x)
#   endif
#   ifndef ntohll
#       define ntohll(x) be64toh(x)
#   endif
#else
#   include <sys/param.h>
    static inline uint64_t htonll(uint64_t x) {
        const uint32_t hi = htonl((uint32_t)(x >> 32));
        const uint32_t lo = htonl((uint32_t)(x & 0xFFFFFFFFULL));
        return ((uint64_t)lo << 32) | hi;
    }
    static inline uint64_t ntohll(uint64_t x) { return htonll(x); }
#endif

#if BYTE_ORDER == BIG_ENDIAN
#   define IS_BIG_ENDIAN 1
#   define IS_LITTLE_ENDIAN 0
#elif BYTE_ORDER == LITTLE_ENDIAN
#   define IS_BIG_ENDIAN 0
#   define IS_LITTLE_ENDIAN 1
#else
#error "Unknown byte order"
#endif

// 网络字节序（大端）的未对齐安全读写（同时完成字节序转换）
// sc_nread/sc_nwrite: 通过指针读写，sc_nread_s(&result, bytes) / sc_nwrite_s(bytes, value)
// sc_nget/sc_nset: 值↔值字节序转换（operand 的 nget/nset）
//   sc_nget_X(h)：取主机序值 h 的网络序值（host→net，等价 htonX）
//   sc_nset_X(n)：取网络序值 n 的主机序值（net→host，等价 ntohX）
//                字节交换自反，故 sc_nset 复用 sc_nget；h/n 会多次求值，不得带副作用
// + 避免未对齐访问，适用于从网络包 payload 直接读取/构造
#if IS_BIG_ENDIAN
#   define sc_nread_s(bytes, result)   memcpy((result), (bytes), 2)
#   define sc_nread_l(bytes, result)   memcpy((result), (bytes), 4)
#   define sc_nread_ll(bytes, result)  memcpy((result), (bytes), 8)
#   define sc_nwrite_s(h, bytes)       memcpy((bytes), &(h), 2)
#   define sc_nwrite_l(h, bytes)       memcpy((bytes), &(h), 4)
#   define sc_nwrite_ll(h, bytes)      memcpy((bytes), &(h), 8)
#   define sc_nget_s(h)                ((uint16_t)(h))
#   define sc_nget_l(h)                ((uint32_t)(h))
#   define sc_nget_ll(h)               ((uint64_t)(h))
#else
#   define sc_nread_s(sp, bytes)       (*(sp) = ((uint16_t)(bytes)[0] << 8) | (uint16_t)(bytes)[1])
#   define sc_nread_l(lp, bytes)       (*(lp) = ((uint32_t)(bytes)[0] << 24) | \
                                             ((uint32_t)(bytes)[1] << 16) | \
                                             ((uint32_t)(bytes)[2] << 8) | \
                                             (uint32_t)(bytes)[3])
#   define sc_nread_ll(llp, bytes)     (*(llp) = ((uint64_t)(bytes)[0] << 56) | \
                                              ((uint64_t)(bytes)[1] << 48) | \
                                              ((uint64_t)(bytes)[2] << 40) | \
                                              ((uint64_t)(bytes)[3] << 32) | \
                                              ((uint64_t)(bytes)[4] << 24) | \
                                              ((uint64_t)(bytes)[5] << 16) | \
                                              ((uint64_t)(bytes)[6] << 8) | \
                                              (uint64_t)(bytes)[7])
#   define sc_nwrite_s(bytes, s)       ((bytes)[0] = (uint8_t)((s) >> 8), \
                                     (bytes)[1] = (uint8_t)((s) & 0xFF))
#   define sc_nwrite_l(bytes, l)       ((bytes)[0] = (uint8_t)((l) >> 24), \
                                     (bytes)[1] = (uint8_t)((l) >> 16), \
                                     (bytes)[2] = (uint8_t)((l) >> 8), \
                                     (bytes)[3] = (uint8_t)((l) & 0xFF))
#   define sc_nwrite_ll(bytes, ll)     ((bytes)[0] = (uint8_t)((ll) >> 56), \
                                     (bytes)[1] = (uint8_t)((ll) >> 48), \
                                     (bytes)[2] = (uint8_t)((ll) >> 40), \
                                     (bytes)[3] = (uint8_t)((ll) >> 32), \
                                     (bytes)[4] = (uint8_t)((ll) >> 24), \
                                     (bytes)[5] = (uint8_t)((ll) >> 16), \
                                     (bytes)[6] = (uint8_t)((ll) >> 8), \
                                     (bytes)[7] = (uint8_t)((ll) & 0xFF))
#   define sc_nget_s(h)                ((uint16_t)((((uint16_t)(h)) >> 8) | (((uint16_t)(h)) << 8)))
#   define sc_nget_l(h)                ((uint32_t)((((uint32_t)(h)) >> 24) | \
                                       ((((uint32_t)(h)) >> 8) & 0x0000FF00U) | \
                                       ((((uint32_t)(h)) << 8) & 0x00FF0000U) | \
                                       (((uint32_t)(h)) << 24)))
#   define sc_nget_ll(h)               ((uint64_t)((((uint64_t)(h)) >> 56) | \
                                       ((((uint64_t)(h)) >> 40) & 0x000000000000FF00ULL) | \
                                       ((((uint64_t)(h)) >> 24) & 0x0000000000FF0000ULL) | \
                                       ((((uint64_t)(h)) >> 8)  & 0x00000000FF000000ULL) | \
                                       ((((uint64_t)(h)) << 8)  & 0x000000FF00000000ULL) | \
                                       ((((uint64_t)(h)) << 24) & 0x0000FF0000000000ULL) | \
                                       ((((uint64_t)(h)) << 40) & 0x00FF000000000000ULL) | \
                                       (((uint64_t)(h)) << 56)))
#endif
// sc_nset_X(n) = net→host（字节交换自反，与 sc_nget 同操作）
#define sc_nset_s(n)                   sc_nget_s(n)
#define sc_nset_l(n)                   sc_nget_l(n)
#define sc_nset_ll(n)                  sc_nget_ll(n)

static inline bool is_little_endian(void) { int i = 1; return *(char*)&i; }

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

/* ---------------- 启动 / 退出钩子（constructor / destructor） ---------------- */
/* 进入 main 前自动执行 SC_CONSTRUCTOR，退出（main 返回 / exit）后自动执行 SC_DESTRUCTOR。
 * 用法（宏紧跟函数体，宏展开为「前置声明 + 定义签名」，其后的 { ... } 即钩子体）：
 *     SC_CONSTRUCTOR(my_init) { ... }
 *     SC_DESTRUCTOR(my_fini)  { ... }
 * 可移植性：GCC / Clang 用 __attribute__；MSVC 经 .CRT$XCU 段放置 ctor 指针、
 * 经 atexit 注册 dtor（需 <stdlib.h>，本头已含）。
 * SC_HAVE_AUTO_HOOKS：本平台是否具备「进入 main 前 / 退出后自动运行」能力。
 *   为 1（GCC/Clang/MSVC）：钩子由平台机制自动触发；scc 生成的显式调用以
 *     #if !SC_HAVE_AUTO_HOOKS 包裹，不参与编译。
 *   为 0（未知编译器）：宏仅定义「具名函数」而不自动注册，改由 scc 生成的
 *     main 序言/尾声（及库模块 sc_mod_*_init/drop）显式调用。 */
#if defined(__GNUC__) || defined(__clang__)
#   define SC_HAVE_AUTO_HOOKS 1
#   define SC_CONSTRUCTOR(f) \
        static void f(void) __attribute__((constructor)); \
        static void f(void)
#   define SC_DESTRUCTOR(f) \
        static void f(void) __attribute__((destructor)); \
        static void f(void)
#elif defined(_MSC_VER)
#   define SC_HAVE_AUTO_HOOKS 1
#   pragma section(".CRT$XCU", read)
    /* ctor：把函数指针放入 CRT 初始化段；/include 链接器指令防止被裁剪。
     * x86 下 C 符号带前导下划线，故 32 位加 "_" 前缀。 */
#   define SC_CTOR_PLACE_(f, pfx) \
        static void f(void); \
        __declspec(allocate(".CRT$XCU")) void (*f##_)(void) = f; \
        __pragma(comment(linker, "/include:" pfx #f "_")) \
        static void f(void)
#   ifdef _WIN64
#       define SC_CONSTRUCTOR(f) SC_CTOR_PLACE_(f, "")
#   else
#       define SC_CONSTRUCTOR(f) SC_CTOR_PLACE_(f, "_")
#   endif
    /* dtor：在 ctor 内 atexit 注册，进程退出时 LIFO 调用。 */
#   define SC_DESTRUCTOR(f) \
        static void f(void); \
        SC_CONSTRUCTOR(f##_reg) { atexit(f); } \
        static void f(void)
#else
    /* 未知编译器：无自动 ctor/dtor 注册能力。退化为「仅定义具名函数」，
     * 由 scc 生成的 main 序言/尾声（及库模块 sc_mod_*_init/drop）显式调用，
     * 调用点以 #if !SC_HAVE_AUTO_HOOKS 包裹。 */
#   define SC_HAVE_AUTO_HOOKS 0
#   define SC_CONSTRUCTOR(f) static void f(void)
#   define SC_DESTRUCTOR(f)  static void f(void)
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

/* ---------------- 互斥 / 条件变量 / 线程 ---------------- */
/* 跨平台薄包装（POSIX pthread ↔ Windows Win32），供 builtins 内 m / op 等模块
 * 共用，避免各处散落 #ifdef。互斥与条件变量均为无全局态的句柄包装，适合 header。 */

#if P_WIN
typedef CRITICAL_SECTION sc_mutex_t;
#define sc_mutex_init(pm)    InitializeCriticalSection(pm)
#define sc_mutex_final(pm)   DeleteCriticalSection(pm)
#define sc_mutex_lock(pm)    EnterCriticalSection(pm)
#define sc_mutex_unlock(pm)  LeaveCriticalSection(pm)
#define sc_mutex_try(pm)     (TryEnterCriticalSection(pm) ? 1 : 0)   /* 成功 1 / 否则 0 */

typedef CONDITION_VARIABLE sc_cond_t;
#define sc_cond_init(pc)     InitializeConditionVariable(pc)
#define sc_cond_final(pc)    ((void)0)                               /* Win 条件变量无需销毁 */
#define sc_cond_one(pc)      WakeConditionVariable(pc)
#define sc_cond_all(pc)      WakeAllConditionVariable(pc)
#else
typedef pthread_mutex_t sc_mutex_t;
#define sc_mutex_init(pm)    pthread_mutex_init(pm, NULL)
#define sc_mutex_final(pm)   pthread_mutex_destroy(pm)
#define sc_mutex_lock(pm)    pthread_mutex_lock(pm)
#define sc_mutex_unlock(pm)  pthread_mutex_unlock(pm)
#define sc_mutex_try(pm)     (pthread_mutex_trylock(pm) == 0 ? 1 : 0) /* 成功 1 / 否则 0 */

typedef pthread_cond_t sc_cond_t;
#define sc_cond_init(pc)     pthread_cond_init(pc, NULL)
#define sc_cond_final(pc)    pthread_cond_destroy(pc)
#define sc_cond_one(pc)      pthread_cond_signal(pc)
#define sc_cond_all(pc)      pthread_cond_broadcast(pc)
#endif

/* 条件等待：nsec/sec 全 0 → 无限等待；否则相对超时（sec 秒 + nsec 纳秒）。
 * 调用前须持有 pm。返回 0=被唤醒 / 1=超时 / -1=错误。 */
static inline int sc_cond_wait(sc_cond_t* pc, sc_mutex_t* pm, uint64_t nsec, uint64_t sec) {
#if P_WIN
    if (!nsec && !sec)
        return SleepConditionVariableCS(pc, pm, INFINITE) ? 0 : -1;
    /* Windows 仅毫秒精度，不足 1ms 向上取整 */
    DWORD ms = (DWORD)(sec * 1000ULL + (nsec + 999999ULL) / 1000000ULL);
    return SleepConditionVariableCS(pc, pm, ms) ? 0
         : (GetLastError() == ERROR_TIMEOUT ? 1 : -1);
#else
    if (!nsec && !sec)
        return pthread_cond_wait(pc, pm) == 0 ? 0 : -1;
    int ret;
#if P_DARWIN
    /* macOS 提供相对超时接口，无需转绝对时间 */
    struct timespec rel = { (time_t)(sec + nsec / 1000000000ULL),
                            (long)(nsec % 1000000000ULL) };
    ret = pthread_cond_timedwait_relative_np(pc, pm, &rel);
#else
    /* 其他 POSIX：转换为 CLOCK_REALTIME 绝对时间 */
    struct timespec abs_time;
    if (clock_gettime(CLOCK_REALTIME, &abs_time) != 0) return -1;
    uint64_t total_ns = (uint64_t)abs_time.tv_nsec + nsec;
    abs_time.tv_sec += (time_t)(sec + total_ns / 1000000000ULL);
    abs_time.tv_nsec = (long)(total_ns % 1000000000ULL);
    ret = pthread_cond_timedwait(pc, pm, &abs_time);
#endif
    return ret == 0 ? 0 : (ret == ETIMEDOUT ? 1 : -1);
#endif
}

/* 当前线程的内核级 id（mach tid / gettid / GetCurrentThreadId；其余回退 pthread_self） */
static inline uint64_t sc_thread_id(void) {
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

/* 屏障：N 方汇合。自实现（mutex + cond），因 macOS 无 pthread_barrier_t、
 * Windows 无 pthread；代际 phase 防本轮唤醒被下一轮抢用，并天然抗虚假唤醒。 */
typedef struct {
    sc_mutex_t mu;
    sc_cond_t  cv;
    uint32_t   total;    /* 需汇合的线程数 */
    uint32_t   count;    /* 本代已到达数 */
    uint32_t   phase;    /* 代际，每满一轮翻转 */
} sc_barrier_t;

static inline void sc_barrier_init(sc_barrier_t* b, uint32_t n) {
    sc_mutex_init(&b->mu);
    sc_cond_init(&b->cv);
    b->total = n ? n : 1;
    b->count = 0;
    b->phase = 0;
}

static inline void sc_barrier_final(sc_barrier_t* b) {
    sc_mutex_final(&b->mu);
    sc_cond_final(&b->cv);
}

/* 汇合点：阻塞至 total 个线程全部到达。返回非 0 表示本线程为最后到达者
 * （对应 PTHREAD_BARRIER_SERIAL_THREAD，可用于选一个线程做收尾工作）。 */
static inline int sc_barrier_wait(sc_barrier_t* b) {
    sc_mutex_lock(&b->mu);
    uint32_t ph = b->phase;
    if (++b->count == b->total) {
        b->phase++;                 /* 进入下一代 */
        b->count = 0;
        sc_cond_all(&b->cv);        /* 放行全部 */
        sc_mutex_unlock(&b->mu);
        return 1;                   /* serial thread */
    }
    while (ph == b->phase)          /* 等代际翻转，抗虚假唤醒/跨轮抢用 */
        sc_cond_wait(&b->cv, &b->mu, 0, 0);
    sc_mutex_unlock(&b->mu);
    return 0;
}

//------------------  原子操作（operand 指令的 C 侧实现：sc_*）  ----------------
// 优先使用 C11 stdatomic.h，否则使用平台特定实现。命名直接采用 op.sc 的 operand
// 指令名 sc_<op>，由编译器对基础类型的 . 操作透传调用（见文末 operand 指令透传说明）。
// 注意：所有 sc_inc/sc_and/sc_or/sc_xor 等操作返回新值（操作后的值）
// sc_get_and_xxx 操作返回旧值（操作前的值）
// sc_test_and_set 返回 bool（true 表示成功）；失败时 *pTestVar 更新为实际旧值
// 注意：C11/MSVC 分支的返回新值宏中 v 会求值两次，v 不得带副作用
//-----------------------------------------------------------------------------

//------------------  operand 指令透传（sc_*）  -------------------------------
// op.sc 的 operand 伪结构体在 sc 侧声明设备操作数通用指令；scc 把基础/任意类型
// 上的 . 操作（如 v.get() / p->set(x)）透传为上面同名的 sc_<op> 宏（按平台分支
// 直接定义，无中间层）。接收者一律以指针传入（值接收者 v.op() 自动取址 &v），故各
// sc_<op> 首参均为指针 pVar。这些宏类型无关（__typeof__ 推导），新增 operand 操作
// 时在 op.sc 与本文件三个平台分支中成对添加同名 sc_<op>。
//-----------------------------------------------------------------------------

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
// C11 atomics
#   include <stdatomic.h>

/* 用 __typeof__ 而非 typeof：typeof 是 GCC/Clang 扩展，C23 前非标准，
 * 会触发 Clang -Wlanguage-extension-token 警告；__typeof__ 是双下划线形式，
 * 同样受 GCC/Clang 支持但不触发该警告。 */
#define sc_get(pVar) atomic_load_explicit((_Atomic __typeof__(*pVar)*)pVar, memory_order_relaxed)
#define sc_set(pVar, v) atomic_store_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_relaxed)
#define sc_get_acq(pVar) atomic_load_explicit((_Atomic __typeof__(*pVar)*)pVar, memory_order_acquire)
#define sc_set_rel(pVar, v) atomic_store_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_release)
#define sc_get_ord(pVar) atomic_load_explicit((_Atomic __typeof__(*pVar)*)pVar, memory_order_seq_cst)
#define sc_set_ord(pVar, v) atomic_store_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_seq_cst)

#define sc_get_and_set(pVar, v) atomic_exchange_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_relaxed)
#define sc_get_and_set_dbl(pVar, v) atomic_exchange_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acq_rel)
#define sc_get_and_set_acq(pVar, v) atomic_exchange_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acquire)
#define sc_get_and_set_rel(pVar, v) atomic_exchange_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_release)
#define sc_get_and_set_ord(pVar, v) atomic_exchange_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_seq_cst)

#define sc_inc(pVar, v) (atomic_fetch_add_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_relaxed) + (v))
#define sc_inc_dbl(pVar, v) (atomic_fetch_add_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acq_rel) + (v))
#define sc_inc_acq(pVar, v) (atomic_fetch_add_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acquire) + (v))
#define sc_inc_rel(pVar, v) (atomic_fetch_add_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_release) + (v))
#define sc_inc_ord(pVar, v) (atomic_fetch_add_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_seq_cst) + (v))
#define sc_and(pVar, v) (atomic_fetch_and_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_relaxed) & (v))
#define sc_and_dbl(pVar, v) (atomic_fetch_and_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acq_rel) & (v))
#define sc_and_acq(pVar, v) (atomic_fetch_and_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acquire) & (v))
#define sc_and_rel(pVar, v) (atomic_fetch_and_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_release) & (v))
#define sc_and_ord(pVar, v) (atomic_fetch_and_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_seq_cst) & (v))
#define sc_or(pVar, v) (atomic_fetch_or_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_relaxed) | (v))
#define sc_or_dbl(pVar, v) (atomic_fetch_or_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acq_rel) | (v))
#define sc_or_acq(pVar, v) (atomic_fetch_or_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acquire) | (v))
#define sc_or_rel(pVar, v) (atomic_fetch_or_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_release) | (v))
#define sc_or_ord(pVar, v) (atomic_fetch_or_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_seq_cst) | (v))
#define sc_xor(pVar, v) (atomic_fetch_xor_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_relaxed) ^ (v))
#define sc_xor_dbl(pVar, v) (atomic_fetch_xor_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acq_rel) ^ (v))
#define sc_xor_acq(pVar, v) (atomic_fetch_xor_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acquire) ^ (v))
#define sc_xor_rel(pVar, v) (atomic_fetch_xor_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_release) ^ (v))
#define sc_xor_ord(pVar, v) (atomic_fetch_xor_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_seq_cst) ^ (v))

#define sc_get_and_inc(pVar, v) atomic_fetch_add_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_relaxed)
#define sc_get_and_inc_dbl(pVar, v) atomic_fetch_add_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acq_rel)
#define sc_get_and_inc_acq(pVar, v) atomic_fetch_add_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acquire)
#define sc_get_and_inc_rel(pVar, v) atomic_fetch_add_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_release)
#define sc_get_and_inc_ord(pVar, v) atomic_fetch_add_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_seq_cst)
#define sc_get_and_and(pVar, v) atomic_fetch_and_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_relaxed)
#define sc_get_and_and_dbl(pVar, v) atomic_fetch_and_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acq_rel)
#define sc_get_and_and_acq(pVar, v) atomic_fetch_and_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acquire)
#define sc_get_and_and_rel(pVar, v) atomic_fetch_and_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_release)
#define sc_get_and_and_ord(pVar, v) atomic_fetch_and_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_seq_cst)
#define sc_get_and_or(pVar, v) atomic_fetch_or_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_relaxed)
#define sc_get_and_or_dbl(pVar, v) atomic_fetch_or_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acq_rel)
#define sc_get_and_or_acq(pVar, v) atomic_fetch_or_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acquire)
#define sc_get_and_or_rel(pVar, v) atomic_fetch_or_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_release)
#define sc_get_and_or_ord(pVar, v) atomic_fetch_or_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_seq_cst)
#define sc_get_and_xor(pVar, v) atomic_fetch_xor_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_relaxed)
#define sc_get_and_xor_dbl(pVar, v) atomic_fetch_xor_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acq_rel)
#define sc_get_and_xor_acq(pVar, v) atomic_fetch_xor_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_acquire)
#define sc_get_and_xor_rel(pVar, v) atomic_fetch_xor_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_release)
#define sc_get_and_xor_ord(pVar, v) atomic_fetch_xor_explicit((_Atomic __typeof__(*pVar)*)pVar, v, memory_order_seq_cst)

#define sc_test_and_set(pVar, pTestVar, v) atomic_compare_exchange_strong_explicit((_Atomic __typeof__(*pVar)*)pVar, pTestVar, v, memory_order_relaxed, memory_order_relaxed)
#define sc_test_and_set_acq(pVar, pTestVar, v) atomic_compare_exchange_strong_explicit((_Atomic __typeof__(*pVar)*)pVar, pTestVar, v, memory_order_acquire, memory_order_relaxed)
#define sc_test_and_set_rel(pVar, pTestVar, v) atomic_compare_exchange_strong_explicit((_Atomic __typeof__(*pVar)*)pVar, pTestVar, v, memory_order_release, memory_order_relaxed)
#define sc_test_and_set_dbl(pVar, pTestVar, v) atomic_compare_exchange_strong_explicit((_Atomic __typeof__(*pVar)*)pVar, pTestVar, v, memory_order_acq_rel, memory_order_relaxed)
#define sc_test_and_set_ord(pVar, pTestVar, v) atomic_compare_exchange_strong_explicit((_Atomic __typeof__(*pVar)*)pVar, pTestVar, v, memory_order_seq_cst, memory_order_relaxed)

/* 注：以下 _or_acq 变体的失败序强于成功序，C11 禁止该组合（C17 起放宽）；
 * gcc/clang 实际均接受，严格 C11 环境下请用成功序 ≥ 失败序的变体 */
#define sc_test_and_set_or_acq(pVar, pTestVar, v) atomic_compare_exchange_strong_explicit((_Atomic __typeof__(*pVar)*)pVar, pTestVar, v, memory_order_relaxed, memory_order_acquire)
#define sc_test_and_set_acq_or_acq(pVar, pTestVar, v) atomic_compare_exchange_strong_explicit((_Atomic __typeof__(*pVar)*)pVar, pTestVar, v, memory_order_acquire, memory_order_acquire)
#define sc_test_and_set_rel_or_acq(pVar, pTestVar, v) atomic_compare_exchange_strong_explicit((_Atomic __typeof__(*pVar)*)pVar, pTestVar, v, memory_order_release, memory_order_acquire)
#define sc_test_and_set_dbl_or_acq(pVar, pTestVar, v) atomic_compare_exchange_strong_explicit((_Atomic __typeof__(*pVar)*)pVar, pTestVar, v, memory_order_acq_rel, memory_order_acquire)
#define sc_test_and_set_ord_or_acq(pVar, pTestVar, v) atomic_compare_exchange_strong_explicit((_Atomic __typeof__(*pVar)*)pVar, pTestVar, v, memory_order_seq_cst, memory_order_acquire)

#elif P_WIN && !defined(__GNUC__)
// Windows MSVC (Interlocked* 操作默认有 full barrier，无法实现弱内存序)
// 限制：本分支统一按 32 位 LONG 操作，仅适用于 32 位整型；
//        64 位变量需 Interlocked*64 系列，未封装（MinGW 走 __GNUC__ 分支不受限）。
//        sc_get/sc_get_acq 为普通读（x86 对齐读天然原子且自带 acquire）；
//        sc_get_ord 用 InterlockedOr(p, 0) 获得 full barrier 语义。

#define sc_get(pVar) (*(pVar))
#define sc_set(pVar, v) InterlockedExchange((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_acq(pVar) (*(pVar))
#define sc_set_rel(pVar, v) InterlockedExchange((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_ord(pVar) InterlockedOr((volatile LONG*)(pVar), 0)
#define sc_set_ord(pVar, v) InterlockedExchange((volatile LONG*)(pVar), (LONG)(v))

#define sc_get_and_set(pVar, v) InterlockedExchange((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_and_set_dbl(pVar, v) InterlockedExchange((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_and_set_acq(pVar, v) InterlockedExchange((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_and_set_rel(pVar, v) InterlockedExchange((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_and_set_ord(pVar, v) InterlockedExchange((volatile LONG*)(pVar), (LONG)(v))

// 返回新值：InterlockedExchangeAdd 返回旧值，需要 + v
#define sc_inc(pVar, v) (InterlockedExchangeAdd((volatile LONG*)(pVar), (LONG)(v)) + (v))
#define sc_inc_dbl(pVar, v) (InterlockedExchangeAdd((volatile LONG*)(pVar), (LONG)(v)) + (v))
#define sc_inc_acq(pVar, v) (InterlockedExchangeAdd((volatile LONG*)(pVar), (LONG)(v)) + (v))
#define sc_inc_rel(pVar, v) (InterlockedExchangeAdd((volatile LONG*)(pVar), (LONG)(v)) + (v))
#define sc_inc_ord(pVar, v) (InterlockedExchangeAdd((volatile LONG*)(pVar), (LONG)(v)) + (v))
// 返回新值：InterlockedAnd 返回旧值，需要 & v
#define sc_and(pVar, v) (InterlockedAnd((volatile LONG*)(pVar), (LONG)(v)) & (v))
#define sc_and_dbl(pVar, v) (InterlockedAnd((volatile LONG*)(pVar), (LONG)(v)) & (v))
#define sc_and_acq(pVar, v) (InterlockedAnd((volatile LONG*)(pVar), (LONG)(v)) & (v))
#define sc_and_rel(pVar, v) (InterlockedAnd((volatile LONG*)(pVar), (LONG)(v)) & (v))
#define sc_and_ord(pVar, v) (InterlockedAnd((volatile LONG*)(pVar), (LONG)(v)) & (v))
#define sc_or(pVar, v) (InterlockedOr((volatile LONG*)(pVar), (LONG)(v)) | (v))
#define sc_or_dbl(pVar, v) (InterlockedOr((volatile LONG*)(pVar), (LONG)(v)) | (v))
#define sc_or_acq(pVar, v) (InterlockedOr((volatile LONG*)(pVar), (LONG)(v)) | (v))
#define sc_or_rel(pVar, v) (InterlockedOr((volatile LONG*)(pVar), (LONG)(v)) | (v))
#define sc_or_ord(pVar, v) (InterlockedOr((volatile LONG*)(pVar), (LONG)(v)) | (v))
#define sc_xor(pVar, v) (InterlockedXor((volatile LONG*)(pVar), (LONG)(v)) ^ (v))
#define sc_xor_dbl(pVar, v) (InterlockedXor((volatile LONG*)(pVar), (LONG)(v)) ^ (v))
#define sc_xor_acq(pVar, v) (InterlockedXor((volatile LONG*)(pVar), (LONG)(v)) ^ (v))
#define sc_xor_rel(pVar, v) (InterlockedXor((volatile LONG*)(pVar), (LONG)(v)) ^ (v))
#define sc_xor_ord(pVar, v) (InterlockedXor((volatile LONG*)(pVar), (LONG)(v)) ^ (v))

// 返回旧值
#define sc_get_and_inc(pVar, v) InterlockedExchangeAdd((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_and_inc_dbl(pVar, v) InterlockedExchangeAdd((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_and_inc_acq(pVar, v) InterlockedExchangeAdd((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_and_inc_rel(pVar, v) InterlockedExchangeAdd((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_and_inc_ord(pVar, v) InterlockedExchangeAdd((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_and_and(pVar, v) InterlockedAnd((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_and_and_dbl(pVar, v) InterlockedAnd((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_and_and_acq(pVar, v) InterlockedAnd((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_and_and_rel(pVar, v) InterlockedAnd((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_and_and_ord(pVar, v) InterlockedAnd((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_and_or(pVar, v) InterlockedOr((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_and_or_dbl(pVar, v) InterlockedOr((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_and_or_acq(pVar, v) InterlockedOr((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_and_or_rel(pVar, v) InterlockedOr((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_and_or_ord(pVar, v) InterlockedOr((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_and_xor(pVar, v) InterlockedXor((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_and_xor_dbl(pVar, v) InterlockedXor((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_and_xor_acq(pVar, v) InterlockedXor((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_and_xor_rel(pVar, v) InterlockedXor((volatile LONG*)(pVar), (LONG)(v))
#define sc_get_and_xor_ord(pVar, v) InterlockedXor((volatile LONG*)(pVar), (LONG)(v))

// 返回 bool：InterlockedCompareExchange 返回旧值，比较是否等于 expected
static inline bool sc_test_and_set_impl(volatile LONG* pVar, LONG* pTestVar, LONG v) {
    LONG old = InterlockedCompareExchange(pVar, v, *pTestVar);
    if (old == *pTestVar) return true;
    *pTestVar = old;
    return false;
}
#define sc_test_and_set(pVar, pTestVar, v) sc_test_and_set_impl((volatile LONG*)(pVar), (LONG*)(pTestVar), (LONG)(v))
#define sc_test_and_set_acq(pVar, pTestVar, v) sc_test_and_set_impl((volatile LONG*)(pVar), (LONG*)(pTestVar), (LONG)(v))
#define sc_test_and_set_rel(pVar, pTestVar, v) sc_test_and_set_impl((volatile LONG*)(pVar), (LONG*)(pTestVar), (LONG)(v))
#define sc_test_and_set_dbl(pVar, pTestVar, v) sc_test_and_set_impl((volatile LONG*)(pVar), (LONG*)(pTestVar), (LONG)(v))
#define sc_test_and_set_ord(pVar, pTestVar, v) sc_test_and_set_impl((volatile LONG*)(pVar), (LONG*)(pTestVar), (LONG)(v))

#define sc_test_and_set_or_acq(pVar, pTestVar, v) sc_test_and_set_impl((volatile LONG*)(pVar), (LONG*)(pTestVar), (LONG)(v))
#define sc_test_and_set_acq_or_acq(pVar, pTestVar, v) sc_test_and_set_impl((volatile LONG*)(pVar), (LONG*)(pTestVar), (LONG)(v))
#define sc_test_and_set_rel_or_acq(pVar, pTestVar, v) sc_test_and_set_impl((volatile LONG*)(pVar), (LONG*)(pTestVar), (LONG)(v))
#define sc_test_and_set_dbl_or_acq(pVar, pTestVar, v) sc_test_and_set_impl((volatile LONG*)(pVar), (LONG*)(pTestVar), (LONG)(v))
#define sc_test_and_set_ord_or_acq(pVar, pTestVar, v) sc_test_and_set_impl((volatile LONG*)(pVar), (LONG*)(pTestVar), (LONG)(v))

#elif defined(__GNUC__)

#define sc_get(pVar) __atomic_load_n(pVar, __ATOMIC_RELAXED)
#define sc_set(pVar, v) __atomic_store_n(pVar, v, __ATOMIC_RELAXED)
#define sc_get_acq(pVar) __atomic_load_n(pVar, __ATOMIC_ACQUIRE)
#define sc_set_rel(pVar, v) __atomic_store_n(pVar, v, __ATOMIC_RELEASE)
#define sc_get_ord(pVar) __atomic_load_n(pVar, __ATOMIC_SEQ_CST)
#define sc_set_ord(pVar, v) __atomic_store_n(pVar, v, __ATOMIC_SEQ_CST)

#define sc_get_and_set(pVar, v) __atomic_exchange_n(pVar, v, __ATOMIC_RELAXED)
#define sc_get_and_set_dbl(pVar, v) __atomic_exchange_n(pVar, v, __ATOMIC_ACQ_REL)
#define sc_get_and_set_acq(pVar, v) __atomic_exchange_n(pVar, v, __ATOMIC_ACQUIRE)
#define sc_get_and_set_rel(pVar, v) __atomic_exchange_n(pVar, v, __ATOMIC_RELEASE)
#define sc_get_and_set_ord(pVar, v) __atomic_exchange_n(pVar, v, __ATOMIC_SEQ_CST)

#define sc_inc(pVar, v) __atomic_add_fetch(pVar, v, __ATOMIC_RELAXED)
#define sc_inc_dbl(pVar, v) __atomic_add_fetch(pVar, v, __ATOMIC_ACQ_REL)
#define sc_inc_acq(pVar, v) __atomic_add_fetch(pVar, v, __ATOMIC_ACQUIRE)
#define sc_inc_rel(pVar, v) __atomic_add_fetch(pVar, v, __ATOMIC_RELEASE)
#define sc_inc_ord(pVar, v) __atomic_add_fetch(pVar, v, __ATOMIC_SEQ_CST)
#define sc_and(pVar, v) __atomic_and_fetch(pVar, v, __ATOMIC_RELAXED)
#define sc_and_dbl(pVar, v) __atomic_and_fetch(pVar, v, __ATOMIC_ACQ_REL)
#define sc_and_acq(pVar, v) __atomic_and_fetch(pVar, v, __ATOMIC_ACQUIRE)
#define sc_and_rel(pVar, v) __atomic_and_fetch(pVar, v, __ATOMIC_RELEASE)
#define sc_and_ord(pVar, v) __atomic_and_fetch(pVar, v, __ATOMIC_SEQ_CST)
#define sc_or(pVar, v) __atomic_or_fetch(pVar, v, __ATOMIC_RELAXED)
#define sc_or_dbl(pVar, v) __atomic_or_fetch(pVar, v, __ATOMIC_ACQ_REL)
#define sc_or_acq(pVar, v) __atomic_or_fetch(pVar, v, __ATOMIC_ACQUIRE)
#define sc_or_rel(pVar, v) __atomic_or_fetch(pVar, v, __ATOMIC_RELEASE)
#define sc_or_ord(pVar, v) __atomic_or_fetch(pVar, v, __ATOMIC_SEQ_CST)
#define sc_xor(pVar, v) __atomic_xor_fetch(pVar, v, __ATOMIC_RELAXED)
#define sc_xor_dbl(pVar, v) __atomic_xor_fetch(pVar, v, __ATOMIC_ACQ_REL)
#define sc_xor_acq(pVar, v) __atomic_xor_fetch(pVar, v, __ATOMIC_ACQUIRE)
#define sc_xor_rel(pVar, v) __atomic_xor_fetch(pVar, v, __ATOMIC_RELEASE)
#define sc_xor_ord(pVar, v) __atomic_xor_fetch(pVar, v, __ATOMIC_SEQ_CST)

#define sc_get_and_inc(pVar, v) __atomic_fetch_add(pVar, v, __ATOMIC_RELAXED)
#define sc_get_and_inc_dbl(pVar, v) __atomic_fetch_add(pVar, v, __ATOMIC_ACQ_REL)
#define sc_get_and_inc_acq(pVar, v) __atomic_fetch_add(pVar, v, __ATOMIC_ACQUIRE)
#define sc_get_and_inc_rel(pVar, v) __atomic_fetch_add(pVar, v, __ATOMIC_RELEASE)
#define sc_get_and_inc_ord(pVar, v) __atomic_fetch_add(pVar, v, __ATOMIC_SEQ_CST)
#define sc_get_and_and(pVar, v) __atomic_fetch_and(pVar, v, __ATOMIC_RELAXED)
#define sc_get_and_and_dbl(pVar, v) __atomic_fetch_and(pVar, v, __ATOMIC_ACQ_REL)
#define sc_get_and_and_acq(pVar, v) __atomic_fetch_and(pVar, v, __ATOMIC_ACQUIRE)
#define sc_get_and_and_rel(pVar, v) __atomic_fetch_and(pVar, v, __ATOMIC_RELEASE)
#define sc_get_and_and_ord(pVar, v) __atomic_fetch_and(pVar, v, __ATOMIC_SEQ_CST)
#define sc_get_and_or(pVar, v) __atomic_fetch_or(pVar, v, __ATOMIC_RELAXED)
#define sc_get_and_or_dbl(pVar, v) __atomic_fetch_or(pVar, v, __ATOMIC_ACQ_REL)
#define sc_get_and_or_acq(pVar, v) __atomic_fetch_or(pVar, v, __ATOMIC_ACQUIRE)
#define sc_get_and_or_rel(pVar, v) __atomic_fetch_or(pVar, v, __ATOMIC_RELEASE)
#define sc_get_and_or_ord(pVar, v) __atomic_fetch_or(pVar, v, __ATOMIC_SEQ_CST)
#define sc_get_and_xor(pVar, v) __atomic_fetch_xor(pVar, v, __ATOMIC_RELAXED)
#define sc_get_and_xor_dbl(pVar, v) __atomic_fetch_xor(pVar, v, __ATOMIC_ACQ_REL)
#define sc_get_and_xor_acq(pVar, v) __atomic_fetch_xor(pVar, v, __ATOMIC_ACQUIRE)
#define sc_get_and_xor_rel(pVar, v) __atomic_fetch_xor(pVar, v, __ATOMIC_RELEASE)
#define sc_get_and_xor_ord(pVar, v) __atomic_fetch_xor(pVar, v, __ATOMIC_SEQ_CST)

#define sc_test_and_set(pVar, pTestVar, v) __atomic_compare_exchange_n(pVar, pTestVar, v, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)

#define sc_test_and_set_acq(pVar, pTestVar, v) __atomic_compare_exchange_n(pVar, pTestVar, v, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
#define sc_test_and_set_rel(pVar, pTestVar, v) __atomic_compare_exchange_n(pVar, pTestVar, v, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED)
#define sc_test_and_set_dbl(pVar, pTestVar, v) __atomic_compare_exchange_n(pVar, pTestVar, v, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)
#define sc_test_and_set_ord(pVar, pTestVar, v) __atomic_compare_exchange_n(pVar, pTestVar, v, false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED)

#define sc_test_and_set_or_acq(pVar, pTestVar, v) __atomic_compare_exchange_n(pVar, pTestVar, v, false, __ATOMIC_RELAXED, __ATOMIC_ACQUIRE)
#define sc_test_and_set_acq_or_acq(pVar, pTestVar, v) __atomic_compare_exchange_n(pVar, pTestVar, v, false, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE)
#define sc_test_and_set_rel_or_acq(pVar, pTestVar, v) __atomic_compare_exchange_n(pVar, pTestVar, v, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)
#define sc_test_and_set_dbl_or_acq(pVar, pTestVar, v) __atomic_compare_exchange_n(pVar, pTestVar, v, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)
#define sc_test_and_set_ord_or_acq(pVar, pTestVar, v) __atomic_compare_exchange_n(pVar, pTestVar, v, false, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE)

#else
#error "Unsupported platform"
#endif

///////////////////////////////////////////////////////////////////////////////
// 随机数生成
///////////////////////////////////////////////////////////////////////////////

/*
 * P_rand_init()   - 初始化随机数生成器（可选，仅降级方案需要）
 * P_rand32()      - 生成 32 位随机数（加密安全）
 * P_rand64()      - 生成 64 位随机数（加密安全）
 * P_rand_bytes()  - 填充随机字节
 *
 * 平台实现：
 *   - macOS/BSD:  arc4random() (ChaCha20, CSPRNG) - 无需初始化
 *   - Windows:    rand_s() (RtlGenRandom, CSPRNG) - 无需初始化
 *   - Linux:      /dev/urandom (内核 CSPRNG) - 随用随开，无需初始化
 *   - 降级方案:   srand(time) + rand() (自动初始化，仅用于测试)
 *
 * 性能考虑：
 *   - Linux 平台采用"随用随开"策略，每次打开/关闭 /dev/urandom
 *   - 现代内核的 open() 开销很小，适合中低频调用
 *   - 避免长期占用文件描述符，简化生命周期管理
 *
 * 返回值：非零随机数（0 保留为无效值）
 */

#if P_DARWIN || P_BSD
    // macOS/BSD: arc4random() 无需初始化
    static inline void P_rand_init(void) { /* 无操作 */ }

    static inline uint32_t P_rand32(void) {
        uint32_t r = arc4random();
        return r ? r : 1;  // 避免返回 0
    }

    static inline uint64_t P_rand64(void) {
        uint64_t r = ((uint64_t)arc4random() << 32) | arc4random();
        return r ? r : 1;
    }

#elif P_WIN
    // Windows: rand_s() 在 <stdlib.h> 中，需要定义 _CRT_RAND_S
    #ifndef _CRT_RAND_S
        #define _CRT_RAND_S
    #endif

    #if !(defined(_MSC_VER) && _MSC_VER >= 1400)
        static bool g_rand_initialized = false;
    #endif

    static inline void P_rand_init(void) {
        #if defined(_MSC_VER) && _MSC_VER >= 1400
            /* rand_s() 无需初始化 */
        #else
            /* 降级方案：初始化 rand() */
            if (!g_rand_initialized) {
                srand((unsigned int)time(NULL));
                g_rand_initialized = true;
            }
        #endif
    }

    static inline uint32_t P_rand32(void) {
        uint32_t r;
        #if defined(_MSC_VER) && _MSC_VER >= 1400
            if (rand_s(&r) != 0) r = (uint32_t)time(NULL);
        #else
            if (!g_rand_initialized) P_rand_init();
            r = (uint32_t)rand();  // 降级方案
        #endif
        return r ? r : 1;
    }

    static inline uint64_t P_rand64(void) {
        uint64_t r;
        #if defined(_MSC_VER) && _MSC_VER >= 1400
            uint32_t hi, lo;
            while (rand_s(&hi) != 0 || rand_s(&lo) != 0) { /* retry */ }
            r = ((uint64_t)hi << 32) | lo;
        #else
            if (!g_rand_initialized) P_rand_init();
            // 降级方案：组合多个 rand() 调用
            r = ((uint64_t)rand() << 48) ^ ((uint64_t)rand() << 32) ^
                ((uint64_t)rand() << 16) ^ (uint64_t)rand();
        #endif
        return r ? r : 1;
    }

#else
    // Linux/其他 POSIX: 使用 /dev/urandom（随用随开，避免长期占用文件描述符）
    static bool g_rand_initialized = false;

    static inline void P_rand_init(void) {
        if (!g_rand_initialized) {
            /* 初始化 rand() 作为降级方案 */
            srand((unsigned int)time(NULL));
            g_rand_initialized = true;
        }
    }

    static inline uint32_t P_rand32(void) {
        uint32_t r;
        FILE *fp = fopen("/dev/urandom", "rb");
        
        if (fp && fread(&r, sizeof(r), 1, fp) == 1) {
            fclose(fp);
            /* 成功从 /dev/urandom 读取 */
        } else {
            if (fp) fclose(fp);
            /* 降级方案：使用 rand() */
            if (!g_rand_initialized) P_rand_init();
            r = (uint32_t)rand();
        }
        return r ? r : 1;
    }

    static inline uint64_t P_rand64(void) {
        uint64_t r;
        FILE *fp = fopen("/dev/urandom", "rb");
        
        if (fp && fread(&r, sizeof(r), 1, fp) == 1) {
            fclose(fp);
            /* 成功从 /dev/urandom 读取 */
        } else {
            if (fp) fclose(fp);
            /* 降级方案：组合多个 rand() */
            if (!g_rand_initialized) P_rand_init();
            r = ((uint64_t)rand() << 48) ^ ((uint64_t)rand() << 32) ^
                ((uint64_t)rand() << 16) ^ (uint64_t)rand();
        }
        return r ? r : 1;
    }
#endif


/*
 * P_rand_bytes() - 填充缓冲区为随机字节
 * @param buf  目标缓冲区
 * @param len  字节数
 */
static inline void P_rand_bytes(void *buf, size_t len) {
    if (!buf || len == 0) return;
    
    uint8_t *p = (uint8_t *)buf;
    
    // 每次填充 4 字节，利用 P_rand32()
    while (len >= 4) {
        uint32_t r = P_rand32();
        memcpy(p, &r, 4);
        p += 4;
        len -= 4;
    }
    
    // 处理剩余的 1-3 字节
    if (len > 0) {
        uint32_t r = P_rand32();
        memcpy(p, &r, len);
    }
}


//------------------  op.sc 机制运行时（默认带入）  ----------------------
// op.sc 为默认导入的语法机制声明模块；op.h 是其 C 侧伴随头（chain 等
// 机制的结构体与运行时原型）。随 platform.h 一同进入每个生成的 C 单元。
#include "op.h"

#endif /* SC_PLATFORM_H */
