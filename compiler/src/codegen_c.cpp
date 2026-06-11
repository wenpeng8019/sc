// ============================================================
// C 代码生成器 —— Program AST → C 源码字符串
// ============================================================
// 核心工作：
//   1. 类型映射：sc 内置类型 → C 标准类型（i4→int32_t 等）
//   2. 默认类型推断：无类型对象→char*，无类型指针→void*
//   3. 函数类型展开：fnc name -> func_type 从函数类型表查找签名
//   4. 输出顺序：类型定义 → 全局变量 → 函数原型 → 函数实现
// 两遍扫描：第一遍输出类型/变量/原型，第二遍输出函数体。
// ============================================================
#include "codegen_c.h"
#include "error.h"
#include <cctype>
#include <sstream>
#include <unordered_map>

namespace {

// sc 内置基本类型 → C 标准类型映射
std::string mapBase(const std::string& n) {
    static const std::unordered_map<std::string, std::string> m = {
        {"i1", "int8_t"},  {"i2", "int16_t"}, {"i4", "int32_t"}, {"i8", "int64_t"},
        {"u1", "uint8_t"}, {"u2", "uint16_t"}, {"u4", "uint32_t"}, {"u8", "uint64_t"},
        {"f4", "float"},   {"f8", "double"},  {"v", "void"},
        {"b", "uint8_t"},  // bool：u1 的引用别名（true/false 即 1/0）
        {"va_list", "va_list"},  // 透传：可变参数列表类型
    };
    auto it = m.find(n);
    return it == m.end() ? n : it->second;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string moduleFileToken(const std::string& s) {
    std::string out = "scm_";
    for (unsigned char ch : s) out += std::isalnum(ch) ? (char)ch : '_';
    return out;
}

std::string moduleHeaderName(const std::string& s) {
    return moduleFileToken(s) + ".h";
}

// CGen 内部类 —— 封装 C 代码生成的状态
struct CGen {
    const Program& prog;
    std::ostringstream out;     // 输出流
    int depth = 0;              // 当前缩进深度（每级4空格）
    std::unordered_map<std::string, const Decl*> funcTypes;  // 函数类型名→Decl 映射

    // ---- 伪 class 支撑：类型注册表与变量类型跟踪 ----
    std::unordered_map<std::string, const Decl*> aggrs;    // struct/union 名 → Decl
    std::unordered_map<std::string, std::string> aliases;  // 别名 → 目标类型名
    // 变量的轻量类型信息（类型名 + 指针层数 + 数组维数），用于方法调用识别
    struct VType { std::string name; int ptr = 0; int arr = 0; };
    std::unordered_map<std::string, VType> globalsT, localsT;
    bool inFunc = false;        // 当前是否在函数体内（决定变量注册到哪个表）
    std::string curAggr;        // 当前正在输出的 struct/union 名（方法字段展开用）
    std::string curAggrKind;    // "struct" / "union"

    explicit CGen(const Program& p) : prog(p) {}

    void indent() { for (int i = 0; i < depth; i++) out << "    "; }

    // ---------------- 类型处理 ----------------
    // 解析类型引用，确定 C 中的底层类型名和指针层数
    // 默认规则：无类型无指针→char*，无类型有指针→void*
    static void resolveType(const TypeRef& t, std::string& base, int& ptr) {
        if (t.name.empty() && !t.hasInline) {
            if (t.ptr > 0) { base = "void"; ptr = t.ptr; }
            else { base = "char"; ptr = 1; }
        } else {
            base = mapBase(t.name);
            ptr = t.ptr;
        }
    }

    // 声明一个字段/变量：T [*...]name[size]
    void emitDeclarator(const Field& f, bool asConst = false) {
        if (f.type.fnKind != TypeRef::FncKind::None) {
            if (f.type.fnRet) {
                std::string base; int ptr;
                resolveType(*f.type.fnRet, base, ptr);
                out << base << " ";
                for (int i = 0; i < ptr; i++) out << "*";
            } else out << "int32_t ";  // 默认返回类型
            out << "(*" << f.name << ")(";
            if (f.type.fnKind == TypeRef::FncKind::MethodPtr) {
                out << curAggrKind << " " << curAggr << " *_this";
                if (!f.type.fnParams.empty() || f.type.fnVariadic) out << ", ";
            }
            for (size_t i = 0; i < f.type.fnParams.size(); i++) {
                if (i) out << ", ";
                emitDeclarator(f.type.fnParams[i]);
            }
            if (f.type.fnVariadic) out << (f.type.fnParams.empty() ? "..." : ", ...");
            out << ")";
            return;
        }
        if (f.type.hasInline) { // 内联/匿名结构联合
            out << (f.type.inlineUnion ? "union" : "struct") << " {\n";
            depth++;
            for (auto& sub : f.type.inlineFields) {
                indent();
                emitDeclarator(sub);
                out << ";\n";
            }
            depth--;
            indent();
            out << "}";
            if (!f.name.empty()) out << " " << f.name;
            for (auto& dim : f.type.arrayDims) out << "[" << dim << "]";
            return;
        }
        std::string base; int ptr;
        resolveType(f.type, base, ptr);
        if (asConst) out << "const ";
        out << base << " ";
        for (int i = 0; i < ptr; i++) out << "*";
        out << (f.name == "this" ? "_this" : f.name);  // 参数名 this → _this
        for (auto& dim : f.type.arrayDims) out << "[" << dim << "]";
    }

    bool shouldStaticize(const Decl& d) const {
        return !d.exported && !d.external;
    }

    // ---------------- 伪 class：类型查询与方法识别 ----------------
    // 类型名 → struct/union 节点（穿透别名，最多 8 层防环）
    const Decl* aggrOf(std::string name) const {
        for (int i = 0; i < 8 && !name.empty(); i++) {
            auto it = aggrs.find(name);
            if (it != aggrs.end()) return it->second;
            auto al = aliases.find(name);
            if (al == aliases.end()) return nullptr;
            name = al->second;
        }
        return nullptr;
    }

    static bool hasMethods(const Decl* d) {
        for (auto& f : d->fields) if (f.type.fnKind == TypeRef::FncKind::MethodPtr) return true;
        return false;
    }

    static bool hasFieldDefaults(const Decl* d) {
        for (auto& f : d->fields) if (f.init) return true;
        return false;
    }

    // 表达式的轻量类型推断（仅覆盖方法调用需要的场景）
    bool exprVType(const Expr& e, VType& vt) const {
        switch (e.kind) {
            case Expr::Ident: {
                auto it = localsT.find(e.text);
                if (it != localsT.end()) { vt = it->second; return true; }
                it = globalsT.find(e.text);
                if (it != globalsT.end()) { vt = it->second; return true; }
                return false;
            }
            case Expr::Member: {
                VType base;
                if (!exprVType(*e.a, base)) return false;
                const Decl* sd = aggrOf(base.name);
                if (!sd) return false;
                for (auto& f : sd->fields)
                    if (f.name == e.text) {
                        vt = {f.type.name, f.type.ptr, (int)f.type.arrayDims.size()};
                        return true;
                    }
                return false;
            }
            case Expr::Index:
                if (!exprVType(*e.a, vt)) return false;
                if (vt.arr) vt.arr--; else if (vt.ptr) vt.ptr--;
                return true;
            case Expr::Unary:
                if (e.op == "*") {
                    if (!exprVType(*e.a, vt)) return false;
                    if (vt.ptr) vt.ptr--;
                    return true;
                }
                if (e.op == "&") {
                    if (!exprVType(*e.a, vt)) return false;
                    vt.ptr++;
                    return true;
                }
                return false;
            default: return false;
        }
    }

    // 若 m 是成员访问且该成员是方法字段，返回字段指针（否则 nullptr）
    const Field* callableField(const Expr& m) const {
        if (m.kind != Expr::Member) return nullptr;
        VType base;
        if (!exprVType(*m.a, base)) return nullptr;
        const Decl* sd = aggrOf(base.name);
        if (!sd) return nullptr;
        for (auto& f : sd->fields)
            if (f.name == m.text && f.type.fnKind != TypeRef::FncKind::None) return &f;
        return nullptr;
    }

    // ---------------- 表达式 ----------------
    void emitExpr(const Expr& e, bool top = false) {
        switch (e.kind) {
            case Expr::IntLit: case Expr::FloatLit:
            case Expr::StrLit: case Expr::CharLit:
                out << e.text;
                break;
            case Expr::Ident:
                if (e.text == "this") out << "_this";      // 方法内接收者
                else if (e.text == "nil") out << "NULL";   // 空指针常量
                else out << e.text;                        // true/false 由 stdbool.h 提供
                break;
            case Expr::Unary:
                out << e.op;
                out << "(";
                emitExpr(*e.a);
                out << ")";
                break;
            case Expr::PostUnary:
                emitExpr(*e.a);
                out << e.op;
                break;
            case Expr::Binary:
                if (!top) out << "(";
                emitExpr(*e.a);
                out << " " << e.op << " ";
                emitExpr(*e.b);
                if (!top) out << ")";
                break;
            case Expr::Ternary:
                if (!top) out << "(";
                emitExpr(*e.a);
                out << " ? ";
                emitExpr(*e.b);
                out << " : ";
                emitExpr(*e.c);
                if (!top) out << ")";
                break;
            case Expr::Call: {
                // 方法调用：仅成员函数指针才自动注入接收者
                const Field* mf = callableField(*e.a);
                emitExpr(*e.a);
                out << "(";
                bool first = true;
                if (mf && mf->type.fnKind == TypeRef::FncKind::MethodPtr) {
                    if (e.a->op == ".") out << "&";
                    emitExpr(*e.a->a);
                    first = false;
                }
                for (size_t i = 0; i < e.args.size(); i++) {
                    if (!first) out << ", ";
                    first = false;
                    emitExpr(*e.args[i], true);
                }
                out << ")";
                break;
            }
            case Expr::Index:
                emitExpr(*e.a);
                out << "[";
                emitExpr(*e.b, true);
                out << "]";
                break;
            case Expr::Member:
                emitExpr(*e.a);
                out << e.op << e.text;
                break;
            case Expr::Sizeof: {
                out << "sizeof(";
                // 若内层是单纯标识符且是 sc 内置类型名，做类型映射再输出
                if (e.a && e.a->kind == Expr::Ident) {
                    const std::string mapped = mapBase(e.a->text);
                    if (mapped != e.a->text) { out << mapped; }
                    else emitExpr(*e.a, true);
                } else {
                    emitExpr(*e.a, true);
                }
                out << ")";
                break;
            }
            case Expr::Offsetof:
                out << "offsetof(" << e.text << ", " << e.op << ")";
                break;
        }
    }

    // ---------------- 语句 ----------------
    void emitVarDecls(const std::vector<Field>& decls, bool asConst, bool isStatic = false) {
        for (auto& f : decls) {
            regVar(f);
            indent();
            if (isStatic) out << "static ";
            emitDeclarator(f, asConst);
            if (f.init) {
                out << " = ";
                emitExpr(*f.init, true);
            } else {
                const Decl* sd = aggrOf(f.type.name);
                if (sd && (sd->kind == Decl::StructD || sd->kind == Decl::UnionD)) {
                    if (sd->kind == Decl::StructD && hasFieldDefaults(sd)) {
                        out << " = " << sd->name << "__default()";
                    } else {
                        out << " = {0}";
                    }
                } else if (f.type.ptr == 0 && f.type.arrayDims.empty() && !f.type.hasInline) {
                    // 含方法字段的结构变量默认零初始化（方法指针默认 nil）
                    if (sd && hasMethods(sd)) out << " = {0}";
                }
            }
            out << ";\n";
        }
    }

    // 记录变量的轻量类型（函数内→局部表，否则→全局表）
    void regVar(const Field& f) {
        VType vt{f.type.name, f.type.ptr, (int)f.type.arrayDims.size()};
        (inFunc ? localsT : globalsT)[f.name] = vt;
    }

    void emitStmts(const std::vector<StmtPtr>& stmts) {
        for (auto& s : stmts) emitStmt(*s);
    }

    void emitStmt(const Stmt& s) {
        // 行号映射：为每个语句添加行号注释，便于调试时关联回原始 SC 源代码
        if (s.line > 0) {
            indent();
            out << "/* line " << s.line << " */\n";
        }
        
        switch (s.kind) {
            case Stmt::ExprS:
                indent();
                emitExpr(*s.expr, true);
                out << ";\n";
                break;
            case Stmt::VarS: emitVarDecls(s.decls, false); break;
            case Stmt::LetS: emitVarDecls(s.decls, true); break;
            case Stmt::ReturnS:
                indent();
                out << "return";
                if (s.expr) { out << " "; emitExpr(*s.expr, true); }
                out << ";\n";
                break;
            case Stmt::BreakS: indent(); out << "break;\n"; break;
            case Stmt::ContinueS: indent(); out << "continue;\n"; break;
            case Stmt::IfS:
                indent();
                out << "if (";
                emitExpr(*s.expr, true);
                out << ") {\n";
                depth++; emitStmts(s.body); depth--;
                indent(); out << "}";
                if (!s.elseBody.empty()) {
                    // else if 折叠
                    if (s.elseBody.size() == 1 && s.elseBody[0]->kind == Stmt::IfS) {
                        out << " else ";
                        emitElseIf(*s.elseBody[0]);
                    } else {
                        out << " else {\n";
                        depth++; emitStmts(s.elseBody); depth--;
                        indent(); out << "}\n";
                    }
                } else out << "\n";
                break;
            case Stmt::WhileS:
                indent();
                out << "while (";
                emitExpr(*s.expr, true);
                out << ") {\n";
                depth++; emitStmts(s.body); depth--;
                indent(); out << "}\n";
                break;
            case Stmt::DoWhileS:
                indent(); out << "do {\n";
                depth++; emitStmts(s.body); depth--;
                indent(); out << "} while (";
                emitExpr(*s.expr, true);
                out << ");\n";
                break;
            case Stmt::ForS:
                indent();
                out << "for (";
                if (s.forInit) emitExpr(*s.forInit, true);
                out << "; ";
                if (s.forCond) emitExpr(*s.forCond, true);
                out << "; ";
                if (s.forStep) emitExpr(*s.forStep, true);
                out << ") {\n";
                depth++; emitStmts(s.body); depth--;
                indent(); out << "}\n";
                break;
            case Stmt::CaseS:
                indent();
                out << "switch (";
                emitExpr(*s.expr, true);
                out << ") {\n";
                depth++;
                for (auto& arm : s.caseArms) {
                    if (arm.labels.empty()) {
                        indent(); out << "default:\n";
                    } else {
                        for (auto& lab : arm.labels) {
                            indent();
                            out << "case ";
                            emitExpr(*lab, true);
                            out << ":\n";
                        }
                    }
                    indent(); out << "{\n";
                    depth++;
                    emitStmts(arm.body);
                    if (!arm.through) {
                        indent(); out << "break;\n";
                    }
                    depth--;
                    indent(); out << "}\n";
                }
                depth--;
                indent(); out << "}\n";
                break;
            case Stmt::GotoS:
                indent(); out << "goto " << s.text << ";\n";
                break;
            case Stmt::LabelS:
                indent(); out << s.text << ":\n";
                depth++; emitStmts(s.body); depth--;
                break;
            case Stmt::DeclS:
                emitTypeDecl(*s.decl);
                break;
        }
    }

    void emitElseIf(const Stmt& s) { // "} else " 之后接 if，不缩进首行
        out << "if (";
        emitExpr(*s.expr, true);
        out << ") {\n";
        depth++; emitStmts(s.body); depth--;
        indent(); out << "}";
        if (!s.elseBody.empty()) {
            if (s.elseBody.size() == 1 && s.elseBody[0]->kind == Stmt::IfS) {
                out << " else ";
                emitElseIf(*s.elseBody[0]);
            } else {
                out << " else {\n";
                depth++; emitStmts(s.elseBody); depth--;
                indent(); out << "}\n";
            }
        } else out << "\n";
    }

    // ---------------- 类型定义 ----------------
    void emitFieldList(const std::vector<Field>& fields) {
        depth++;
        for (auto& f : fields) {
            indent();
            emitDeclarator(f);
            out << ";\n";
        }
        depth--;
    }

    void emitTypeDecl(const Decl& d) {
        switch (d.kind) {
            case Decl::EnumD:
                indent();
                out << "typedef enum { /* base: " << mapBase(d.type.name) << " */\n";
                depth++;
                for (size_t i = 0; i < d.fields.size(); i++) {
                    indent();
                    out << d.fields[i].name;
                    if (d.fields[i].init) {
                        out << " = ";
                        emitExpr(*d.fields[i].init, true);
                    }
                    if (i + 1 < d.fields.size()) out << ",";
                    out << "\n";
                }
                depth--;
                indent();
                out << "} " << d.name << ";\n\n";
                break;
            case Decl::StructD:
            case Decl::UnionD:
                curAggr = d.name;  // 方法字段展开需要所属类型名
                curAggrKind = d.kind == Decl::UnionD ? "union" : "struct";
                indent();
                out << "typedef " << (d.kind == Decl::UnionD ? "union" : "struct")
                    << " " << d.name << " {\n";
                emitFieldList(d.fields);
                indent();
                out << "} " << d.name << ";\n\n";
                if (d.kind == Decl::StructD && hasFieldDefaults(&d)) {
                    indent();
                    out << "static inline " << d.name << " " << d.name << "__default(void) {\n";
                    depth++;
                    indent(); out << d.name << " _v = {0};\n";
                    for (auto& f : d.fields) {
                        if (!f.init) continue;
                        indent();
                        out << "_v." << f.name << " = ";
                        emitExpr(*f.init, true);
                        out << ";\n";
                    }
                    indent(); out << "return _v;\n";
                    depth--;
                    indent(); out << "}\n\n";
                }
                break;
            case Decl::AliasD: {
                std::string base; int ptr;
                resolveType(d.type, base, ptr);
                indent();
                out << "typedef " << base << " ";
                for (int i = 0; i < ptr; i++) out << "*";
                out << d.name << ";\n\n";
                break;
            }
            case Decl::FuncTypeD: {
                indent();
                out << "typedef ";
                emitRetType(d);
                out << " " << d.name << "(";
                emitParams(d.fields, d.variadic);
                out << ");\n\n";
                break;
            }
            default:
                throw CompileError{"内部错误：非类型定义", d.line};
        }
    }

    // ---------------- 函数 ----------------
    void emitRetType(const Decl& d) {
        if (d.retType.name.empty() && d.retType.ptr == 0) {
            out << "int32_t"; // 默认返回类型
            return;
        }
        std::string base; int ptr;
        resolveType(d.retType, base, ptr);
        out << base;
        for (int i = 0; i < ptr; i++) out << " *";
    }

    void emitParams(const std::vector<Field>& params, bool variadic = false) {
        if (params.empty() && !variadic) { out << "void"; return; }
        for (size_t i = 0; i < params.size(); i++) {
            if (i) out << ", ";
            emitDeclarator(params[i]);
        }
        if (variadic) out << (params.empty() ? "..." : ", ...");
    }

    // 函数签名（实现预定义类型的函数，从函数类型表展开签名）
    void emitFuncSig(const Decl& d) {
        const Decl* sig = &d;
        if (!d.funcTypeName.empty()) {
            auto it = funcTypes.find(d.funcTypeName);
            if (it == funcTypes.end())
                throw CompileError{"未定义的函数类型: " + d.funcTypeName, d.line};
            sig = it->second;
        }
        emitRetType(*sig);
        out << " " << d.name << "(";
        if (!d.methodOwner.empty()) {
            out << d.methodOwner << " *_this";
            if (!sig->fields.empty() || sig->variadic) out << ", ";
        }
        emitParams(sig->fields, sig->variadic);
        out << ")";
    }

    void emitFunc(const Decl& d) {
        if (d.name != "main" && shouldStaticize(d)) out << "static ";
        emitFuncSig(d);
        out << " {\n";
        // 函数作用域：注册参数类型（含预定义函数类型展开的签名）
        localsT.clear();
        inFunc = true;
        const Decl* sig = &d;
        if (!d.funcTypeName.empty()) {
            auto it = funcTypes.find(d.funcTypeName);
            if (it != funcTypes.end()) sig = it->second;
        }
        for (auto& p : sig->fields) regVar(p);
        depth++;
        emitStmts(d.body);
        depth--;
        inFunc = false;
        out << "}\n\n";
    }

    // inc 头文件引入 → #include 行
    //   inc stdio.h    → #include <stdio.h>
    //   inc "my.h"     → #include "my.h"
    //   inc <stdio.h>  → #include <stdio.h>（原样）
    void emitInclude(const Decl& d) {
        const std::string& h = d.name;
        if (endsWith(h, ".sc")) {
            const std::string key = d.origin.empty() ? h : d.origin;
            out << "#include \"" << moduleHeaderName(key) << "\"\n";
            return;
        }
        if (!h.empty() && (h[0] == '"' || h[0] == '<')) out << "#include " << h << "\n";
        else out << "#include <" << h << ">\n";
    }

    // ---------------- 主流程：两遍扫描输出 ----------------
    // 第一遍：类型定义 + 全局变量 + 函数原型声明（forward declaration）
    // 第二遍：函数体实现
    // 这样做的目的是支持函数间的任意引用顺序（包括递归/互递归）
    std::string run() {
        out << "/* 由 scc 生成，请勿手工修改 */\n"
            << "#include <stdint.h>\n"
            << "#include <stddef.h>\n"
            << "#include <stdbool.h>\n"
            << "#include <stdarg.h>\n"
            << "#include <stdio.h>\n"
            << "#include <stdlib.h>\n"
            << "#include <string.h>\n";

        // 用户 inc 引入的头文件
        for (auto& d : prog.decls)
            if (d->kind == Decl::IncD) emitInclude(*d);
        out << "\n";

        // 收集函数类型与聚合类型（struct/union/别名）注册表
        for (auto& d : prog.decls) {
            if (d->kind == Decl::FuncTypeD) funcTypes[d->name] = d.get();
            else if (d->kind == Decl::StructD || d->kind == Decl::UnionD)
                aggrs[d->name] = d.get();
            else if (d->kind == Decl::AliasD) aliases[d->name] = d->type.name;
        }

        // 第一遍：类型、全局变量、函数原型
        for (auto& d : prog.decls) {
            switch (d->kind) {
                case Decl::EnumD: case Decl::StructD:
                case Decl::UnionD: case Decl::AliasD:
                case Decl::FuncTypeD:
                    emitTypeDecl(*d);
                    break;
                case Decl::VarD:
                    emitVarDecls(d->fields, false, shouldStaticize(*d));
                    break;
                case Decl::LetD:
                    emitVarDecls(d->fields, true, shouldStaticize(*d));
                    break;
                case Decl::FuncD:
                    if (d->name != "main") {
                        if (shouldStaticize(*d)) out << "static ";
                        emitFuncSig(*d);
                        out << ";\n";
                    }
                    break;
                case Decl::IncD: break;  // 已在顶部输出
            }
        }
        out << "\n";
        // 第二遍：函数定义
        for (auto& d : prog.decls)
            if (d->kind == Decl::FuncD) emitFunc(*d);
        return out.str();
    }

    // ---------------- 头文件生成（@导出对象） ----------------
    // 导出类型 → 完整 typedef；导出变量/常量 → extern；导出函数 → 原型
    std::string runHeader(const std::string& guard) {
        bool any = false;
        for (auto& d : prog.decls) if (d->exported) { any = true; break; }
        if (!any) return "";

        out << "/* 由 scc 生成，请勿手工修改 —— @导出对象声明 */\n"
            << "#ifndef " << guard << "\n"
            << "#define " << guard << "\n\n"
            << "#include <stdint.h>\n"
            << "#include <stddef.h>\n"
            << "#include <stdbool.h>\n\n";

        // 函数类型表（导出函数可能引用未导出的函数类型签名）
        for (auto& d : prog.decls)
            if (d->kind == Decl::FuncTypeD) funcTypes[d->name] = d.get();

        for (auto& d : prog.decls) {
            if (!d->exported) continue;
            switch (d->kind) {
                case Decl::EnumD: case Decl::StructD:
                case Decl::UnionD: case Decl::AliasD:
                case Decl::FuncTypeD:
                    emitTypeDecl(*d);
                    break;
                case Decl::VarD: emitExternVars(d->fields, false); break;
                case Decl::LetD: emitExternVars(d->fields, true); break;
                case Decl::FuncD:
                    emitFuncSig(*d);
                    out << ";\n";
                    break;
                case Decl::IncD: break;  // inc 不可导出（parser 已拦截）
            }
        }
        out << "\n#endif /* " << guard << " */\n";
        return out.str();
    }

    // extern 变量声明（头文件用：不带初值）
    void emitExternVars(const std::vector<Field>& decls, bool asConst) {
        for (auto& f : decls) {
            indent();
            out << "extern ";
            emitDeclarator(f, asConst);
            out << ";\n";
        }
    }
};

} // namespace

std::string emitC(const Program& prog) {
    CGen g(prog);
    return g.run();
}

std::string emitCHeader(const Program& prog, const std::string& guardName) {
    CGen g(prog);
    return g.runHeader(guardName);
}
