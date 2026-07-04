#pragma once
// ============================================================
// 着色器中枢 IR：GLSL → SPIR-V（glslang 封装）
// ------------------------------------------------------------
// 一期把 glslang 以静态库（vendor/glslang-src，走其自身 CMake）链入 scc，
// 由 scc 内部把 Vulkan 语义 GLSL 编译为 SPIR-V（中枢 IR），取代外部 glslangValidator。
// 未启用 SCC_WITH_GLSLANG 时，glslToSpirv 抛 CompileError（不含 glslang 头）。
// 最后一期将由自研 codegen_spirv（AST→SPIR-V）取代本模块。
// ============================================================
#include <cstdint>
#include <string>
#include <vector>

namespace scc_shader {

enum class SpvStage { Vertex, Fragment, Compute };

// 把 Vulkan 语义的 GLSL 源编译为 SPIR-V 字（uint32 数组）；失败抛 CompileError。
// 入口固定为 GLSL 惯例的 main（后续 SPIR-V→MSL 时再按需重命名）。
std::vector<uint32_t> glslToSpirv(const std::string& glsl, SpvStage stage);

} // namespace scc_shader
