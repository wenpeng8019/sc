/* io.h —— sc 输入输出模块的 C ABI 契约（与 builtins/io/io.sc 同步维护）
 *
 * print 语言关键字 → 编译器生成 sc_print 调用：
 *   - fmt 前缀 "X:"（X ∈ FEWIDV）指定日志级别，无前缀默认 D（调试）
 *   - 输出 stdout：HH:MM:SS.mmm L| 文本（自动补换行）
 *   - 级别过滤：环境变量 SC_LOG=F/E/W/I/D/V（默认 D），首次调用时读取
 *
 * string(值[, 缓存, 大小]) 格式化关键字由编译器按静态类型生成格式化器
 * （依赖 adt string），不经由本契约。
 */
#ifndef SC_IO_H
#define SC_IO_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* print 关键字原语：C printf 风格 + "X:" 级别前缀 */
void sc_print(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* SC_IO_H */
