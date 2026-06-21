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
#include <unordered_map>
#include <vector>

namespace {

struct SGen {
    std::ostringstream out;
    int depth = 0;
    // 成员函数表：所属类型 → 方法 Decl 列表（再生时印回结构体内部）
    std::unordered_map<std::string, std::vector<const Decl*>> methodImpls;

    void ind() { for (int i = 0; i < depth; i++) out << "    "; }

    void emitStmts(const std::vector<StmtPtr>& stmts) {
        for (auto& s : stmts) emitStmt(*s);
    }

    // 赋值链尾部的匿名函数字面量（var/赋值 RHS 为 fnc: ...）：用于语句层多行输出函数体
    static const Expr* trailingFncLit(const Expr& e) {
        if (e.kind == Expr::FncLit) return &e;
        if (e.kind == Expr::Binary && e.op == "=" && e.b)
            return trailingFncLit(*e.b);
        return nullptr;
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
                // 赋值右侧为匿名函数字面量：签名已单行输出，再缩进输出函数体
                if (const Expr* fl = trailingFncLit(*s.expr)) {
                    depth++; emitStmts(fl->fncBody); depth--;
                }
                break;
            case Stmt::VarS: emitVarLine("var", s.decls); break;
            case Stmt::LetS: emitVarLine("let", s.decls); break;
            case Stmt::TlsS: emitVarLine("tls", s.decls); break;
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
            case Stmt::RetCallS:
                // ret 调用语法糖：retOp func()[ ?][ \n body]
                ind(); out << s.retOp << " " << exprToStr(*s.expr);
                if (s.retProp) out << " ?";
                out << "\n";
                if (s.retOp != "!!") { depth++; emitStmts(s.body); depth--; }
                break;
            case Stmt::WhileS:
                ind(); out << "while " << exprToStr(*s.expr) << "\n";
                depth++; emitStmts(s.body); depth--;
                break;
            case Stmt::DoWhileS:
                ind(); out << "do\n";
                depth++; emitStmts(s.body); depth--;
                ind(); out << "while " << exprToStr(*s.expr) << "\n";
                break;
            case Stmt::ForS:
                ind();
                if (s.forIn) {
                    out << "for " << s.forVar;
                    if (s.forVarHasType) out << ": " << typeToStr(s.forVarType);
                    for (auto& iv : s.forIdxVars) out << ", " << iv;
                    out << " in ";
                    if (s.forIsRange) {
                        out << "[" << exprToStr(*s.forRangeLo) << ", " << exprToStr(*s.forRangeHi)
                            << (s.forRangeIncl ? "]" : ")");
                    } else {
                        out << exprToStr(*s.forColl);
                    }
                    if (s.forRevert) out << " revert";
                    if (s.forStepE)   out << " step " << exprToStr(*s.forStepE);
                    if (s.forOffsetE) out << " offset " << exprToStr(*s.forOffsetE);
                    if (s.forNumE)    out << " num " << exprToStr(*s.forNumE);
                    out << "\n";
                    depth++; emitStmts(s.body); depth--;
                    break;
                }
                out << "for "
                    << (s.forInit ? exprToStr(*s.forInit) : "") << "; "
                    << (s.forCond ? exprToStr(*s.forCond) : "") << "; "
                    << (s.forStep ? exprToStr(*s.forStep) : "") << "\n";
                depth++; emitStmts(s.body); depth--;
                break;
            case Stmt::CaseS:
                ind(); out << "case " << exprToStr(*s.expr) << ":\n";
                depth++;
                for (auto& arm : s.caseArms) {
                    ind();
                    if (arm.labels.empty()) out << ":\n";
                    else {
                        for (size_t i = 0; i < arm.labels.size(); i++) {
                            if (i) out << ", ";
                            out << exprToStr(*arm.labels[i]);
                        }
                        if (!arm.binding.empty()) out << " as " << arm.binding;
                        out << ":\n";
                    }
                    depth++;
                    emitStmts(arm.body);
                    if (arm.through) { ind(); out << "through\n"; }
                    depth--;
                }
                depth--;
                break;
            case Stmt::GotoS:
                ind(); out << "goto " << s.text << "\n";
                break;
            case Stmt::LabelS:
                ind(); out << s.text << ":\n";
                depth++; emitStmts(s.body); depth--;
                break;
            case Stmt::DeclS:
                emitDecl(*s.decl);
                break;
            case Stmt::FinalS:
                ind(); out << "final\n";
                depth++; emitStmts(s.body); depth--;
                break;
            case Stmt::RunS:
                ind();
                out << "run";
                if (!s.runOpts.empty()) {
                    out << "<";
                    for (size_t i = 0; i < s.runOpts.size(); i++) {
                        if (i) out << ", ";
                        out << s.runOpts[i].first << ":" << s.runOpts[i].second;
                    }
                    out << ">";
                }
                out << " " << exprToStr(*s.expr);
                if (s.forInit) out << ", " << exprToStr(*s.forInit);
                out << "\n";
                break;
            case Stmt::DoneS:
                ind();
                out << "done " << exprToStr(*s.expr);
                if (s.forInit) out << ", " << exprToStr(*s.forInit);
                out << "\n";
                break;
            case Stmt::PrintS:
                ind();
                out << "print";
                if (s.printChn != "0") out << "<" << s.printChn << ">";
                if (s.printCompat) {
                    out << "(";
                    for (size_t i = 0; i < s.printArgs.size(); i++) {
                        if (i) out << ", ";
                        out << exprToStr(*s.printArgs[i]);
                    }
                    out << ")";
                } else {
                    for (size_t i = 0; i < s.printArgs.size(); i++) {
                        out << (i ? ", " : " ");
                        out << exprToStr(*s.printArgs[i]);
                    }
                }
                out << "\n";
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
        const char* X = d.exported ? "@" : "";  // 导出前缀
        switch (d.kind) {
            case Decl::IncD:
                ind(); out << (d.exported ? "@inc " : "inc ") << d.name << "\n";
                break;
            case Decl::AddD:
                ind(); out << "add " << d.name << "\n";
                break;
            case Decl::EnumD:
                ind(); out << X << "def " << d.name << ": " << typeToStr(d.structCommon.type) << "\n";
                depth++;
                for (auto& f : d.structCommon.fields) {
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
                std::string mark = d.linked ? "~ "
                    : (!d.adtItem.empty() ? ("<" + d.adtColl + ", " + d.adtItem + "> ")
                    : (!d.projectSelf.empty() ? ("<" + d.projectSelf + "> ") : ""));
                if (d.tagged) mark = "@";  // 标签联合：def T: @( ... )
                ind(); out << X << "def " << d.name << ": "
                           << mark << open << "\n";
                depth++;
                for (auto& f : d.structCommon.fields) {
                    if (f.synthetic) continue;  // 链表注入成员由 ~ 标记再生
                    // 标签联合无载荷变体：只回写变体名（无 ": 类型"）
                    if (d.tagged && f.type.name.empty() && f.type.ptr == 0
                        && !f.type.hasInline && f.type.arrayDims.empty()) {
                        ind(); out << f.name << "\n";
                    } else {
                        ind(); out << fieldToStr(f, true) << "\n";
                    }
                }
                // 成员函数：签名字段 + 缩进函数体（结构体内实现）
                auto mi = methodImpls.find(d.name);
                if (mi != methodImpls.end()) {
                    for (const Decl* m : mi->second) {
                        if (m->cImpl) {
                            // C 实现接口成员函数：fnc name:: ...（无函数体）
                            ind(); out << "fnc " << m->methodName << "::" << fncItems(*m) << "\n";
                        } else if (m->kind == Decl::FuncD) {
                            // sc 实现成员函数：name: fnc ... + 缩进函数体
                            ind(); out << m->methodName << ": fnc" << fncItems(*m) << "\n";
                            depth++;
                            emitStmts(m->body);
                            depth--;
                        }
                    }
                }
                depth--;
                ind(); out << close << "\n";
                break;
            }
            case Decl::AliasD:
                ind(); out << X << "def " << d.name << " -> " << typeToStr(d.structCommon.type) << "\n";
                break;
            case Decl::FuncTypeD:
                ind();
                if (d.cImpl) {
                    // C 实现接口：fnc name:: ... （methodOwner 非空时在结构体内输出）
                    if (!d.methodOwner.empty()) break;  // 在结构体内输出
                    out << X << "fnc " << d.name << "::" << fncItems(d) << "\n";
                } else if (!d.methodOwner.empty())
                    out << X << "fnc " << d.methodOwner << "::" << d.methodName
                        << fncItems(d) << "\n";
                else
                    out << X << (d.isRpc ? "rpc " : "fnc ") << d.name << fncItems(d) << "\n";
                break;
            case Decl::FuncD:
                if (!d.methodOwner.empty()) break;  // 成员函数已在结构体内输出
                ind();
                if (!d.funcTypeName.empty())
                    out << X << "fnc " << d.name << " -> " << d.funcTypeName << "\n";
                else
                    out << X << (d.isRpc ? "rpc " : "fnc ") << d.name << fncItems(d) << "\n";
                depth++;
                emitStmts(d.body);
                depth--;
                break;
            case Decl::VarD: emitVarLine(d.exported ? "@var" : "var", d.structCommon.fields); break;
            case Decl::LetD: emitVarLine(d.exported ? "@let" : "let", d.structCommon.fields); break;
            case Decl::TlsD: emitVarLine("tls", d.structCommon.fields); break;
        }
    }

