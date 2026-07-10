#ifndef FEATURE34_CMAC_H
#define FEATURE34_CMAC_H
#include <stdio.h>

/* 由 C 实现的「定义型」宏：拼装出一个计数器全局 + 一对存取函数。
 * sc 侧用 def NAME:: 把它映射进来（见 feature34.sc）。 */
#define DEFINE_COUNTER(N)                                  \
    static int counter_##N = 0;                            \
    static void counter_##N##_inc(void) {                  \
        counter_##N++;                                     \
    }                                                      \
    static int counter_##N##_get(void) {                   \
        return counter_##N;                                \
    }

#endif
