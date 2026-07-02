/* io.h —— sc 输入输出模块的 C ABI 契约（与 builtins/io/io.sc 同步维护）
 *
 * file(name, txt, read, write) 文件 com 设备：打开文件构造 com 端点。
 *   - txt：1=文本 / 0=二进制（"b" 后缀）；read/write：0=禁用 1=同步 2=异步(自动建 ioq)
 *   - 返回 struct com*（失败 NULL）；com 内嵌于设备结构首位，实现见 io_impl.c
 *
 * stream(mem, size, read, write) 内存 com 设备：把 mem 指向的 size 字节内存绑定为 com 后端。
 *   - mem：绑定的内存基址（调用方所有）；size：容量字节数；读写各自独立游标 rpos/wpos
 *   - read/write：0=禁用 1=同步 2=异步(自动建 ioq，内存恒就绪)
 *   - 返回 struct com*（失败 NULL）；不分配数据缓冲，close 仅释放端点结构、不碰 mem
 *
 * tcp(fd, nonblock, read, write) 套接字 com 设备：以一个已连接 TCP 套接字为后端。
 *   - fd：已连接套接字（设备全托管：nonblock 时内部置 O_NONBLOCK，close 负责关闭 fd）
 *   - nonblock：1=置非阻塞，并强制启用方向走异步（建 ioq）；0=保持调用方原状
 *   - read/write：0=禁用 1=同步 2=异步(自动建 ioq)；实现 readable/writable 回填 fd 供多路复用
 *   - 返回 struct com*（失败 NULL）；EAGAIN→IO_AGAIN、对端关闭→IO_EOF
 */
#ifndef SC_IO_H
#define SC_IO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct com;   /* op.h 定义；此处仅作返回类型的不完全声明 */

/* file 文件 com 设备：read/write ∈ {0 禁用, 1 同步, 2 异步}，失败返回 NULL */
struct com *file(const char *name, bool txt, uint8_t read, uint8_t write);

/* stream 内存 com 设备：绑定 mem[0..size) 为后端，read/write ∈ {0,1,2}，失败返回 NULL */
struct com *stream(void *mem, uint64_t size, uint8_t read, uint8_t write);

/* tcp 套接字 com 设备：以已连接 fd 为后端（设备托管 fd），read/write ∈ {0,1,2}，失败返回 NULL */
struct com *tcp(int32_t fd, bool nonblock, uint8_t read, uint8_t write);

#ifdef __cplusplus
}
#endif

#endif /* SC_IO_H */
