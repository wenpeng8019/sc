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

struct GlslOptions {
    uint32_t version = 410;   // GLSL #version 整数（410/300/100 等）
    bool es = false;          // true = GLSL ES（300 es / 100 legacy）
};

// 把 SPIR-V 转译为指定版本的 GLSL 源文本（默认产物链：全目标统一 SPIR-V 中枢，
// gl/gles 经本函数反译；自研 codegen_glsl 作对照/兜底/--emit-glsl）。
// 无显式 binding 目标由 SPIRV-Cross 自动剔除 set/binding 限定，运行时按名绑定；
// ES100 走 legacy 形态（attribute/varying/gl_FragData/struct uniform）。
std::string spirvToGlsl(const std::vector<uint32_t>& spirv, const GlslOptions& opt);

struct HlslOptions {
    uint32_t shaderModel = 50;   // D3D 着色模型整数：40/41/50/51/60...（SM5.0=50）
};

// 把 SPIR-V 转译为 HLSL 源文本（D3D11 后端运行时再 D3DCompile→DXBC）。
// 顶点输入按 location 映射为 TEXCOORD<loc> 语义（与 d3d11_gfx 输入布局契约）；
// 片段输出 location 0 → SV_Target；入口恒为 main。
std::string spirvToHlsl(const std::vector<uint32_t>& spirv, const HlslOptions& opt);

} // namespace scc_shader
