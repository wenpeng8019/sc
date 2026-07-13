// ============================================================
// codegen_cpu 实现（syntax-s-design §17，M1 最小版）
// ============================================================
// 发射模型：
//   · 每个 comp kernel → 一个静态 C 函数
//       static void <entry>_impl(u32 gx0,gx1, gy0,gy1, gz0,gz1, void* const* bind)
//     函数内自带三重全局线程循环（外 gz/gy、内 gx），内循环给
//     vectorize 提示——SPMD 语义整体交给目标编译器向量化。
//   · 资源块 = bind[binding] 基指针 + 编译期偏移别名：
//       标量成员 → `T* restrict <Blk>_<f> = (T*)(p + off);` 经 (*x) 访问
//       数组成员 → `T* restrict <Blk>_<f> = (T*)(p + off);` 经 x[i] 访问
//     布局与 GPU 侧同一事实源规则（uniform=std140 / storage=std430）。
//   · 计算内建（标量声明惯例）：global_invocation_id→gx、
//     local_invocation_index→gx % local_x、workgroup_id→gx / local_x。
//   · 文件尾：注册表 + __attribute__((constructor)) 自注册到 spc cpu 后端
//     （契约结构 sc_spc_cpu_kernel 与 builtins/spc/src/cpu_spc.c 同步）。
//
// M1 限制（超出报 CompileError，M2 放行）：
//   · 仅标量/标量数组（vec*/mat*/sampler 报错）
//   · uniform 块成员限标量（std140 数组 stride 16 与 C 布局冲突）；push 未支持
//   · 无 barrier/shared/atomic/subgroup/spec（能力表 cpu 列已门控）
// ============================================================
#include "codegen_cpu.h"
#include "error.h"

#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// sc 标量类型名 → C 类型名（M1：仅标量；vec/mat/采样器返回空串=不支持）
std::string cType(const std::string& n) {
    if (n == "f4" || n == "float") return "float";
    if (n == "f8")   return "double";
    if (n == "f2")   return "_Float16";
    if (n == "i4" || n == "int")  return "int32_t";
    if (n == "u4" || n == "uint") return "uint32_t";
    if (n == "i1")   return "int8_t";
    if (n == "u1")   return "uint8_t";
    if (n == "i2")   return "int16_t";
    if (n == "u2")   return "uint16_t";
    if (n == "i8")   return "int64_t";
    if (n == "u8")   return "uint64_t";
    if (n == "bool") return "_Bool";
    if (n == "void" || n.empty()) return "void";
    return "";
}

// 标量布局（对齐=尺寸=字节数；与 codegen_glsl::layoutOf 标量规则同源）
int scalarSize(const std::string& t) {
    if (t == "f2" || t == "i2" || t == "u2") return 2;
    if (t == "i1" || t == "u1") return 1;
    if (t == "i8" || t == "u8" || t == "f8") return 8;
    return 4;
}
int roundUpC(int v, int a) { return a ? ((v + a - 1) / a) * a : v; }

// 数学内建 → C 函数/宏（M1 子集：float 语义；超出者报错）
const char* mathFn(const std::string& fn) {
    static const std::unordered_map<std::string, const char*> M = {
        {"sin", "sinf"}, {"cos", "cosf"}, {"tan", "tanf"},
        {"asin", "asinf"}, {"acos", "acosf"}, {"atan", "atanf"},
        {"exp", "expf"}, {"log", "logf"}, {"exp2", "exp2f"}, {"log2", "log2f"},
        {"pow", "powf"}, {"sqrt", "sqrtf"}, {"floor", "floorf"}, {"ceil", "ceilf"},
        {"round", "roundf"}, {"trunc", "truncf"}, {"abs", "fabsf"},
        {"min", "fminf"}, {"max", "fmaxf"}, {"fma", "fmaf"},
        {"inversesqrt", "SC_RSQRTF"}, {"fract", "SC_FRACTF"},
        {"clamp", "SC_CLAMPF"}, {"mix", "SC_MIXF"}, {"step", "SC_STEPF"},
        {"smoothstep", "SC_SMOOTHSTEPF"}, {"mod", "fmodf"},
    };
    auto it = M.find(fn);
    return it == M.end() ? nullptr : it->second;
}

