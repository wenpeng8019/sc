#pragma once

#include <string>
#include <vector>
#include <sstream>

// ============================================================
// shader_caps —— 转义目标（GL 版本/profile）与能力矩阵（syntax-s §13.1）
// ============================================================
// 「转义目标」由 .ss 源码内的顶层 `tar` 指令声明（版本必填、精确锚定），
// 是该着色器的「兼容性契约」。本头提供单一事实源：
//   · GlslTarget —— 一个目标 = API 族 + 精确 GLSL 版本 + 扩展集。
//   · 能力矩阵  —— sg 能力 × API 族，每格 = 核心起始版本 或 替代扩展。
// shader_sema 与 codegen_glsl 共用此表：前者按目标做能力门控报错，
// 后者按目标决定发射形态（set/binding 语法、#extension 指令、精度限定）。
//
// 能力满足的两条途径（capResolve）：
//   Core —— 声明版本 ≥ 该 api 的核心起始版本，直接可用；
//   Ext  —— 版本不够但目标声明了替代扩展（且版本 ≥ 扩展下限），
//           codegen 须发射 `#extension <名> : require`。
// 扩展集来源 = 外部设备能力档案（caps profile 文件，`tar "file.caps"`）：
//   # 注释
//   api gles
//   version 3.0        （或 300；Metal 同 tar 规则打包）
//   ext GL_EXT_frag_depth
//   ext GL_OES_standard_derivatives
// 这使能力契约不再限于「API@版本」网格点，可精确锚定一块具体板卡。
//
// 该头仅依赖标准库字符串设施，可被 ast.h 安全 #include（不引入循环）。
// 新增能力加一行、新增目标加一列——不易遗漏、易扩展。
// ============================================================

// 目标 API 族。Metal 非 GLSL 方言，而是 SPIR-V 转译出的发行后端（MSL）；
// 其内部 GLSL 中间产物按 Vulkan 语义生成（见 glslIsVulkanFlavor）。
enum class GlApi { Vulkan, GLCore, GLES, WebGL, Metal, D3D };

// 一个转义目标：API 族 + 精确版本号 + 扩展集（来自 caps profile）。
//   GL 家族：GLSL #version 整数（如 450 / 330 / 300 / 100）。
//   Metal  ：MSL 打包整数 major*10000 + minor*100（如 2.0→20000、2.1→20100）。
struct GlslTarget {
    GlApi api = GlApi::Vulkan;
    int   version = 0;
    std::vector<std::string> extensions;  // 设备声明的可用扩展（caps profile）
    std::string profile;                  // 来源 profile 文件路径（tar "..."）；
                                          // 非空且 version==0 = 待加载

    bool isES()     const { return api == GlApi::GLES || api == GlApi::WebGL; }
    bool isVulkan() const { return api == GlApi::Vulkan; }
    bool isMetal()  const { return api == GlApi::Metal; }
    bool isD3D()    const { return api == GlApi::D3D; }
    // 内部 GLSL 中间产物是否用 Vulkan 语义（Vulkan 目标本身 + Metal/D3D 经 SPIR-V 转译）。
    bool glslIsVulkanFlavor() const { return isVulkan() || isMetal() || isD3D(); }

    // `#version N <profile>` 里的 profile 词：Vulkan 无、桌面 core、ES/WebGL es。
    // 注：GLSL ES 100 无 profile 词（`#version 100` 单独成立）。
    const char* profileWord() const {
        if (isES())               return version >= 300 ? "es" : "";
        if (api == GlApi::GLCore) return version >= 150 ? "core" : "";
        return "";
    }
    // set= 描述符集限定符仅 Vulkan-GLSL 可表达（Metal 内部亦走 Vulkan 语义）。
    bool useSetQualifier() const { return glslIsVulkanFlavor(); }

