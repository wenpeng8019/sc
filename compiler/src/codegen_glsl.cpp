#include "codegen_glsl.h"
#include "codegen_spirv.h"
#include "lexer.h"
#include "parser.h"
#include "shader_sema.h"
#include "shader_spv.h"
#include "shader_msl.h"
#include "error.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ============================================================
// codegen_glsl 实现（syntax-s 一期）
// ============================================================
// 把 .ss 里的着色阶段（vert/frag/comp）+ I/O 结构体 + 资源块 + 辅助函数
// 翻译为 Vulkan-GLSL 文本，并产出反射清单 JSON。GLSL→SPIR-V→各后端交给
// 运行时的 glslang/SPIRV-Cross/MoltenVK（见 syntax-s §10）。
//
// 阶段 I/O 采用「成员改写」模型（无需在 GLSL 里重建结构体）：
//   · 顶点属性：vert 入参结构体的 loc 字段 → layout(location=N) in <t> <field>;
//     体内 `in.field` 改写为该全局名。
//   · varying：vert 返回结构体字段 → out；frag 同结构体入参 → in；location 按
//     字段序自动配对（vert out 与 frag in 用同一份 @def，天然一致）；builtin
//     字段映射到 gl_Position / gl_FragCoord 等，不占 location。
//   · 片元标量返回（vec4）→ layout(location=0) out；`return e` 改写为写该输出。
//   · uniform/storage/push 结构体 → layout(...) uniform/buffer 块；sampler 全局 →
//     layout(...) uniform sampler2D。
// ============================================================

namespace {

// sc 标量类型名 → GLSL 类型名。向量/矩阵/自定义原样透传。
std::string mapType(const std::string& n) {
    if (n == "f4") return "float";
    if (n == "f8") return "double";
    if (n == "i1" || n == "i2" || n == "i4") return "int";
    if (n == "u1" || n == "u2" || n == "u4") return "uint";
    if (n == "bool") return "bool";
    if (n == "void" || n.empty()) return "void";
    return n;   // vec2/3/4、ivec*、mat*、sampler2D 及自定义类型原样保留
}

// 遗留 ES（GLSL ES 100 = OpenGL ES 2.0）：发射形态整体切换——
// attribute/varying、gl_FragColor/gl_FragData、无 layout 限定、无 uniform 块
//（平铺为普通 uniform）、无数组构造器（降级逐元素赋值）、texture2D()。
bool legacyES(const GlslTarget& t) { return t.isES() && t.version < 300; }

// 内建语义名（sc 标注）→ GLSL 内建变量。区分输入侧 / 输出侧、以及目标 API：
// Vulkan 用 gl_VertexIndex/gl_InstanceIndex；GL/ES 用 gl_VertexID/gl_InstanceID。
std::string builtinGlsl(const std::string& sem, bool asOutput, const GlslTarget& t) {
    if (sem == "position")    return asOutput ? "gl_Position" : "gl_FragCoord";
    if (sem == "frag_coord")  return "gl_FragCoord";
    if (sem == "frag_depth")  return legacyES(t) ? "gl_FragDepthEXT"   // GL_EXT_frag_depth
                                                 : "gl_FragDepth";
    if (sem == "vertex_id")   return t.isVulkan() ? "gl_VertexIndex"   : "gl_VertexID";
    if (sem == "instance_id") return t.isVulkan() ? "gl_InstanceIndex" : "gl_InstanceID";
    /* 计算阶段（comp）内建 */
    if (sem == "global_invocation_id")    return "gl_GlobalInvocationID";   /* uvec3 */
    if (sem == "local_invocation_id")     return "gl_LocalInvocationID";    /* uvec3 */
    if (sem == "workgroup_id")            return "gl_WorkGroupID";          /* uvec3 */
    if (sem == "num_workgroups")          return "gl_NumWorkGroups";        /* uvec3 */
    if (sem == "local_invocation_index")  return "gl_LocalInvocationIndex"; /* uint */
    return "gl_" + sem;       // 兔底：原样加前缀（未知语义）
}

// comp 阶段的 uvec3 内建：字段声明为标量（u4/i4）时自动取 .x（1D 调度惯用）。
bool builtinIsUvec3(const std::string& sem) {
    return sem == "global_invocation_id" || sem == "local_invocation_id" ||
           sem == "workgroup_id" || sem == "num_workgroups";
}

// std140 / std430 布局的对齐与大小（字节），支持标量 / 向量 / 方阵 / 数组。
struct Layout { int align; int size; };

int roundUp(int v, int a) { return a ? ((v + a - 1) / a) * a : v; }

Layout layoutOf(const std::string& t, const std::vector<std::string>& dims, bool std430) {
    int a = 4, s = 4;
    if (t == "vec2" || t == "ivec2" || t == "uvec2" || t == "bvec2") { a = 8;  s = 8;  }
    else if (t == "vec3" || t == "ivec3" || t == "uvec3" || t == "bvec3") { a = 16; s = 12; }
    else if (t == "vec4" || t == "ivec4" || t == "uvec4" || t == "bvec4") { a = 16; s = 16; }
    else if (t == "mat2") { a = 16; s = 32; }   // std140: 列按 vec4 对齐
    else if (t == "mat3") { a = 16; s = 48; }
    else if (t == "mat4") { a = 16; s = 64; }
    // 标量 float/int/uint/bool 及别名走默认 {4,4}
    if (!dims.empty()) {
        int count = 1;
        for (const auto& d : dims) count *= (d.empty() ? 1 : std::atoi(d.c_str()));
        int stride = std430 ? s : roundUp(s, 16);   // std140 数组步长向上取整到 16
        a = std430 ? a : std::max(a, 16);
        s = stride * count;
    }
    return {a, s};
}

const char* stageExt(ShaderStage st) {
    switch (st) {
        case ShaderStage::Vert: return "vert";
        case ShaderStage::Frag: return "frag";
        case ShaderStage::Comp: return "comp";
        default: return "glsl";
    }
}

// JSON 字符串转义（够用即可）。
std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else o += c;
    }
    return o + "\"";
}