struct Res {                        // 资源块视图
    const Decl* d = nullptr;
    int binding = -1;
    bool storage = false;           // std430；false = uniform std140
};

struct Model {
    std::unordered_map<std::string, const Decl*> structs;
    std::vector<Res> resources;
    const Decl* findStruct(const std::string& n) const {
        auto it = structs.find(n);
        return it == structs.end() ? nullptr : it->second;
    }
};

[[noreturn]] void err(const std::string& m, int line) { throw CompileError(m, line); }

// ---- 表达式/语句文本发射 ---------------------------------------------------
struct CEmit {
    const Model& m;
    std::ostringstream os;
    int indent = 2;
    // 成员改写："Blk.field" → 别名访问文本；内建改写："in.gid" → "gx" 等
    std::unordered_map<std::string, std::string> memberMap;

    explicit CEmit(const Model& mm) : m(mm) {}
    void pad() { for (int i = 0; i < indent; i++) os << ' '; }

    // 条件位置的表达式：Binary/Ternary 已自带外括号，不再叠括（免双括号 warning）
    std::string cond(const Expr* e) {
        std::string s = expr(e);
        if (s.size() >= 2 && s.front() == '(' && s.back() == ')') return s;
        return "(" + s + ")";
    }

    std::string expr(const Expr* e) {
        if (!e) return "";
        switch (e->kind) {
            case Expr::IntLit:
            case Expr::CharLit:  return e->text;
            case Expr::FloatLit: {
                std::string s = e->text;
                if (s.find('.') == std::string::npos && s.find('e') == std::string::npos)
                    s += ".0";
                return s + "f";
            }
            case Expr::Ident: {
                auto it = memberMap.find(e->text);
                return it != memberMap.end() ? it->second : e->text;
            }
            case Expr::Unary:     return e->op + expr(e->a.get());
            case Expr::PostUnary: return expr(e->a.get()) + e->op;
            case Expr::Binary:
                return "(" + expr(e->a.get()) + " " + e->op + " " + expr(e->b.get()) + ")";
            case Expr::Ternary:
                return "(" + expr(e->a.get()) + " ? " + expr(e->b.get()) +
                       " : " + expr(e->c.get()) + ")";
            case Expr::Index:
                return expr(e->a.get()) + "[" + expr(e->b.get()) + "]";
            case Expr::Member: {
                if (e->a && e->a->kind == Expr::Ident) {
                    auto it = memberMap.find(e->a->text + "." + e->text);
                    if (it != memberMap.end()) return it->second;
                }
                err("cpu 后端暂不支持该成员访问（M1 限标量；vec swizzle 见 M2+）", e->line);
            }
            case Expr::Call: {
                if (!e->a || e->a->kind != Expr::Ident)
                    err("cpu 后端不支持间接调用", e->line);
                const std::string& fn = e->a->text;
                // 标量转换 float(x)/uint(x)/f2(x)/...
                std::string ct = cType(fn);
                if (!ct.empty() && ct != "void" && e->args.size() == 1)
                    return "((" + ct + ")" + expr(e->args[0].get()) + ")";
                if (fn == "barrier" || fn == "memory_barrier" ||
                    fn.rfind("atomic_", 0) == 0 || fn.rfind("subgroup_", 0) == 0)
                    err("cpu 后端 M1 暂不支持 `" + fn + "`（M2 落地，见 syntax-s-design §17.6）", e->line);
                const char* mf = mathFn(fn);
                if (!mf) {
                    // 辅助函数（.ss 内 fnc）原名直调
                    std::string s = fn + "(";
                    for (size_t i = 0; i < e->args.size(); i++) {
                        if (i) s += ", ";
                        s += expr(e->args[i].get());
                    }
                    return s + ")";
                }
                std::string s = std::string(mf) + "(";
                for (size_t i = 0; i < e->args.size(); i++) {
                    if (i) s += ", ";
                    s += expr(e->args[i].get());
                }
                return s + ")";
            }
            case Expr::Cast: {
                std::string ct = cType(e->op);
                if (ct.empty()) err("cpu 后端不支持强转目标 `" + e->op + "`", e->line);
                return "((" + ct + ")" + expr(e->a.get()) + ")";
            }
            default:
                err("cpu 后端暂不支持该表达式", e->line);
        }
    }

