/* platform.h —— sc 内置库的跨平台基础头（单头文件，参考摘取自 stdc）
 *
 * 角色：builtins 内其他内置模块的 C 实现（adt_impl.c / mt_impl.c ...）
 *       统一经由本头文件实现跨平台，不直接散落 #ifdef。
 * 内容：常用标准 C 头（scc 生成的 C 统一由本头带入）、平台判定宏、
 *       平台基础头、路径分隔符、TLS、字节序、
 *       时钟（墙钟/单调/CPU 耗时）、原子操作、
 *       互斥/条件变量/线程 id/动态 TLS（跨平台 pthread ↔ Win32）。
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
#include <math.h>      /* libm 标量超越函数（expf/tanhf/logf/powf/sqrtf 等）：::libm C 域桥接用 */
#include <time.h>
#include <assert.h>     /* assert：ret 调用语法糖 !! func() 的失败中止 */
#include <inttypes.h>   /* PRId64 / PRIu64：64 位整数 printf 说明符跨平台适配（print 关键字用） */

#define SC_MACRO_STR_(x) #x
#define SC_MACRO_STR(x)  SC_MACRO_STR_(x)

#define SC_MACRO_CAT2_(a, b) a##b
#define SC_MACRO_CAT2(a, b) SC_MACRO_CAT2_(a, b)
#define SC_MACRO_CAT3_(a, b, c) a##b##c
#define SC_MACRO_CAT3(a, b, c) SC_MACRO_CAT3_(a, b, c)

/* ---------------- 平台判定 ---------------- */

/* 目标平台选择：默认按 C 编译器为其 target 预定义的宏自动判定
 * （_WIN32 / __APPLE__ / __linux__ 等）——用合适的交叉工具链（前缀 gcc 或
 * clang + -target）时这些宏即「目标」宏，平台分支本就面向目标平台。
 *
 * 但当 scc 仅按目标档 triple 解析平台行为（线程库/调试打包等）、而 C 编译器
 * 未带匹配的 target 选项时，预定义宏会回退到「宿主」，使 platform.h 的平台
 * 分支与 scc 的目标判定相互矛盾。为此 scc 在交叉目标下注入
 * SC_TARGET_{WIN,DARWIN,LINUX} 之一，令本头以「目标平台」为准；裸机/无 target
 * 选项的单一 clang 等场景也借此显式定向，不再回退宿主。
 *   未注入（本机构建/未知目标）→ 保持自动判定，行为完全不变。 */
#if defined(SC_TARGET_WIN) || defined(SC_TARGET_DARWIN) || defined(SC_TARGET_LINUX)
#define P_TARGET_EXPLICIT 1
#else
#define P_TARGET_EXPLICIT 0
#endif

/* defined(_WIN32) 也包含 defined(_WIN64) */
#if P_TARGET_EXPLICIT
#   if defined(SC_TARGET_WIN)
#   define P_WIN 1
#   else
#   define P_WIN 0
#   endif
#elif defined(_WIN32)
#define P_WIN 1
#else
#define P_WIN 0
#endif

/* __APPLE__ 说明是 Apple 平台; __MACH__ 说明内核是 Darwin */
#if P_TARGET_EXPLICIT
#   if defined(SC_TARGET_DARWIN)
#   define P_DARWIN 1
#   else
#   define P_DARWIN 0
#   endif
#elif defined(__APPLE__) && defined(__MACH__)
#define P_DARWIN 1
#else
#define P_DARWIN 0
#endif

/* 显式目标只在 WIN/DARWIN/LINUX 三族注入；BSD 仍走自动判定（显式时非 BSD） */
#if P_TARGET_EXPLICIT
#define P_BSD 0
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#define P_BSD 1
#else
#define P_BSD 0
#endif

/* Unix-like（macOS, BSD 等），__unix 是旧 gcc 兼容 */
#if P_TARGET_EXPLICIT
#   if defined(SC_TARGET_DARWIN) || defined(SC_TARGET_LINUX)
#   define P_UNIX 1
#   else
#   define P_UNIX 0
#   endif
#elif defined(__unix__) || defined(__unix)
#define P_UNIX 1
#else
#define P_UNIX 0
#endif

#if P_TARGET_EXPLICIT
#   if defined(SC_TARGET_LINUX)
#   define P_LINUX 1
#   else
#   define P_LINUX 0
#   endif
#elif defined(__linux__) || defined(__linux)
#define P_LINUX 1
#else
#define P_LINUX 0
#endif

/* ---------------- 路径分隔符 ---------------- */

/* 用 !P_WIN 而非 P_POSIX_LIKE：后者在本行之下方才定义（依赖 unistd.h 引入的
 * _POSIX_VERSION），此处尚不可见；P_WIN 已在上方平台判定中定型，与 P_IS_SEP 一致。 */
#if !P_WIN
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
/* WIN32_LEAN_AND_MEAN：令 <windows.h> 不拉入旧版 <winsock.h>（winsock1），
 * 以免与后续 <winsock2.h>（op/os/ssl 的异步内核与套接字）重复定义 sockaddr/select 等冲突。
 * 须在 <windows.h> 之前定义。 */
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   include <windows.h>
#endif
#if !P_WIN || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__)
#   include <unistd.h>
#   include <errno.h>
#endif
/* 注：POSIX 线程头 <pthread.h> / <sys/syscall.h> 不在此引入——
 * 互斥/条件变量/线程/屏障层已移至文末 P_MT_IMPL 延迟展开块（见文件底部），
 * 仅由 opt-in 的 builtins 实现 TU（mt_impl.c / op_impl.c）在包含本头前
 * #define P_MT_IMPL 触发，普通生成单元不引入线程层。 */

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
//  线程局部存储 
///////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////
// 动态库符号可见性（跨平台统一）
///////////////////////////////////////////////////////////////////////////////