    bool hasExtension(const char* name) const {
        for (const auto& e : extensions) if (e == name) return true;
        return false;
    }
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

// 版本白名单：GLSL 版本号不是连续数，写错（如 gles@200）应当场拒绝并提示。
//   典型误区：ES2.0 的着色语言是 GLSL ES 100（非 200），ES3.0 才是 300。
//   Metal 为打包整数，只验范围（MSL 1.0–4.x）。
// 合法返回 true；非法返回 false 并置 hint（可直接拼进报错）。
inline bool validShaderVersion(GlApi api, int v, std::string& hint) {
    auto in = [&](std::initializer_list<int> list) {
        for (int x : list) if (x == v) return true;
        return false;
    };
    switch (api) {
        case GlApi::Vulkan:
            if (in({450, 460})) return true;
            hint = "vulkan 有效版本：450/460";
            return false;
        case GlApi::GLCore:
            if (in({110, 120, 130, 140, 150, 330, 400, 410, 420, 430, 440, 450, 460}))
                return true;
            hint = "glcore 有效版本：110–150/330/400–460";
            return false;
        case GlApi::GLES:
            if (in({100, 300, 310, 320})) return true;
            hint = v == 200
                ? "gles 无 GLSL 版本 200：ES2.0 的着色语言是 gles@100，ES3.0 才是 gles@300"
                : "gles 有效版本：100(ES2.0)/300(ES3.0)/310(ES3.1)/320(ES3.2)";
            return false;
        case GlApi::WebGL:
            if (in({100, 300})) return true;
            hint = "webgl 有效版本：100(WebGL1)/300(WebGL2)";
            return false;
        case GlApi::Metal:
            if (v >= 10000 && v < 50000) return true;
            hint = "metal 版本如 2.0（MSL 1.0–4.x）";
            return false;
        case GlApi::D3D:
            if (in({40, 41, 50, 51})) return true;
            hint = "d3d 有效着色模型：40/41/50/51（SM4.0–5.1）";
            return false;
    }
    hint = "未知 API";
    return false;
}

// API 族名（诊断 / 反射清单 / 产物 tag）。
inline const char* glApiName(GlApi a) {
    switch (a) {
        case GlApi::Vulkan: return "vulkan";
        case GlApi::GLCore: return "glcore";
        case GlApi::GLES:   return "gles";
        case GlApi::WebGL:  return "webgl";
        case GlApi::Metal:  return "metal";
        case GlApi::D3D:    return "d3d";
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
    if (s == "d3d" || s == "hlsl" || s == "d3d11") { out = GlApi::D3D; return true; }
    return false;
}

// 产物 / 反射 tag：如 "vulkan450"、"gles300"；profile 目标用文件 stem
// （如 "rk3588"），使产物名可读且不同板卡档案互不覆盖。
inline std::string glTargetTag(const GlslTarget& t) {
    if (!t.profile.empty()) {
        // 取路径 stem（无目录、无扩展名）——纯字符串处理避免引入 <filesystem>
        std::string s = t.profile;
        size_t slash = s.find_last_of("/\\");
        if (slash != std::string::npos) s = s.substr(slash + 1);
        size_t dot = s.find_last_of('.');
        if (dot != std::string::npos && dot > 0) s = s.substr(0, dot);
        if (!s.empty()) return s;
    }
    return std::string(glApiName(t.api)) + std::to_string(t.version);
}

// ---- 设备能力档案（caps profile）解析 ----
// 行式通用协议（外部文件配置目标 + 扩展）：
//   api <vulkan|glcore|gles|webgl|metal>
//   version <整数|major.minor>
//   ext <扩展名>            （可多行）
//   #                        行注释；空行忽略
// 只做文本解析（无 I/O）——文件读取由调用方完成（codegen_glsl 按源文件相对路径）。
// 成功返回 true；失败置 err（含行号）。
inline bool parseCapsProfile(const std::string& text, GlslTarget& out, std::string& err) {
    std::istringstream is(text);
    std::string line;
    int lineNo = 0;
    bool hasApi = false, hasVersion = false;
    while (std::getline(is, line)) {
        lineNo++;
        // 去注释、修剪
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        size_t b = line.find_first_not_of(" \t\r");
        if (b == std::string::npos) continue;
        size_t e = line.find_last_not_of(" \t\r");
        line = line.substr(b, e - b + 1);

        std::istringstream ls(line);
        std::string key, val;
        ls >> key >> val;
        if (val.empty()) {
            err = "第 " + std::to_string(lineNo) + " 行：'" + key + "' 缺少取值";
            return false;
        }
        if (key == "api") {
            if (!parseGlApi(val, out.api)) {
                err = "第 " + std::to_string(lineNo) + " 行：未知 api '" + val + "'";
                return false;
            }
            hasApi = true;
        } else if (key == "version") {
            int major = 0, minor = 0; bool hasDecimal = false;
            size_t dot = val.find('.');
            try {
                if (dot != std::string::npos) {
                    major = std::stoi(val.substr(0, dot));
                    std::string frac = val.substr(dot + 1);
                    minor = frac.empty() ? 0 : std::stoi(frac);
                    hasDecimal = true;
                } else {
                    major = std::stoi(val);
                }
            } catch (...) {
                err = "第 " + std::to_string(lineNo) + " 行：无效版本 '" + val + "'";
                return false;
            }
            if (!hasApi) {
                err = "第 " + std::to_string(lineNo) + " 行：version 须在 api 之后";
                return false;
            }
            out.version = normalizeShaderVersion(out.api, major, minor, hasDecimal);
            {
                std::string hint;
                if (!validShaderVersion(out.api, out.version, hint)) {
                    err = "第 " + std::to_string(lineNo) + " 行：无效版本 " +
                          std::to_string(out.version) + "（" + hint + "）";
                    return false;
                }
            }
            hasVersion = true;
        } else if (key == "ext") {
            out.extensions.push_back(val);
        } else {
            err = "第 " + std::to_string(lineNo) + " 行：未知指令 '" + key +
                  "'（api/version/ext）";
            return false;
        }
    }
    if (!hasApi)     { err = "缺少 api 行"; return false; }
    if (!hasVersion) { err = "缺少 version 行"; return false; }
    return true;
}

// ---- 能力矩阵（单一事实源）----
// 行 = sg 能力；列 = API 族；格值 = { 核心起始版本, 替代扩展, 扩展版本下限 }。
//   core = -1 且 ext 为空 → 该 api 永不支持。
//   ext 非空：版本 ≥ extFrom 且目标声明了该扩展 → 经扩展满足
//   （codegen 发射 #extension）。
enum class Cap {
    StorageBuffer,    // storage / SSBO
    ComputeStage,     // comp 计算着色阶段
    PushConstant,     // push 常量块
    DoubleType,       // f8 双精度
    DescriptorSet,    // 多描述符集（set >= 1）
    ExplicitBinding,  // layout(binding=) 显式绑定（codegen 策略：不支持则退化按名绑定）
    UintType,         // u1/u2/u4 无符号整型（GLSL ES 100 / 早期桌面 GL 无 uint）
    VertexIdBuiltin,  // builtin vertex_id / instance_id（ES 100 无 gl_VertexID）
    FragDepthBuiltin, // builtin frag_depth（ES 100 经 GL_EXT_frag_depth → gl_FragDepthEXT）
    MultiRenderTarget,// frag 多输出（ES 100 经 GL_EXT_draw_buffers → gl_FragData[i]）
    CapCount
};

// 单 api 的能力需求：核心版本 + 可选替代扩展（扩展亦有版本下限）。
struct CapReq {
    int core;             // 核心起始版本；-1 = 核心永不支持
    const char* ext;      // 替代扩展名；NULL = 无扩展途径
    int extFrom;          // 扩展生效的版本下限（ext 非空时有意义）
};

struct CapRow {
    const char* name;                          // 诊断用可读名
    CapReq vulkan, glcore, gles, webgl, metal; // 各 api 的满足途径
};

// inline 函数内的静态局部数组保证跨 TU 单实例（避免 ODR 重复定义）。
// Metal 列用 MSL 打包整数（20000 = MSL 2.0）；f8 双精度 Metal 永不支持。
// 扩展途径当前覆盖桌面 GL 的 ARB 后向移植扩展；GLES/WebGL 的 OES/EXT
// 扩展行随 GLES2/WebGL 代码生成落地时补充（见 syntax-s §13.1）。
inline const CapRow& capRow(Cap c) {
    static const CapRow TABLE[(int)Cap::CapCount] = {
        /*StorageBuffer  */ {"storage 缓冲",
            /*vulkan*/{450, nullptr, 0},
            /*glcore*/{430, "GL_ARB_shader_storage_buffer_object", 400},
            /*gles  */{310, nullptr, 0},
            /*webgl */{ -1, nullptr, 0},
            /*metal */{20000, nullptr, 0}},
        /*ComputeStage   */ {"comp 计算着色",
            {450, nullptr, 0},
            {430, "GL_ARB_compute_shader", 420},
            {310, nullptr, 0},
            { -1, nullptr, 0},
            {20000, nullptr, 0}},
        /*PushConstant   */ {"push 常量",
            {450, nullptr, 0},
            { -1, nullptr, 0},
            { -1, nullptr, 0},
            { -1, nullptr, 0},
            {20000, nullptr, 0}},
        /*DoubleType     */ {"f8 双精度",
            {450, nullptr, 0},
            {400, "GL_ARB_gpu_shader_fp64", 330},
            { -1, nullptr, 0},
            { -1, nullptr, 0},
            { -1, nullptr, 0}},
        /*DescriptorSet  */ {"多描述符集(set>=1)",
            {450, nullptr, 0},
            { -1, nullptr, 0},
            { -1, nullptr, 0},
            { -1, nullptr, 0},
            {20000, nullptr, 0}},
        /*ExplicitBinding*/ {"binding 显式绑定",
            {450, nullptr, 0},
            {420, "GL_ARB_shading_language_420pack", 300},
            {310, nullptr, 0},
            { -1, nullptr, 0},
            {20000, nullptr, 0}},
        /*UintType       */ {"u* 无符号整型",
            {450, nullptr, 0},
            {130, nullptr, 0},
            {300, nullptr, 0},
            {300, nullptr, 0},
            {10000, nullptr, 0}},
        /*VertexIdBuiltin*/ {"vertex_id/instance_id 内建",
            {450, nullptr, 0},
            {130, nullptr, 0},
            {300, nullptr, 0},   // ES 100 无 gl_VertexID（也无标准扩展）
            {300, nullptr, 0},
            {10000, nullptr, 0}},
        /*FragDepthBuiltin*/ {"frag_depth 内建",
            {450, nullptr, 0},
            {110, nullptr, 0},
            {300, "GL_EXT_frag_depth", 100},   // ES 100 经扩展 → gl_FragDepthEXT
            {300, "GL_EXT_frag_depth", 100},
            {10000, nullptr, 0}},
        /*MultiRenderTarget*/ {"frag 多输出(MRT)",
            {450, nullptr, 0},
            {110, nullptr, 0},
            {300, "GL_EXT_draw_buffers", 100}, // ES 100 经扩展 → gl_FragData[i]
            {300, nullptr, 0},
            {10000, nullptr, 0}},
    };
    return TABLE[(int)c];
}

// 某能力在给定 api 的需求行。
inline const CapReq& capReq(Cap c, GlApi a) {
    const CapRow& r = capRow(c);
    switch (a) {
        case GlApi::Vulkan: return r.vulkan;
        case GlApi::GLCore: return r.glcore;
        case GlApi::GLES:   return r.gles;
        case GlApi::WebGL:  return r.webgl;
        case GlApi::Metal:  return r.metal;
        case GlApi::D3D:    return r.vulkan;   // HLSL 经 SPIRV-Cross 吃 Vulkan 语义，SM5 能力等同
    }
    return r.vulkan;
}

// 某能力在给定 api 的核心起始版本（-1 = 核心永不支持）。
inline int capMinVersion(Cap c, GlApi a) { return capReq(c, a).core; }

// 能力满足途径判定。
enum class CapVia {
    No,     // 不满足
    Core,   // 核心版本直接支持
    Ext,    // 经目标声明的替代扩展满足（codegen 须发射 #extension）
};

// 目标 t 对能力 c 的满足途径；经扩展满足时 outExt 给出扩展名（可 NULL）。
inline CapVia capResolve(Cap c, const GlslTarget& t, const char** outExt = nullptr) {
    if (outExt) *outExt = nullptr;
    // D3D（HLSL/SM）版本是着色模型整数（50=SM5.0），与 GLSL #version 尺度不同，
    // 不能借 vulkan 列的版本阈值比对。D3D11 SM5.0 支持本能力集全部：
    //   storage/compute/double 需 SM5.0（50）；其余（binding/uint/vertex_id/
    //   frag_depth/MRT/多描述符集经 register space）SM4.0（40）即可。
    if (t.isD3D()) {
        int need = (c == Cap::StorageBuffer || c == Cap::ComputeStage ||
                    c == Cap::DoubleType) ? 50 : 40;
        return (t.version >= need) ? CapVia::Core : CapVia::No;
    }
    const CapReq& q = capReq(c, t.api);
    if (q.core >= 0 && t.version >= q.core) return CapVia::Core;
    if (q.ext && t.version >= q.extFrom && t.hasExtension(q.ext)) {
        if (outExt) *outExt = q.ext;
        return CapVia::Ext;
    }
    return CapVia::No;
}

// 目标 t 是否支持能力 c（核心或扩展任一途径）。
inline bool capSupported(Cap c, const GlslTarget& t) {
    return capResolve(c, t) != CapVia::No;
}
