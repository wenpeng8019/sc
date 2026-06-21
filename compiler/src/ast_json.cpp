// ============================================================
// AST JSON 导出 —— AST 树 → JSON 字符串（VSCode TreeView 数据源）
// ============================================================
// 递归遍历 Program AST，为每个节点生成格式化的 JSON 对象：
//   {"k":kind, "n":name, "d":detail, "l":line, "c":[children]}
// VSCode 插件 extension.js 解析此 JSON 并用 TreeView 渲染。
// ============================================================
#include "ast_json.h"
#include "ast_print.h"
#include <sstream>

namespace {

// JSON 字符串转义：处理 " \ \n \t 和控制字符（<0x20）
std::string jesc(const std::string& s) {
    std::string r;
    for (char c : s) {
        switch (c) {
            case '"': r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n"; break;
            case '\t': r += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof buf, "\\u%04x", c);
                    r += buf;
                } else r += c;
        }
    }
    return r;
}

// 构造一个节点
std::string node(const std::string& k, const std::string& n, const std::string& d,
                 int line, const std::vector<std::string>& children = {}) {
    std::string s = "{\"k\":\"" + k + "\"";
    if (!n.empty()) s += ",\"n\":\"" + jesc(n) + "\"";
    if (!d.empty()) s += ",\"d\":\"" + jesc(d) + "\"";
    if (line) s += ",\"l\":" + std::to_string(line);
    if (!children.empty()) {
        s += ",\"c\":[";
        for (size_t i = 0; i < children.size(); i++) {
            if (i) s += ",";
            s += children[i];
        }
        s += "]";
    }
    return s + "}";
}

std::string nodeExt(const std::string& k, const std::string& n, const std::string& d,
                    int line, bool external, const std::string& origin, bool used,
                    const std::vector<std::string>& children = {}) {
    std::string s = node(k, n, d, line, children);
    if (!external && origin.empty()) return s;
    if (!s.empty() && s.back() == '}') s.pop_back();
    if (external) {
        s += ",\"x\":1";
        s += used ? ",\"u\":1" : ",\"u\":0";  // 已引用 / 仅导入未用
    }
    if (!origin.empty()) s += ",\"o\":\"" + jesc(origin) + "\"";
    s += "}";
    return s;
}

std::string declNode(const Decl& d);