#if P_WIN
#define SC_API_EXPORT __declspec(dllexport)
#define SC_API_IMPORT __declspec(dllimport)
#elif defined(__GNUC__) || defined(__clang__)
#define SC_API_EXPORT __attribute__((visibility("default")))
#define SC_API_IMPORT
#else
#define SC_API_EXPORT
#define SC_API_IMPORT
#endif

/* SC_API 统一导入/导出可见性（项目级约定）
 *
 * 用法（各库公开头）:
 *   1) 约定库前缀（如 WSI / UI / SURFACE / PLIB）
 *   2) 给出两个开关（通常在头内兜底为 0；由构建系统按需覆盖）
 *        <PREFIX>_SHARED   : 是否构建/使用共享库（0/1）
 *        <PREFIX>_EXPORTS  : 当前编译单元是否在“导出侧”（0/1）
 *   3) 定义 API 宏
 *        #define <PREFIX>_API SC_API(<PREFIX>)
 *
 * 目的:
 *   - 把“平台差异”（Windows 的 dllexport/dllimport 与 Unix 可见性）集中在 platform.h。
 *   - 把“模块差异”（不同库前缀）统一成同一套写法，避免每个库重复写条件分支。
 *   - 让头文件作者只关心 <PREFIX>_API，不关心底层平台细节。
 *
 * 原理:
 *   - SC_API(shared,exports) 先映射到 4 种状态：00/01/10/11 -> 空/导出/导入/导出。
 *   - SC_API(prefix) 通过前缀派生出 <PREFIX>_SHARED 与 <PREFIX>_EXPORTS，再走上面映射。
 *   - 其中 SC_MACRO_CAT3 是 token 拼接基础设施（把 prefix 与后缀拼成宏名）。
 *     采用两层 CAT 宏（*_ 与非 *_）确保“参数先展开，再 ## 拼接”。
 *
 * 最小示例（以 UI 为例）:
 *   #ifndef UI_SHARED
 *   #define UI_SHARED 0
 *   #endif
 *   #ifndef UI_EXPORTS
 *   #define UI_EXPORTS 0
 *   #endif
 *   #define UI_API SC_API(UI) */

#define SC_API_KIND_00
#define SC_API_KIND_01 SC_API_EXPORT
#define SC_API_KIND_10 SC_API_IMPORT
#define SC_API_KIND_11 SC_API_EXPORT

#define SC_API_FROM_FLAGS_(shared, exports) SC_MACRO_CAT3(SC_API_KIND_, shared, exports)
#define SC_API_FROM_FLAGS(shared, exports) SC_API_FROM_FLAGS_(shared, exports)

#define SC_API_SHARED_FLAG(prefix) SC_MACRO_CAT3(prefix, _, SHARED)
#define SC_API_EXPORTS_FLAG(prefix) SC_MACRO_CAT3(prefix, _, EXPORTS)
#define SC_API(prefix) SC_API_FROM_FLAGS(SC_API_SHARED_FLAG(prefix), SC_API_EXPORTS_FLAG(prefix))

///////////////////////////////////////////////////////////////////////////////
// 动态库加载（运行时按名装载共享库、取符号地址、卸载）
///////////////////////////////////////////////////////////////////////////////
// 跨平台封装 POSIX 的 dlopen/dlsym/dlclose ↔ Win32 的 LoadLibrary/GetProcAddress/
// FreeLibrary。各处运行时装载（wsi 按需装载 X11/Wayland 扩展库、gpu 的 Vulkan/GL
// 加载器等）统一经此，不再各自 #ifdef。
//   - 模块句柄为不透明 void*（NULL 表示失败）；
//   - 符号取回为通用函数指针 P_dlproc，调用方按目标原型强转（ISO C 保证函数指针
//     互转、转回原型后调用合法；POSIX/Win32 亦保证 dlsym/GetProcAddress 返回值
//     可用作函数指针）。
//   - <windows.h> 已由上方平台基础头在 P_WIN 下带入；此处补 POSIX 的 <dlfcn.h>。

typedef void (*P_dlproc)(void);

#if !P_WIN
#   include <dlfcn.h>
#endif

static inline void* P_dl_load(const char* path) {
#if P_WIN
    return (void*) LoadLibraryA(path);
#else
    return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
#endif
}

static inline void P_dl_unload(void* module) {
#if P_WIN
    FreeLibrary((HMODULE) module);
#else
    dlclose(module);
#endif
}

static inline P_dlproc P_dl_get_proc(void* module, const char* symbol) {
#if P_WIN
    return (P_dlproc) GetProcAddress((HMODULE) module, symbol);
#else
    return (P_dlproc) dlsym(module, symbol);
#endif
}

///////////////////////////////////////////////////////////////////////////////
// 启动 / 退出钩子（constructor / destructor）
///////////////////////////////////////////////////////////////////////////////

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

/* 跨平台 typeof（P_TYPEOF）：把编译器专有的 typeof token 收敛到本可移植层，
 * 避免 __typeof__ 等扩展 token 散落各头文件（如 op.h 的 SC_PTRCHK 保型转换）。
 *   - gcc/clang：__typeof__（双下划线形式，不触发 -Wlanguage-extension-token）
 *   - C++：decltype（标准）
 *   - C23：标准 typeof
 *   - 其余（含 VS2022 17.9+ MSVC C）：回退 __typeof__ */
#if defined(__GNUC__) || defined(__clang__)
#  define P_TYPEOF(x) __typeof__(x)
#elif defined(__cplusplus)
#  define P_TYPEOF(x) decltype(x)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#  define P_TYPEOF(x) typeof(x)
#else
#  define P_TYPEOF(x) __typeof__(x)
#endif

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