// ---- 表达式 / 语句发射器 -------------------------------------------------
struct Emitter {
    std::ostringstream os;
    int indent = 1;
    const std::unordered_map<std::string, std::string>* memberMap = nullptr;  // "base.field" → 替换名
    const std::unordered_set<std::string>* outAggVars = nullptr;              // 输出聚合局部变量名
    std::string scalarOut;   // 片元标量返回的目标输出名（空 = 非标量返回）
    bool legacy = false;     // GLSL ES 100 遗留模式（无数组构造器、texture2D）

    void pad() { for (int i = 0; i < indent; i++) os << "    "; }

    // 语句语境的表达式：顶层二元/赋值不加外层括号（更干净，且优先级安全）。
    std::string exprTop(const Expr* e) {
        if (e && e->kind == Expr::Binary)
            return expr(e->a.get()) + " " + e->op + " " + expr(e->b.get());
        return expr(e);
    }

    // 数组字面量 {a, b, ...} → GLSL 数组构造器 TYPE[N](a, b, ...)。
    std::string initListArray(const TypeRef& t, const Expr* e) {
        std::string dim = t.arrayDims.empty() ? "" : t.arrayDims.front();
        std::string s = mapType(t.name) + "[" + dim + "](";
        for (size_t i = 0; i < e->args.size(); i++) {
            if (i) s += ", ";
            s += expr(e->args[i].get());
        }
        return s + ")";
    }

    std::string expr(const Expr* e) {
        if (!e) return "";
        switch (e->kind) {
            case Expr::Ident:
            case Expr::IntLit:
            case Expr::CharLit:
                return e->text;
            case Expr::FloatLit: {
                std::string s = e->text;
                if (s.find('.') == std::string::npos && s.find('e') == std::string::npos &&
                    s.find('E') == std::string::npos)
                    s += ".0";
                return s;
            }
            case Expr::StrLit:
                return "/* str */";
            case Expr::Unary:
                return e->op + expr(e->a.get());
            case Expr::PostUnary:
                return expr(e->a.get()) + e->op;
            case Expr::Binary:
                return "(" + expr(e->a.get()) + " " + e->op + " " + expr(e->b.get()) + ")";
            case Expr::Ternary:
                return "(" + expr(e->a.get()) + " ? " + expr(e->b.get()) +
                       " : " + expr(e->c.get()) + ")";
            case Expr::Index:
                return expr(e->a.get()) + "[" + expr(e->b.get()) + "]";
            case Expr::Member: {
                if (memberMap && e->a && e->a->kind == Expr::Ident) {
                    auto it = memberMap->find(e->a->text + "." + e->text);
                    if (it != memberMap->end()) return it->second;
                }
                return expr(e->a.get()) + "." + e->text;
            }
            case Expr::Call: {
                std::string callee = expr(e->a.get());
                if (legacy && callee == "texture") callee = "texture2D";   // ES 100 无重载 texture()
                std::string s = callee + "(";
                for (size_t i = 0; i < e->args.size(); i++) {
                    if (i) s += ", ";
                    s += expr(e->args[i].get());
                }
                return s + ")";
            }
            case Expr::Cast:
                return mapType(e->op) + "(" + expr(e->a.get()) + ")";
            default:
                return "/* TODO expr */";
        }
    }

