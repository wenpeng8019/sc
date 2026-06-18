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
        case Expr::Sizeof:
            return "sizeof(" + rec(*e.a, true) + ")";
        case Expr::Offsetof:
            return "offsetof(" + e.text + ", " + e.op + ")";
        case Expr::Cast: {
            std::string s = "(" + rec(*e.a, true) + ": " + e.op;
            for (int i = 0; i < e.castPtr; i++) s += "&";
            return s + ")";
        }
        case Expr::InitList: {
            std::string s = "{";
            for (size_t i = 0; i < e.args.size(); i++) {
                if (i) s += ", ";
                s += rec(*e.args[i], true);
            }
            return s + "}";
        }
        case Expr::FncLit: {
            // 匿名函数签名（不含函数体；函数体由 codegen_sc 在语句层多行输出）
            std::string s = "fnc";
            std::vector<std::string> parts;
            if (e.fncSig.type) {
                std::string ret = typeToStr(*e.fncSig.type);
                if (!ret.empty()) parts.push_back(ret);
            }
            for (auto& f : e.fncSig.fields) parts.push_back(f.name + fieldDetail(f, false));
            if (e.fncSig.variadic) parts.push_back("...");
            for (size_t i = 0; i < parts.size(); i++) s += (i ? ", " : ": ") + parts[i];
            if (parts.empty()) s += ":";
            return s;
        }
        case Expr::Await:
            return "await " + rec(*e.a, false);
        case Expr::Async:
            return "async " + rec(*e.a, false);
    }
    return "";
}

} // namespace

std::string exprToStr(const Expr& e) { return rec(e, true); }

// 函数签名字段："fnc: ret, p1: t1, ..."（无返回值且无参数时仅 "fnc"）
static std::string fncSigStr(const TypeRef& t) {
    std::string s = "fnc";
    std::vector<std::string> parts;
    if (t.structCommon.type) {
        std::string r = typeToStr(*t.structCommon.type);
        if (!r.empty()) parts.push_back(r);
    }
    for (auto& p : t.structCommon.fields) parts.push_back(fieldToStr(p, false));
    for (size_t i = 0; i < parts.size(); i++) s += (i ? ", " : ": ") + parts[i];
    if (t.structCommon.variadic) s += parts.empty() ? ": ..." : ", ...";
    return s;
}

std::string typeToStr(const TypeRef& t) {
    if (t.fnKind != TypeRef::FncKind::None) return fncSigStr(t);
    if (t.hasInline) return inlineStr(t);
    std::string s = t.name;
    // 分身/切片句柄 T[...]：方括号内为初值实参（类型侧）
    if (t.project) {
        s += "[";
        if (t.projectArgs)
            for (size_t i = 0; i < t.projectArgs->size(); i++) {
                if (i) s += ", ";
                s += exprToStr(*(*t.projectArgs)[i]);
            }
        s += "]";
        return s;
    }
    for (int i = 0; i < t.ptr; i++) s += "&";
    return s;
}

// StructCommon::type 重载：空指针（函数省略返回类型 = void）返回空串
std::string typeToStr(const std::shared_ptr<TypeRef>& t) {
    return t ? typeToStr(*t) : std::string();
}

std::string inlineStr(const TypeRef& t) {
    std::string s = t.inlineUnion ? "( " : "{ ";
    for (size_t i = 0; i < t.structCommon.fields.size(); i++) {
        if (i) s += ", ";
        s += fieldToStr(t.structCommon.fields[i], false);
    }
    s += t.inlineUnion ? " )" : " }";
    return s;
}

std::string fieldDetail(const Field& f, bool withInit) {
    if (f.type.fnKind == TypeRef::FncKind::PlainPtr) return ": " + fncSigStr(f.type);
    std::string s;
    // 数组维度 [dims] 保留在名字侧（冒号前）
    for (auto& dim : f.type.arrayDims) s += "[" + dim + "]";
    if (f.type.hasInline) {
        s += ": " + inlineStr(f.type);
    } else if (f.type.project) {
        // 分身/切片句柄 s: T[args]
        s += ": " + f.type.name + "[";
        if (f.type.projectArgs)
            for (size_t i = 0; i < f.type.projectArgs->size(); i++) {
                if (i) s += ", ";
                s += exprToStr(*(*f.type.projectArgs)[i]);
            }
        s += "]";
    } else {
        s += ":";
        if (!f.type.name.empty()) s += " " + f.type.name;
        // 指针 & 写在类型侧（冒号后）：i4& / &（裸 void*）
        if (f.type.ptr > 0) {
            s += f.type.name.empty() ? " " : "";
            for (int i = 0; i < f.type.ptr; i++) s += "&";
        }
    }
    if (withInit && f.init) s += " = " + exprToStr(*f.init);
    return s;
}

std::string fieldToStr(const Field& f, bool withInit) {
    if (f.name.empty() && f.type.hasInline) return inlineStr(f.type); // 匿名成员
    return f.name + fieldDetail(f, withInit);
}
