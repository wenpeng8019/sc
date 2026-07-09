#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "ast.h"

// ============================================================
// codegen_spirv —— GPU/着色器扩展（syntax-s）三期后端：AST → SPIR-V 直发
// ============================================================
// 把 .ss 的着色阶段直接发射为 SPIR-V 1.0 二进制（Vulkan 语义），取代
// 「AST→GLSL 文本→glslang→SPIR-V」的中转（见 syntax-s.md §2.2）。
//
// 发射形式与 glslang 同构，驱动与 SPIRV-Cross 均可直接消费：
//   · Logical 寻址 + OpVariable(Function/Input/Output/Uniform/...) + Load/Store，
//     不做 SSA/OpPhi（下游编译器自会优化）；
//   · 结构化控制流：OpSelectionMerge / OpLoopMerge；
//   · 数学库走 GLSL.std.450 扩展指令集；
//   · 每个 stage 一个独立 SPIR-V 模块（与「每 stage 一份产物」的模型一致），
//     入口名恒为 "main"（下游按需重命名，与旧链约定一致）。
//
// I/O 模型与 codegen_glsl 完全同源（成员改写）：入参结构体字段 → Input 变量
// （Location/BuiltIn 装饰），返回结构体字段 → Output 变量；location 自动分配
// 顺序与 GLSL 后端/反射清单严格一致。
//
// 验证体系（外部工具，非链接依赖）：spirv-val 通过 + spirv-dis 可读 +
// SPIRV-Cross 反译 round-trip + 旧链产物对照。
// ============================================================

// 一个着色阶段的 SPIR-V 产物。
struct SpvUnit {
    ShaderStage stage;            // 着色阶段
    std::string entry;            // 入口名（来自 .ss 源；模块内 OpEntryPoint 恒 "main"）
    std::vector<uint32_t> words;  // SPIR-V 二进制字流
};

// 将已按 shaderMode 解析并通过 shaderSemaCheck 的 Program 中所有阶段入口
// 发射为 SPIR-V 模块。失败抛 CompileError（带行号）。
std::vector<SpvUnit> emitSpirv(const Program& prog, const GlslTarget& target);
