// ============================================================
// codegen_cpu 实现（syntax-s-design §17，M1+M2）
// ============================================================
// 发射模型（两条路径，按 kernel 是否含 barrier 选择）：
//   ① 无 barrier（M1）：三重全局线程循环，内循环 vectorize 提示。
//   ② 含 barrier（M2 相位分裂）：workgroup 外循环 + 按 barrier 切相位，
//     每相位一个 lid 内循环（barrier 语义在相位边界处天然成立）：
//       顶层 var/let = uniform 变量（每 invocation 同值）提升到 wg 层，
//       初值引用 gid/lid 则报错（移进相位内或用 shared）；
//       含 barrier 的顶层 while = 循环交换（条件/步进须 uniform），
//       体内递归切相位；其它控制流内的 barrier 报错（一期限制）。
//     shared 块 = wg 层栈数组；atomic_* → GCC/Clang __atomic 内建
//     （min/max 用 typeof+CAS 循环宏，与产物已用的 GNU 扩展一致）。
//   顶层 let（含 `spec N`）= kernel 内 const 常量；spec 经参数表传值
//   （fn 签名尾 const uint32_t* spec，按 id 索引，缺省用默认值）。
//
// 其余同 M1：资源块 bind[binding] 基指针 + restrict 别名；仅标量。
// ============================================================
#include "codegen_cpu.h"
#include "error.h"

#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
    std::vector<const Decl*> shared;    // shared 块（wg 层栈数组）
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
                if (fn == "barrier" || fn == "memory_barrier")
                    err("cpu 后端：barrier 仅支持 kernel 顶层或顶层 while 循环内"
                        "（相位分裂限制，见 syntax-s-design §17.5）", e->line);
                if (fn.rfind("atomic_", 0) == 0) {
                    // __atomic 内建（GNU 扩展，clang/gcc/DSP-clang 通用）；
                    // 首参是左值表达式，取地址传入
                    const std::string op = fn.substr(7);
                    std::string m0 = "&" + expr(e->args[0].get());
                    std::string v1 = e->args.size() > 1 ? expr(e->args[1].get()) : "";
                    if (op == "add") return "__atomic_fetch_add(" + m0 + ", " + v1 + ", __ATOMIC_RELAXED)";
                    if (op == "sub") return "__atomic_fetch_sub(" + m0 + ", " + v1 + ", __ATOMIC_RELAXED)";
                    if (op == "and") return "__atomic_fetch_and(" + m0 + ", " + v1 + ", __ATOMIC_RELAXED)";
                    if (op == "or")  return "__atomic_fetch_or("  + m0 + ", " + v1 + ", __ATOMIC_RELAXED)";
                    if (op == "xor") return "__atomic_fetch_xor(" + m0 + ", " + v1 + ", __ATOMIC_RELAXED)";
                    if (op == "exchange") return "__atomic_exchange_n(" + m0 + ", " + v1 + ", __ATOMIC_RELAXED)";
                    if (op == "min") return "SC_ATOMIC_MIN(" + m0 + ", " + v1 + ")";
                    if (op == "max") return "SC_ATOMIC_MAX(" + m0 + ", " + v1 + ")";
                    if (op == "cas" && e->args.size() == 3)
                        return "SC_ATOMIC_CAS(" + m0 + ", " + v1 + ", " +
                               expr(e->args[2].get()) + ")";
                    err("cpu 后端不支持 `" + fn + "`", e->line);
                }
                if (fn.rfind("subgroup_", 0) == 0)
                    err("cpu 后端不支持 subgroup（能力门控；模拟无性能意义）", e->line);
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

// 表达式是否引用 per-invocation 变量（gx/lid 映射）——uniform 性检查用
bool usesInvocation(const Expr* e, const CEmit& em) {
    if (!e) return false;
    if (e->kind == Expr::Ident) {
        auto it = em.memberMap.find(e->text);
        if (it != em.memberMap.end() && (it->second == "gx" || it->second == "lid"))
            return true;
    }
    if (e->kind == Expr::Member && e->a && e->a->kind == Expr::Ident) {
        auto it = em.memberMap.find(e->a->text + "." + e->text);
        if (it != em.memberMap.end() && (it->second == "gx" || it->second == "lid"))
            return true;
    }
    if (usesInvocation(e->a.get(), em) || usesInvocation(e->b.get(), em) ||
        usesInvocation(e->c.get(), em)) return true;
    for (const auto& a : e->args)
        if (usesInvocation(a.get(), em)) return true;
    return false;
}

