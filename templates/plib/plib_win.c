/* plib_win.c —— Windows 平台实现 */
#include "plib.h"

const char* sc_plib_hello(void) {
#if _WIN64
    return "hello from Windows (x64)";
#else
    return "hello from Windows (x86)";
#endif
}

int sc_plib_add(int a, int b) { return a + b; }
