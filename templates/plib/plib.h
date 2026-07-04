/* ============================================================
 * plib.h —— 跨平台库模板：公共 C API 头文件
 *
 * 所有平台共用同一个 .h 接口。
 * 平台实现在 plib_<platform>.c/.m 中，由 build.sh 选择编译。
 * ============================================================ */
#ifndef PLIB_H
#define PLIB_H

#ifdef __cplusplus
extern "C" {
#endif

const char* sc_plib_hello(void);
int sc_plib_add(int a, int b);

#ifdef __cplusplus
}
#endif
#endif /* PLIB_H */
