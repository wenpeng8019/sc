#pragma once
#include "ast.h"
#include <string>

// ============================================================
// C 代码生成器 —— AST → C 源码（一期后端）
// ============================================================
// 将解析完成的 AST 转译为等价的 C 语言源码。
//
// 类型映射规则：
//   sc 内置类型 → C 标准类型（通过 mapBase() 映射）：
//     i1→int8_t, i2→int16_t, i4→int32_t, i8→int64_t
//     u1→uint8_t, u2→uint16_t, u4→uint32_t, u8→uint64_t
//     f4→float, f8→double, v→void
//   未指定类型 → 默认推断（无指针→char*, 有指针→void*）
//
// 函数类型展开：
//   fnc name -> func_type   → 从已注册的函数类型表中查找签名并展开为 C 函数
//
// 输出结构：
//   1. 头文件 #include
//   2. 类型定义（typedef enum/struct/union/alias）
//   3. 全局变量声明
//   4. 非 main 函数的原型声明（forward declaration）
//   5. 函数体实现（包括 main）
//
// 后续规划：二期基于同一 AST 增加 LLVM IR 后端，AST 与后端完全解耦。
// ============================================================
// srcFile 非空时：生成的 C 代码中插入 #line 指令，调试信息（DWARF）
// 映射回 .sc 源文件，lldb/gdb 断点与单步直接落在 sc 源码上。
// 传绝对路径以保证调试器在任意工作目录都能找到源文件。
std::string emitC(const Program& prog, const std::string& srcFile = "");

// 自动指针 T@ 引用检查开关（--check=ref / SCC_REF_CHECK）。
//   开启：注入栈对象 sc_ref 头 + 退域 sc_release_check 断言（带源码定位 site），
//        捕获「借用比目标活得久」的悬挂（auto_ptr.md §7.3）。
//   关闭（默认）：栈断言编译掉，省开销；堆对象 ARC（in→0 自动 free / out>0 报错）始终保留。
void setRefCheck(bool on);
bool getRefCheck();
// 越界 canary 开关（--check=mem）：开启则 ref 头堆对象注入头尾哨兵，释放点校验越界损坏。
void setMemCheck(bool on);
bool getMemCheck();
// 栈悬挂断言 site 文案使用的源码文件名（独立于 #line 的 srcFile）。
void setRefSrcFile(const std::string& path);

// emitC 变体：程序使用 stringify(...) 时，将按类型生成的 JSON 格式化器写入独立头文件。
//   stringifyHeaderName 非空且 stringifyHeaderOut 非空：格式化器写入 *stringifyHeaderOut
//   （含 include guard），生成的 .c 在类型定义之后 #include 该头文件名；程序未使用
//   stringify 时 *stringifyHeaderOut 为空。stringifyHeaderName 为空时回退为内联进 .c。
std::string emitC(const Program& prog, const std::string& srcFile,
                  const std::string& stringifyHeaderName, std::string* stringifyHeaderOut);

// 生成 C 头文件内容：仅包含 @导出对象的声明
//   导出类型 → 完整 typedef；导出变量/常量 → extern 声明；导出函数 → 原型
// 程序中没有任何 @导出对象时返回空字符串。
// guardName: include guard 宏名（由调用方从输出文件名推导）
std::string emitCHeader(const Program& prog, const std::string& guardName);

// 生成 future<ID> 聚合枚举头 type.h 内容：typedef enum { ... } future_id;（含 include guard）。
//   ids 为去重后的事件 ID 列表（默认值 0,1,2...，各单元按名引用即一致）。
//   ids 为空时返回空字符串。由转译/构建管线在工程输出同级落盘，各 .c #include "type.h"。
std::string emitFutureIdHeader(const std::vector<std::string>& ids);
