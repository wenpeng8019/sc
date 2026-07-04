/* plib_darwin.m —— macOS 平台实现（Objective-C 编译，纯 C 代码） */
#include "plib.h"

const char* sc_plib_hello(void) {
#if __arm64__
    return "hello from macOS (arm64)";
#elif __x86_64__
    return "hello from macOS (x86_64)";
#else
    return "hello from macOS (unknown arch)";
#endif
}

int sc_plib_add(int a, int b) { return a + b; }
