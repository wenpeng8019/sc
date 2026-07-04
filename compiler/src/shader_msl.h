#pragma once
// ============================================================
// 着色器发行后端：SPIR-V → MSL（Metal Shading Language）
// ------------------------------------------------------------
// 一期把 SPIRV-Cross 以静态库（vendor/spirv-cross，走其自身 CMake）链入 scc，
// 由 scc 内部完成 SPIR-V→MSL 转译，取代外部 `spirv-cross` CLI。
// 未启用 SCC_WITH_SPIRV_CROSS 时，spirvToMsl 抛 CompileError（不含 spirv-cross 头）。
// ============================================================
#include <cstdint>
#include <string>
#include <vector>

namespace scc_shader {

struct MslOptions {
    // MSL 版本，打包整数 major*10000 + minor*100 + patch（默认 20000 = Metal 2.0.0）
    uint32_t mslVersion = 20000;
    // 若非空：把模块里名为 "main" 的入口重命名为此名（多阶段链入同一 metallib 时避免 main0 冲突）
    std::string renameEntry;
};

// 把 SPIR-V 字（uint32 数组）转译为 MSL 源文本；失败抛 CompileError。
std::string spirvToMsl(const std::vector<uint32_t>& spirv, const MslOptions& opt);

} // namespace scc_shader
