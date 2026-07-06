/* ============================================================
 * plib.h —— 跨平台库模板：公共 C API 头文件
 *
 * 所有平台共用同一个 .h 接口。
 * 平台实现在 plib_<platform>.c/.m 中，由 build.sh 选择编译。
 * ============================================================ */
#ifndef PLIB_H
#define PLIB_H

#include "../../builtins/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PLIB_SHARED
 #define PLIB_SHARED 0
#endif
#ifndef PLIB_EXPORTS
 #define PLIB_EXPORTS 0
#endif

#define PLIB_API SC_API(PLIB)

PLIB_API const char* sc_plib_hello(void);
PLIB_API int sc_plib_add(int a, int b);

#ifdef __cplusplus
}
#endif
#endif /* PLIB_H */
