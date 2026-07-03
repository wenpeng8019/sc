#include "codegen_glsl.h"
#include "lexer.h"
#include "parser.h"
#include "shader_sema.h"
#include "error.h"

#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>

// ============================================================
// codegen_glsl 实现（syntax-g 一期 walking skeleton）
// ============================================================
// 把着色阶段体（本质是 sc 语句/表达式子集）翻译为 Vulkan-GLSL 文本。
// 当前覆盖：字面量 / 标识符 / 调用 / 成员(swizzle) / 下标 / 一元 / 二元 /
//           三元 / 赋值 / var|let 声明 / return / if。
// 未覆盖的形态发射为 // TODO 注释，保证产物仍是合法 GLSL 且不静默丢失信息。
// ============================================================

namespace {

// sc 标量类型名 → GLSL 类型名。非标量（vecN/matN/自定义）原样透传。
std::string mapType(const std::string& n) {
    if (n == "f4") return "float";
    if (n == "f8") return "double";
    if (n == "i1" || n == "i2" || n == "i4") return "int";
    if (n == "u1" || n == "u2" || n == "u4") return "uint";
    if (n == "bool") return "bool";
    if (n == "void" || n.empty()) return "void";
    return n;   // vec2/vec3/vec4/mat4/... 及自定义类型原样保留
}

struct GlslEmitter {
    std::ostringstream os;
    int indent = 1;     // main() 体内起始缩进层级

    void pad() { for (int i = 0; i < indent; i++) os << "    "; }

    // ---- 表达式 ----------------------------------------------------------
    std::string expr(const Expr* e) {
        if (!e) return "";
        switch (e->kind) {
            case Expr::Ident:
            case Expr::IntLit:
            case Expr::CharLit:
                return e->text;
            case Expr::FloatLit: {
                // GLSL 无隐式 double→float；裸浮点字面量补 'f' 保持 float 语义
                std::string s = e->text;
                if (s.find('.') == std::string::npos &&
                    s.find('e') == std::string::npos &&
                    s.find('E') == std::string::npos)
                    s += ".0";
                return s;
            }
            case Expr::StrLit:
                // GLSL 无字符串类型；仅作占位（正常着色器不应出现）
                return "/* str */";
            case Expr::Unary:
                return e->op + expr(e->a.get());
            case Expr::PostUnary:
                return expr(e->a.get()) + e->op;
            case Expr::Binary:
                return expr(e->a.get()) + " " + e->op + " " + expr(e->b.get());
            case Expr::Ternary:
                return "(" + expr(e->a.get()) + " ? " + expr(e->b.get()) +
                       " : " + expr(e->c.get()) + ")";
            case Expr::Index:
                return expr(e->a.get()) + "[" + expr(e->b.get()) + "]";
            case Expr::Member:
                // GLSL 无 -> ；成员访问与 swizzle 统一用 '.'
                return expr(e->a.get()) + "." + e->text;
            case Expr::Call: {
                std::string s = expr(e->a.get()) + "(";
                for (size_t i = 0; i < e->args.size(); i++) {
                    if (i) s += ", ";
                    s += expr(e->args[i].get());
                }
                return s + ")";
            }
            case Expr::Cast:
                // 类型转换按构造语法：float(x) / vec3(x) ...
                return mapType(e->op) + "(" + expr(e->a.get()) + ")";
            default:
                return "/* TODO expr */";
        }
    }

    // ---- 语句 ------------------------------------------------------------
    void stmt(const Stmt* s) {
        if (!s) return;
        switch (s->kind) {
            case Stmt::ExprS:
                pad(); os << expr(s->expr.get()) << ";\n";
                break;
            case Stmt::ReturnS:
                pad();
                if (s->expr) os << "return " << expr(s->expr.get()) << ";\n";
                else os << "return;\n";
                break;
            case Stmt::VarS:
            case Stmt::LetS:
                for (const auto& f : s->decls) {
                    pad();
                    os << mapType(f.type.name) << " " << f.name;
                    for (const auto& d : f.type.arrayDims)
                        os << "[" << d << "]";
                    if (f.init) os << " = " << expr(f.init.get());
                    os << ";\n";
                }
                break;
            case Stmt::IfS:
                pad(); os << "if (" << expr(s->expr.get()) << ") {\n";
                indent++;
                for (const auto& b : s->body) stmt(b.get());
                indent--;
                pad(); os << "}";
                if (!s->elseBody.empty()) {
                    os << " else {\n";
                    indent++;
                    for (const auto& b : s->elseBody) stmt(b.get());
                    indent--;
                    pad(); os << "}";
                }
                os << "\n";
                break;
            default:
                pad(); os << "// TODO stmt\n";
                break;
        }
    }
};

const char* stageExt(ShaderStage st) {
    switch (st) {
        case ShaderStage::Vert: return "vert";
        case ShaderStage::Frag: return "frag";
        case ShaderStage::Comp: return "comp";
        default: return "glsl";
    }
}

} // namespace

std::vector<GlslUnit> emitGlsl(const Program& prog) {
    std::vector<GlslUnit> units;
    for (const auto& d : prog.decls) {
        if (!d || d->kind != Decl::FuncD || d->shaderStage == ShaderStage::None)
            continue;

        GlslEmitter em;
        for (const auto& st : d->body) em.stmt(st.get());

        std::ostringstream out;
        out << "#version 450\n";
        out << "// generated from " << d->name
            << " (" << stageExt(d->shaderStage) << ")\n\n";
        out << "void main() {\n" << em.os.str() << "}\n";

        units.push_back(GlslUnit{
            d->shaderStage, d->name, stageExt(d->shaderStage), out.str()
        });
    }
    return units;
}

int compileShaderSource(const std::string& src, const std::string& srcPath,
                        const std::string& outDir) {
    try {
        Program prog = parse(lex(src), /*shaderMode*/ true);
        shaderSemaCheck(prog);                  // 子集强制 + 基础结构检查（syntax-g §9）
        auto units = emitGlsl(prog);

        if (units.empty()) {
            std::fprintf(stderr, "sg: %s 中未找到着色阶段入口（vert/frag/comp）\n",
                         srcPath.c_str());
            return 1;
        }

        if (outDir.empty()) {
            for (const auto& u : units) {
                std::printf("// ===== %s.%s =====\n%s\n",
                            u.entry.c_str(), u.ext.c_str(), u.text.c_str());
            }
        } else {
            std::error_code ec;
            std::filesystem::create_directories(outDir, ec);
            for (const auto& u : units) {
                std::string path = outDir + "/" + u.entry + "." + u.ext;
                FILE* f = std::fopen(path.c_str(), "wb");
                if (!f) {
                    std::fprintf(stderr, "sg: 无法写入 %s\n", path.c_str());
                    return 1;
                }
                std::fwrite(u.text.data(), 1, u.text.size(), f);
                std::fclose(f);
                std::fprintf(stderr, "sg: 生成 %s\n", path.c_str());
            }
        }
        return 0;
    } catch (const CompileError& e) {
        std::fprintf(stderr, "%s:%d: 着色器编译错误: %s\n",
                     srcPath.c_str(), e.line, e.msg.c_str());
        return 1;
    }
}