/* ---------------- 时间与时钟 ---------------- */

/* 统一时钟值类型 clk_t（timespec 封装，无堆分配）。
 * 不叫 clock：会与 libc 的 clock() 函数（time.h）重定义；
 * 也不叫 clock_t：会与 <time.h> 标准 typedef clock_t 冲突（重定义为不同类型）。
 * sc 侧 op.sc 的 @def clock 以 h: ::clk_t 内联持有本类型，采集/换算方法
 * （sc_clock_*）为 op.h 内 static inline 薄封装，直转本段 P_* 与 clock_* 宏。 */
#if P_POSIX_LIKE
typedef struct timespec clk_t;
#else
typedef struct { time_t tv_sec; long tv_nsec; } clk_t;
#endif

/* 墙钟（系统实时时间，受调时影响），成功返回 0 */
static inline int P_time_now(clk_t* clk) {
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
static inline int P_clock_now(clk_t* clk) {
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
static inline int P_cost_now(clk_t* clk, bool process_or_thread) {
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

/* 单调时钟快照（秒/毫秒/微秒，uint64）与差值。便捷计时无需构造 clock 对象；
 * 直接经 op.sc 的 @fnc tick_s/ms/us/tick_diff 暴露给 sc（scc 生成 sc_tick_* 调用，
 * 解析到本处 static inline，零开销）。tick_diff 用函数而非宏：避免宏对实参的多次求值。 */
static inline uint64_t sc_tick_s(void)  { clk_t _clk; P_clock_now(&_clk); return (uint64_t)clock_s(_clk); }
static inline uint64_t sc_tick_ms(void) { clk_t _clk; P_clock_now(&_clk); return (uint64_t)clock_ms(_clk); }
static inline uint64_t sc_tick_us(void) { clk_t _clk; P_clock_now(&_clk); return (uint64_t)clock_us(_clk); }

static inline uint64_t sc_tick_diff(uint64_t now, uint64_t nlast) { return now > nlast ? now - nlast : 0; }

///////////////////////////////////////////////////////////////////////////////
// 日志：终端着色 + 系统日志（跨平台自适应，参考 stdc）
///////////////////////////////////////////////////////////////////////////////
// print 关键字运行时（op_impl.c）据此着色 stdout / 写系统日志。
// 级别/通道 1..6 = F/E/W/I/D/V（与 op.h enum、op.sc def log 对齐）。

/* ---------------- 控制台 UTF-8 ---------------- */
/* sc 程序内部一律以 UTF-8 字节输出，但 Windows 控制台默认代码页为本地 ANSI
 * （简体中文为 GBK/936），直接 printf UTF-8 会显示为乱码。SC_CONSOLE_UTF8() 在
 * 程序入口把本进程控制台的输出/输入代码页切到 UTF-8（65001），令 UTF-8 字节正确
 * 显示与读取；非 Windows 平台为空操作。
 *   - 仅作用于当前进程的控制台句柄；stdout 被重定向到文件/管道时不影响其字节。
 *   - <windows.h> 已由上方平台基础头在 P_WIN 下带入，SetConsole*CP 直接可用。 */
#if P_WIN
#define SC_CONSOLE_UTF8() do { SetConsoleOutputCP(65001u); SetConsoleCP(65001u); } while (0)
#else
#define SC_CONSOLE_UTF8() ((void)0)
#endif

/* 终端检测：判断流是否连到 tty（print 据此决定是否着色，重定向/管道则纯文本） */
#if P_WIN
#   include <io.h>
#   define P_isatty(f) _isatty(_fileno(f))
#else
#   define P_isatty(f) isatty(fileno(f))
#endif

/* ANSI 前景色码（按级别取用；I=状态用默认色，无码）。仅在 tty 输出时启用。 */
#define P_ANSI_RESET   "\033[0m"
#define P_ANSI_PURPLE  "\033[35m"   /* F 致命 */
#define P_ANSI_RED     "\033[31m"   /* E 错误 */
#define P_ANSI_YELLOW  "\033[33m"   /* W 警告 */
#define P_ANSI_CYAN    "\033[36m"   /* D 调试 */
#define P_ANSI_GRAY    "\033[90m"   /* V 详尽 */

/* 系统日志（跨平台自适应）：把一行文本写入各平台系统日志设施。
 *   level：1..6 = F/E/W/I/D/V；  tag：日志标签（NULL/空 → "sc"）；
 *   text ：单行文本（不含级别/颜色修饰，末尾无换行）。
 * 平台映射：Win=OutputDebugStringA / macOS=os_log / Android=logcat /
 *          QNX=slog2 / Linux·BSD=syslog。实现体较重（含各平台专属头），仅在定义
 *          P_LOG_SYS_IMPL 的翻译单元（op_impl.c）内展开；其余 TU 不引入。
 * 注：实现体置于本头「主 include guard 之外」的末尾（见文件底部），因 op_impl.c 常在
 *     op.h 已先行带入 platform.h 之后才 #define P_LOG_SYS_IMPL 重包含本头——主体被
 *     guard 跳过，唯有 guard 外的实现块能据 P_LOG_SYS_IMPL 延迟展开。 */

//------------------  op.sc 机制运行时（默认带入）  ----------------------
// op.sc 为默认导入的语法机制声明模块；op.h 是其 C 侧伴随头（chain 等
// 机制的结构体与运行时原型）。随 platform.h 一同进入每个生成的 C 单元。
#include "op.h"

#endif /* SC_PLATFORM_H */

///////////////////////////////////////////////////////////////////////////////
// 系统日志实现（延迟展开，置于主 include guard 之外）
///////////////////////////////////////////////////////////////////////////////
// 仅当 TU 定义了 P_LOG_SYS_IMPL（op_impl.c）时展开；一次性守卫，独立于 SC_PLATFORM_H。
// 依赖的平台判定宏（P_WIN/P_DARWIN/...）由上方主体首次包含时已定义并留存。
#if defined(P_LOG_SYS_IMPL) && !defined(SC_PLATFORM_LOG_SYS_DONE)
#define SC_PLATFORM_LOG_SYS_DONE

#if P_WIN
    /* <windows.h> 已由主体在 P_WIN 下带入，OutputDebugStringA 直接可用 */
#elif P_DARWIN
#   include <os/log.h>
#elif defined(__ANDROID__)
#   include <android/log.h>
#elif defined(__QNX__)
#   include <sys/slog2.h>
#elif P_LINUX || P_BSD
#   include <syslog.h>
#endif

static void P_log_sys(int level, const char *tag, const char *text) {
    if (!text) return;
    if (level < 1) level = 1;
    if (level > 6) level = 6;
    const int i = level - 1;                 /* 0=F 1=E 2=W 3=I 4=D 5=V */
    if (!tag || !*tag) tag = "sc";
#if P_WIN
    char buf[4096];
    snprintf(buf, sizeof(buf), "[%s] %s\n", tag, text);
    OutputDebugStringA(buf);
#elif P_DARWIN
    static const os_log_type_t s_lv[6] = {
        OS_LOG_TYPE_FAULT,   /* F */ OS_LOG_TYPE_ERROR,   /* E */
        OS_LOG_TYPE_DEFAULT, /* W */ OS_LOG_TYPE_INFO,    /* I */
        OS_LOG_TYPE_DEBUG,   /* D */ OS_LOG_TYPE_DEBUG    /* V */
    };
    static os_log_t s_h;
    if (!s_h) s_h = os_log_create(tag, "sc");
    os_log_with_type(s_h, s_lv[i], "%{public}s", text);
#elif defined(__ANDROID__)
    static const int s_lv[6] = {
        ANDROID_LOG_FATAL, ANDROID_LOG_ERROR, ANDROID_LOG_WARN,
        ANDROID_LOG_INFO,  ANDROID_LOG_DEBUG, ANDROID_LOG_VERBOSE
    };
    __android_log_write(s_lv[i], tag, text);
#elif defined(__QNX__)
    static const int s_lv[6] = {
        SLOG2_CRITICAL, SLOG2_ERROR, SLOG2_WARNING,
        SLOG2_INFO,     SLOG2_DEBUG1, SLOG2_DEBUG2
    };
    static slog2_buffer_t s_h;
    if (!s_h) {
        slog2_buffer_set_config_t cfg;
        cfg.buffer_set_name = tag;
        cfg.num_buffers = 1;
        cfg.verbosity_level = SLOG2_DEBUG2;
        cfg.buffer_config[0].buffer_name = "main";
        cfg.buffer_config[0].num_pages = 8;
        slog2_register(&cfg, &s_h, 0);
    }
    if (s_h) slog2c(s_h, 0, s_lv[i], text);
#elif P_LINUX || P_BSD
    static const int s_lv[6] = {
        LOG_CRIT, LOG_ERR, LOG_WARNING, LOG_INFO, LOG_DEBUG, LOG_DEBUG
    };
    static int s_opened = 0;
    if (!s_opened) { openlog(tag, LOG_CONS | LOG_PID, LOG_USER); s_opened = 1; }
    syslog(s_lv[i], "%s", text);
#else
    (void)i;
    fprintf(stderr, "[%s] %s\n", tag, text);
#endif
}

#endif /* P_LOG_SYS_IMPL && !SC_PLATFORM_LOG_SYS_DONE */

///////////////////////////////////////////////////////////////////////////////
// 随机数实现（延迟展开，置于主 include guard 之外）
///////////////////////////////////////////////////////////////////////////////
// 仅当 TU 定义 P_RAND_IMPL（op_impl.c）时展开；一次性守卫 SC_PLATFORM_RAND_DONE，
// 独立于 SC_PLATFORM_H。平台判定宏（P_WIN/P_DARWIN/...）与原子层（sc_test_and_set）
// 由主体首次包含时已定义并留存。集中到 op_impl.c 单 TU 落盘：降级种子的 static 状态
// 进程内仅一份、仅播种一次（原子 CAS 保线程安全），且系统 CSPRNG 一次填满整块缓冲
// （消除旧实现在 Linux 每次调用都 open/close /dev/urandom 的开销）。与日志 P_LOG_SYS_IMPL
// 同款延迟落盘；由 op 运行时 sc_rand_*（op.h）薄封装转调，其余模块只调 sc_rand_*。
#if defined(P_RAND_IMPL) && !defined(SC_PLATFORM_RAND_DONE)
#define SC_PLATFORM_RAND_DONE

#if P_WIN
/* RtlGenRandom（SystemFunction036）：系统 CSPRNG，一次填满缓冲、无需种子/初始化。
 * 无公开头文件，按 SDK 惯例手动声明；MSVC 自动链接 advapi32，MinGW 需 -ladvapi32。 */
BOOLEAN NTAPI SystemFunction036(PVOID RandomBuffer, ULONG RandomBufferLength);
#   if defined(_MSC_VER)
#       pragma comment(lib, "advapi32.lib")
#   endif
#elif P_LINUX
#   include <sys/syscall.h>   /* SYS_getrandom（Linux 3.17+；缺失/失败则回退 /dev/urandom） */
#endif

/* 末级兜底：一次性播种 libc rand()（仅当系统 CSPRNG 全部不可用才触发，近乎不发生）。
 * 用主体已提供的跨平台原子 CAS（sc_test_and_set）保证进程内仅播种一次、线程安全。 */
static inline void P_rand_seed_once(void) {
    static int g_rand_seeded = 0;
    int expected = 0;
    if (sc_test_and_set(&g_rand_seeded, &expected, 1))
        srand((unsigned int)((uintptr_t)&g_rand_seeded ^ (uintptr_t)time(NULL)));
}

/* 用系统 CSPRNG 一次填满整个缓冲区（无 per-call 开销、无「种子」概念）：
 *   mac/BSD → arc4random_buf · Windows → RtlGenRandom · Linux → getrandom→/dev/urandom。
 * 仅当系统源全部失败才逐字节降级到 rand()。 */
static void P_rand_bytes(void *buf, size_t len) {
    if (!buf || len == 0) return;
    uint8_t *p = (uint8_t *)buf;
#if P_DARWIN || P_BSD
    arc4random_buf(p, len);
#elif P_WIN
    size_t off = 0;
    while (off < len) {
        ULONG chunk = (len - off > 0xFFFFFFFFul) ? 0xFFFFFFFFul : (ULONG)(len - off);
        if (!SystemFunction036(p + off, chunk)) break;
        off += chunk;
    }
    if (off < len) {                       /* 系统源失败 → 逐字节降级 */
        P_rand_seed_once();
        for (; off < len; off++) p[off] = (uint8_t)(rand() & 0xFF);
    }
#else /* Linux / 其他 POSIX */
    size_t off = 0;
#   if defined(SYS_getrandom)
    while (off < len) {
        long n = syscall(SYS_getrandom, p + off, len - off, 0);
        if (n > 0) { off += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        break;                             /* getrandom 不可用/出错 → 回退 urandom */
    }
#   endif
    if (off < len) {                       /* 一次性读满 /dev/urandom */
        FILE *fp = fopen("/dev/urandom", "rb");
        if (fp) { off += fread(p + off, 1, len - off, fp); fclose(fp); }
    }
    if (off < len) {                       /* 系统源全部失败 → 逐字节降级 */
        P_rand_seed_once();
        for (; off < len; off++) p[off] = (uint8_t)(rand() & 0xFF);
    }
#endif
}

/* 32/64 位随机整数：一律基于 P_rand_bytes，保证非零（0 保留为无效值）。 */
static inline uint32_t P_rand32(void) { uint32_t r; P_rand_bytes(&r, sizeof r); return r ? r : 1u; }
static inline uint64_t P_rand64(void) { uint64_t r; P_rand_bytes(&r, sizeof r); return r ? r : 1u; }

#endif /* P_RAND_IMPL && !SC_PLATFORM_RAND_DONE */

///////////////////////////////////////////////////////////////////////////////
// 互斥 / 条件变量 / 线程 / 屏障 / 动态 TLS（延迟展开，置于主 include guard 之外）
///////////////////////////////////////////////////////////////////////////////
// 仅当 TU 在包含本头前定义了 P_MT_IMPL 时展开（mt_impl.c / op_impl.c 等 builtins
// 实现 opt-in），避免给不用线程的普通生成单元凭空拉入 <pthread.h> 等线程头。与 socket
// 的 SC_WITH_SOCKET、系统日志的 P_LOG_SYS_IMPL 同款做法。一次性守卫 SC_PLATFORM_MT_DONE，
// 独立于 SC_PLATFORM_H；依赖的平台判定宏（P_WIN/P_DARWIN/P_LINUX/...）由主体首次包含时
// 已定义并留存。
//
// 命名：类型为 mutex_t / cond_t / barrier_t / tls_t（平台句柄类型，无 sc_ 前缀——sc
// 无法暴露 C 侧类型声明，故本层类型仅经 '::' 逃逸供 mt 的 @def 直接作字段）；操作一律
// P_ 前缀（平台适配层，非 sc 命名域）——sc 命名域的 mutex/cond/barrier 方法由 mt 模块
//（mt.h / mt_impl.c）以 sc_mutex_* 等薄包装转调本层 P_* 提供。动态 TLS（tls_t/P_tls_*，
// pthread_key ↔ TlsAlloc）作为跨平台原语一并收录，与主体编译期 TLS 宏互补。
#if defined(P_MT_IMPL) && !defined(SC_PLATFORM_MT_DONE)
#define SC_PLATFORM_MT_DONE

#if !P_WIN
#   include <pthread.h>          /* 互斥/条件变量/线程：POSIX 后端 */
#   if P_LINUX
#       include <sys/syscall.h>  /* SYS_gettid（线程 id） */
#   endif
#endif

#if P_WIN
typedef CRITICAL_SECTION mutex_t;
#define P_mutex_init(pm)    InitializeCriticalSection(pm)
#define P_mutex_final(pm)   DeleteCriticalSection(pm)
#define P_mutex_lock(pm)    EnterCriticalSection(pm)
#define P_mutex_unlock(pm)  LeaveCriticalSection(pm)
#define P_mutex_try(pm)     (TryEnterCriticalSection(pm) ? 1 : 0)   /* 成功 1 / 否则 0 */

typedef CONDITION_VARIABLE cond_t;
#define P_cond_init(pc)     InitializeConditionVariable(pc)
#define P_cond_final(pc)    ((void)0)                               /* Win 条件变量无需销毁 */
#define P_cond_one(pc)      WakeConditionVariable(pc)
#define P_cond_all(pc)      WakeAllConditionVariable(pc)
#else
typedef pthread_mutex_t mutex_t;
#define P_mutex_init(pm)    pthread_mutex_init(pm, NULL)
#define P_mutex_final(pm)   pthread_mutex_destroy(pm)
#define P_mutex_lock(pm)    pthread_mutex_lock(pm)
#define P_mutex_unlock(pm)  pthread_mutex_unlock(pm)
#define P_mutex_try(pm)     (pthread_mutex_trylock(pm) == 0 ? 1 : 0) /* 成功 1 / 否则 0 */

typedef pthread_cond_t cond_t;
#define P_cond_init(pc)     pthread_cond_init(pc, NULL)
#define P_cond_final(pc)    pthread_cond_destroy(pc)
#define P_cond_one(pc)      pthread_cond_signal(pc)
#define P_cond_all(pc)      pthread_cond_broadcast(pc)
#endif

/* 条件等待：nsec/sec 全 0 → 无限等待；否则相对超时（sec 秒 + nsec 纳秒）。
 * 调用前须持有 pm。返回 0=被唤醒 / 1=超时 / -1=错误。 */
static inline int P_cond_wait(cond_t* pc, mutex_t* pm, uint64_t nsec, uint64_t sec) {
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
static inline uint64_t P_thread_id(void) {
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

/* 动态线程局部存储（运行时 key 式 TLS，pthread_key_t ↔ TlsAlloc）。
 * 与主体的编译期 TLS 存储类修饰符互补：TLS 宏（__thread/_Thread_local/__declspec）
 * 适合「编译期已知的固定线程局部变量」；本 tls_t/P_tls_* 则适合「运行时按需创建/销毁
 * 槽位」（槽数或生命周期需运行时决定，或槽位随对象动态分配）。全部 0=成功 / -1=失败。 */
#if P_WIN
typedef DWORD tls_t;
static inline int  P_tls_create(tls_t* key)       { *key = TlsAlloc(); return *key == TLS_OUT_OF_INDEXES ? -1 : 0; }
static inline void P_tls_final(tls_t* key)        { TlsFree(*key); }
static inline void* P_tls_get(tls_t key)          { return TlsGetValue(key); }
static inline void P_tls_set(tls_t key, void* v)  { TlsSetValue(key, v); }
#else
typedef pthread_key_t tls_t;
static inline int  P_tls_create(tls_t* key)       { return pthread_key_create(key, NULL) == 0 ? 0 : -1; }
static inline void P_tls_final(tls_t* key)        { pthread_key_delete(*key); }
static inline void* P_tls_get(tls_t key)          { return pthread_getspecific(key); }
static inline void P_tls_set(tls_t key, void* v)  { pthread_setspecific(key, v); }
#endif

/* 屏障：N 方汇合。自实现（mutex + cond），因 macOS 无 pthread_barrier_t、
 * Windows 无 pthread；代际 phase 防本轮唤醒被下一轮抢用，并天然抗虚假唤醒。 */
typedef struct {
    mutex_t mu;
    cond_t  cv;
    uint32_t   total;    /* 需汇合的线程数 */
    uint32_t   count;    /* 本代已到达数 */
    uint32_t   phase;    /* 代际，每满一轮翻转 */
} barrier_t;

static inline void P_barrier_init(barrier_t* b, uint32_t n) {
    P_mutex_init(&b->mu);
    P_cond_init(&b->cv);
    b->total = n ? n : 1;
    b->count = 0;
    b->phase = 0;
}

static inline void P_barrier_final(barrier_t* b) {
    P_mutex_final(&b->mu);
    P_cond_final(&b->cv);
}

/* 汇合点：阻塞至 total 个线程全部到达。返回非 0 表示本线程为最后到达者
 * （对应 PTHREAD_BARRIER_SERIAL_THREAD，可用于选一个线程做收尾工作）。 */
static inline int P_barrier_wait(barrier_t* b) {
    P_mutex_lock(&b->mu);
    uint32_t ph = b->phase;
    if (++b->count == b->total) {
        b->phase++;                 /* 进入下一代 */
        b->count = 0;
        P_cond_all(&b->cv);        /* 放行全部 */
        P_mutex_unlock(&b->mu);
        return 1;                   /* serial thread */
    }
    while (ph == b->phase)          /* 等代际翻转，抗虚假唤醒/跨轮抢用 */
        P_cond_wait(&b->cv, &b->mu, 0, 0);
    P_mutex_unlock(&b->mu);
    return 0;
}

#endif /* P_MT_IMPL && !SC_PLATFORM_MT_DONE */

///////////////////////////////////////////////////////////////////////////////
// Socket 跨平台层（延迟展开，置于主 include guard 之外；参考 stdc 网络段）
///////////////////////////////////////////////////////////////////////////////
// 仅当 TU 在包含本头前定义了 SC_WITH_SOCKET 时展开（io/ssh 等套接字模块 opt-in），
// 避免给不用网络的普通生成单元凭空拉入 winsock2 等重头文件。一次性守卫 SC_SOCKET_DONE，
// 独立于 SC_PLATFORM_H：主体首次包含时已定义 P_WIN/P_DARWIN/... 并留存，此处直接复用。
//
// 命名统一 sc_ 前缀；返回约定为 sc 风格：
//   · 选项设置/close/nonblock/connect → int（0=成功 / -1=失败）
//   · 错误谓词                        → bool
//   · 单次收发 recv/send             → ssize_t（实收发字节数；recv 返回 0=对端关闭；-1=失败）
//   · 满额非阻塞收发 recv/send_nonblock → int 状态（0=已满额 / 1=未就绪待重试 / -1=错误 / -2=对端关闭）
//   · 分散发送 sendmsg               → ssize_t（已发送字节数 / -1=失败）
// bind/listen/accept/getaddrinfo 等各平台同名同义，直接用，只统一类型即可。
// 仅提供 socket 原语适配；host:port 解析建连（getaddrinfo）等 compound 逻辑属应用层，见 sys 模块。
// 说明：sendmsg 封装用函数内 static 惰性缓存 WSASendMsg 扩展指针（各 TU 各持一份，天然免重定义，
//       不再需 stdc 那样的头内 extern 定义）；非阻塞满额收发用自洽状态码，不依赖 stdc 专有 E_*。
#if defined(SC_WITH_SOCKET) && !defined(SC_SOCKET_DONE)
#define SC_SOCKET_DONE

#if P_WIN
/* <windows.h> 已由主体在 P_WIN 下带入（WIN32_LEAN_AND_MEAN，未拉 winsock1），
 * 此处再引 winsock2/ws2tcpip，顺序正确不冲突。 */
#   include <winsock2.h>
#   include <ws2tcpip.h>
#   include <mswsock.h>                 /* WSASendMsg / WSAID_WSASENDMSG（sc_sendmsg 用） */
#   if defined(_MSC_VER)
#       pragma comment(lib, "ws2_32.lib")   /* MSVC 自动链接；MinGW 需 -lws2_32 */
#   endif
typedef SOCKET sc_sock;
#   define SC_SOCK_INVALID  INVALID_SOCKET
#   define SC_SOCK_ERROR    SOCKET_ERROR
#   if defined(_MSC_VER) && !defined(_SSIZE_T_DEFINED)
typedef intptr_t ssize_t;            /* MSVC 无 ssize_t（MinGW 自带） */
#       define _SSIZE_T_DEFINED
#   endif
#else
#   include <sys/socket.h>
#   include <sys/types.h>
#   include <netinet/in.h>
#   include <netinet/tcp.h>
#   include <arpa/inet.h>
#   include <netdb.h>
#   include <fcntl.h>
#   include <unistd.h>
#   include <errno.h>
typedef int sc_sock;
#   define SC_SOCK_INVALID  (-1)
#   define SC_SOCK_ERROR    (-1)
#   ifndef MSG_NOSIGNAL
#       define MSG_NOSIGNAL 0        /* macOS/BSD 无此标志（改用 SO_NOSIGPIPE），置 0 兜底 */
#   endif
#endif

/* 网络子系统初始化/清理（仅 Windows WSAStartup 需要；POSIX 为空操作，幂等）。 */
static inline int sc_net_init(void) {
#if P_WIN
    static int s_done = 0;
    if (s_done) return 0;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
    s_done = 1;
#endif
    return 0;
}
static inline void sc_net_cleanup(void) {
#if P_WIN
    WSACleanup();
#endif
}

/* 最近一次套接字错误码（Win=WSAGetLastError / POSIX=errno）。 */
static inline int sc_errno(void) {
#if P_WIN
    return WSAGetLastError();
#else
    return errno;
#endif
}

/* 错误谓词：非阻塞连接进行中 / 暂无数据(缓冲满) / 连接被重置 / 被信号中断。 */
static inline bool sc_is_inprogress(void) {
#if P_WIN
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
    return errno == EINPROGRESS || errno == EWOULDBLOCK;
#endif
}
static inline bool sc_is_wouldblock(void) {
#if P_WIN
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}
static inline bool sc_is_connreset(void) {
#if P_WIN
    return WSAGetLastError() == WSAECONNRESET;
#else
    return errno == ECONNRESET;
#endif
}
static inline bool sc_is_interrupted(void) {
#if P_WIN
    return WSAGetLastError() == WSAEINTR;
#else
    return errno == EINTR;
#endif
}

/* 关闭套接字（Win=closesocket / POSIX=close）。0=成功 / -1=失败。 */
static inline int sc_close(sc_sock s) {
    if (s == SC_SOCK_INVALID) return -1;
#if P_WIN
    return closesocket(s) == 0 ? 0 : -1;
#else
    return close(s) == 0 ? 0 : -1;
#endif
}

/* 设置非阻塞模式。0=成功 / -1=失败。 */
static inline int sc_nonblock(sc_sock s, bool enable) {
    if (s == SC_SOCK_INVALID) return -1;
#if P_WIN
    u_long mode = enable ? 1u : 0u;
    return ioctlsocket(s, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    int fl = fcntl(s, F_GETFL, 0);
    if (fl < 0) return -1;
    if (enable) fl |= O_NONBLOCK;
    else        fl &= ~O_NONBLOCK;
    return fcntl(s, F_SETFL, fl) == 0 ? 0 : -1;
#endif
}

/* 套接字选项（全部 0=成功 / -1=失败）。setsockopt 值指针在 Win 需 (const char*)。 */
static inline int sc_reuseaddr(sc_sock s, bool on) {
    int opt = on ? 1 : 0;
    return setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof opt) == 0 ? 0 : -1;
}
#if !P_WIN
static inline int sc_reuseport(sc_sock s, bool on) {   /* 仅 POSIX 有 SO_REUSEPORT */
    int opt = on ? 1 : 0;
    return setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof opt) == 0 ? 0 : -1;
}
#endif
static inline int sc_nodelay(sc_sock s, bool on) {     /* 禁用 Nagle */
    int opt = on ? 1 : 0;
    return setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&opt, sizeof opt) == 0 ? 0 : -1;
}
static inline int sc_keepalive(sc_sock s, bool on) {
    int opt = on ? 1 : 0;
    return setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (const char *)&opt, sizeof opt) == 0 ? 0 : -1;
}
static inline int sc_sndtimeo(sc_sock s, int ms) {
#if P_WIN
    DWORD tv = (DWORD)ms;
    return setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof tv) == 0 ? 0 : -1;
#else
    struct timeval tv; tv.tv_sec = ms / 1000; tv.tv_usec = (ms % 1000) * 1000;
    return setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv) == 0 ? 0 : -1;
#endif
}
static inline int sc_rcvtimeo(sc_sock s, int ms) {
#if P_WIN
    DWORD tv = (DWORD)ms;
    return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv) == 0 ? 0 : -1;
#else
    struct timeval tv; tv.tv_sec = ms / 1000; tv.tv_usec = (ms % 1000) * 1000;
    return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) == 0 ? 0 : -1;