// 语句/语句序是否含 barrier 调用（相位分裂路径选择）
bool exprHasBarrier(const Expr* e) {
    if (!e) return false;
    if (e->kind == Expr::Call && e->a && e->a->kind == Expr::Ident &&
        (e->a->text == "barrier" || e->a->text == "memory_barrier")) return true;
    if (exprHasBarrier(e->a.get()) || exprHasBarrier(e->b.get()) ||
        exprHasBarrier(e->c.get())) return true;
    for (const auto& a : e->args) if (exprHasBarrier(a.get())) return true;
    return false;
}
bool stmtHasBarrier(const Stmt* s) {
    if (!s) return false;
    if (exprHasBarrier(s->expr.get())) return true;
    for (const auto& x : s->body) if (stmtHasBarrier(x.get())) return true;
    for (const auto& x : s->elseBody) if (stmtHasBarrier(x.get())) return true;
    for (const auto& f : s->decls) if (exprHasBarrier(f.init.get())) return true;
    return false;
}
// 顶层 ExprS 是否恰为裸 barrier()/memory_barrier() 调用
bool isBarrierStmt(const Stmt* s) {
    return s && s->kind == Stmt::ExprS && s->expr &&
           s->expr->kind == Expr::Call && s->expr->a &&
           s->expr->a->kind == Expr::Ident &&
           (s->expr->a->text == "barrier" || s->expr->a->text == "memory_barrier");
}

// 表达式是否引用集合内名字（per-invocation 传染分析用）
bool usesNames(const Expr* e, const std::unordered_set<std::string>& names) {
    if (!e) return false;
    if (e->kind == Expr::Ident && names.count(e->text)) return true;
    if (usesNames(e->a.get(), names) || usesNames(e->b.get(), names) ||
        usesNames(e->c.get(), names)) return true;
    for (const auto& a : e->args) if (usesNames(a.get(), names)) return true;
    return false;
}

