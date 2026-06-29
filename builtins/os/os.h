/* os.h —— sc 操作系统基本操作的 C ABI 契约（与 builtins/os/os.sc 同步维护） */
#ifndef SC_OS_H
#define SC_OS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* net_socketpair：建一对已连接本地套接字，填 fds[0]/fds[1]；成功 0 / 失败 -1。
 * POSIX 用 socketpair(AF_UNIX)；Windows 用 127.0.0.1 回环 listen/connect/accept 模拟。 */
int32_t net_socketpair(int32_t *fds);

/* os_rand：填充 n 字节密码学强随机到 buf；返回 0。
 * 转发 platform.h 的 P_rand_bytes（CSPRNG）：arc4random / rand_s / /dev/urandom。 */
int32_t os_rand(void *buf, uint32_t n);

/* （待实现：fs_*（文件/目录/路径）/ env_*（环境变量）/ proc_*（进程）等基本操作） */

#ifdef __cplusplus
}
#endif

#endif /* SC_OS_H */