std::string stmtNode(const Stmt& s) {
    switch (s.kind) {
        case Stmt::ExprS:
            return node("expr", "", exprToStr(*s.expr), s.line);
        case Stmt::VarS:
        case Stmt::LetS:
        case Stmt::TlsS: {
            std::vector<std::string> c;
            for (auto& f : s.decls)
                c.push_back(node("item", f.name, fieldDetail(f, true), f.line));
            return node(s.kind == Stmt::VarS ? "var"
                      : s.kind == Stmt::LetS ? "let" : "tls", "", "", s.line, c);
        }
        case Stmt::ReturnS:
            return node("return", "", s.expr ? exprToStr(*s.expr) : "", s.line);
        case Stmt::BreakS: return node("break", "", "", s.line);
        case Stmt::ContinueS: return node("continue", "", "", s.line);
        case Stmt::IfS: {
            std::vector<std::string> c;
            for (auto& b : s.body) c.push_back(stmtNode(*b));
            if (!s.elseBody.empty()) {
                std::vector<std::string> ec;
                for (auto& b : s.elseBody) ec.push_back(stmtNode(*b));
                c.push_back(node("else", "", "", 0, ec));
            }
            return node("if", "", exprToStr(*s.expr), s.line, c);
        }
        case Stmt::WhileS: {
            std::vector<std::string> c;
            for (auto& b : s.body) c.push_back(stmtNode(*b));
            return node("while", "", exprToStr(*s.expr), s.line, c);
        }
        case Stmt::DoWhileS: {
            std::vector<std::string> c;
            for (auto& b : s.body) c.push_back(stmtNode(*b));
            return node("do-while", "", exprToStr(*s.expr), s.line, c);
        }
        case Stmt::ForS: {
            std::vector<std::string> c;
            for (auto& b : s.body) c.push_back(stmtNode(*b));
            std::string d = (s.forInit ? exprToStr(*s.forInit) : "") + "; " +
                            (s.forCond ? exprToStr(*s.forCond) : "") + "; " +
                            (s.forStep ? exprToStr(*s.forStep) : "");
            return node("for", "", d, s.line, c);
        }
        case Stmt::CaseS: {
            std::vector<std::string> c;
            for (auto& arm : s.caseArms) {
                std::vector<std::string> bc;
                for (auto& b : arm.body) bc.push_back(stmtNode(*b));
                if (arm.through) bc.push_back(node("through", "", "", arm.line));

                std::string d;
                if (arm.labels.empty()) d = ":";
                else {
                    for (size_t i = 0; i < arm.labels.size(); i++) {
                        if (i) d += ", ";
                        d += exprToStr(*arm.labels[i]);
                    }
                    d += ":";
                }
                c.push_back(node("arm", "", d, arm.line, bc));
            }
            return node("case", "", exprToStr(*s.expr), s.line, c);
        }
        case Stmt::GotoS:
            return node("goto", s.text, "", s.line);
        case Stmt::LabelS: {
            std::vector<std::string> c;
            for (auto& b : s.body) c.push_back(stmtNode(*b));
            return node("label", s.text, "", s.line, c);
        }
        case Stmt::DeclS:
            return declNode(*s.decl);
        case Stmt::FinalS: {
            std::vector<std::string> c;
            for (auto& b : s.body) c.push_back(stmtNode(*b));
            return node("final", "", "", s.line, c);
        }
        case Stmt::RunS: {
            std::string opts;
            for (size_t i = 0; i < s.runOpts.size(); i++) {
                if (i) opts += ", ";
                opts += s.runOpts[i].first + ":" + std::to_string(s.runOpts[i].second);
            }
            return node("run", opts.empty() ? "" : "<" + opts + ">",
                        exprToStr(*s.expr) +
                        (s.forInit ? ", " + exprToStr(*s.forInit) : ""), s.line);
        }
        case Stmt::DoneS:
            return node("done", "", exprToStr(*s.expr) +
                        (s.forInit ? ", " + exprToStr(*s.forInit) : ""), s.line);
        case Stmt::PrintS: {
            std::string d;
            for (size_t i = 0; i < s.printArgs.size(); i++) {
                if (i) d += ", ";
                d += exprToStr(*s.printArgs[i]);
            }
            if (s.printCompat) d = "(" + d + ")";
            return node("print", s.printChn != "0" ? "<" + s.printChn + ">" : "", d, s.line);
        }
    }
    return "{}";
}

