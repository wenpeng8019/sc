/* os_impl.c —— sc 操作系统基本操作（os.h 契约）默认实现
 * 跨平台经由 builtins/platform.h
 */
#include "os.h"
#include "platform.h"

/* CPU 逻辑核数（至少返回 1）。跨平台分支经 platform.h（P_WIN/POSIX） */
uint32_t sc_ncpu(void) {
#if P_WIN
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors > 0 ? (uint32_t)si.dwNumberOfProcessors : 1;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (uint32_t)n : 1;
#endif
}

/* （待实现：网卡/防火墙/路由等系统管理查询；fs_*（文件/目录/路径）/ env_*（环境变量）/
 *   proc_*（进程）等基本操作。应用网络套接字已迁至 sys 模块（sock_*）。） */
