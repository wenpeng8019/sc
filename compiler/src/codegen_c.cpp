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
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace {

// sc 内置基本类型 → C 标准类型映射
std::string mapBase(const std::string& n) {
    static const std::unordered_map<std::string, std::string> m = {
        {"i1", "int8_t"},  {"i2", "int16_t"}, {"i4", "int32_t"}, {"i8", "int64_t"},
        {"u1", "uint8_t"}, {"u2", "uint16_t"}, {"u4", "uint32_t"}, {"u8", "uint64_t"},
        {"f4", "float"},   {"f8", "double"},
        {"bool", "uint8_t"},  // 布尔：u1 的语义别名（true/false 即 1/0）
        {"char", "char"},     // 字符：与 C 字符串字面量/接口互操作用（区别于 i1/u1）
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
    std::string srcFile;        // 非空时输出 #line 指令，调试器映射回 .sc 源码
    std::unordered_map<std::string, const Decl*> funcTypes;  // 函数类型名→Decl 映射
    std::unordered_map<std::string, const Decl*> rpcs;       // rpc 名→Decl（run 语句查询）
    bool usesRun = false;       // 程序中出现 run 语句：需输出 thread_run 原型
    bool usesWait = false;      // 程序中出现 wait 语句：需输出 cond_wait 原型

    // ---- 伪 class 支撑：类型注册表与变量类型跟踪 ----
    std::unordered_map<std::string, const Decl*> aggrs;    // struct/union 名 → Decl
    std::unordered_map<std::string, std::string> aliases;  // 别名 → 目标类型名
    // 顶层方法表：所属类型 → 方法名 → Decl（fnc T::m 定义或声明）
    std::unordered_map<std::string, std::unordered_map<std::string, const Decl*>> methods;
    // 变量的轻量类型信息（类型名 + 指针层数 + 数组维数），用于方法调用识别
    struct VType { std::string name; int ptr = 0; int arr = 0; };
    std::unordered_map<std::string, VType> globalsT, localsT;
    bool inFunc = false;        // 当前是否在函数体内（决定变量注册到哪个表）
    std::string curAggr;        // 当前正在输出的 struct/union 名（方法字段展开用）
    std::string curAggrKind;    // "struct" / "union"

    // ---- rpc 支撑：实际函数体内，参数引用改写为 _p->name ----
    const Decl* curRpc = nullptr;               // 非空时正在输出 rpc 实际函数体
    std::unordered_set<std::string> rpcParams;  // 当前 rpc 的参数名集合
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
            } else out << "void ";  // 省略返回类型 = void
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

    // ---- T() 伪调用预扫描：收集需要生成 T__new 辅助函数的类型 ----
    std::set<std::string> heapNews;

    void scanExprForNew(const Expr& e) {
        if (e.kind == Expr::Call && e.a && e.a->kind == Expr::Ident && e.args.empty())
            if (const Decl* sd = aggrOf(e.a->text)) heapNews.insert(sd->name);
        if (e.a) scanExprForNew(*e.a);
        if (e.b) scanExprForNew(*e.b);
        if (e.c) scanExprForNew(*e.c);
        for (auto& a : e.args) scanExprForNew(*a);
    }

    void scanStmtForNew(const Stmt& s) {
        if (s.kind == Stmt::RunS) usesRun = true;
        if (s.kind == Stmt::WaitS) usesWait = true;
        if (s.expr) scanExprForNew(*s.expr);
        for (auto& f : s.decls) if (f.init) scanExprForNew(*f.init);
        if (s.forInit) scanExprForNew(*s.forInit);
        if (s.forCond) scanExprForNew(*s.forCond);
        if (s.forStep) scanExprForNew(*s.forStep);
        for (auto& b : s.body) scanStmtForNew(*b);
        for (auto& b : s.elseBody) scanStmtForNew(*b);
        for (auto& arm : s.caseArms) {
            for (auto& l : arm.labels) scanExprForNew(*l);
            for (auto& b : arm.body) scanStmtForNew(*b);
        }
    }

    // 生成堆构造辅助函数：T *T__new(void) = malloc + 默认值/清零 + init
    void emitNewHelpers() {
        for (auto& tn : heapNews) {
            auto it = aggrs.find(tn);
            if (it == aggrs.end()) continue;
            const Decl* sd = it->second;
            out << "static inline " << tn << " *" << tn << "__new(void) {\n"
                << "    " << tn << " *_p = (" << tn << " *)malloc(sizeof(" << tn << "));\n"
                << "    if (_p) {\n";
            if (sd->kind == Decl::StructD && hasFieldDefaults(sd))
                out << "        *_p = " << tn << "__default();\n";
            else
                out << "        memset(_p, 0, sizeof(" << tn << "));\n";
            const Decl* im = findMethod(tn, "init");
            if (im && im->fields.empty())
                out << "        " << im->name << "(_p);\n";
            out << "    }\n    return _p;\n}\n\n";
        }
    }

    // 查找顶层方法：类型名（穿透别名）→ 方法 Decl（未找到 nullptr）
    const Decl* findMethod(std::string typeName, const std::string& m) const {
        for (int i = 0; i < 8 && !typeName.empty(); i++) {
            auto it = methods.find(typeName);
            if (it != methods.end()) {
                auto mit = it->second.find(m);
                if (mit != it->second.end()) return mit->second;
            }
            auto al = aliases.find(typeName);
            if (al == aliases.end()) return nullptr;
            typeName = al->second;
        }
        return nullptr;
    }

    // T() 类型伪调用：被调对象是聚合类型名（无参、未被变量遮蔽）
    // → 堆构造糖，返回解析后的聚合类型 Decl（否则 nullptr）
    const Decl* typeCallee(const Expr& call) const {
        if (!call.a || call.a->kind != Expr::Ident || !call.args.empty()) return nullptr;
        const std::string& n = call.a->text;
        if (localsT.count(n) || globalsT.count(n)) return nullptr;
        return aggrOf(n);
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
            case Expr::Cast:
                vt = {e.text, e.castPtr, 0};
                return true;
            case Expr::Binary:
                // 赋值表达式的结果类型 = 左操作数类型（支持 (p = T())->m() 等）
                if (!e.op.empty() && e.op.back() == '='
                    && e.op != "==" && e.op != "!=" && e.op != "<=" && e.op != ">=")
                    return exprVType(*e.a, vt);
                return false;
            case Expr::Call:
                // T() 伪调用结果类型：T&（使链式方法调用可推断）
                if (const Decl* td = typeCallee(e)) {
                    vt = {td->name, 1, 0};
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
                else if (curRpc && rpcParams.count(e.text))
                    out << "_p->" << e.text;               // rpc 实际函数内：参数即结构体成员
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
                // 类型伪调用糖：T() → 堆构造 T__new()（malloc + 默认值 + init）
                if (const Decl* td = typeCallee(e)) {
                    out << td->name << "__new()";
                    break;
                }
                // 顶层方法调用糖：o.m(...) / p->m(...) → T_m(&o/p, ...)
                if (e.a->kind == Expr::Member && !callableField(*e.a)) {
                    VType base;
                    if (exprVType(*e.a->a, base) && base.arr == 0 && base.ptr <= 1) {
                        if (const Decl* md = findMethod(base.name, e.a->text)) {
                            out << md->name << "(";   // 修饰名 T_m
                            if (e.a->op == ".") out << "&";
                            emitExpr(*e.a->a);
                            for (auto& a : e.args) {
                                out << ", ";
                                emitExpr(*a, true);
                            }
                            out << ")";
                            break;
                        }
                    }
                }
                // 方法字段调用：成员函数指针自动注入接收者
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
            case Expr::Cast: {
                // (expr: type&) → ((T*)(expr))
                out << "((" << mapBase(e.text);
                for (int i = 0; i < e.castPtr; i++) out << "*";
                out << ")(";
                emitExpr(*e.a, true);
                out << "))";
                break;
            }
            case Expr::InitList: {
                out << "{";
                for (size_t i = 0; i < e.args.size(); i++) {
                    if (i) out << ", ";
                    emitExpr(*e.args[i], true);
                }
                out << "}";
                break;
            }
        }
    }

    // ---------------- 语句 ----------------
    void emitVarDecls(const std::vector<Field>& decls, bool asConst,
                      bool isStatic = false, bool isTls = false) {
        for (auto& f : decls) {
            regVar(f);
            indent();
            if (isTls) out << "static TLS ";   // tls：必为 static（C 规范），TLS 宏见 platform.h
            else if (isStatic) out << "static ";
            emitDeclarator(f, asConst);
            if (f.init) {
                out << " = ";
                emitExpr(*f.init, true);
            } else {
                const Decl* sd = aggrOf(f.type.name);
                if (sd && (sd->kind == Decl::StructD || sd->kind == Decl::UnionD)) {
                    // tls 为 static 存储期：初始化须常量表达式，不能调 __default()
                    if (!isTls && sd->kind == Decl::StructD && hasFieldDefaults(sd)) {
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
            // 声明即构造：函数内无初值的结构变量，若类型有无参 init 方法则自动调用
            // （tls 除外：static 存储期只初始化一次，此处会每次进函数重执行）
            if (inFunc && !isTls && !f.init && f.type.ptr == 0 && f.type.arrayDims.empty()
                && !f.type.hasInline) {
                const Decl* im = findMethod(f.type.name, "init");
                if (im && im->fields.empty()) {
                    indent();
                    out << im->name << "(&" << f.name << ");\n";
                }
            }
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
        // 行号映射：指定了源文件时输出 #line 指令（调试器断点/单步/堆栈
        // 直接落在 .sc 源码）；否则输出注释供人工对照
        if (s.line > 0) {
            if (!srcFile.empty()) {
                out << "#line " << s.line << " \"" << srcFile << "\"\n";
            } else {
                indent();
                out << "/* line " << s.line << " */\n";
            }
        }
        
        switch (s.kind) {
            case Stmt::ExprS:
                indent();
                emitExpr(*s.expr, true);
                out << ";\n";
                break;
            case Stmt::VarS: emitVarDecls(s.decls, false); break;
            case Stmt::LetS: emitVarDecls(s.decls, true); break;
            case Stmt::TlsS: emitVarDecls(s.decls, false, false, true); break;
            case Stmt::ReturnS:
                indent();
                if (curRpc) {
                    // rpc 实际函数：返回值写入结构体首个默认成员 _
                    if (s.expr && rpcHasRet(*curRpc)) {
                        out << "_p->_ = ";
                        emitExpr(*s.expr, true);
                        out << "; return;\n";
                    } else out << "return;\n";
                    break;
                }
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
            case Stmt::RunS:
                emitRunStmt(s);
                break;
            case Stmt::WaitS:
                emitWaitStmt(s);
                break;
        }
    }

    // run 语句 → 装填 rpc 参数结构体 + thread_run 调用
    //   run worker(a, b), &t →
    //   { struct worker _rp = {0}; _rp.x = a; ...;
    //     thread_run((void (*)(void *))worker_rpc, &_rp, sizeof(_rp), (thread **)(&t)); }
    // thread_run 在 m_impl 中实现：单次 alloc(sizeof(thread)+sizeof(参数)+实现私有区)，
    // 参数 memcpy 到 thread 对象紧随位置；出参为空时 detach 自释放。
    // run 语句 → 装填 rpc 参数结构体 + 线程原语调用（第二参按类型静态分派）
    //   run worker(a, b), &t →
    //   { struct worker _rp = {0}; _rp.x = a; ...;
    //     thread_run((void (*)(void *))worker_rpc, &_rp, sizeof(_rp), (thread **)(&t)); }
    //   run worker(a, b), p（p 为 pool 对象或指针，对象自动取地址）→
    //     pool_run(&p, (void (*)(void *))worker_rpc, &_rp, sizeof(_rp));
    // thread_run 在 m_impl 中实现：单次 alloc(sizeof(thread)+sizeof(参数)+实现私有区)，
    // 参数 memcpy 到 thread 对象紧随位置；出参为空时 detach 自释放。
    // pool_run 同哲学：参数拷贝入任务节点，调用点无需保活。
    void emitRunStmt(const Stmt& s) {
        const Expr& call = *s.expr;
        auto it = rpcs.find(call.a->text);
        if (it == rpcs.end())
            throw CompileError{"run 的目标必须是 rpc: " + call.a->text, s.line};
        const Decl* r = it->second;
        if (call.args.size() != r->fields.size())
            throw CompileError{"rpc 实参数量不匹配: " + r->name + " 期望 " +
                               std::to_string(r->fields.size()) + " 个", s.line};
        if (!aggrOf("thread"))
            throw CompileError{"run 语句需要 thread 类型，请先 inc m.sc", s.line};
        // 第二参类型分派：pool（对象/指针）→ 入池；其余 → thread 出参
        bool toPool = false;
        if (s.forInit) {
            VType vt;
            if (exprVType(*s.forInit, vt) && vt.arr == 0 && vt.ptr <= 1) {
                const Decl* sd = aggrOf(vt.name);
                if (sd && sd->name == "pool") toPool = true;
            }
        }
        indent(); out << "{\n";
        depth++;
        indent(); out << "struct " << r->name << " _rp = {0};\n";
        for (size_t i = 0; i < call.args.size(); i++) {
            indent();
            out << "_rp." << r->fields[i].name << " = ";
            emitExpr(*call.args[i], true);
            out << ";\n";
        }
        indent();
        if (toPool) {
            out << "pool_run(";
            emitAutoAddr(*s.forInit);
            out << ", (void (*)(void *))" << r->name << "_rpc, &_rp, sizeof(_rp));\n";
        } else {
            out << "thread_run((void (*)(void *))" << r->name << "_rpc, &_rp, sizeof(_rp), ";
            if (s.forInit) {
                out << "(thread **)(";
                emitExpr(*s.forInit, true);
                out << ")";
            } else out << "NULL";
            out << ");\n";
        }
        depth--;
        indent(); out << "}\n";
    }

    // wait 语句 → cond_wait 调用（cond_wait 在 m_impl 中实现）
    //   wait c, m            → cond_wait(&c, &m, 0, 0);        无限等待
    //   wait c, m, ns, s     → cond_wait(&c, &m, ns, s);       超时等待
    // cond/mutex 实参可为对象或指针：对象自动取地址（与方法调用糖一致）
    void emitWaitStmt(const Stmt& s) {
        if (!aggrOf("cond") || !aggrOf("mutex"))
            throw CompileError{"wait 语句需要 cond/mutex 类型，请先 inc m.sc", s.line};
        indent();
        out << "cond_wait(";
        emitAutoAddr(*s.expr);     // cond
        out << ", ";
        emitAutoAddr(*s.forInit);  // mutex
        out << ", ";
        if (s.forCond) emitExpr(*s.forCond, true); else out << "0";
        out << ", ";
        if (s.forStep) emitExpr(*s.forStep, true); else out << "0";
        out << ");\n";
    }

    // 对象 → &(对象)，指针 → 原样（按轻量类型推断；不可推断时默认按对象取地址）
    void emitAutoAddr(const Expr& e) {
        VType vt;
        bool isPtr = exprVType(e, vt) && vt.ptr > 0 && vt.arr == 0;
        if (!isPtr) out << "&(";
        emitExpr(e, true);
        if (!isPtr) out << ")";
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
            out << "void"; // 省略返回类型 = void
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
            if (!sig->fields.empty() || sig->variadic) {
                out << ", ";
                emitParams(sig->fields, sig->variadic);
            }
        } else {
            emitParams(sig->fields, sig->variadic);
        }
        out << ")";
    }

    void emitFunc(const Decl& d) {
        // 函数定义行映射回 .sc 源码（函数序言断点落在 fnc 行）
        if (!srcFile.empty() && d.line > 0)
            out << "#line " << d.line << " \"" << srcFile << "\"\n";
        if (d.isRpc) { emitRpcWorker(d); return; }
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

    // ---------------- rpc：伪形参函数糖 ----------------
    // rpc add: i4, a: i4, b: i4 展开为三件套：
    //   struct add { int32_t _; int32_t a; int32_t b; };  // 同名参数结构体
    //   void add_rpc(struct add *_p);                      // 实际函数
    //   static inline int32_t add(int32_t a, int32_t b);   // 调用包装
    // 结构体仅用 tag（不 typedef）：C 中 struct tag 与函数名分属不同
    // 命名空间，故二者可同名，调用形式与 fnc 完全一致。

    // 是否有返回值（与 fnc 一致：省略返回类型 = void 无返回值）
    static bool rpcHasRet(const Decl& d) {
        return !d.retType.name.empty() || d.retType.ptr > 0;
    }

    // 同名参数结构体：返回槽 _ 为首个默认成员（C 侧可用 _ 访问）
    void emitRpcStruct(const Decl& d) {
        out << "struct " << d.name << " {\n";
        depth++;
        if (rpcHasRet(d)) {
            indent();
            emitRetType(d);
            out << " _;\n";
        } else if (d.fields.empty()) {
            indent();
            out << "char _;\n";  // C 不允许空结构体：占位
        }
        for (auto& f : d.fields) {
            indent();
            emitDeclarator(f);
            out << ";\n";
        }
        depth--;
        out << "};\n";
    }

    // 实际函数签名：void name_rpc(struct name *_p)
    void emitRpcWorkerSig(const Decl& d) {
        out << "void " << d.name << "_rpc(struct " << d.name << " *_p)";
    }

    // 调用包装：装填结构体 → 执行实际函数 → 取返回槽
    void emitRpcWrapper(const Decl& d) {
        out << "static inline ";
        emitRetType(d);
        out << " " << d.name << "(";
        emitParams(d.fields);
        out << ") {\n";
        depth++;
        indent(); out << "struct " << d.name << " _p = {0};\n";
        for (auto& f : d.fields) {
            indent();
            out << "_p." << f.name << " = "
                << (f.name == "this" ? "_this" : f.name) << ";\n";
        }
        indent(); out << d.name << "_rpc(&_p);\n";
        if (rpcHasRet(d)) { indent(); out << "return _p._;\n"; }
        depth--;
        out << "}\n\n";
    }

    // rpc 接口三件套：结构体 + 实际函数原型 + 调用包装
    // workerStatic：本模块定义且未导出时 static；仅声明/导出时 extern
    void emitRpcInterface(const Decl& d, bool workerStatic) {
        emitRpcStruct(d);
        if (workerStatic) out << "static ";
        emitRpcWorkerSig(d);
        out << ";\n";
        emitRpcWrapper(d);
    }

    // rpc 实际函数体：参数引用由 emitExpr/ReturnS 改写为 _p->xxx
    void emitRpcWorker(const Decl& d) {
        if (shouldStaticize(d)) out << "static ";
        emitRpcWorkerSig(d);
        out << " {\n";
        localsT.clear();
        inFunc = true;
        for (auto& p : d.fields) regVar(p);
        curRpc = &d;
        rpcParams.clear();
        for (auto& p : d.fields) rpcParams.insert(p.name);
        depth++;
        emitStmts(d.body);
        depth--;
        curRpc = nullptr;
        rpcParams.clear();
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

    // 为所有结构/联合生成前置 typedef 声明：
    //   typedef struct X X;
    //   typedef union U U;
    // 目的：让结构/函数定义顺序与源码解耦，先使用再定义也可通过 C 编译。
    void emitForwardAggrDecls(bool exportedOnly = false) {
        std::unordered_set<std::string> emitted;
        for (auto& d : prog.decls) {
            if (d->kind != Decl::StructD && d->kind != Decl::UnionD) continue;
            if (exportedOnly && !d->exported) continue;
            if (d->external) continue;  // 外部模块类型由其模块头提供
            if (!emitted.insert(d->name).second) continue;
            out << "typedef " << (d->kind == Decl::UnionD ? "union" : "struct")
                << " " << d->name << " " << d->name << ";\n";
        }
        if (!emitted.empty()) out << "\n";
    }

    // ---------------- 主流程：两遍扫描输出 ----------------
    // 第一遍：类型定义 + 全局变量 + 函数原型声明（forward declaration）
    // 第二遍：函数体实现
    // 这样做的目的是支持函数间的任意引用顺序（包括递归/互递归）
    std::string run() {
        // 标准 C 头统一由 builtins/platform.h 提供（该目录默认在 -I 路径），
        // 同时带入 TLS 宏等跨平台适配
        out << "/* 由 scc 生成，请勿手工修改 */\n"
            << "#include \"platform.h\"\n";

        // 用户 inc 引入的头文件
        for (auto& d : prog.decls)
            if (d->kind == Decl::IncD) emitInclude(*d);
        out << "\n";

        // 最简策略：默认为所有结构/联合输出前置声明，消除定义顺序依赖
        emitForwardAggrDecls();

        // 收集函数类型、聚合类型与顶层方法注册表（含外部模块合并声明，供语法糖识别）
        for (auto& d : prog.decls) {
            if (d->kind == Decl::FuncTypeD && !d->isRpc && d->methodOwner.empty())
                funcTypes[d->name] = d.get();
            else if (d->kind == Decl::StructD || d->kind == Decl::UnionD)
                aggrs[d->name] = d.get();
            else if (d->kind == Decl::AliasD) aliases[d->name] = d->type.name;
            if (!d->methodOwner.empty() && !d->isRpc)
                methods[d->methodOwner][d->methodName] = d.get();
            if (d->isRpc) rpcs[d->name] = d.get();  // run 语句目标查询
        }

        // 预扫描 T() 伪调用（仅本单元代码，外部合并声明不扫），顺带标记 run 语句
        heapNews.clear();
        for (auto& d : prog.decls) {
            if (d->external) continue;
            for (auto& f : d->fields) if (f.init) scanExprForNew(*f.init);
            for (auto& s : d->body) scanStmtForNew(*s);
        }

        // run 语句线程原语：thread 对象与 rpc 参数联合分配，实现在 m 子项目（m_impl）
        if (usesRun) {
            out << "typedef struct thread thread;\n"
                << "extern uint8_t thread_run(void (*)(void *), const void *, size_t, thread **);\n";
            // 第二参可能是 pool：pool 类型可见即一并输出 pool_run 原型
            if (aggrOf("pool"))
                out << "typedef struct pool pool;\n"
                    << "extern uint8_t pool_run(pool *, void (*)(void *), const void *, size_t);\n";
            out << "\n";
        }

        // wait 语句条件等待原语：实现在 m 子项目（m_impl）
        if (usesWait)
            out << "typedef struct cond cond;\n"
                << "typedef struct mutex mutex;\n"
                << "extern int32_t cond_wait(cond *, mutex *, uint64_t, uint64_t);\n\n";

        // 第一遍：类型、全局变量、函数原型（外部模块声明不参与输出，由模块头提供）
        for (auto& d : prog.decls) {
            if (d->external && d->kind != Decl::IncD) continue;
            switch (d->kind) {
                case Decl::EnumD: case Decl::StructD:
                case Decl::UnionD: case Decl::AliasD:
                    emitTypeDecl(*d);
                    break;
                case Decl::FuncTypeD:
                    // rpc 仅声明：接口三件套，实际函数在外部（C 侧）实现
                    if (d->isRpc) emitRpcInterface(*d, false);
                    // 方法声明（fnc T::m 无函数体）：extern 原型，实现在外部（C 侧）
                    else if (!d->methodOwner.empty()) { emitFuncSig(*d); out << ";\n"; }
                    else emitTypeDecl(*d);
                    break;
                case Decl::VarD:
                    emitVarDecls(d->fields, false, shouldStaticize(*d));
                    break;
                case Decl::LetD:
                    emitVarDecls(d->fields, true, shouldStaticize(*d));
                    break;
                case Decl::TlsD:
                    emitVarDecls(d->fields, false, false, true);  // 始终 static TLS
                    break;
                case Decl::FuncD:
                    if (d->isRpc) { emitRpcInterface(*d, shouldStaticize(*d)); break; }
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
        // 堆构造辅助函数（T() 伪调用糖使用）
        emitNewHelpers();
        // 第二遍：函数定义（外部模块函数在其自身单元实现）
        for (auto& d : prog.decls)
            if (d->kind == Decl::FuncD && !d->external) emitFunc(*d);
        return out.str();
    }

    // ---------------- 头文件生成（@导出对象） ----------------
    // 导出类型 → 完整 typedef；导出变量/常量 → extern；导出函数 → 原型
    std::string runHeader(const std::string& guard) {
        bool any = false;
        for (auto& d : prog.decls) if (d->exported && !d->external) { any = true; break; }
        if (!any) return "";

        out << "/* 由 scc 生成，请勿手工修改 —— @导出对象声明 */\n"
            << "#ifndef " << guard << "\n"
            << "#define " << guard << "\n\n"
            << "#include \"platform.h\"\n\n";

        // 头文件同样先输出导出结构/联合的前置声明，减少声明顺序耦合
        emitForwardAggrDecls(true);

        // 函数类型表与方法表（导出函数可能引用未导出的函数类型签名）
        for (auto& d : prog.decls) {
            if (d->kind == Decl::FuncTypeD && !d->isRpc && d->methodOwner.empty())
                funcTypes[d->name] = d.get();
            if (!d->methodOwner.empty() && !d->isRpc)
                methods[d->methodOwner][d->methodName] = d.get();
        }

        for (auto& d : prog.decls) {
            if (!d->exported || d->external) continue;
            switch (d->kind) {
                case Decl::EnumD: case Decl::StructD:
                case Decl::UnionD: case Decl::AliasD:
                    emitTypeDecl(*d);
                    break;
                case Decl::FuncTypeD:
                    if (d->isRpc) emitRpcInterface(*d, false);
                    else if (!d->methodOwner.empty()) { emitFuncSig(*d); out << ";\n"; }
                    else emitTypeDecl(*d);
                    break;
                case Decl::VarD: emitExternVars(d->fields, false); break;
                case Decl::LetD: emitExternVars(d->fields, true); break;
                case Decl::TlsD: break;  // tls 不可导出（parser 已拦截）
                case Decl::FuncD:
                    if (d->isRpc) { emitRpcInterface(*d, false); break; }
                    emitFuncSig(*d);
                    out << ";\n";
                    break;
                case Decl::IncD:
                    emitInclude(*d);
                    break;
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

std::string emitC(const Program& prog, const std::string& srcFile) {
    CGen g(prog);
    g.srcFile = srcFile;
    return g.run();
}

std::string emitCHeader(const Program& prog, const std::string& guardName) {
    CGen g(prog);
    return g.runHeader(guardName);
}
