/* io.h —— sc 输入输出模块的 C ABI 契约（与 builtins/io/io.sc 同步维护）
 *
 * file(name, txt, read, write) 文件 com 设备：打开文件构造 com 端点。
 *   - txt：1=文本 / 0=二进制（"b" 后缀）；read/write：0=禁用 1=同步 2=异步(自动建 ioq)
 *   - 返回 struct com*（失败 NULL）；com 内嵌于设备结构首位，实现见 io_impl.c
 */
#ifndef SC_IO_H
#define SC_IO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct com;   /* op.h 定义；此处仅作返回类型的不完全声明 */

/* file 文件 com 设备：read/write ∈ {0 禁用, 1 同步, 2 异步}，失败返回 NULL */
struct com *file(const char *name, uint8_t txt, uint8_t read, uint8_t write);

#ifdef __cplusplus
}
#endif

#endif /* SC_IO_H */
