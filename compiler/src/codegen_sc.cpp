// ============================================================
// SC 源码再生器 —— Program AST → 规范化 sc 源码
// ============================================================
// 从 AST 反向生成整齐缩进的 sc 源码（保持原始语法，仅格式化）。
// VSCode 插件用它生成"树结构源码"虚拟文档（.tree.sc），
// 可与 AST 视图左右对照阅读。
// ============================================================
#include "codegen_sc.h"
#include "ast_print.h"
#include <sstream>

namespace {

struct SGen {
    std::ostringstream out;
    int depth = 0;

    void ind() { for (int i = 0; i < depth; i++) out << "\t"; }

    void emitStmts(const std::vector<StmtPtr>& stmts) {
        for (auto& s : stmts) emitStmt(*s);
    }

    void emitVarLine(const char* kw, const std::vector<Field>& items) {
        ind();
        out << kw << " ";
        for (size_t i = 0; i < items.size(); i++) {
            if (i) out << ", ";
            out << fieldToStr(items[i], true);
        }
        out << "\n";
    }

    void emitStmt(const Stmt& s) {
        switch (s.kind) {
            case Stmt::ExprS:
                ind(); out << exprToStr(*s.expr) << "\n";
                break;
            case Stmt::VarS: emitVarLine("var", s.decls); break;
            case Stmt::LetS: emitVarLine("let", s.decls); break;
            case Stmt::ReturnS:
                ind();
                out << "return";
                if (s.expr) out << " " << exprToStr(*s.expr);
                out << "\n";
                break;
            case Stmt::BreakS: ind(); out << "break\n"; break;
            case Stmt::ContinueS: ind(); out << "continue\n"; break;
            case Stmt::IfS:
                ind(); out << "if " << exprToStr(*s.expr) << "\n";
                depth++; emitStmts(s.body); depth--;
                emitElse(s.elseBody);
                break;
            case Stmt::WhileS:
                ind(); out << "while " << exprToStr(*s.expr) << "\n";
                depth++; emitStmts(s.body); depth--;
                break;
            case Stmt::ForS:
                ind();
                out << "for "
                    << (s.forInit ? exprToStr(*s.forInit) : "") << "; "
                    << (s.forCond ? exprToStr(*s.forCond) : "") << "; "
                    << (s.forStep ? exprToStr(*s.forStep) : "") << "\n";
                depth++; emitStmts(s.body); depth--;
                break;
            case Stmt::DeclS:
                emitDecl(*s.decl);
                break;
        }
    }

    void emitElse(const std::vector<StmtPtr>& elseBody) {
        if (elseBody.empty()) return;
        if (elseBody.size() == 1 && elseBody[0]->kind == Stmt::IfS) { // else if 链
            const Stmt& s = *elseBody[0];
            ind(); out << "else if " << exprToStr(*s.expr) << "\n";
            depth++; emitStmts(s.body); depth--;
            emitElse(s.elseBody);
        } else {
            ind(); out << "else\n";
            depth++; emitStmts(elseBody); depth--;
        }
    }

    void emitDecl(const Decl& d) {
        switch (d.kind) {
            case Decl::EnumD:
                ind(); out << "def " << d.name << ": " << typeToStr(d.type) << "\n";
                depth++;
                for (auto& f : d.fields) {
                    ind();
                    out << f.name;
                    if (f.init) out << " = " << exprToStr(*f.init);
                    out << "\n";
                }
                depth--;
                break;
            case Decl::StructD:
            case Decl::UnionD: {
                const char* open = d.kind == Decl::StructD ? "{" : "(";
                const char* close = d.kind == Decl::StructD ? "}" : ")";
                ind(); out << "def " << d.name << ": " << open << "\n";
                depth++;
                for (auto& f : d.fields) { ind(); out << fieldToStr(f, false) << "\n"; }
                depth--;
                ind(); out << close << "\n";
                break;
            }
            case Decl::AliasD:
                ind(); out << "def " << d.name << " -> " << typeToStr(d.type) << "\n";
                break;
            case Decl::FuncTypeD:
                ind(); out << "fnc " << d.name << ":" << fncItems(d) << "\n";
                break;
            case Decl::FuncD:
                ind();
                if (!d.funcTypeName.empty())
                    out << "fnc " << d.name << " -> " << d.funcTypeName << "\n";
                else
                    out << "fnc " << d.name << ":" << fncItems(d) << "\n";
                depth++;
                emitStmts(d.body);
                depth--;
                break;
            case Decl::VarD: emitVarLine("var", d.fields); break;
            case Decl::LetD: emitVarLine("let", d.fields); break;
        }
    }

    // fnc 单行项：返回类型 + 参数
    std::string fncItems(const Decl& d) {
        std::vector<std::string> parts;
        std::string ret = typeToStr(d.retType);
        if (!ret.empty()) parts.push_back(ret);
        for (auto& f : d.fields) parts.push_back(fieldToStr(f, false));
        std::string s;
        for (size_t i = 0; i < parts.size(); i++)
            s += (i ? ", " : " ") + parts[i];
        return s;
    }

    std::string run(const Program& prog) {
        out << "# 由 scc --emit-sc 从 AST 再生成\n\n";
        for (auto& d : prog.decls) {
            emitDecl(*d);
            out << "\n";
        }
        return out.str();
    }
};

} // namespace

std::string emitSc(const Program& prog) {
    SGen g;
    return g.run(prog);
}
