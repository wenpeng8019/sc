/* io.h —— sc 输入输出模块的 C ABI 契约（与 builtins/io/io.sc 同步维护）
 *
 * print 语言关键字 → 编译器生成 print 调用（首参为 u1 通道 chn，默认 0）：
 *   - chn：日志通道（透传），chn==0 为默认通道；F/E/W/I/D/V 级别与通道正交
 *   - fmt 前缀 "X:"（X ∈ FEWIDV）指定日志级别，无前缀默认 D（调试）
 *   - 输出 stdout：HH:MM:SS.mmm L| 文本（chn!=0 时加 通道标记；自动补换行）
 *   - 级别过滤：环境变量 SC_LOG=F/E/W/I/D/V（默认 D），首次调用时读取
 *
 * stringify<选项>(值[, 缓存, 大小]) JSON 格式化关键字由编译器按静态类型生成
 * 格式化器（写入独立 stringify.h，依赖 adt string）。生成的格式化器签名携带
 * stringify_t 选项参数（本契约定义），调用点据 stringify<key:val> 构造之。
 *   - compact:1 → 紧凑单行 {"x":3,"y":4}
 *   - 默认（compact:0）→ 多行美化（2 空格逐层缩进）
 */
#ifndef SC_IO_H
#define SC_IO_H

#include <stdarg.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* print 关键字原语：u1 通道 chn + C printf 风格 + "X:" 级别前缀 */
void print(uint8_t chn, const char *fmt, ...);

/* stringify<...> 选项块：编译器据此构造 (stringify_t){...} 传入格式化器 */
typedef struct stringify {
    uint8_t compact;                /* 1 → 紧凑单行；0 → 多行美化（默认） */
} stringify_t;

#ifdef __cplusplus
}
#endif

#endif /* SC_IO_H */