// 相位分裂的顶层变量分类：per-invocation（每 invocation 不同值，需数组扩展）
// vs uniform（同值，提升 wg 层）。判定：初值或任一赋值 RHS 引用 gid/lid 或
// 已判定 per-inv 的变量 → per-inv；传染至不动点（保守正确）。
void collectAssigns(const Stmt* s, std::vector<const Expr*>& out) {
    if (!s) return;
    if (s->expr && s->expr->kind == Expr::Binary &&
        (s->expr->op == "=" || s->expr->op == "+=" || s->expr->op == "-=" ||
         s->expr->op == "*=" || s->expr->op == "/="))
        out.push_back(s->expr.get());
    for (const auto& x : s->body) collectAssigns(x.get(), out);
    for (const auto& x : s->elseBody) collectAssigns(x.get(), out);
}
std::unordered_set<std::string> collectPerInv(
        const std::vector<StmtPtr>& body, const CEmit& em) {
    // 顶层声明名 + 初值
    std::unordered_map<std::string, const Expr*> topVars;
    std::vector<const Expr*> assigns;
    for (const auto& sp : body) {
        const Stmt* s = sp.get();
        if (!s) continue;
        if (s->kind == Stmt::VarS || s->kind == Stmt::LetS)
            for (const auto& f : s->decls) topVars[f.name] = f.init.get();
        collectAssigns(s, assigns);
    }
    std::unordered_set<std::string> pv;
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& kv : topVars) {
            if (pv.count(kv.first)) continue;
            bool isPv = kv.second &&
                (usesInvocation(kv.second, em) || usesNames(kv.second, pv));
            if (!isPv) {
                for (const Expr* a : assigns) {
                    if (a->a->kind != Expr::Ident || a->a->text != kv.first) continue;
                    if (usesInvocation(a->b.get(), em) || usesNames(a->b.get(), pv)) {
                        isPv = true;
                        break;
                    }
                }
            }
            if (isPv) { pv.insert(kv.first); changed = true; }
        }
    }
    return pv;
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
                if (res == ShaderDeclAttr::Shared) {
                    m.shared.push_back(d.get());    // wg 层栈数组（相位模式声明）
                    continue;
                }
                if (res == ShaderDeclAttr::Push)
                    err("cpu 后端暂不支持 push 常量（用 uniform 块）", d->line);
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
      << "#define SC_SMOOTHSTEPF(a,b,x) ({ float _t = SC_CLAMPF(((x)-(a))/((b)-(a)),0.0f,1.0f); _t*_t*(3.0f-2.0f*_t); })\n"
      << "/* 原子 min/max/cas：typeof + CAS 循环（GNU 扩展，clang/gcc/DSP-clang 通用） */\n"
      << "#define SC_ATOMIC_MIN(p,v) ({ __typeof__(*(p)) _o = __atomic_load_n((p), __ATOMIC_RELAXED), _v = (v), _n2; \\\n"
      << "    do { _n2 = _o < _v ? _o : _v; } while (!__atomic_compare_exchange_n((p), &_o, _n2, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED)); _o; })\n"
      << "#define SC_ATOMIC_MAX(p,v) ({ __typeof__(*(p)) _o = __atomic_load_n((p), __ATOMIC_RELAXED), _v = (v), _n2; \\\n"
      << "    do { _n2 = _o > _v ? _o : _v; } while (!__atomic_compare_exchange_n((p), &_o, _n2, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED)); _o; })\n"
      << "#define SC_ATOMIC_CAS(p,c,v) ({ __typeof__(*(p)) _c = (c); \\\n"
      << "    __atomic_compare_exchange_n((p), &_c, (v), 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED); _c; })\n\n"
      << "#ifndef SC_SPC_CPU_KERNEL_DEFINED\n#define SC_SPC_CPU_KERNEL_DEFINED\n"
      << "typedef struct sc_spc_cpu_kernel {\n"
      << "    const char* entry;\n"
      << "    void (*fn)(uint32_t gx0, uint32_t gx1, uint32_t gy0, uint32_t gy1,\n"
      << "               uint32_t gz0, uint32_t gz1, void* const* bind,\n"
      << "               const uint32_t* spec);\n"
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

        // 顶层 let 常量（含 spec）：kernel 头部 const 声明；spec 经参数表按 id 取值
        std::ostringstream lets;
        for (const auto& ld : prog.decls) {
            if (!ld || ld->kind != Decl::LetD) continue;
            for (const auto& f : ld->structCommon.fields) {
                if (!f.init) continue;
                std::string ct = cType(f.type.name.empty() ?
                    (f.init->kind == Expr::FloatLit ? "f4" : "i4") : f.type.name);
                if (ct.empty()) err("cpu 后端顶层 let 仅支持标量", f.line);
                std::string init = em.expr(f.init.get());
                if (f.shaderAttr && f.shaderAttr->specId >= 0) {
                    // spec 特化常量：传值数组 8 槽 + 掩码槽 spec[8]（bit i = id i 已传），
                    // 未传用默认值（与 cpu_spc.c dispatch 装配契约同步）
                    std::string idx = std::to_string(f.shaderAttr->specId);
                    std::string hit = "(spec && (spec[8] & (1u<<" + idx + ")))";
                    if (ct == "float")
                        lets << "  const float " << f.name << " = " << hit
                             << " ? *(const float*)&spec[" << idx << "] : " << init << ";\n";
                    else
                        lets << "  const " << ct << " " << f.name << " = " << hit
                             << " ? (" << ct << ")spec[" << idx << "] : " << init << ";\n";
                } else {
                    lets << "  const " << ct << " " << f.name << " = " << init << ";\n";
                }
            }
        }

        const bool phased = [&]{
            for (const auto& s : d->body) if (stmtHasBarrier(s.get())) return true;
            return false;
        }();

        c << "static void " << d->name
          << "_impl(uint32_t gx0, uint32_t gx1, uint32_t gy0, uint32_t gy1,\n"
          << "        uint32_t gz0, uint32_t gz1, void* const* bind,\n"
          << "        const uint32_t* spec) {\n"
          << aliases
          << lets.str()
          << "  (void)bind; (void)spec; (void)gy0; (void)gy1; (void)gz0; (void)gz1;\n";

        if (!phased) {
            // ---- M1 路径：三重全局线程循环（无 barrier）----
            // 向量化：clang/gcc -O3 对规整 SPMD 循环自动兑现（restrict 已给足
            // 别名保证）；不发显式 pragma——复杂循环体上会产生 -Wpass-failed 警告。
            c << "  for (uint32_t gz = gz0; gz < gz1; gz++)\n"
              << "  for (uint32_t gy = gy0; gy < gy1; gy++) {\n"
              << "    (void)gz; (void)gy;\n"
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
            continue;
        }

        // ---- M2 路径：barrier 相位分裂（workgroup 外循环 + 相位 lid 循环）----
        // 1D 语义（gy/gz 恒 1；多维含 barrier 待后续），lid = 相位循环变量、
        // wg = workgroup 序号、gx = wg*LX+lid（越界由 kernel n 守卫，与 GPU 组数取整同义）。
        if (km.local[1] != 1 || km.local[2] != 1)
            err("cpu 后端相位分裂暂限 1D 工作组（local Y/Z = 1）", d->line);
        // 内建映射改相位形态：wkid/lid 直接是循环变量
        for (auto& kv : em.memberMap) {
            if (kv.second == "wg") { usesWg = true; }
        }
        const std::string LX = std::to_string(km.local[0]) + "u";
        // wg 序号用绝对坐标（gx0/LX 起）：多线程分片时 gx0 按 LX 对齐切割（cpu_spc.c 保证）
        c << "  for (uint32_t wg = gx0 / " << LX << "; wg < (gx1 + " << LX << " - 1) / "
          << LX << "; wg++) {\n";
        // shared 块 → wg 层栈数组/标量
        for (const Decl* sd : m.shared) {
            for (const auto& f : sd->structCommon.fields) {
                if (f.synthetic) continue;
                std::string ct = cType(f.type.name);
                if (ct.empty())
                    err("cpu 后端 shared 块仅支持标量成员", f.line ? f.line : sd->line);
                std::string alias = sd->name + "_" + f.name;
                c << "    " << ct << " " << alias;
                for (const auto& dim : f.type.arrayDims) c << "[" << dim << "]";
                c << ";\n";
                em.memberMap[sd->name + "." + f.name] =
                    f.type.arrayDims.empty() ? ("(" + alias + ")") : alias;
            }
        }
        // 相位发射器：flushPhase 把累积语句包进 lid 循环
        std::ostringstream body;
        std::vector<const Stmt*> phase;
        std::unordered_set<std::string> wgVars;   // wg 层提升的 uniform 变量名
        // per-invocation 顶层变量（跨相位存活）→ 数组扩展 X_pv[LX]，相位内 X → X_pv[lid]
        std::unordered_set<std::string> perInv = collectPerInv(d->body, em);
        int phaseIndent = 4;
        auto flushPhase = [&](int indentLv) {
            if (phase.empty()) return;
            std::string padS((size_t)indentLv, ' ');
            body << padS << "for (uint32_t lid = 0; lid < " << LX << "; lid++) {\n"
                 << padS << "  const uint32_t gx = wg * " << LX << " + lid; (void)gx;\n";
            CEmit pe(m);
            pe.memberMap = em.memberMap;
            pe.indent = indentLv + 2;
            for (const Stmt* s : phase) pe.stmt(s);
            body << pe.os.str() << padS << "}\n";
            phase.clear();
        };
        // 递归处理语句序列（顶层与含 barrier 的顶层 while 体共用）
        std::function<void(const std::vector<StmtPtr>&, int)> emitSeq =
            [&](const std::vector<StmtPtr>& stmts, int lv) {
            for (const auto& sp : stmts) {
                const Stmt* s = sp.get();
                if (!s) continue;
                if (isBarrierStmt(s)) {              // barrier = 相位边界（CPU 串行天然满足）
                    flushPhase(lv);
                    continue;
                }
                if ((s->kind == Stmt::VarS || s->kind == Stmt::LetS) && lv == phaseIndent) {
                    flushPhase(lv);
                    for (const auto& f : s->decls) {
                        std::string padS((size_t)lv, ' ');
                        if (perInv.count(f.name)) {
                            // per-invocation 变量：wg 层数组扩展 + 初始化 lid 循环
                            //（初值可引用 gid/lid，在循环内求值）
                            std::string ct = cType(f.type.name);
                            if (ct.empty())
                                err("cpu 后端跨相位变量仅支持标量（`" + f.type.name + "`）",
                                    f.line ? f.line : s->line);
                            if (!f.type.arrayDims.empty())
                                err("cpu 后端跨相位变量不支持数组（改用 shared）",
                                    f.line ? f.line : s->line);
                            std::string alias = f.name + "_pv";
                            body << padS << ct << " " << alias << "[" << km.local[0] << "];\n";
                            if (f.init) {
                                CEmit ie(m);
                                ie.memberMap = em.memberMap;   // 初值在旧映射下求值
                                body << padS << "for (uint32_t lid = 0; lid < "
                                     << km.local[0] << "u; lid++) {\n"
                                     << padS << "  const uint32_t gx = wg * " << km.local[0]
                                     << "u + lid; (void)gx;\n"
                                     << padS << "  " << alias << "[lid] = "
                                     << ie.expr(f.init.get()) << ";\n"
                                     << padS << "}\n";
                            }
                            em.memberMap[f.name] = alias + "[lid]";   // 相位内改写
                            continue;
                        }
                        // uniform 变量：提升 wg 层（初值不得引用 gid/lid）
                        if (usesInvocation(f.init.get(), em))
                            err("cpu 后端：跨相位顶层变量 `" + f.name +
                                "` 初值引用 gid/lid 但未判定为 per-invocation（内部矛盾，报告 bug）",
                                f.line ? f.line : s->line);
                        CEmit de(m);
                        de.memberMap = em.memberMap;
                        de.indent = lv;
                        de.declLocal(f, s->line);
                        body << de.os.str();
                        wgVars.insert(f.name);
                    }
                    continue;
                }
                // uniform 赋值外提：目标是 wg 层变量且右侧不引用 gid/lid →
                // 只执行一次（GPU 上每 invocation 同值重复执行，CPU 入 lid 循环会错）
                if (s->kind == Stmt::ExprS && s->expr && s->expr->kind == Expr::Binary &&
                    (s->expr->op == "=" || s->expr->op == "+=" || s->expr->op == "-=" ||
                     s->expr->op == "*=" || s->expr->op == "/=") &&
                    s->expr->a && s->expr->a->kind == Expr::Ident &&
                    wgVars.count(s->expr->a->text) &&
                    !usesInvocation(s->expr->b.get(), em) &&
                    !usesNames(s->expr->b.get(), perInv)) {
                    flushPhase(lv);
                    CEmit ae(m);
                    ae.memberMap = em.memberMap;
                    ae.indent = lv;
                    ae.pad(); ae.os << ae.expr(s->expr.get()) << ";\n";
                    body << ae.os.str();
                    continue;
                }
                if (s->kind == Stmt::WhileS && stmtHasBarrier(s)) {
                    // 循环交换：uniform while 提到 wg 层，体内递归切相位
                    flushPhase(lv);
                    if (usesInvocation(s->expr.get(), em))
                        err("cpu 后端：含 barrier 的循环条件须为 uniform"
                            "（不得引用 gid/lid）", s->line);
                    CEmit ce(m);
                    ce.memberMap = em.memberMap;
                    std::string padS((size_t)lv, ' ');
                    body << padS << "while " << ce.cond(s->expr.get()) << " {\n";
                    emitSeq(s->body, lv + 2);
                    flushPhase(lv + 2);
                    body << padS << "}\n";
                    continue;
                }
                if (stmtHasBarrier(s))
                    err("cpu 后端：barrier 仅支持 kernel 顶层或顶层 while 循环内"
                        "（相位分裂限制，见 syntax-s-design §17.5）", s->line);
                phase.push_back(s);
            }
        };
        emitSeq(d->body, phaseIndent);
        flushPhase(phaseIndent);
        c << body.str()
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
