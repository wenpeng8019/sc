/* async.h —— 异步功能库叶子原语的 C ABI 契约
 *               （与 builtins/async/async.sc 同步维护）
 *
 * 语言底层异步机制（future 类型、async_init/loop/final、future_* 原语）的 C ABI
 * 声明已迁入 builtins/op.h（默认带入每个 C 单元）。本头只声明本模块新增的叶子异步
 * 原语（delay）；其运行时实现（连同 future/async_* 的全部实现）均在语言自有异步内核
 * builtins/op_impl.c（POSIX poll + 自管道 + pthread，不依赖 libuv）。
 *
 * 模型与线程安全说明见 op.h 头注（单线程协作式事件循环 + rpc 状态机）。
 */
#ifndef SC_ASYNC_H
#define SC_ASYNC_H

#include "../op.h"   /* future / async_* / future_* 的 C ABI 声明 */

#ifdef __cplusplus
extern "C" {
#endif

/* delay —— 叶子异步原语（单调时钟定时器，实现在 op_impl.c） */
future  *delay(uint32_t ms);            /* 启动 ms 毫秒定时器，立即返回 future */

#ifdef __cplusplus
}
#endif

#endif /* SC_ASYNC_H */

