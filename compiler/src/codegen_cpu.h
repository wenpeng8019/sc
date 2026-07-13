#pragma once

#include <string>
#include "ast.h"
#include "shader_caps.h"

// ============================================================
// codegen_cpu —— .ss comp → SPMD 循环化 C 源码（syntax-s-design §17）
// ============================================================
// `tar cpu@99` 的发射后端：把 comp kernel 转成「workgroup 区间外循环 +
// invocation 内循环」的规整 C99，向量化交给目标 C 编译器（NEON/SVE/AVX/
// RVV/DSP 各自兑现）。产物为单个 C 文本（全部 comp kernel + 注册表 +
// 构造器自注册），经资源化产物 add 进宿主，spc cpu 后端按 entry 名查表。
//
// M1 范围（§17.6）：标量/数组 kernel（vec/mat 报错）、storage/uniform/
// push 块、无 barrier；M2 补 barrier 相位分裂 + shared + atomic。
// ============================================================

// 生成完整 C 文本；prog 须已过 shaderSemaCheck（cpu 目标能力门控）。
// 不支持的构造抛 CompileError（带行号）。
std::string emitCpu(const Program& prog, const GlslTarget& target);
