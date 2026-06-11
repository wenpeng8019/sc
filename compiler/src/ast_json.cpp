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

std::string declNode(const Decl& d);

std::string stmtNode(const Stmt& s) {
    switch (s.kind) {
        case Stmt::ExprS:
            return node("expr", "", exprToStr(*s.expr), s.line);
        case Stmt::VarS:
        case Stmt::LetS: {
            std::vector<std::string> c;
            for (auto& f : s.decls)
                c.push_back(node("item", f.name, fieldDetail(f, true), f.line));
            return node(s.kind == Stmt::VarS ? "var" : "let", "", "", s.line, c);
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
        case Stmt::ForS: {
            std::vector<std::string> c;
            for (auto& b : s.body) c.push_back(stmtNode(*b));
            std::string d = (s.forInit ? exprToStr(*s.forInit) : "") + "; " +
                            (s.forCond ? exprToStr(*s.forCond) : "") + "; " +
                            (s.forStep ? exprToStr(*s.forStep) : "");
            return node("for", "", d, s.line, c);
        }
        case Stmt::DeclS:
            return declNode(*s.decl);
    }
    return "{}";
}

std::string declNode(const Decl& d) {
    // @导出对象在 detail 前加 "@" 标记
    const std::string X = d.exported ? "@ " : "";
    switch (d.kind) {
        case Decl::IncD:
            return node("inc", d.name, "", d.line);
        case Decl::EnumD: {
            std::vector<std::string> c;
            for (auto& f : d.fields)
                c.push_back(node("item", f.name,
                                 f.init ? "= " + exprToStr(*f.init) : "", f.line));
            return node("enum", d.name, X + ": " + typeToStr(d.type), d.line, c);
        }
        case Decl::StructD:
        case Decl::UnionD: {
            std::vector<std::string> c;
            for (auto& f : d.fields)
                c.push_back(node("field", f.name, fieldDetail(f, false), f.line));
            return node(d.kind == Decl::StructD ? "struct" : "union",
                        d.name, X, d.line, c);
        }
        case Decl::AliasD:
            return node("alias", d.name, X + "-> " + typeToStr(d.type), d.line);
        case Decl::FuncTypeD: {
            std::vector<std::string> c;
            for (auto& f : d.fields)
                c.push_back(node("param", f.name, fieldDetail(f, false), f.line));
            std::string ret = typeToStr(d.retType);
            return node("fnctype", d.name, ret.empty() ? X : X + ": " + ret, d.line, c);
        }
        case Decl::FuncD: {
            std::vector<std::string> c;
            for (auto& f : d.fields)
                c.push_back(node("param", f.name, fieldDetail(f, false), f.line));
            for (auto& s : d.body) c.push_back(stmtNode(*s));
            std::string detail;
            if (!d.funcTypeName.empty()) detail = "-> " + d.funcTypeName;
            else {
                std::string ret = typeToStr(d.retType);
                if (!ret.empty()) detail = ": " + ret;
            }
            return node("fnc", d.name, X + detail, d.line, c);
        }
        case Decl::VarD:
        case Decl::LetD: {
            std::vector<std::string> c;
            for (auto& f : d.fields)
                c.push_back(node("item", f.name, fieldDetail(f, true), f.line));
            return node(d.kind == Decl::VarD ? "var" : "let", "", X, d.line, c);
        }
    }
    return "{}";
}

} // namespace

std::string emitAstJson(const Program& prog) {
    std::vector<std::string> c;
    for (auto& d : prog.decls) c.push_back(declNode(*d));
    return node("program", "", "", 0, c) + "\n";
}