    // fnc 单行项：返回类型 + 参数（皆无时连同 ':' 一并省略）
    std::string fncItems(const Decl& d) {
        std::vector<std::string> parts;
        std::string ret = typeToStr(d.structCommon.type);
        if (!ret.empty()) parts.push_back(ret);
        for (auto& f : d.structCommon.fields) parts.push_back(fieldToStr(f, false));
        std::string s;
        for (size_t i = 0; i < parts.size(); i++)
            s += (i ? ", " : " ") + parts[i];
        if (d.structCommon.variadic) s += parts.empty() ? " ..." : ", ...";
        return s.empty() ? s : ":" + s;
    }

    std::string run(const Program& prog) {
        // 预扫：结构内成员函数（FuncD / cImpl FuncTypeD + methodOwner）归入所属类型
        for (auto& d : prog.decls)
            if (!d->external && (
                (d->kind == Decl::FuncD && !d->methodOwner.empty()) ||
                (d->kind == Decl::FuncTypeD && d->cImpl && !d->methodOwner.empty())))
                methodImpls[d->methodOwner].push_back(d.get());
        out << "# 由 scc --emit-sc 从 AST 再生成\n\n";
        for (auto& d : prog.decls) {
            // 编译器合成的 future_id 枚举不输出（源码无此声明，由 future<ID> 聚合而来）
            if (d->genTypeHeader) continue;
            // 外部模块合并进来的声明不输出（由 inc 引入，输出会导致重定义），
            // 但 inc 行本身保留（与 codegen_c 第一遍的跳过规则一致）
            if (d->external && d->kind != Decl::IncD) continue;
            // 结构内成员函数随所属类型输出，顶层不单独成段
            if ((d->kind == Decl::FuncD && !d->methodOwner.empty())
                || (d->kind == Decl::FuncTypeD && d->cImpl && !d->methodOwner.empty())) continue;
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