#endif
}
static inline int sc_sndbuf(sc_sock s, int size) {
    return setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char *)&size, sizeof size) == 0 ? 0 : -1;
}
static inline int sc_rcvbuf(sc_sock s, int size) {
    return setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char *)&size, sizeof size) == 0 ? 0 : -1;
}

/* 发起连接：0=已连接 / -1=失败。非阻塞套接字连接进行中亦返回 -1，
 * 用 sc_is_inprogress() 甄别（随后 writable 即表示连接完成/失败）。 */
static inline int sc_connect(sc_sock s, const struct sockaddr *addr, socklen_t len) {
    return connect(s, addr, len) == 0 ? 0 : -1;
}

/* 单次收发：屏蔽 Win 的 char* / int 形参差异与 POSIX 的 SIGPIPE（MSG_NOSIGNAL）。
 * 返回实际收发字节数；recv 返回 0 表示对端正常关闭；-1=失败（码见 sc_errno / 谓词）。 */
static inline ssize_t sc_recv(sc_sock s, void *buf, size_t len) {
#if P_WIN
    return recv(s, (char *)buf, (int)len, 0);
#else
    return recv(s, buf, len, 0);
#endif
}
static inline ssize_t sc_send(sc_sock s, const void *buf, size_t len) {
#if P_WIN
    return send(s, (const char *)buf, (int)len, 0);
#else
    return send(s, buf, len, MSG_NOSIGNAL);
#endif
}

