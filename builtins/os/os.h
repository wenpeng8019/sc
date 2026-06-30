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

/* net_connect：对外建立 TCP 连接（阻塞）。getaddrinfo 解析 host:port（IPv4/IPv6 通吃），
 * 逐候选 socket+connect，成功返回已连接阻塞 fd（调用方可 io.tcp(fd,0,1,1) 包成同步 com，
 * 或叠 ssl_com 做 TLS 客户端）；全部失败返回 -1。port ∈ 1..65535。 */
int32_t net_connect(const char *host, int32_t port);

/* os_rand：填充 n 字节密码学强随机到 buf；返回 0。
 * 转发 platform.h 的 P_rand_bytes（CSPRNG）：arc4random / rand_s / /dev/urandom。 */
int32_t os_rand(void *buf, uint32_t n);

/* （待实现：fs_*（文件/目录/路径）/ env_*（环境变量）/ proc_*（进程）等基本操作） */

#ifdef __cplusplus
}
#endif

#endif /* SC_OS_H */