    // 返回 true 表示该顶层语句已被 I/O 改写「吸收」（不再普通发射）。
    bool absorbedTop(const Stmt* s) {
        if (s->kind == Stmt::VarS && outAggVars) {
            for (const auto& f : s->decls)
                if (outAggVars->count(f.name)) return true;   // 输出聚合局部：跳过声明
        }
        if (s->kind == Stmt::ReturnS) {
            if (!scalarOut.empty()) {
                if (s->expr) { pad(); os << scalarOut << " = " << expr(s->expr.get()) << ";\n"; }
                pad(); os << "return;\n";
                return true;
            }
            if (outAggVars && s->expr && s->expr->kind == Expr::Ident &&
                outAggVars->count(s->expr->text))
                return true;   // return <聚合>：字段已散射，丢弃
        }
        return false;
    }

    void stmt(const Stmt* s) {
        if (!s) return;
        if (indent == 1 && absorbedTop(s)) return;
        switch (s->kind) {
            case Stmt::ExprS:
                pad(); os << exprTop(s->expr.get()) << ";\n";
                break;
            case Stmt::ReturnS:
                pad();
                if (s->expr) os << "return " << expr(s->expr.get()) << ";\n";
                else os << "return;\n";
                break;
            case Stmt::VarS:
            case Stmt::LetS:
                for (const auto& f : s->decls) {
                    // ES 100 无数组构造器：声明后逐元素赋值（const 随之丢弃）
                    if (legacy && f.init && f.init->kind == Expr::InitList &&
                        !f.type.arrayDims.empty()) {
                        pad();
                        os << mapType(f.type.name) << " " << f.name;
                        for (const auto& d : f.type.arrayDims) os << "[" << d << "]";
                        os << ";\n";
                        for (size_t i = 0; i < f.init->args.size(); i++) {
                            pad();
                            os << f.name << "[" << i << "] = "
                               << expr(f.init->args[i].get()) << ";\n";
                        }
                        continue;
                    }
                    pad();
                    if (s->kind == Stmt::LetS) os << "const ";
                    os << mapType(f.type.name) << " " << f.name;
                    for (const auto& d : f.type.arrayDims) os << "[" << d << "]";
                    if (f.init) {
                        if (f.init->kind == Expr::InitList && !f.type.arrayDims.empty())
                            os << " = " << initListArray(f.type, f.init.get());
                        else
                            os << " = " << expr(f.init.get());
                    }
                    os << ";\n";
                }
                break;
            case Stmt::IfS:
                pad(); os << "if (" << expr(s->expr.get()) << ") {\n";
                indent++; for (const auto& b : s->body) stmt(b.get()); indent--;
                pad(); os << "}";
                if (!s->elseBody.empty()) {
                    os << " else {\n";
                    indent++; for (const auto& b : s->elseBody) stmt(b.get()); indent--;
                    pad(); os << "}";
                }
                os << "\n";
                break;
            case Stmt::WhileS:
                pad(); os << "while (" << expr(s->expr.get()) << ") {\n";
                indent++; for (const auto& b : s->body) stmt(b.get()); indent--;
                pad(); os << "}\n";
                break;
            default:
                pad(); os << "// TODO stmt\n";
                break;
        }
    }
};

// ---- 程序级前处理：收集结构体与资源 --------------------------------------
struct Model {
    std::unordered_map<std::string, const Decl*> structs;  // 名 → StructD
    std::vector<const Decl*> resources;                    // uniform/storage/push/sampler
};

Model buildModel(const Program& prog) {
    Model m;
    for (const auto& d : prog.decls) {
        if (!d) continue;
        if (d->kind == Decl::StructD) {
            m.structs[d->name] = d.get();
            if (d->shaderAttr && d->shaderAttr->res != ShaderDeclAttr::None)
                m.resources.push_back(d.get());
        } else if (d->kind == Decl::VarD && d->shaderAttr) {
            m.resources.push_back(d.get());   // sampler/image 全局
        }
    }
    return m;
}