    void declLocal(const Field& f, int line) {
        std::string ct = cType(f.type.name);
        if (ct.empty())
            err("cpu 后端 M1 仅支持标量局部变量（`" + f.type.name + "` 不支持）",
                f.line ? f.line : line);
        pad(); os << ct << " " << f.name;
        for (const auto& d : f.type.arrayDims) os << "[" << d << "]";
        if (f.init) {
            if (f.init->kind == Expr::InitList) {
                os << " = {";
                for (size_t i = 0; i < f.init->args.size(); i++) {
                    if (i) os << ", ";
                    os << expr(f.init->args[i].get());
                }
                os << "}";
            } else {
                os << " = " << expr(f.init.get());
            }
        }
        os << ";\n";
    }

    void stmt(const Stmt* s) {
        if (!s) return;
        switch (s->kind) {
            case Stmt::ExprS:
                pad(); os << expr(s->expr.get()) << ";\n";
                break;
            case Stmt::VarS:
            case Stmt::LetS:
                for (const auto& f : s->decls) declLocal(f, s->line);
                break;
            case Stmt::ReturnS:
                pad(); os << "return;\n";   // comp 无返回值（sema 已保证）
                break;
            case Stmt::IfS:
                pad(); os << "if " << cond(s->expr.get()) << " {\n";
                indent += 2;
                for (const auto& x : s->body) stmt(x.get());
                indent -= 2;
                if (!s->elseBody.empty()) {
                    pad(); os << "} else {\n";
                    indent += 2;
                    for (const auto& x : s->elseBody) stmt(x.get());
                    indent -= 2;
                }
                pad(); os << "}\n";
                break;
            case Stmt::WhileS:
                pad(); os << "while " << cond(s->expr.get()) << " {\n";
                indent += 2;
                for (const auto& x : s->body) stmt(x.get());
                indent -= 2;
                pad(); os << "}\n";
                break;
            case Stmt::DoWhileS:
                pad(); os << "do {\n";
                indent += 2;
                for (const auto& x : s->body) stmt(x.get());
                indent -= 2;
                pad(); os << "} while " << cond(s->expr.get()) << ";\n";
                break;
            case Stmt::ForS:
                pad(); os << "for (" << (s->forInit ? expr(s->forInit.get()) : "")
                          << "; " << (s->forCond ? expr(s->forCond.get()) : "")
                          << "; " << (s->forStep ? expr(s->forStep.get()) : "") << ") {\n";
                indent += 2;
                for (const auto& x : s->body) stmt(x.get());
                indent -= 2;
                pad(); os << "}\n";
                break;
            case Stmt::BreakS:    pad(); os << "break;\n"; break;
            case Stmt::ContinueS: pad(); os << "continue;\n"; break;
            default:
                err("cpu 后端暂不支持该语句", s->line);
        }
    }
};

// 资源块别名预声明 + 成员改写表：每成员一个 restrict 指针别名。
// 返回声明文本；memberMap 填 "Blk.field" → "(*别名)" / "别名"（数组）。
std::string emitBlockAliases(const Model& m, CEmit& em, int line) {
    std::ostringstream os;
    for (const auto& r : m.resources) {
        const Decl* d = r.d;
        const bool std430 = r.storage;
        os << "  const unsigned char* " << d->name << "_p = (const unsigned char*)bind["
           << r.binding << "];\n";
        int off = 0;
        for (const auto& f : d->structCommon.fields) {
            if (f.synthetic) continue;
            std::string ct = cType(f.type.name);
            if (ct.empty())
                err("cpu 后端 M1 资源块仅支持标量成员（`" + f.type.name + "`）",
                    f.line ? f.line : line);
            int sz = scalarSize(f.type.name);
            const bool runtimeArr = !f.type.arrayDims.empty() && f.type.arrayDims[0].empty();
            const bool fixedArr = !f.type.arrayDims.empty() && !runtimeArr;
            if (!std430 && !f.type.arrayDims.empty())
                err("cpu 后端 M1 uniform 块不支持数组成员（std140 stride 与 C 布局不一致；改用 storage）",
                    f.line ? f.line : line);
            off = roundUpC(off, sz);
            const char* cq = r.storage ? "" : "const ";
            std::string alias = d->name + "_" + f.name;
            os << "  " << cq << ct << "* restrict " << alias << " = ("
               << cq << ct << "*)(" << d->name << "_p + " << off << ");\n";
            if (runtimeArr || fixedArr) {
                em.memberMap[d->name + "." + f.name] = alias;    // 数组：alias[i]
                if (fixedArr) {
                    int n = std::atoi(f.type.arrayDims[0].c_str());
                    off += sz * n;
                }
                // 运行时数组：必须末成员（sema/GPU 侧同规），不再推进 off
            } else {
                em.memberMap[d->name + "." + f.name] = "(*" + alias + ")";
                off += sz;
            }
        }
    }
    return os.str();
}

