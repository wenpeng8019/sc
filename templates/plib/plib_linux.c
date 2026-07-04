/* plib_linux.c —— Linux 平台实现 */
#include "plib.h"

const char* sc_plib_hello(void) {
#if __x86_64__
    return "hello from Linux (x86_64)";
#elif __aarch64__
    return "hello from Linux (aarch64)";
#else
    return "hello from Linux (unknown arch)";
#endif
}

int sc_plib_add(int a, int b) { return a + b; }
