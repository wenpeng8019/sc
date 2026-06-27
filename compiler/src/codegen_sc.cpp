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
    // tok/dep 合成的隐藏回调（combine/follow）：C 名 → Decl，供 tok/dep 块回写其体
    std::unordered_map<std::string, const Decl*> tokHiddenFns;

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
            case Stmt::VarS: emitVarLine(s.exported ? "@var" : "var", s.decls); break;
            case Stmt::LetS: emitVarLine(s.exported ? "@let" : "let", s.decls); break;
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
            case Stmt::MixS:
                ind(); out << "mix " << (s.expr ? exprToStr(*s.expr) : "") << "\n";
                break;
            case Stmt::FinalS:
                ind(); out << "final\n";
                depth++; emitStmts(s.body); depth--;
                break;
            case Stmt::RunS:
                ind();
                out << "run";
                if (s.runTarget || !s.runOpts.empty()) {
                    out << "<";
                    bool first = true;
                    if (s.runTarget) { out << exprToStr(*s.runTarget); first = false; }
                    for (size_t i = 0; i < s.runOpts.size(); i++) {
                        if (!first) out << ", ";
                        out << s.runOpts[i].first << ":" << exprToStr(*s.runOpts[i].second);
                        first = false;
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
            case Stmt::FormS:
                ind();
                out << "form " << exprToStr(*s.expr);
                if (s.forInit) out << ", " << exprToStr(*s.forInit);
                out << "\n";
                break;
            case Stmt::BackS:
                ind();
                out << "back " << exprToStr(*s.expr);
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
            case Stmt::AssertS:
                ind();
                out << "assert " << exprToStr(*s.expr);
                if (s.assertMsg) out << ", " << exprToStr(*s.assertMsg);
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
                ind(); out << X << "def " << d.name << ": [\n";
                depth++;
                for (auto& f : d.structCommon.fields) {
                    ind();
                    out << f.name;
                    if (f.init) out << " = " << exprToStr(*f.init);
                    out << "\n";
                }
                depth--;
                ind(); out << "] : " << typeToStr(d.structCommon.type) << "\n";
                break;
            case Decl::StructD:
            case Decl::UnionD: {
                // mod 模块单例：回写 `mod N:` 缩进块（字段 + 成员函数，含各自 @ 导出），
                //   配套 VarD 实例由 VarD 分支据 modInstance 跳过。
                if (d.kind == Decl::StructD && !d.modName.empty()) {
                    ind(); out << (d.exported ? "@mod " : "mod ") << d.modName << ":\n";
                    depth++;
                    for (auto& f : d.structCommon.fields) {
                        if (f.synthetic) continue;
                        ind(); out << fieldToStr(f, true) << "\n";
                    }
                    auto mi = methodImpls.find(d.name);
                    if (mi != methodImpls.end()) {
                        for (const Decl* m : mi->second) {
                            if (m->kind != Decl::FuncD) continue;
                            ind(); out << (m->exported ? "@fnc " : "fnc ")
                                       << m->methodName << fncItems(*m) << "\n";
                            depth++;
                            emitStmts(m->body);
                            depth--;
                        }
                    }
                    depth--;
                    break;
                }
                const char* open = d.kind == Decl::StructD ? "{" : "(";
                const char* close = d.kind == Decl::StructD ? "}" : ")";
                std::string mark = d.linked ? "~ "
                    : (!d.adtItem.empty() ? ("<" + d.adtColl + ", " + d.adtItem + "> ")
                    : (!d.projectSelf.empty() ? ("<" + d.projectSelf + "> ") : ""));
                if (d.tagged) mark = "@";  // 标签联合：def T: @( ... )
                ind(); out << X << (d.isClass ? "cls " : "def ") << d.name
                           << (d.heapOnly ? "&" : "") << ": "
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
            case Decl::VarD:
                if (d.modInstance) break;   // mod 配套实例：已并入 `mod N:` 块回写，跳过
                if (d.isTok) {              // tok 句柄：tok <name>: "<id>"[紧随 combine 体]
                    ind();
                    out << "tok " << (d.structCommon.fields.empty() ? "" : d.structCommon.fields.front().name)
                        << ": \"" << d.tokId << "\"";
                    out << "\n";
                    if (!d.tokFn.empty()) { // 带 combine：回写隐藏 FuncD 体（紧随缩进体，无引导冒号）
                        auto it = tokHiddenFns.find(d.tokFn);
                        if (it != tokHiddenFns.end()) {
                            depth++; emitStmts(it->second->body); depth--;
                        }
                    }
                    break;
                }
                emitVarLine(d.exported ? "@var" : "var", d.structCommon.fields); break;
            case Decl::LetD: emitVarLine(d.exported ? "@let" : "let", d.structCommon.fields); break;
            case Decl::TlsD: emitVarLine("tls", d.structCommon.fields); break;
            case Decl::TestD:
                ind();
                out << (d.testSkip ? "tst.skip \"" : "tst \"") << d.name << "\"\n";
                depth++;
                emitStmts(d.body);
                depth--;
                break;
            case Decl::MacroD:
                if (d.macroObject) {
                    ind(); out << X << "def " << d.name << ": = "
                               << (d.expr ? exprToStr(*d.expr) : "") << "\n";
                } else if (d.cImpl) {
                    // C 宏桥接：def name:: p1, ... \n\t<fnc::/let:: 映射体>
                    ind(); out << "def " << d.name << "::";
                    for (size_t i = 0; i < d.structCommon.fields.size(); i++)
                        out << (i ? ", " : " ") << d.structCommon.fields[i].name;
                    if (d.structCommon.variadic)
                        out << (d.structCommon.fields.empty() ? " ..." : ", ...");
                    out << "\n";
                    depth++;
                    emitStmts(d.body);
                    depth--;
                } else {
                    ind(); out << X << "def " << d.name << ":";
                    if (!d.macroTypeParams.empty()) {
                        out << " <";                                // 泛型宏类型参数 <T,...>
                        for (size_t i = 0; i < d.macroTypeParams.size(); i++)
                            out << (i ? ", " : "") << d.macroTypeParams[i];
                        out << ">";
                        for (auto& f : d.structCommon.fields)       // 其后文本名参数（逗号分隔）
                            out << ", " << f.name;
                        if (d.structCommon.variadic) out << ", ...";
                    } else {
                        for (size_t i = 0; i < d.structCommon.fields.size(); i++)
                            out << (i ? ", " : " ") << d.structCommon.fields[i].name;
                        if (d.structCommon.variadic)
                            out << (d.structCommon.fields.empty() ? " ..." : ", ...");
                    }
                    out << "\n";
                    depth++;
                    emitStmts(d.body);
                    depth--;
                }
                break;
            case Decl::MixD:
                ind(); out << "mix " << (d.expr ? exprToStr(*d.expr) : "") << "\n";
                break;
            case Decl::DepD: {         // dep 依赖关系：dep all/any: 源... [map|loop 目标...] + follow 体
                ind();
                out << "dep " << (d.depAll ? "all:" : "any:");
                for (size_t i = 0; i < d.depItems.size(); i++)
                    out << (i ? ", " : " ") << d.depItems[i].first
                        << ":\"" << d.depItems[i].second << "\"";
                const char* sep = d.depLoop ? " loop " : " map ";  // loop=受控反馈环边；map=DAG 边
                for (size_t i = 0; i < d.depTargets.size(); i++)   // map/loop 目标项（输出/下游）
                    out << (i ? ", " : sep) << d.depTargets[i].first
                        << ":\"" << d.depTargets[i].second << "\"";
                out << "\n";
                auto it = tokHiddenFns.find(d.tokFn);  // 回写隐藏 follow FuncD 体
                if (it != tokHiddenFns.end()) {
                    // 体首注入了 depItems+depTargets 条局部名糖（var a: token& = this->toks[i]），
                    // 回写源码时跳过这些合成语句，仅输出用户原始 follow 体。
                    depth++;
                    size_t skip = d.depItems.size() + d.depTargets.size();
                    const auto& fb = it->second->body;
                    for (size_t i = skip; i < fb.size(); i++) emitStmt(*fb[i]);
                    depth--;
                }
                break;
            }
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
        // 预扫：tok/dep 合成的隐藏 combine/follow 回调，按 C 名建表（tok/dep 块回写其体）
        for (auto& d : prog.decls)
            if (d->kind == Decl::FuncD && d->tokHidden)
                tokHiddenFns[d->name] = d.get();
        out << "# 由 scc --emit-sc 从 AST 再生成\n\n";
        if (prog.isRoot) out << "@@\n\n";              // 根模块标记（头部）
        for (auto& d : prog.decls) {
            // 编译器合成的 future_id 枚举不输出（源码无此声明，由 future<ID> 聚合而来）
            if (d->genTypeHeader) continue;
            // 外部模块合并进来的声明不输出（由 inc 引入，输出会导致重定义），
            // 但 inc 行本身保留（与 codegen_c 第一遍的跳过规则一致）
            if (d->external && d->kind != Decl::IncD) continue;
            // 结构内成员函数随所属类型输出，顶层不单独成段
            if ((d->kind == Decl::FuncD && !d->methodOwner.empty())
                || (d->kind == Decl::FuncTypeD && d->cImpl && !d->methodOwner.empty())) continue;
            // tok/dep 合成的隐藏回调（combine/follow）：随 tok/dep 块回写，顶层不输出
            if (d->kind == Decl::FuncD && d->tokHidden) continue;
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

std::string emitMacroBodySc(const std::vector<StmtPtr>& body) {
    SGen g;
    // 预扫：宏体内嵌套结构体的成员函数归入所属类型，使再生时印回结构体内部
    for (auto& s : body)
        if (s->kind == Stmt::DeclS && s->decl) {
            auto* d = s->decl.get();
            if ((d->kind == Decl::FuncD && !d->methodOwner.empty()) ||
                (d->kind == Decl::FuncTypeD && d->cImpl && !d->methodOwner.empty()))
                g.methodImpls[d->methodOwner].push_back(d);
        }
    g.emitStmts(body);
    return g.out.str();
}