// 计算内建（标量声明惯例）→ 循环变量映射
void mapCompIn(const Model& m, const Decl& stage, CEmit& em, bool& usesLid, bool& usesWg) {
    usesLid = usesWg = false;
    for (const auto& p : stage.structCommon.fields) {
        const Decl* ps = m.findStruct(p.type.name);
        if (!ps) continue;
        for (const auto& f : ps->structCommon.fields) {
            const std::string& sem = f.shaderAttr ? f.shaderAttr->builtin : "";
            if (sem.empty()) continue;
            std::string ct = cType(f.type.name);
            if (ct != "uint32_t" && ct != "int32_t")
                err("cpu 后端 M1 计算内建仅支持标量声明（u4/i4）", f.line);
            const std::string key = p.name + "." + f.name;
            if (sem == "global_invocation_id")      em.memberMap[key] = "gx";
            else if (sem == "local_invocation_index") { em.memberMap[key] = "lid"; usesLid = true; }
            else if (sem == "local_invocation_id")  { em.memberMap[key] = "lid"; usesLid = true; }
            else if (sem == "workgroup_id")         { em.memberMap[key] = "wg"; usesWg = true; }
            else if (sem == "num_workgroups")       em.memberMap[key] = "nwg";
            else err("cpu 后端暂不支持内建 `" + sem + "`", f.line);
        }
    }
}

} // namespace