// 资源块 GLSL（对所有阶段一致发射；未用者对编译无害）。
// legacy ES（GLSL ES 100）无 uniform 块：平铺为普通 uniform（名 = 块_字段，
// 运行时按名 glUniform 上传；块成员访问由 emitStage 的 memberMap 改写）。
std::string emitResources(const Model& m, const GlslTarget& t) {
    const bool useSet  = t.useSetQualifier();                 // 仅 Vulkan 有 set= 限定
    const bool explicitBind = capSupported(Cap::ExplicitBinding, t);
    const bool legacy = legacyES(t);
    std::string out;
    for (const Decl* r : m.resources) {
        auto* a = r->shaderAttr.get();
        std::string bind;
        auto add = [&](const std::string& s) { bind += (bind.empty() ? "" : ", ") + s; };
        if (a->res == ShaderDeclAttr::Push) bind = "push_constant";   // 仅 Vulkan 抵达（已语义门控）
        else if (!legacy) {
            if (useSet && a->set >= 0)          add("set=" + std::to_string(a->set));
            if (explicitBind && a->binding >= 0) add("binding=" + std::to_string(a->binding));
        }
        if (r->kind == Decl::VarD) {                       // sampler / image 全局
            const Field& f = r->structCommon.fields.front();
            std::string pfx = bind.empty() ? "" : ("layout(" + bind + ") ");
            out += pfx + "uniform " + mapType(f.type.name) + " " + f.name + ";\n";
            continue;
        }
        if (legacy) {                                      // 块平铺（仅 uniform 可抵达）
            for (const auto& f : r->structCommon.fields) {
                out += "uniform " + mapType(f.type.name) + " " + r->name + "_" + f.name;
                for (const auto& d : f.type.arrayDims) out += "[" + d + "]";
                out += ";\n";
            }
            continue;
        }
        bool std430 = a->res == ShaderDeclAttr::Storage;   // uniform / storage / push 块
        add(std430 ? "std430" : "std140");
        std::string blockKw = std430 ? "buffer" : "uniform";
        out += "layout(" + bind + ") " + blockKw + " " + r->name + "_blk {\n";
        for (const auto& f : r->structCommon.fields) {
            out += "    " + mapType(f.type.name) + " " + f.name;
            for (const auto& d : f.type.arrayDims) out += "[" + d + "]";
            out += ";\n";
        }
        out += "} " + r->name + ";\n";
    }
    if (!out.empty()) out += "\n";
    return out;
}

// 辅助函数（.ss 里的普通 fnc）GLSL。
std::string emitHelper(const Decl& d) {
    std::string ret = d.structCommon.type ? mapType(d.structCommon.type->name) : "void";
    std::string out = ret + " " + d.name + "(";
    for (size_t i = 0; i < d.structCommon.fields.size(); i++) {
        const auto& p = d.structCommon.fields[i];
        if (i) out += ", ";
        out += mapType(p.type.name) + " " + p.name;
    }
    out += ") {\n";
    Emitter em;
    for (const auto& s : d.body) em.stmt(s.get());
    out += em.os.str();
    out += "}\n\n";
    return out;
}

