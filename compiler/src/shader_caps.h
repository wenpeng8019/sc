#pragma once

#include <string>

// ============================================================
// shader_caps —— 转义目标（GL 版本/profile）与能力矩阵（syntax-g §13.1）
// ============================================================
// 「转义目标」由 .sg 源码内的顶层 `tar` 指令声明（版本必填、精确锚定），
// 是该着色器的「兼容性契约」。本头提供单一事实源：
//   · GlslTarget —— 一个目标 = API 族 + 精确 GLSL 版本。
//   · CAP_TABLE  —— sg 能力 × API 族 的起始支持版本矩阵。
// shader_sema 与 codegen_glsl 共用此表：前者按目标做能力门控报错，
// 后者按目标决定发射形态（set/binding 语法、内建名、精度限定）。
//
// 该头无任何依赖，可被 ast.h 安全 #include（不引入循环）。新增能力加一行、
// 新增目标加一列——不易遗漏、易扩展。
// ============================================================

// 目标 API 族。
enum class GlApi { Vulkan, GLCore, GLES, WebGL };

// 一个转义目标：API 族 + 精确 GLSL 版本号（如 450 / 330 / 300 / 100）。
struct GlslTarget {
    GlApi api = GlApi::Vulkan;
    int   version = 0;

    bool isES()     const { return api == GlApi::GLES || api == GlApi::WebGL; }
    bool isVulkan() const { return api == GlApi::Vulkan; }

    // `#version N <profile>` 里的 profile 词：Vulkan 无、桌面 core、ES/WebGL es。
    const char* profileWord() const {
        if (isES())               return "es";
        if (api == GlApi::GLCore) return "core";
        return "";
    }
    // set= 描述符集限定符仅 Vulkan-GLSL 可表达。
    bool useSetQualifier() const { return api == GlApi::Vulkan; }
};

// API 族名（诊断 / 反射清单 / 产物 tag）。
inline const char* glApiName(GlApi a) {
    switch (a) {
        case GlApi::Vulkan: return "vulkan";
        case GlApi::GLCore: return "glcore";
        case GlApi::GLES:   return "gles";
        case GlApi::WebGL:  return "webgl";
    }
    return "?";
}

// 名字 → API 族（`gl` 视作 `glcore` 别名）；未知返回 false。
inline bool parseGlApi(const std::string& s, GlApi& out) {
    if (s == "vulkan")               { out = GlApi::Vulkan; return true; }
    if (s == "glcore" || s == "gl")  { out = GlApi::GLCore; return true; }
    if (s == "gles")                 { out = GlApi::GLES;   return true; }
    if (s == "webgl")                { out = GlApi::WebGL;  return true; }
    return false;
}

// 产物 / 反射 tag：如 "vulkan450"、"gles300"。
inline std::string glTargetTag(const GlslTarget& t) {
    return std::string(glApiName(t.api)) + std::to_string(t.version);
}

// ---- 能力矩阵（单一事实源）----
// 行 = sg 能力；列 = API 族；格值 = 该能力在该 api 的起始支持版本（-1 = 永不支持）。
enum class Cap {
    StorageBuffer,    // storage / SSBO
    ComputeStage,     // comp 计算着色阶段
    PushConstant,     // push 常量块
    DoubleType,       // f8 双精度
    DescriptorSet,    // 多描述符集（set >= 1）
    ExplicitBinding,  // layout(binding=) 显式绑定（codegen 策略：不支持则退化按名绑定）
    CapCount
};

struct CapRow {
    const char* name;                  // 诊断用可读名
    int vulkan, glcore, gles, webgl;   // 起始支持版本；-1 = 不支持
};

// inline 函数内的静态局部数组保证跨 TU 单实例（避免 ODR 重复定义）。
inline const CapRow& capRow(Cap c) {
    static const CapRow TABLE[(int)Cap::CapCount] = {
        /*StorageBuffer  */ {"storage 缓冲",     450, 430, 310, -1},
        /*ComputeStage   */ {"comp 计算着色",    450, 430, 310, -1},
        /*PushConstant   */ {"push 常量",        450,  -1,  -1, -1},
        /*DoubleType     */ {"f8 双精度",        450, 400,  -1, -1},
        /*DescriptorSet  */ {"多描述符集(set>=1)", 450, -1, -1, -1},
        /*ExplicitBinding*/ {"binding 显式绑定", 450, 420, 310, -1},
    };
    return TABLE[(int)c];
}

// 某能力在给定 api 的起始支持版本（-1 = 永不支持）。
inline int capMinVersion(Cap c, GlApi a) {
    const CapRow& r = capRow(c);
    switch (a) {
        case GlApi::Vulkan: return r.vulkan;
        case GlApi::GLCore: return r.glcore;
        case GlApi::GLES:   return r.gles;
        case GlApi::WebGL:  return r.webgl;
    }
    return -1;
}

// 目标 t 是否支持能力 c（该 api 支持该能力，且声明版本达到起始版本）。
inline bool capSupported(Cap c, const GlslTarget& t) {
    int mn = capMinVersion(c, t.api);
    return mn >= 0 && t.version >= mn;
}