/* 非阻塞满额收：循环 recv 直至读满 *r_sz 或缓冲耗尽；EINTR 自动重试。
 * 入参 *r_sz=期望字节数，返回前回填为实读字节数。返回：
 *   0  = 已读满期望字节数
 *   1  = 未就绪（EWOULDBLOCK，尚未读满，稍后就绪时以剩余量续读）
 *  -1  = 出错
 *  -2  = 对端关闭且一字节未读到（读到部分后关闭则回填 *r_sz 并返回 1）*/
static inline int sc_recv_nonblock(sc_sock s, void *buf, size_t *r_sz) {
    size_t want = *r_sz; *r_sz = 0;
    while (*r_sz < want) {
        ssize_t n = sc_recv(s, (char *)buf + *r_sz, want - *r_sz);
        if (n == 0) return *r_sz > 0 ? 1 : -2;      /* 对端关闭 */
        if (n < 0) {
            if (sc_is_interrupted()) continue;
            if (sc_is_wouldblock())  return 1;
            return -1;
        }
        *r_sz += (size_t)n;
    }
    return 0;
}

/* 非阻塞满额发：循环 send 直至写完 *w_sz 或缓冲满；EINTR 自动重试。
 * 入参 *w_sz=待发字节数，返回前回填为实发字节数。返回：
 *   0  = 已全部发出
 *   1  = 未就绪（EWOULDBLOCK，尚有剩余，稍后就绪时以剩余量续发）
 *  -1  = 出错 */