std::string emitCpu(const Program& prog, const GlslTarget& target) {
    (void)target;
    Model m;
    for (const auto& d : prog.decls) {
        if (!d) continue;
        if (d->kind == Decl::StructD) {
            m.structs[d->name] = d.get();
            if (d->shaderAttr && d->shaderAttr->res != ShaderDeclAttr::None) {
                auto res = d->shaderAttr->res;
                if (res == ShaderDeclAttr::Shared)
                    err("cpu 后端 M1 暂不支持 shared（M2 落地，见 syntax-s-design §17.6）", d->line);
                if (res == ShaderDeclAttr::Push)
                    err("cpu 后端 M1 暂不支持 push 常量（用 uniform 块）", d->line);
                m.resources.push_back({d.get(), d->shaderAttr->binding,
                                       res == ShaderDeclAttr::Storage});
            }
        } else if (d->kind == Decl::VarD && d->shaderAttr) {
            err("cpu 后端不支持采样器资源（无图形面）", d->line);
        }
    }

    std::ostringstream c;
    c << "/* generated by scc —— cpu SPMD kernel（勿手改；契约见 builtins/spc/src/cpu_spc.c） */\n"
      << "#include <stdint.h>\n#include <stddef.h>\n#include <math.h>\n\n"
      << "#define SC_RSQRTF(x)      (1.0f / sqrtf(x))\n"
      << "#define SC_FRACTF(x)      ((x) - floorf(x))\n"
      << "#define SC_CLAMPF(x,a,b)  (fminf(fmaxf((x),(a)),(b)))\n"
      << "#define SC_MIXF(a,b,t)    ((a) + ((b) - (a)) * (t))\n"
      << "#define SC_STEPF(e,x)     ((x) < (e) ? 0.0f : 1.0f)\n"
      << "#define SC_SMOOTHSTEPF(a,b,x) ({ float _t = SC_CLAMPF(((x)-(a))/((b)-(a)),0.0f,1.0f); _t*_t*(3.0f-2.0f*_t); })\n\n"
      << "#ifndef SC_SPC_CPU_KERNEL_DEFINED\n#define SC_SPC_CPU_KERNEL_DEFINED\n"
      << "typedef struct sc_spc_cpu_kernel {\n"
      << "    const char* entry;\n"
      << "    void (*fn)(uint32_t gx0, uint32_t gx1, uint32_t gy0, uint32_t gy1,\n"
      << "               uint32_t gz0, uint32_t gz1, void* const* bind);\n"
      << "    int local[3];\n"
      << "} sc_spc_cpu_kernel;\n#endif\n"
      << "extern void sc_spc_cpu_register(const sc_spc_cpu_kernel* ks, int n);\n\n";

    // 辅助函数（.ss 内 fnc）——M1 标量签名直译
    for (const auto& d : prog.decls) {
        if (!d || d->kind != Decl::FuncD || d->shaderStage != ShaderStage::None) continue;
        std::string rt = d->structCommon.type ? cType(d->structCommon.type->name) : "void";
        if (rt.empty()) err("cpu 后端 M1 辅助函数仅支持标量签名", d->line);
        c << "static " << rt << " " << d->name << "(";
        for (size_t i = 0; i < d->structCommon.fields.size(); i++) {
            const auto& p = d->structCommon.fields[i];
            std::string pt = cType(p.type.name);
            if (pt.empty()) err("cpu 后端 M1 辅助函数仅支持标量参数", d->line);
            if (i) c << ", ";
            c << pt << " " << p.name;
        }
        c << ") {\n";
        CEmit em(m);
        for (const auto& s : d->body) {
            if (s->kind == Stmt::ReturnS && s->expr) {   // 辅助函数带值 return
                em.pad(); em.os << "return " << em.expr(s->expr.get()) << ";\n";
            } else {
                em.stmt(s.get());
            }
        }
        c << em.os.str() << "}\n\n";
    }

    // kernel 函数
    struct KMeta { std::string entry; int local[3]; };
    std::vector<KMeta> kernels;
    for (const auto& d : prog.decls) {
        if (!d || d->kind != Decl::FuncD || d->shaderStage != ShaderStage::Comp) continue;
        KMeta km{d->name, {64, 1, 1}};
        if (d->shaderAttr && d->shaderAttr->local[0] > 0) {
            km.local[0] = d->shaderAttr->local[0];
            km.local[1] = d->shaderAttr->local[1];
            km.local[2] = d->shaderAttr->local[2];
        }
        kernels.push_back(km);

        CEmit em(m);
        bool usesLid = false, usesWg = false;
        mapCompIn(m, *d, em, usesLid, usesWg);
        std::string aliases = emitBlockAliases(m, em, d->line);

        c << "static void " << d->name
          << "_impl(uint32_t gx0, uint32_t gx1, uint32_t gy0, uint32_t gy1,\n"
          << "        uint32_t gz0, uint32_t gz1, void* const* bind) {\n"
          << aliases
          << "  (void)bind;\n"
          << "  for (uint32_t gz = gz0; gz < gz1; gz++)\n"
          << "  for (uint32_t gy = gy0; gy < gy1; gy++) {\n"
          << "    (void)gz; (void)gy;\n"
          << "#if defined(__clang__)\n"
          << "#pragma clang loop vectorize(enable)\n"
          << "#elif defined(__GNUC__)\n"
          << "#pragma GCC ivdep\n"
          << "#endif\n"
          << "    for (uint32_t gx = gx0; gx < gx1; gx++) {\n";
        if (usesLid)
            c << "      const uint32_t lid = gx % " << km.local[0] << "u;\n";
        if (usesWg)
            c << "      const uint32_t wg = gx / " << km.local[0] << "u;\n";
        em.indent = 6;
        for (const auto& s : d->body) em.stmt(s.get());
        c << em.os.str()
          << "    }\n"
          << "  }\n"
          << "}\n\n";
    }
    if (kernels.empty())
        err("cpu 后端：未找到 comp 阶段入口", 0);

    // 注册表 + 构造器自注册
    c << "static const sc_spc_cpu_kernel sc_cpu_kernels_[] = {\n";
    for (const auto& k : kernels)
        c << "    {\"" << k.entry << "\", " << k.entry << "_impl, {"
          << k.local[0] << ", " << k.local[1] << ", " << k.local[2] << "}},\n";
    c << "};\n"
      << "__attribute__((constructor)) static void sc_cpu_kernels_ctor_(void) {\n"
      << "    sc_spc_cpu_register(sc_cpu_kernels_, "
      << kernels.size() << ");\n"
      << "}\n";
    return c.str();
}
