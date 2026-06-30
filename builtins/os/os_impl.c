/* os_impl.c —— sc 操作系统基本操作（os.h 契约）默认实现
 * 跨平台经由 builtins/platform.h
 */
#include "os.h"
#include "platform.h"

/* ---------------- net_socketpair：一对已连接本地套接字 ----------------
 * tcp() 设备只包「已连接 fd」；建立连接属 OS 边界，故由本模块提供。
 *   POSIX：socketpair(AF_UNIX, SOCK_STREAM) 直接给全双工对。
 *   Windows：无 socketpair，用 127.0.0.1 回环 listen→connect→accept 模拟等价语义。
 */
#if P_WIN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  ifdef _MSC_VER
#    pragma comment(lib, "ws2_32.lib")  /* MSVC 自动链接；MinGW 需 ldflags 加 -lws2_32 */
#  endif

int32_t net_socketpair(int32_t *fds) {
    WSADATA wsa;
    static int wsa_inited = 0;
    if (!wsa_inited) {
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
        wsa_inited = 1;
    }
    SOCKET lst = socket(AF_INET, SOCK_STREAM, 0);
    if (lst == INVALID_SOCKET) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  /* 任选端口 */

    int alen = (int)sizeof(addr);
    if (bind(lst, (struct sockaddr *)&addr, alen) != 0 ||
        getsockname(lst, (struct sockaddr *)&addr, &alen) != 0 ||
        listen(lst, 1) != 0) {
        closesocket(lst);
        return -1;
    }

    SOCKET cli = socket(AF_INET, SOCK_STREAM, 0);
    if (cli == INVALID_SOCKET) { closesocket(lst); return -1; }
    if (connect(cli, (struct sockaddr *)&addr, alen) != 0) {
        closesocket(cli); closesocket(lst);
        return -1;
    }
    SOCKET srv = accept(lst, NULL, NULL);
    closesocket(lst);
    if (srv == INVALID_SOCKET) { closesocket(cli); return -1; }

    fds[0] = (int32_t)srv;
    fds[1] = (int32_t)cli;
    return 0;
}
#else
#  include <sys/socket.h>

int32_t net_socketpair(int32_t *fds) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return -1;
    fds[0] = sv[0];
    fds[1] = sv[1];
    return 0;
}
#endif

/* ---------------- os_rand：填充 n 字节密码学强随机 ----------------
 * 直接转发 platform.h 的 P_rand_bytes（CSPRNG）：
 *   macOS/BSD arc4random、Windows rand_s、Linux /dev/urandom。
 * 恒成功返回 0。用于 WS 客户端掩码键等需不可预测随机处（RFC 6455 §5.3）。
 */
int32_t os_rand(void *buf, uint32_t n) {
    P_rand_bytes(buf, (size_t)n);
    return 0;
}

/* ---------------- net_connect：对外建立 TCP 连接（阻塞） ----------------
 * getaddrinfo 解析 host:port（IPv4/IPv6 通吃），逐候选 socket+connect，成功返回已连接
 * 阻塞 fd（调用方 io.tcp(fd,false,1,1) 包同步 com，或叠 ssl_com 做 TLS 客户端）；全失败 -1。
 */
#include <stdio.h>
#include <string.h>
#if P_WIN
int32_t net_connect(const char *host, int32_t port) {
    WSADATA wsa;
    static int wsa_inited = 0;
    if (!wsa_inited) {
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
        wsa_inited = 1;
    }
    char portstr[16];
    snprintf(portstr, sizeof portstr, "%d", (int)port);
    struct addrinfo hints, *res = NULL, *rp;
    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0) return -1;
    SOCKET fd = INVALID_SOCKET;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == INVALID_SOCKET) continue;
        if (connect(fd, rp->ai_addr, (int)rp->ai_addrlen) == 0) break;
        closesocket(fd); fd = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    return (fd == INVALID_SOCKET) ? -1 : (int32_t)fd;
}
#else
#  include <netdb.h>
#  include <unistd.h>
int32_t net_connect(const char *host, int32_t port) {
    char portstr[16];
    snprintf(portstr, sizeof portstr, "%d", (int)port);
    struct addrinfo hints, *res = NULL, *rp;
    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0) return -1;
    int fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}
#endif

/* （待实现：fs_*（文件/目录/路径）/ env_*（环境变量）/ proc_*（进程）等基本操作） */
