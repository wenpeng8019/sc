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
#include <sstream>
#include <unordered_map>

namespace {

// sc 内置基本类型 → C 标准类型映射
std::string mapBase(const std::string& n) {
    static const std::unordered_map<std::string, std::string> m = {
        {"i1", "int8_t"},  {"i2", "int16_t"}, {"i4", "int32_t"}, {"i8", "int64_t"},
        {"u1", "uint8_t"}, {"u2", "uint16_t"}, {"u4", "uint32_t"}, {"u8", "uint64_t"},
        {"f4", "float"},   {"f8", "double"},  {"v", "void"},
    };
    auto it = m.find(n);
    return it == m.end() ? n : it->second;
}

// CGen 内部类 —— 封装 C 代码生成的状态
struct CGen {
    const Program& prog;
    std::ostringstream out;     // 输出流
    int depth = 0;              // 当前缩进深度（每级4空格）
    std::unordered_map<std::string, const Decl*> funcTypes;  // 函数类型名→Decl 映射

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
            if (f.type.isArray) out << "[" << f.type.arraySize << "]";
            return;
        }
        std::string base; int ptr;
        resolveType(f.type, base, ptr);
        if (asConst) out << "const ";
        out << base << " ";
        for (int i = 0; i < ptr; i++) out << "*";
        out << f.name;
        if (f.type.isArray) out << "[" << f.type.arraySize << "]";
    }

    // ---------------- 表达式 ----------------
    void emitExpr(const Expr& e, bool top = false) {
        switch (e.kind) {
            case Expr::IntLit: case Expr::FloatLit:
            case Expr::StrLit: case Expr::CharLit:
            case Expr::Ident:
                out << e.text;
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
            case Expr::Call:
                emitExpr(*e.a);
                out << "(";
                for (size_t i = 0; i < e.args.size(); i++) {
                    if (i) out << ", ";
                    emitExpr(*e.args[i], true);
                }
                out << ")";
                break;
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
        }
    }

    // ---------------- 语句 ----------------
    void emitVarDecls(const std::vector<Field>& decls, bool asConst) {
        for (auto& f : decls) {
            indent();
            emitDeclarator(f, asConst);
            if (f.init) {
                out << " = ";
                emitExpr(*f.init, true);
            }
            out << ";\n";
        }
    }

    void emitStmts(const std::vector<StmtPtr>& stmts) {
        for (auto& s : stmts) emitStmt(*s);
    }

    void emitStmt(const Stmt& s) {
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
                indent();
                out << "typedef " << (d.kind == Decl::UnionD ? "union" : "struct")
                    << " " << d.name << " {\n";
                emitFieldList(d.fields);
                indent();
                out << "} " << d.name << ";\n\n";
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
                emitParams(d.fields);
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

    void emitParams(const std::vector<Field>& params) {
        if (params.empty()) { out << "void"; return; }
        for (size_t i = 0; i < params.size(); i++) {
            if (i) out << ", ";
            emitDeclarator(params[i]);
        }
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
        emitParams(sig->fields);
        out << ")";
    }

    void emitFunc(const Decl& d) {
        emitFuncSig(d);
        out << " {\n";
        depth++;
        emitStmts(d.body);
        depth--;
        out << "}\n\n";
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
            << "#include <stdio.h>\n"
            << "#include <stdlib.h>\n"
            << "#include <string.h>\n\n";

        // 收集函数类型
        for (auto& d : prog.decls)
            if (d->kind == Decl::FuncTypeD) funcTypes[d->name] = d.get();

        // 第一遍：类型、全局变量、函数原型
        for (auto& d : prog.decls) {
            switch (d->kind) {
                case Decl::EnumD: case Decl::StructD:
                case Decl::UnionD: case Decl::AliasD:
                case Decl::FuncTypeD:
                    emitTypeDecl(*d);
                    break;
                case Decl::VarD: emitVarDecls(d->fields, false); break;
                case Decl::LetD: emitVarDecls(d->fields, true); break;
                case Decl::FuncD:
                    if (d->name != "main") {
                        emitFuncSig(*d);
                        out << ";\n";
                    }
                    break;
            }
        }
        out << "\n";
        // 第二遍：函数定义
        for (auto& d : prog.decls)
            if (d->kind == Decl::FuncD) emitFunc(*d);
        return out.str();
    }
};

} // namespace

std::string emitC(const Program& prog) {
    CGen g(prog);
    return g.run();
}
