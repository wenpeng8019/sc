/* mt_p.h —— mt 模块的「平台锁层」opt-in 前置头
 *
 * 供 mt.sc 经 inc "mt_p.h" 拉起 platform.h 的互斥/条件变量/屏障层
 * （mutex_t / cond_t / barrier_t + P_* 操作），使 @def 可直接以平台句柄
 * 类型为字段（h: ::mutex_t）——布局精确、无需不透明字节缓冲占位。
 *
 * 机制：platform.h 的互斥/条件/屏障层置于主 include guard 之外的「P_MT_IMPL 延迟
 * 展开块」（见 platform.h 文末）。编译器为每个生成单元在顶部自动 #include "platform.h"
 * （彼时未定义 P_MT_IMPL，MT 块不展开）；本头再次 #include 前先 #define P_MT_IMPL，
 * 触发那段延迟块展开（其一次性守卫 SC_PLATFORM_MT_DONE 独立于主 guard）。
 *
 * C 侧真实 ABI 契约（struct sc_mutex 等 + 方法原型）见同目录 mt.h。 */
#ifndef SC_MT_P_H
#define SC_MT_P_H
#define P_MT_IMPL
#include "platform.h"
#endif /* SC_MT_P_H */
