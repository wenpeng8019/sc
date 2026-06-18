/* io.h —— sc 输入输出模块的 C ABI 契约（与 builtins/io/io.sc 同步维护）
 *
 * print 语言关键字 → 编译器生成 print 调用（首参为 u1 通道 chn，默认 0）：
 *   - chn：日志通道（透传），chn==0 为默认通道；F/E/W/I/D/V 级别与通道正交
 *   - fmt 前缀 "X:"（X ∈ FEWIDV）指定日志级别，无前缀默认 D（调试）
 *   - 输出 stdout：HH:MM:SS.mmm L| 文本（chn!=0 时加 通道标记；自动补换行）
 *   - 级别过滤：环境变量 SC_LOG=F/E/W/I/D/V（默认 D），首次调用时读取
 *
 * file(name, txt, read, write) 文件 com 设备：打开文件构造 com 端点。
 *   - txt：1=文本 / 0=二进制（"b" 后缀）；read/write：0=禁用 1=同步 2=异步(自动建 ioq)
 *   - 返回 struct com*（失败 NULL）；com 内嵌于设备结构首位，实现见 io_impl.c
 */
#ifndef SC_IO_H
#define SC_IO_H

#include <stdarg.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct com;   /* op.h 定义；此处仅作返回类型的不完全声明 */

/* print 关键字原语：u1 通道 chn + C printf 风格 + "X:" 级别前缀 */
void print(uint8_t chn, const char *fmt, ...);

/* file 文件 com 设备：read/write ∈ {0 禁用, 1 同步, 2 异步}，失败返回 NULL */
struct com *file(const char *name, uint8_t txt, uint8_t read, uint8_t write);

#ifdef __cplusplus
}
#endif

#endif /* SC_IO_H */