// 发射单个阶段：装配 in/out 接口 + main() 体（含 I/O 改写）。
// usedCaps = shaderSemaCheck 采集的已用能力：经替代扩展满足的能力在
// #version 后发射 `#extension <名> : require`（能力矩阵 capResolve）。
std::string emitStage(const Decl& stage, const Model& m, const std::string& prelude,
                      const GlslTarget& t, const std::vector<Cap>& usedCaps) {
    const bool isVert = stage.shaderStage == ShaderStage::Vert;
    const bool isComp = stage.shaderStage == ShaderStage::Comp;
    const bool legacy = legacyES(t);
    std::unordered_map<std::string, std::string> memberMap;
    std::unordered_set<std::string> outAggVars;
    std::string ioDecls;
    std::string scalarOut;

    // legacy：uniform 块平铺后的成员访问改写（Params.a → Params_a）
    if (legacy)
        for (const Decl* r : m.resources)
            if (r->kind != Decl::VarD)
                for (const auto& f : r->structCommon.fields)
                    memberMap[r->name + "." + f.name] = r->name + "_" + f.name;

    // 输入/输出限定词：现代 GLSL 用 layout(location=N) in/out；
    // legacy ES 用 attribute（vert 入）/ varying（vert 出 + frag 入）。
    auto inQual = [&](int loc) {
        if (legacy) return std::string(isVert ? "attribute " : "varying ");
        return "layout(location=" + std::to_string(loc) + ") in ";
    };
    auto outQual = [&](int loc) {
        if (legacy) return std::string("varying ");
        return "layout(location=" + std::to_string(loc) + ") out ";
    };

    // comp：工作组尺寸（暂无 .ss 语法，固定 64×1×1；反射清单同步携带，
    // 运行时据此设 threads_per_group）
    if (isComp)
        ioDecls += "layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;\n";

    // —— 输入接口（入参结构体）——
    int autoInLoc = 0;
    for (const auto& p : stage.structCommon.fields) {
        auto it = m.structs.find(p.type.name);
        if (it == m.structs.end()) {   // 标量/向量入参：单个顶点属性
            ioDecls += inQual(autoInLoc++) + mapType(p.type.name) + " " + p.name + ";\n";
            continue;
        }
        for (const auto& f : it->second->structCommon.fields) {
            std::string sem = f.shaderAttr ? f.shaderAttr->builtin : "";
            if (!sem.empty()) {
                std::string g = builtinGlsl(sem, /*out*/false, t);
                // comp 的 uvec3 内建 + 标量字段声明 → 自动取 .x（1D 调度惯用）
                std::string mt = mapType(f.type.name);
                if (isComp && builtinIsUvec3(sem) && (mt == "uint" || mt == "int"))
                    g += ".x";
                memberMap[p.name + "." + f.name] = g;
                continue;
            }
            int loc = (f.shaderAttr && f.shaderAttr->loc >= 0) ? f.shaderAttr->loc : autoInLoc++;
            std::string g = isVert ? f.name : ("v_" + f.name);
            memberMap[p.name + "." + f.name] = g;
            ioDecls += inQual(loc) + mapType(f.type.name) + " " + g + ";\n";
        }
    }

    // —— 输出接口（返回类型）——
    // legacy frag：无自定义 out——单输出写 gl_FragColor，多输出（MRT，已能力
    // 门控 GL_EXT_draw_buffers）全部写 gl_FragData[loc]（两者不可混用）。
    std::string retType = stage.structCommon.type ? stage.structCommon.type->name : "void";
    auto rit = m.structs.find(retType);
    if (retType != "void" && !retType.empty() && rit != m.structs.end()) {
        for (const auto& s : stage.body)   // 先找输出聚合局部变量（type == retType）
            if (s->kind == Stmt::VarS)
                for (const auto& f : s->decls)
                    if (f.type.name == retType) outAggVars.insert(f.name);

        int fragOuts = 0;   // 非 builtin 输出数（legacy frag 选 gl_FragColor/gl_FragData）
        if (legacy && !isVert)
            for (const auto& f : rit->second->structCommon.fields)
                if (!f.shaderAttr || f.shaderAttr->builtin.empty()) fragOuts++;

        int autoOutLoc = 0;
        for (const auto& f : rit->second->structCommon.fields) {
            std::string sem = f.shaderAttr ? f.shaderAttr->builtin : "";
            std::string target;
            if (!sem.empty()) {
                target = builtinGlsl(sem, /*out*/true, t);   // gl_Position / gl_FragDepth
            } else {
                int loc = (f.shaderAttr && f.shaderAttr->loc >= 0) ? f.shaderAttr->loc : autoOutLoc++;
                if (legacy && !isVert) {
                    target = fragOuts >= 2 ? ("gl_FragData[" + std::to_string(loc) + "]")
                                           : "gl_FragColor";
                } else {
                    std::string g = isVert ? ("v_" + f.name) : ("f_" + f.name);
                    ioDecls += outQual(loc) + mapType(f.type.name) + " " + g + ";\n";
                    target = g;
                }
            }
            for (const auto& v : outAggVars) memberMap[v + "." + f.name] = target;
        }
    } else if (retType != "void" && !retType.empty()) {
        if (legacy) {
            scalarOut = "gl_FragColor";   // legacy：直写内建，无声明
        } else {
            scalarOut = "f_color";   // 标量/向量返回（典型 frag vec4）
            ioDecls += outQual(0) + mapType(retType) + " f_color;\n";
        }
    }

    // —— 装配 ——
    std::ostringstream out;
    out << "#version " << t.version;
    if (const char* prof = t.profileWord(); prof && *prof) out << " " << prof;
    out << "\n";
    // 经替代扩展满足的能力 → #extension 指令（去重：多能力可共用同一扩展；
    // 阶段过滤：frag/comp 专属能力的扩展不进其他阶段）
    {
        auto capInStage = [&](Cap c) {
            switch (c) {
                case Cap::FragDepthBuiltin:
                case Cap::MultiRenderTarget: return stage.shaderStage == ShaderStage::Frag;
                case Cap::ComputeStage:      return isComp;
                default: return true;
            }
        };
        std::vector<const char*> emitted;
        for (Cap c : usedCaps) {
            if (!capInStage(c)) continue;
            const char* ext = nullptr;
            if (capResolve(c, t, &ext) == CapVia::Ext && ext) {
                bool dup = false;
                for (const char* e : emitted) if (std::string(e) == ext) { dup = true; break; }
                if (!dup) {
                    out << "#extension " << ext << " : require\n";
                    emitted.push_back(ext);
                }
            }
        }
    }
    out << "// generated from " << stage.name << " (" << stageExt(stage.shaderStage)
        << ") target " << glTargetTag(t) << "\n\n";
    if (t.isES()) {
        // legacy frag：ES2 硬件不保证片元 highp，mediump 是安全缺省
        const char* prec = (legacy && !isVert) ? "mediump" : "highp";
        out << "precision " << prec << " float;\nprecision " << prec << " int;\n\n";
    }
    out << prelude;
    if (!ioDecls.empty()) out << ioDecls << "\n";

    Emitter em;
    em.memberMap = &memberMap;
    em.outAggVars = outAggVars.empty() ? nullptr : &outAggVars;
    em.scalarOut = scalarOut;
    em.legacy = legacy;
    for (const auto& s : stage.body) em.stmt(s.get());

    out << "void main() {\n" << em.os.str() << "}\n";
    return out.str();
}

} // namespace

