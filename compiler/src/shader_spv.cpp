// ============================================================
// GLSL → SPIR-V 转译实现（glslang 封装）
// ------------------------------------------------------------
// 见 shader_spv.h。仅在 SCC_WITH_GLSLANG 时链入 glslang 静态库并实现；
// 否则给出安全失败的桩（抛 CompileError），保证未集成时仍可编译。
// ============================================================
#include "shader_spv.h"
#include "error.h"

#ifdef SCC_WITH_GLSLANG
#include "glslang/Public/ShaderLang.h"
#include "glslang/Public/ResourceLimits.h"
#include "SPIRV/GlslangToSpv.h"

namespace scc_shader {

namespace {
// glslang 进程级初始化：整个 scc 进程只需一次（RAII 静态对象在退出时 Finalize）。
struct GlslangProcess {
    GlslangProcess()  { glslang::InitializeProcess(); }
    ~GlslangProcess() { glslang::FinalizeProcess(); }
};
void ensureGlslangInit() { static GlslangProcess once; (void)once; }

EShLanguage toEShLang(SpvStage s) {
    switch (s) {
        case SpvStage::Vertex:   return EShLangVertex;
        case SpvStage::Fragment: return EShLangFragment;
        case SpvStage::Compute:  return EShLangCompute;
    }
    return EShLangVertex;
}
} // namespace

std::vector<uint32_t> glslToSpirv(const std::string& glsl, SpvStage stage) {
    ensureGlslangInit();

    const EShLanguage lang = toEShLang(stage);
    glslang::TShader shader(lang);

    const char* src = glsl.c_str();
    shader.setStrings(&src, 1);
    // Vulkan 语义输入，SPIR-V 1.0 / Vulkan 1.0 目标（与已定架构：SPIR-V 为中枢 IR）。
    shader.setEnvInput(glslang::EShSourceGlsl, lang, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);

    const TBuiltInResource* res = GetDefaultResources();
    const EShMessages msgs = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);

    if (!shader.parse(res, 100, false, msgs)) {
        throw CompileError(std::string("glslang 编译 GLSL 失败：\n")
                           + shader.getInfoLog(), 0);
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(msgs)) {
        throw CompileError(std::string("glslang 链接失败：\n")
                           + program.getInfoLog(), 0);
    }

    std::vector<uint32_t> spirv;
    glslang::GlslangToSpv(*program.getIntermediate(lang), spirv);
    if (spirv.empty())
        throw CompileError("glslang 未产出 SPIR-V（空结果）", 0);
    return spirv;
}

} // namespace scc_shader

#else // !SCC_WITH_GLSLANG

namespace scc_shader {

std::vector<uint32_t> glslToSpirv(const std::string&, SpvStage) {
    throw CompileError("此 scc 构建未集成 glslang（需 -DSCC_WITH_GLSLANG=ON 且 vendor/glslang-src 存在）", 0);
}

} // namespace scc_shader

#endif
