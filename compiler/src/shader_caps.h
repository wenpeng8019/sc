#pragma once

#include <string>

// ============================================================
// shader_caps —— 转义目标（GL 版本/profile）与能力矩阵（syntax-s §13.1）
// ============================================================
// 「转义目标」由 .ss 源码内的顶层 `tar` 指令声明（版本必填、精确锚定），
// 是该着色器的「兼容性契约」。本头提供单一事实源：
//   · GlslTarget —— 一个目标 = API 族 + 精确 GLSL 版本。
//   · CAP_TABLE  —— sg 能力 × API 族 的起始支持版本矩阵。
// shader_sema 与 codegen_glsl 共用此表：前者按目标做能力门控报错，
// 后者按目标决定发射形态（set/binding 语法、内建名、精度限定）。
//
// 该头无任何依赖，可被 ast.h 安全 #include（不引入循环）。新增能力加一行、
// 新增目标加一列——不易遗漏、易扩展。
// ============================================================

// 目标 API 族。Metal 非 GLSL 方言，而是 SPIR-V 转译出的发行后端（MSL）；
// 其内部 GLSL 中间产物按 Vulkan 语义生成（见 glslIsVulkanFlavor）。
enum class GlApi { Vulkan, GLCore, GLES, WebGL, Metal };

// 一个转义目标：API 族 + 精确版本号。
//   GL 家族：GLSL #version 整数（如 450 / 330 / 300 / 100）。
//   Metal  ：MSL 打包整数 major*10000 + minor*100（如 2.0→20000、2.1→20100）。
struct GlslTarget {
    GlApi api = GlApi::Vulkan;
    int   version = 0;

    bool isES()     const { return api == GlApi::GLES || api == GlApi::WebGL; }
    bool isVulkan() const { return api == GlApi::Vulkan; }
    bool isMetal()  const { return api == GlApi::Metal; }
    // 内部 GLSL 中间产物是否用 Vulkan 语义（Vulkan 目标本身 + Metal 经 SPIR-V 转译）。
    bool glslIsVulkanFlavor() const { return isVulkan() || isMetal(); }

    // `#version N <profile>` 里的 profile 词：Vulkan 无、桌面 core、ES/WebGL es。
    const char* profileWord() const {
        if (isES())               return "es";
        if (api == GlApi::GLCore) return "core";
        return "";
    }
    // set= 描述符集限定符仅 Vulkan-GLSL 可表达（Metal 内部亦走 Vulkan 语义）。
    bool useSetQualifier() const { return glslIsVulkanFlavor(); }
};

// 归一化版本：把源码里书写的版本（整数或 major.minor 小数）换算成该 API 的规范整数。
//   GL 家族规范 = GLSL #version 整数：整数书写(450)即规范值原样返回；
//     小数书写(4.5/3.3/3.0/1.0) → major*100 + minor*10。
//   Metal 规范 = MSL 打包整数：整数书写视为主版本(2→20000)；小数(2.0/2.1) → major*10000 + minor*100。
inline int normalizeShaderVersion(GlApi api, int major, int minor, bool hasDecimal) {
    if (api == GlApi::Metal) return major * 10000 + minor * 100;
    if (hasDecimal)          return major * 100 + minor * 10;
    return major;
}

// API 族名（诊断 / 反射清单 / 产物 tag）。
inline const char* glApiName(GlApi a) {
    switch (a) {
        case GlApi::Vulkan: return "vulkan";
        case GlApi::GLCore: return "glcore";
        case GlApi::GLES:   return "gles";
        case GlApi::WebGL:  return "webgl";
        case GlApi::Metal:  return "metal";
    }
    return "?";
}

// 名字 → API 族（`gl` 视作 `glcore` 别名）；未知返回 false。
inline bool parseGlApi(const std::string& s, GlApi& out) {
    if (s == "vulkan")               { out = GlApi::Vulkan; return true; }
    if (s == "glcore" || s == "gl")  { out = GlApi::GLCore; return true; }
    if (s == "gles")                 { out = GlApi::GLES;   return true; }
    if (s == "webgl")                { out = GlApi::WebGL;  return true; }
    if (s == "metal" || s == "msl")  { out = GlApi::Metal;  return true; }
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
    const char* name;                         // 诊断用可读名
    int vulkan, glcore, gles, webgl, metal;   // 起始支持版本；-1 = 不支持
};

// inline 函数内的静态局部数组保证跨 TU 单实例（避免 ODR 重复定义）。
// Metal 列用 MSL 打包整数（20000 = MSL 2.0）；f8 双精度 Metal 永不支持（-1）。
inline const CapRow& capRow(Cap c) {
    static const CapRow TABLE[(int)Cap::CapCount] = {
        /*StorageBuffer  */ {"storage 缓冲",     450, 430, 310, -1, 20000},
        /*ComputeStage   */ {"comp 计算着色",    450, 430, 310, -1, 20000},
        /*PushConstant   */ {"push 常量",        450,  -1,  -1, -1, 20000},
        /*DoubleType     */ {"f8 双精度",        450, 400,  -1, -1,    -1},
        /*DescriptorSet  */ {"多描述符集(set>=1)", 450, -1, -1, -1, 20000},
        /*ExplicitBinding*/ {"binding 显式绑定", 450, 420, 310, -1, 20000},
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
        case GlApi::Metal:  return r.metal;
    }
    return -1;
}

// 目标 t 是否支持能力 c（该 api 支持该能力，且声明版本达到起始版本）。
inline bool capSupported(Cap c, const GlslTarget& t) {
    int mn = capMinVersion(c, t.api);
    return mn >= 0 && t.version >= mn;
}
