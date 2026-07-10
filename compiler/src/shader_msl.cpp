// ============================================================
// SPIR-V → MSL 转译实现（SPIRV-Cross 封装）
// ------------------------------------------------------------
// 见 shader_msl.h。仅在 SCC_WITH_SPIRV_CROSS 时链入 spirv-cross 静态库并实现；
// 否则给出安全失败的桩（抛 CompileError），保证未集成时仍可编译。
// ============================================================
#include "shader_msl.h"
#include "error.h"

#ifdef SCC_WITH_SPIRV_CROSS
#include "spirv_msl.hpp"
#include "spirv_glsl.hpp"
#include <exception>

namespace scc_shader {

std::string spirvToMsl(const std::vector<uint32_t>& spirv, const MslOptions& opt) {
    if (spirv.empty())
        throw CompileError("SPIR-V 输入为空（非法或截断的 .spv）", 0);
    try {
        spirv_cross::CompilerMSL msl(spirv);

        spirv_cross::CompilerMSL::Options mslOpts = msl.get_msl_options();
        mslOpts.msl_version = opt.mslVersion;
        msl.set_msl_options(mslOpts);

        // 入口重命名：把名为 "main" 的入口改名（保留其执行模型），避免多阶段 main0 冲突
        if (!opt.renameEntry.empty()) {
            for (const auto& e : msl.get_entry_points_and_stages()) {
                if (e.name == "main")
                    msl.rename_entry_point("main", opt.renameEntry, e.execution_model);
            }
        }

        return msl.compile();
    } catch (const std::exception& e) {
        throw CompileError(std::string("SPIRV-Cross 转译 MSL 失败：") + e.what(), 0);
    }
}

std::string spirvToGlsl(const std::vector<uint32_t>& spirv, const GlslOptions& opt) {
    if (spirv.empty())
        throw CompileError("SPIR-V 输入为空（非法或截断的 .spv）", 0);
    try {
        spirv_cross::CompilerGLSL glsl(spirv);
        spirv_cross::CompilerGLSL::Options o = glsl.get_common_options();
        o.version = opt.version;
        o.es = opt.es;
        o.enable_420pack_extension = false;   // GL4.1 无 GL_ARB_shading_language_420pack
        // force_temporary：禁止 spirv-cross 把函数调用结果内联进变量声明。
        // 消除 macOS libc++ 哈希随机化导致的「float a = f(x)」vs「float _N = f(x); float a = _N;」
        // 非确定性，使 golden 测试在跨进程运行时稳定。
        o.force_temporary = true;
        glsl.set_common_options(o);
        return glsl.compile();
    } catch (const std::exception& e) {
        throw CompileError(std::string("SPIRV-Cross 转译 GLSL 失败：") + e.what(), 0);
    }
}

} // namespace scc_shader

#else // !SCC_WITH_SPIRV_CROSS

namespace scc_shader {

std::string spirvToMsl(const std::vector<uint32_t>&, const MslOptions&) {
    throw CompileError("此 scc 构建未集成 SPIRV-Cross（需 -DSCC_WITH_SPIRV_CROSS=ON 且 vendor/spirv-cross 存在）", 0);
}

std::string spirvToGlsl(const std::vector<uint32_t>&, const GlslOptions&) {
    throw CompileError("此 scc 构建未集成 SPIRV-Cross（需 -DSCC_WITH_SPIRV_CROSS=ON 且 vendor/spirv-cross 存在）", 0);
}

} // namespace scc_shader

#endif