static inline int sc_send_nonblock(sc_sock s, const void *buf, size_t *w_sz) {
    size_t want = *w_sz; *w_sz = 0;
    while (*w_sz < want) {
        ssize_t n = sc_send(s, (const char *)buf + *w_sz, want - *w_sz);
        if (n < 0) {
            if (sc_is_interrupted()) continue;
            if (sc_is_wouldblock())  return 1;
            return -1;
        }
        *w_sz += (size_t)n;
    }
    return 0;
}

/* 分散发送（scatter/gather）：跨平台统一 iovec 抽象，addr 可为 NULL（已连接套接字）
 * 或指向目标地址（无连接 UDP 语义）。POSIX=struct iovec+sendmsg，Win=WSABUF+WSASendMsg。
 * Windows 首次调用惰性解析 WSASendMsg 扩展函数指针（函数内 static 缓存，各 TU 各持一份，
 * 天然免多 TU 重定义）。返回已发送字节数 / -1=失败。 */
#if P_WIN
typedef WSABUF sc_iovec;
static inline void sc_iovec_set(sc_iovec *v, const void *buf, size_t len) {
    v->buf = (char *)buf; v->len = (ULONG)len;
}
#else
typedef struct iovec sc_iovec;
static inline void sc_iovec_set(sc_iovec *v, const void *buf, size_t len) {
    v->iov_base = (void *)buf; v->iov_len = len;
}
#endif
static inline ssize_t sc_sendmsg(sc_sock s, const sc_iovec *iov, size_t n,
                                 const struct sockaddr *addr, socklen_t addrlen) {
#if P_WIN
    static LPFN_WSASENDMSG fn = NULL;
    if (!fn) {
        GUID guid = WSAID_WSASENDMSG; DWORD bytes = 0;
        if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER,
                     &guid, sizeof guid, &fn, sizeof fn, &bytes, NULL, NULL) != 0)
            return -1;
    }
    WSAMSG mh = {0};
    mh.name          = (LPSOCKADDR)addr;
    mh.namelen       = addrlen;
    mh.lpBuffers     = (LPWSABUF)iov;
    mh.dwBufferCount = (DWORD)n;
    DWORD sent = 0;
    return fn(s, &mh, 0, &sent, NULL, NULL) == 0 ? (ssize_t)sent : -1;
#else
    struct msghdr mh = {0};
    mh.msg_name    = (void *)addr;
    mh.msg_namelen = addrlen;
    mh.msg_iov     = (struct iovec *)iov;
    mh.msg_iovlen  = n;
    return sendmsg(s, &mh, MSG_NOSIGNAL);
#endif
}

#endif /* SC_WITH_SOCKET && !SC_SOCKET_DONE */
