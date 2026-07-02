/* os.h —— sc 操作系统基本操作的 C ABI 契约（与 builtins/os/os.sc 同步维护） */
#ifndef SC_OS_H
#define SC_OS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* os_rand：填充 n 字节密码学强随机到 buf；返回 0。
 * 转发 platform.h 的 P_rand_bytes（CSPRNG）：arc4random / rand_s / /dev/urandom。 */
int32_t os_rand(void *buf, uint32_t n);

/* （待实现：网卡/防火墙/路由等系统管理查询；fs_* / env_* / proc_* 等基本操作。
 *   应用网络套接字已迁至 sys 模块（sock_*）。） */

#ifdef __cplusplus
}
#endif

#endif /* SC_OS_H */