std::string declNode(const Decl& d) {
    // @导出对象在 detail 前加 "@" 标记
    const std::string X = d.exported ? "@ " : "";
    switch (d.kind) {
        case Decl::IncD:
            if (d.external) {
                // 外部 inc：附加来源声明描述符总数 t（-1=未知/退化无法枚举），
                // 供插件显示"已用 N / 共 M"
                std::string s = nodeExt("inc", d.name, "", d.line, true, d.origin, d.used);
                s.pop_back();  // 去掉结尾 '}'
                s += ",\"t\":" + std::to_string(d.externDeclared) + "}";
                return s;
            }
            return nodeExt("inc", d.name, "", d.line, d.external, d.origin, d.used);
        case Decl::AddD:
            return nodeExt("add", d.name, "", d.line, false, d.origin, false);
        case Decl::EnumD: {
            std::vector<std::string> c;
            for (auto& f : d.structCommon.fields)
                c.push_back(node("item", f.name,
                                 f.init ? "= " + exprToStr(*f.init) : "", f.line));
            return nodeExt("enum", d.name, X + ": " + typeToStr(d.structCommon.type), d.line,
                           d.external, d.origin, d.used, c);
        }
        case Decl::StructD:
        case Decl::UnionD: {
            std::vector<std::string> c;
            for (auto& f : d.structCommon.fields)
                c.push_back(node("field", f.name, fieldDetail(f, true), f.line));
            std::string sd = X;
            if (d.linked) sd += "~";
            else if (!d.adtItem.empty()) sd += "<" + d.adtColl + ", " + d.adtItem + ">";
            else if (!d.projectSelf.empty()) sd += "<" + d.projectSelf + ">";
            return nodeExt(d.kind == Decl::StructD ? "struct" : "union",
                           d.name, sd, d.line, d.external, d.origin, d.used, c);
        }
        case Decl::AliasD:
            return nodeExt("alias", d.name, X + "-> " + typeToStr(d.structCommon.type), d.line,
                           d.external, d.origin, d.used);
        case Decl::FuncTypeD: {
            std::vector<std::string> c;
            for (auto& f : d.structCommon.fields)
                c.push_back(node("param", f.name, fieldDetail(f, false), f.line));
            if (d.structCommon.variadic) c.push_back(node("param", "...", "", d.line));
            std::string ret = typeToStr(d.structCommon.type);
            std::string ftName = d.cImpl ? (d.name + "::")
                : !d.methodOwner.empty() ? (d.methodOwner + "::" + d.methodName)
                : d.name;
            return nodeExt(d.isRpc ? "rpc" : d.cImpl ? "fncimpl" : "fnctype", ftName,
                           ret.empty() ? X : X + ": " + ret,
                           d.line, d.external, d.origin, d.used, c);
        }
        case Decl::FuncD: {
            std::vector<std::string> c;
            for (auto& f : d.structCommon.fields)
                c.push_back(node("param", f.name, fieldDetail(f, false), f.line));
            if (d.structCommon.variadic) c.push_back(node("param", "...", "", d.line));
            for (auto& s : d.body) c.push_back(stmtNode(*s));
            std::string detail;
            if (!d.funcTypeName.empty()) detail = "-> " + d.funcTypeName;
            else {
                std::string ret = typeToStr(d.structCommon.type);
                if (!ret.empty()) detail = ": " + ret;
            }
            const std::string n = d.methodOwner.empty() ? d.name : d.methodOwner + "::" + d.methodName;
            const std::string dtail = d.methodOwner.empty() ? (X + detail)
                                                           : (X + d.methodOwner + "::" + detail);
            return nodeExt(d.isRpc ? "rpc" : "fnc", n, dtail, d.line, d.external, d.origin, d.used, c);
        }
        case Decl::VarD:
        case Decl::LetD:
        case Decl::TlsD: {
            std::vector<std::string> c;
            for (auto& f : d.structCommon.fields)
                c.push_back(node("item", f.name, fieldDetail(f, true), f.line));
            return nodeExt(d.kind == Decl::VarD ? "var"
                         : d.kind == Decl::LetD ? "let" : "tls", "", X, d.line,
                           d.external, d.origin, d.used, c);
        }
    }
    return "{}";
}

} // namespace

std::string emitAstJson(const Program& prog, const std::vector<Diagnostic>& warnings) {
    std::vector<std::string> c;
    for (auto& d : prog.decls) c.push_back(declNode(*d));
    std::string s = node("program", "", "", 0, c);
    if (!prog.externSymbols.empty()) {
        if (!s.empty() && s.back() == '}') s.pop_back();
        s += ",\"e\":[";
        for (size_t i = 0; i < prog.externSymbols.size(); i++) {
            if (i) s += ",";
            s += "\"" + jesc(prog.externSymbols[i]) + "\"";
        }
        s += "]}";
    }
    // 外部描述符分析的非致命警告（导入未使用等）：供插件渲染
    if (!warnings.empty()) {
        if (!s.empty() && s.back() == '}') s.pop_back();
        s += ",\"w\":[";
        for (size_t i = 0; i < warnings.size(); i++) {
            if (i) s += ",";
            s += "{\"m\":\"" + jesc(warnings[i].msg) + "\"";
            if (warnings[i].line) s += ",\"l\":" + std::to_string(warnings[i].line);
            s += "}";
        }
        s += "]}";
    }
    return s + "\n";
}
