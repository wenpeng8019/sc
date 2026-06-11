// ============================================================
// AST 文本序列化 —— 表达式/类型/字段 → 规范化字符串
// ============================================================
// 被 ast_json（生成 detail 文本）和 codegen_sc（生成源码）共用。
// 所有函数都是纯函数，递归遍历 AST 子树生成规范文本。
// ============================================================
#include "ast_print.h"

namespace {

// 表达式递归序列化
// top=true: 顶层，不加括号；top=false: 子表达式，加括号防优先级歧义
std::string rec(const Expr& e, bool top) {
    switch (e.kind) {
        case Expr::IntLit: case Expr::FloatLit:
        case Expr::StrLit: case Expr::CharLit:
        case Expr::Ident:
            return e.text;
        case Expr::Unary:
            return e.op + rec(*e.a, false);
        case Expr::PostUnary:
            return rec(*e.a, false) + e.op;
        case Expr::Binary: {
            std::string s = rec(*e.a, false) + " " + e.op + " " + rec(*e.b, false);
            return top ? s : "(" + s + ")";
        }
        case Expr::Ternary: {
            std::string s = rec(*e.a, false) + " ? " + rec(*e.b, false) +
                            " : " + rec(*e.c, false);
            return top ? s : "(" + s + ")";
        }
        case Expr::Call: {
            std::string s = rec(*e.a, false) + "(";
            for (size_t i = 0; i < e.args.size(); i++) {
                if (i) s += ", ";
                s += rec(*e.args[i], true);
            }
            return s + ")";
        }
        case Expr::Index:
            return rec(*e.a, false) + "[" + rec(*e.b, true) + "]";
        case Expr::Member:
            return rec(*e.a, false) + e.op + e.text;
    }
    return "";
}

} // namespace

std::string exprToStr(const Expr& e) { return rec(e, true); }

std::string typeToStr(const TypeRef& t) {
    if (t.hasInline) return inlineStr(t);
    std::string s = t.name;
    for (int i = 0; i < t.ptr; i++) s += "&";
    return s;
}

std::string inlineStr(const TypeRef& t) {
    std::string s = t.inlineUnion ? "( " : "{ ";
    for (size_t i = 0; i < t.inlineFields.size(); i++) {
        if (i) s += ", ";
        s += fieldToStr(t.inlineFields[i], false);
    }
    s += t.inlineUnion ? " )" : " }";
    return s;
}

std::string fieldDetail(const Field& f, bool withInit) {
    std::string s;
    if (!f.type.hasInline)
        for (int i = 0; i < f.type.ptr; i++) s += "&";
    if (f.type.isArray) s += "[" + f.type.arraySize + "]";
    if (f.type.hasInline) {
        s += ": " + inlineStr(f.type);
    } else {
        s += ":";
        if (!f.type.name.empty()) s += " " + f.type.name;
    }
    if (withInit && f.init) s += " = " + exprToStr(*f.init);
    return s;
}

std::string fieldToStr(const Field& f, bool withInit) {
    if (f.name.empty() && f.type.hasInline) return inlineStr(f.type); // 匿名成员
    return f.name + fieldDetail(f, withInit);
}
