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
            return e.text;
        case Expr::Ident:
            return e.cBridge ? "::" + e.text : e.text;   // C 桥接 ::name 回写
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
            std::string s = rec(*e.a, false);
            if (!e.futureId.empty()) s += "<" + e.futureId + ">";  // future<ID>() 构造糖
            else if (e.ctorAtom) s += "<atom>";                    // T<atom>() 原子计数构造糖
            s += "(";
            for (size_t i = 0; i < e.args.size(); i++) {
                if (i) s += ", ";
                s += rec(*e.args[i], true);
            }
            return s + ")";
        }
        case Expr::Index: {
            std::string s = rec(*e.a, false) + "[" + rec(*e.b, true);
            for (auto& k : e.args) s += ", " + rec(*k, true);  // 容器多键下标
            return s + "]";
        }
        case Expr::Member:
            return rec(*e.a, false) + e.op + e.text;
        case Expr::Sizeof:
            return "sizeof(" + rec(*e.a, true) + ")";
        case Expr::Offsetof:
            return "offsetof(" + e.text + ", " + e.op + ")";
        case Expr::Cast: {
            std::string s = "(" + rec(*e.a, true) + ": ";
            if (e.castConst) s += "const ";
            if (e.castVolatile) s += "volatile ";
            s += e.op;
            for (int i = 0; i < e.castPtr; i++) s += "&";
            if (e.castFat) s += "@";   // 自动指针强转 T@ / 裸 @（op 为空 → 类型擦除）
            if (e.castRestrict) s += " restrict";
            return s + ")";
        }
        case Expr::InitList: {
            // 数组用 [ ]，结构体/联合用 { }（可含指定成员 name=expr）
            const char* open = e.initBracket ? "[" : "{";
            const char* close = e.initBracket ? "]" : "}";
            std::string s = open;
            for (size_t i = 0; i < e.args.size(); i++) {
                if (i) s += ", ";
                if (i < e.initNames.size() && !e.initNames[i].empty())
                    s += e.initNames[i] + " = ";
                s += rec(*e.args[i], true);
            }
            return s + close;
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
    std::string s;
    if (t.qConst) s += "const ";
    if (t.qVolatile) s += "volatile ";
    s += t.name;
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
    if (t.fat) s += "@";   // 自动指针标记（恒单层，与 ptr 互斥）
    if (t.qRestrict) s += " restrict";
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
    if (f.type.fnKind == TypeRef::FncKind::MethodPtr) {
        // 每对象方法指针字段细节：`fnc: ret, params`（接收者隐藏；调试/JSON 用）
        std::string s = ": " + fncSigStr(f.type);
        if (withInit && f.init) s += " = " + exprToStr(*f.init);
        return s;
    }
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
        // C 桥接绑定 name:: type：尾置第二个冒号
        if (f.cBridge) s += ":";
        // 类型侧限定符 const/volatile 写在类型名前
        if (f.type.qConst) s += " const";
        if (f.type.qVolatile) s += " volatile";
        if (!f.type.name.empty()) s += " " + f.type.name;
        // 指针 & 写在类型侧（冒号后）：i4& / &（裸 void*）
        if (f.type.ptr > 0) {
            // 无类型名时（裸 void*）须在 & 前补空格分隔（: & 或 : const &）
            if (f.type.name.empty()) s += " ";
            for (int i = 0; i < f.type.ptr; i++) s += "&";
        }
        // 自动指针 @ 写在类型侧（冒号后）：node@
        if (f.type.fat) s += "@";
        // 尾置 restrict（指针别名约束）
        if (f.type.qRestrict) s += " restrict";
    }
    if (withInit && f.init) s += " = " + exprToStr(*f.init);
    return s;
}

std::string fieldToStr(const Field& f, bool withInit) {
    if (f.name.empty() && f.type.hasInline) return inlineStr(f.type); // 匿名成员
    if (f.type.fnKind == TypeRef::FncKind::MethodPtr) {
        // 每对象方法指针字段回写为 `fnc name: sig`（fnc 前置、无函数体）
        std::string suffix = fncSigStr(f.type).substr(3);  // 去前缀 "fnc"，留 ": ret, params"
        if (suffix.empty()) suffix = ":";
        return "fnc " + f.name + suffix;
    }
    return f.name + fieldDetail(f, withInit);
}