std::vector<GlslUnit> emitGlsl(const Program& prog, const GlslTarget& target) {
    Model m = buildModel(prog);
    std::string prelude = emitResources(m, target);
    for (const auto& d : prog.decls)   // 辅助函数放在资源之后、各 main 之前
        if (d && d->kind == Decl::FuncD && d->shaderStage == ShaderStage::None)
            prelude += emitHelper(*d);

    std::vector<GlslUnit> units;
    for (const auto& d : prog.decls) {
        if (!d || d->kind != Decl::FuncD || d->shaderStage == ShaderStage::None) continue;
        std::string text = emitStage(*d, m, prelude, target, prog.shaderUsedCaps);
        units.push_back(GlslUnit{d->shaderStage, d->name, stageExt(d->shaderStage), text});
    }
    return units;
}

std::string emitReflectionJson(const Program& prog, const GlslTarget& target) {
    Model m = buildModel(prog);
    const bool useSet = target.useSetQualifier();
    const bool explicitBind = capSupported(Cap::ExplicitBinding, target);
    std::ostringstream j;
    j << "{\n";
    j << "  \"target\": {\"api\": " << jstr(glApiName(target.api))
      << ", \"version\": " << target.version
      << ", \"explicitBinding\": " << (explicitBind ? "true" : "false")
      << ", \"flattenUniforms\": " << (legacyES(target) ? "true" : "false") << "},\n";
    j << "  \"stages\": [\n";

    bool firstStage = true;
    for (const auto& d : prog.decls) {
        if (!d || d->kind != Decl::FuncD || d->shaderStage == ShaderStage::None) continue;
        if (!firstStage) j << ",\n";
        firstStage = false;
        j << "    {\n";
        j << "      \"name\": " << jstr(d->name) << ",\n";
        j << "      \"stage\": " << jstr(stageExt(d->shaderStage)) << ",\n";
        j << "      \"entry\": \"main\",\n";
        j << "      \"file\": " << jstr(d->name + "." + stageExt(d->shaderStage)) << ",\n";
        if (d->shaderStage == ShaderStage::Comp)
            j << "      \"local_size\": [64, 1, 1],\n";   /* 与 emitStage 固定值同步 */

        j << "      \"inputs\": [";
        bool firstIn = true; int autoLoc = 0;
        for (const auto& p : d->structCommon.fields) {
            auto it = m.structs.find(p.type.name);
            if (it == m.structs.end()) {
                if (!firstIn) j << ", "; firstIn = false;
                j << "{\"name\": " << jstr(p.name) << ", \"type\": " << jstr(mapType(p.type.name))
                  << ", \"location\": " << autoLoc++ << "}";
                continue;
            }
            for (const auto& f : it->second->structCommon.fields) {
                std::string sem = f.shaderAttr ? f.shaderAttr->builtin : "";
                if (!sem.empty()) continue;
                int loc = (f.shaderAttr && f.shaderAttr->loc >= 0) ? f.shaderAttr->loc : autoLoc++;
                if (!firstIn) j << ", "; firstIn = false;
                j << "{\"name\": " << jstr(f.name) << ", \"type\": " << jstr(mapType(f.type.name))
                  << ", \"location\": " << loc << "}";
            }
        }
        j << "],\n";

        j << "      \"outputs\": [";
        bool firstOut = true;
        std::string retType = d->structCommon.type ? d->structCommon.type->name : "void";
        auto rit = m.structs.find(retType);
        if (retType != "void" && !retType.empty() && rit != m.structs.end()) {
            int autoOut = 0;
            for (const auto& f : rit->second->structCommon.fields) {
                std::string sem = f.shaderAttr ? f.shaderAttr->builtin : "";
                if (!firstOut) j << ", "; firstOut = false;
                if (!sem.empty()) {
                    j << "{\"name\": " << jstr(f.name) << ", \"builtin\": " << jstr(sem) << "}";
                    continue;
                }
                int loc = (f.shaderAttr && f.shaderAttr->loc >= 0) ? f.shaderAttr->loc : autoOut++;
                j << "{\"name\": " << jstr(f.name) << ", \"type\": " << jstr(mapType(f.type.name))
                  << ", \"location\": " << loc << "}";
            }
        } else if (retType != "void" && !retType.empty()) {
            j << "{\"name\": \"f_color\", \"type\": " << jstr(mapType(retType)) << ", \"location\": 0}";
        }
        j << "]\n    }";
    }
    j << "\n  ],\n";

    j << "  \"resources\": [\n";
    bool firstRes = true;
    for (const Decl* r : m.resources) {
        auto* a = r->shaderAttr.get();
        int rset  = useSet ? a->set : -1;              // 仅 Vulkan 发射 set=
        int rbind = explicitBind ? a->binding : -1;    // 低版本无显式 binding → 按名绑定
        if (!firstRes) j << ",\n";
        firstRes = false;
        if (r->kind == Decl::VarD) {
            const Field& f = r->structCommon.fields.front();
            j << "    {\"name\": " << jstr(f.name)
              << ", \"kind\": \"sampler\", \"type\": " << jstr(mapType(f.type.name))
              << ", \"set\": " << rset << ", \"binding\": " << rbind << "}";
            continue;
        }
        const char* kind = a->res == ShaderDeclAttr::Storage ? "storage"
                         : a->res == ShaderDeclAttr::Push ? "push" : "uniform";
        bool std430 = a->res == ShaderDeclAttr::Storage;
        j << "    {\"name\": " << jstr(r->name) << ", \"kind\": " << jstr(kind)
          << ", \"set\": " << rset << ", \"binding\": " << rbind
          << ", \"layout\": " << jstr(std430 ? "std430" : "std140") << ",\n";
        j << "     \"members\": [";
        int off = 0; bool firstMem = true;
        for (const auto& f : r->structCommon.fields) {
            Layout lay = layoutOf(f.type.name, f.type.arrayDims, std430);
            off = roundUp(off, lay.align);
            if (!firstMem) j << ", "; firstMem = false;
            j << "{\"name\": " << jstr(f.name) << ", \"type\": " << jstr(mapType(f.type.name))
              << ", \"offset\": " << off << ", \"size\": " << lay.size << "}";
            off += lay.size;
        }
        j << "], \"size\": " << roundUp(off, 16) << "}";
    }
    j << "\n  ]\n}\n";
    return j.str();
}

