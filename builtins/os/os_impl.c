/* os_impl.c —— sc 操作系统基本操作（os.h 契约）默认实现
 * 跨平台经由 builtins/platform.h
 */
#include "os.h"
#include "platform.h"

/* ---------------- os_rand：填充 n 字节密码学强随机 ----------------
 * 直接转发 platform.h 的 P_rand_bytes（CSPRNG）：
 *   macOS/BSD arc4random、Windows rand_s、Linux /dev/urandom。
 * 恒成功返回 0。用于 WS 客户端掩码键等需不可预测随机处（RFC 6455 §5.3）。
 */
int32_t sc_os_rand(void *buf, uint32_t n) {
    P_rand_bytes(buf, (size_t)n);
    return 0;
}

/* （待实现：网卡/防火墙/路由等系统管理查询；fs_*（文件/目录/路径）/ env_*（环境变量）/
 *   proc_*（进程）等基本操作。应用网络套接字已迁至 sys 模块（sock_*）。） */