// ShaderStage（AST）→ SpvStage（glslang 封装）阶段种类映射。
static scc_shader::SpvStage toSpvStage(ShaderStage s) {
    switch (s) {
        case ShaderStage::Frag: return scc_shader::SpvStage::Fragment;
        case ShaderStage::Comp: return scc_shader::SpvStage::Compute;
        default:                return scc_shader::SpvStage::Vertex;
    }
}

int compileShaderSource(const std::string& src, const std::string& srcPath,
                        const std::string& outDir) {
    try {
        Program prog = parse(lex(src), /*shaderMode*/ true);

        // 外部设备能力档案（tar "file.caps"）加载，在能力门控前完成 ——
        // 使门控与发射都看到完整的 api/版本/扩展集。搜索顺序：
        //   绝对路径 → 相对 .ss 源文件目录 → builtins/gpu/caps/（标准档案库，
        //   随 --builtins 目标适配目录整体替换）
        for (auto& t : prog.shaderTargets) {
            if (t.profile.empty()) continue;
            std::filesystem::path pp(t.profile);
            if (pp.is_relative()) {
                std::filesystem::path rel =
                    std::filesystem::path(srcPath).parent_path() / pp;
                if (std::filesystem::exists(rel)) {
                    pp = rel;
                } else {
                    // builtins/gpu/caps/：从源文件目录与 cwd 逐级向上找 builtins
                    auto findCaps = [&](std::filesystem::path base)
                        -> std::filesystem::path {
                        for (; !base.empty(); base = base.parent_path()) {
                            std::filesystem::path c =
                                base / "builtins" / "gpu" / "caps" / t.profile;
                            if (std::filesystem::exists(c)) return c;
                            if (base == base.parent_path()) break;
                        }
                        return {};
                    };
                    std::filesystem::path hit = findCaps(
                        std::filesystem::absolute(srcPath).parent_path());
                    if (hit.empty()) hit = findCaps(std::filesystem::current_path());
                    pp = hit.empty() ? rel : hit;   // 都没有 → 保留相对路径报错
                }
            }
            std::ifstream pf(pp);
            if (!pf) {
                std::fprintf(stderr, "ss: 无法打开能力档案 %s\n", pp.string().c_str());
                return 1;
            }
            std::stringstream pss; pss << pf.rdbuf();
            std::string perr;
            if (!parseCapsProfile(pss.str(), t, perr)) {
                std::fprintf(stderr, "ss: 能力档案 %s 解析失败：%s\n",
                             pp.string().c_str(), perr.c_str());
                return 1;
            }
        }

        shaderSemaCheck(prog);                  // 子集强制 + 基础结构检查 + 能力门控（syntax-s §9/§13.1）

        if (prog.shaderTargets.empty()) {       // GLSL 生成必须声明目标（无 CLI 默认）
            std::fprintf(stderr, "ss: %s 未声明转义目标（需顶部 `tar`，如 `tar vulkan@450`）\n",
                         srcPath.c_str());
            return 1;
        }

        std::string stem = std::filesystem::path(srcPath).stem().string();
        const bool multi = prog.shaderTargets.size() > 1;

        std::error_code ec;
        if (!outDir.empty()) std::filesystem::create_directories(outDir, ec);
        auto write = [&](const std::string& path, const std::string& data) -> bool {
            FILE* f = std::fopen(path.c_str(), "wb");
            if (!f) { std::fprintf(stderr, "ss: 无法写入 %s\n", path.c_str()); return false; }
            std::fwrite(data.data(), 1, data.size(), f);
            std::fclose(f);
            std::fprintf(stderr, "ss: 生成 %s\n", path.c_str());
            return true;
        };

        for (const auto& target : prog.shaderTargets) {
            // 单目标沿用简洁命名；多目标插入目标标签（entry.gles300.frag、stem.metal20000.reflect.json）。
            std::string tag = multi ? ("." + glTargetTag(target)) : "";

            // ---- Vulkan 目标：自研 AST→SPIR-V 直发，产物 <entry>.spv ----
            // （三期主路径首个落点；spirv-val/spirv-dis/SPIRV-Cross 均可直接消费）
            if (target.isVulkan()) {
                auto sunits = emitSpirv(prog, target);
                if (sunits.empty()) {
                    std::fprintf(stderr, "ss: %s 中未找到着色阶段入口（vert/frag/comp）\n", srcPath.c_str());
                    return 1;
                }
                std::string reflect = emitReflectionJson(prog, target);
                for (const auto& u : sunits) {
                    std::string bin((const char*)u.words.data(), u.words.size() * 4);
                    if (outDir.empty()) {
                        std::fprintf(stderr, "ss: %s%s.spv（%zu 字）需 -o 输出目录（二进制不入 stdout）\n",
                                     u.entry.c_str(), tag.c_str(), u.words.size());
                        continue;
                    }
                    if (!write(outDir + "/" + u.entry + tag + ".spv", bin)) return 1;
                }
                if (outDir.empty())
                    std::printf("// ===== %s%s.reflect.json =====\n%s\n",
                                stem.c_str(), tag.c_str(), reflect.c_str());
                else if (!write(outDir + "/" + stem + tag + ".reflect.json", reflect)) return 1;
                continue;
            }

            // ---- Metal 目标：内部走 Vulkan-GLSL(450) → SPIR-V(glslang) → MSL(spirv-cross) ----
            // 能力门控已在 shaderSemaCheck 按 metal 目标自身的能力矩阵完成；此处 Vulkan-GLSL
            // 仅作中间语（450 支持全部所需能力）。每阶段产 <entry>.metal（入口 main→阶段名）。
            if (target.isMetal()) {
                GlslTarget vk; vk.api = GlApi::Vulkan; vk.version = 450;
                auto units = emitGlsl(prog, vk);
                if (units.empty()) {
                    std::fprintf(stderr, "ss: %s 中未找到着色阶段入口（vert/frag/comp）\n", srcPath.c_str());
                    return 1;
                }
                std::string reflect = emitReflectionJson(prog, target);
                for (const auto& u : units) {
                    std::vector<uint32_t> spv = scc_shader::glslToSpirv(u.text, toSpvStage(u.stage));
                    scc_shader::MslOptions mo;
                    mo.mslVersion = (uint32_t)target.version;
                    mo.renameEntry = u.entry;            // 多阶段链入同一 metallib 时避免 main0 冲突
                    std::string msl = scc_shader::spirvToMsl(spv, mo);
                    if (outDir.empty())
                        std::printf("// ===== %s%s.metal =====\n%s\n",
                                    u.entry.c_str(), tag.c_str(), msl.c_str());
                    else if (!write(outDir + "/" + u.entry + tag + ".metal", msl)) return 1;
                }
                if (outDir.empty())
                    std::printf("// ===== %s%s.reflect.json =====\n%s\n",
                                stem.c_str(), tag.c_str(), reflect.c_str());
                else if (!write(outDir + "/" + stem + tag + ".reflect.json", reflect)) return 1;
                continue;
            }

            auto units = emitGlsl(prog, target);
            std::string reflect = emitReflectionJson(prog, target);

            if (units.empty()) {
                std::fprintf(stderr, "ss: %s 中未找到着色阶段入口（vert/frag/comp）\n", srcPath.c_str());
                return 1;
            }

            if (outDir.empty()) {
                for (const auto& u : units)
                    std::printf("// ===== %s%s.%s =====\n%s\n",
                                u.entry.c_str(), tag.c_str(), u.ext.c_str(), u.text.c_str());
                std::printf("// ===== %s%s.reflect.json =====\n%s\n",
                            stem.c_str(), tag.c_str(), reflect.c_str());
            } else {
                for (const auto& u : units)
                    if (!write(outDir + "/" + u.entry + tag + "." + u.ext, u.text)) return 1;
                if (!write(outDir + "/" + stem + tag + ".reflect.json", reflect)) return 1;
            }
        }
        return 0;
    } catch (const CompileError& e) {
        std::fprintf(stderr, "%s:%d: 着色器编译错误: %s\n", srcPath.c_str(), e.line, e.msg.c_str());
        return 1;
    }
}
