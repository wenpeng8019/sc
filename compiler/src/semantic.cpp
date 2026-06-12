#include "semantic.h"
#include "error.h"

#include <functional>
#include <unordered_map>
#include <vector>

namespace {

struct Ty {
    std::string name;
    int ptr = 0;
    int arr = 0;
    bool valid = false;
    bool isNil = false;
};

bool isAssignOp(const std::string& op) {
    return op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" ||
           op == "%=" || op == "&=" || op == "|=" || op == "^=" ||
           op == "<<=" || op == ">>=";
}

bool isPointerLike(const Ty& t) { return t.ptr > 0 || t.arr > 0; }

Ty fromTypeRef(const TypeRef& t) {
    Ty ty;
    ty.name = t.name;
    ty.ptr = t.ptr;
    ty.arr = (int)t.arrayDims.size();
    ty.valid = true;
    return ty;
}

struct Checker {
    const Program& prog;
    std::unordered_map<std::string, const Decl*> funcTypes;
    std::unordered_map<std::string, const Decl*> structs;
    std::unordered_map<std::string, std::string> aliases;
    std::unordered_map<std::string, Ty> globals;

    explicit Checker(const Program& p) : prog(p) {}

    [[noreturn]] void err(int line, const std::string& msg) const {
        throw CompileError{msg, line};
    }

    const Decl* resolveStruct(std::string name) const {
        for (int i = 0; i < 8 && !name.empty(); i++) {
            auto it = structs.find(name);
            if (it != structs.end()) return it->second;
            auto al = aliases.find(name);
            if (al == aliases.end()) return nullptr;
            name = al->second;
        }
        return nullptr;
    }

    std::string resolveAliasToName(std::string name) const {
        for (int i = 0; i < 16 && !name.empty(); i++) {
            auto al = aliases.find(name);
            if (al == aliases.end()) return name;
            name = al->second;
        }
        return name;
    }

    void collectByValueDepsFromType(const TypeRef& t,
                                    std::vector<std::pair<std::string, int>>& out,
                                    int line) const {
        // 函数字段不参与按值包含图
        if (t.fnKind != TypeRef::FncKind::None) return;

        // 指针或数组字段不会形成“按值递归包含”
        if (t.ptr == 0 && t.arrayDims.empty() && !t.name.empty()) {
            const std::string base = resolveAliasToName(t.name);
            if (structs.find(base) != structs.end()) out.push_back({base, line});
        }

        // 内联结构/联合：递归检查其字段
        if (t.hasInline) {
            for (auto& f : t.inlineFields)
                collectByValueDepsFromType(f.type, out, f.line ? f.line : line);
        }
    }

    void checkAggregateByValueCycles() const {
        std::unordered_map<std::string, std::vector<std::pair<std::string, int>>> g;
        for (auto& kv : structs) g[kv.first] = {};

        // 建图：A 的字段按值包含 B，则 A -> B
        for (auto& kv : structs) {
            const Decl* d = kv.second;
            auto& edges = g[d->name];
            for (auto& f : d->fields) {
                collectByValueDepsFromType(f.type, edges, f.line ? f.line : d->line);
            }
        }

        std::unordered_map<std::string, int> state;  // 0=未访问,1=栈上,2=完成
        std::vector<std::string> stack;

        auto findEdgeLine = [&](const std::string& from, const std::string& to) -> int {
            auto it = g.find(from);
            if (it == g.end()) return 0;
            for (auto& e : it->second) if (e.first == to) return e.second;
            return 0;
        };

        std::function<void(const std::string&)> dfs = [&](const std::string& u) {
            state[u] = 1;
            stack.push_back(u);

            auto it = g.find(u);
            if (it != g.end()) {
                for (auto& e : it->second) {
                    const std::string& v = e.first;
                    if (state[v] == 0) {
                        dfs(v);
                    } else if (state[v] == 1) {
                        // 回边：发现按值包含环
                        std::string cycle;
                        bool inCycle = false;
                        for (auto& n : stack) {
                            if (n == v) inCycle = true;
                            if (!inCycle) continue;
                            if (!cycle.empty()) cycle += " -> ";
                            cycle += n;
                        }
                        cycle += " -> " + v;

                        int line = e.second;
                        if (!line && stack.size() >= 2) {
                            line = findEdgeLine(stack[stack.size() - 2], stack.back());
                        }
                        if (!line) line = 1;

                        err(line, "结构/联合按值循环包含: " + cycle + "。请改为指针字段（&）打破递归包含");
                    }
                }
            }

            stack.pop_back();
            state[u] = 2;
        };

        for (auto& kv : g)
            if (state[kv.first] == 0) dfs(kv.first);
    }

    Ty inferExpr(const Expr& e,
                 const std::unordered_map<std::string, Ty>& locals,
                 int line) {
        switch (e.kind) {
            case Expr::IntLit: return Ty{"i4", 0, 0, true, false};
            case Expr::FloatLit: return Ty{"f8", 0, 0, true, false};
            case Expr::StrLit: return Ty{"char", 1, 0, true, false};
            case Expr::CharLit: return Ty{"i1", 0, 0, true, false};
            case Expr::Ident: {
                if (e.text == "nil") return Ty{"", 0, 0, true, true};
                if (e.text == "true" || e.text == "false") return Ty{"b", 0, 0, true, false};
                auto it = locals.find(e.text);
                if (it != locals.end()) return it->second;
                it = globals.find(e.text);
                if (it != globals.end()) return it->second;
                return Ty{};
            }
            case Expr::Unary: {
                Ty a = inferExpr(*e.a, locals, line);
                if (e.op == "*") {
                    if (a.valid && !isPointerLike(a)) err(e.line, "非法解引用：操作数不是指针/数组");
                    if (a.valid) {
                        if (a.arr > 0) a.arr--;
                        else if (a.ptr > 0) a.ptr--;
                    }
                    return a;
                }
                if (e.op == "&") {
                    if (a.valid) a.ptr++;
                    return a;
                }
                return a;
            }
            case Expr::Index: {
                Ty a = inferExpr(*e.a, locals, line);
                if (a.valid && !isPointerLike(a)) err(e.line, "非法下标：操作数不是指针/数组");
                if (a.valid) {
                    if (a.arr > 0) a.arr--;
                    else if (a.ptr > 0) a.ptr--;
                }
                (void)inferExpr(*e.b, locals, line);
                return a;
            }
            case Expr::Member: {
                Ty base = inferExpr(*e.a, locals, line);
                if (!base.valid) return Ty{};
                const Decl* sd = resolveStruct(base.name);
                if (!sd) return Ty{};
                for (auto& f : sd->fields) {
                    if (f.name == e.text) return fromTypeRef(f.type);
                }
                return Ty{};
            }
            case Expr::Sizeof:
                (void)inferExpr(*e.a, locals, line);
                return Ty{"u8", 0, 0, true, false};
            case Expr::Offsetof:
                return Ty{"u8", 0, 0, true, false};
            case Expr::Call: {
                Ty callee = inferExpr(*e.a, locals, line);
                for (auto& a : e.args) (void)inferExpr(*a, locals, line);
                if (callee.valid && callee.name == "v" && callee.ptr == 0 && callee.arr == 0)
                    err(e.line, "void 值不能作为表达式使用");
                return Ty{"i4", 0, 0, true, false};
            }
            case Expr::PostUnary:
                return inferExpr(*e.a, locals, line);
            case Expr::Ternary: {
                (void)inferExpr(*e.a, locals, line);
                Ty b = inferExpr(*e.b, locals, line);
                Ty c = inferExpr(*e.c, locals, line);
                if (b.valid) return b;
                return c;
            }
            case Expr::Binary: {
                Ty l = inferExpr(*e.a, locals, line);
                Ty r = inferExpr(*e.b, locals, line);
                if (isAssignOp(e.op)) {
                    checkAssignable(l, r, e.line);
                    return l;
                }
                return l.valid ? l : r;
            }
            case Expr::Cast:
                (void)inferExpr(*e.a, locals, line);
                return Ty{e.text, e.castPtr, 0, true, false};
            case Expr::InitList:
                for (auto& a : e.args) (void)inferExpr(*a, locals, line);
                return Ty{};
        }
        return Ty{};
    }

    void checkAssignable(const Ty& lhs, const Ty& rhs, int line) {
        if (!lhs.valid || !rhs.valid) return;
        const bool lp = isPointerLike(lhs);
        const bool rp = isPointerLike(rhs);

        if (rhs.isNil) {
            if (!lp) err(line, "nil 只能赋给指针/数组类型");
            return;
        }
        if (lp && !rp) err(line, "指针/数组不能赋值为非指针标量");
        if (!lp && rp) err(line, "标量不能赋值为指针/数组");
    }

    bool isAddrOfLocalExpr(const Expr& e,
                           const std::unordered_map<std::string, Ty>& locals) const {
        if (e.kind != Expr::Unary || e.op != "&" || !e.a) return false;
        if (e.a->kind == Expr::Ident) {
            auto it = locals.find(e.a->text);
            if (it != locals.end()) return true;
        }
        if (e.a->kind == Expr::Member && e.a->a && e.a->a->kind == Expr::Ident) {
            auto it = locals.find(e.a->a->text);
            if (it != locals.end()) return true;
        }
        return false;
    }

    bool containsAddrOfLocal(const Expr& e,
                             const std::unordered_map<std::string, Ty>& locals) const {
        if (isAddrOfLocalExpr(e, locals)) return true;
        if (e.a && containsAddrOfLocal(*e.a, locals)) return true;
        if (e.b && containsAddrOfLocal(*e.b, locals)) return true;
        if (e.c && containsAddrOfLocal(*e.c, locals)) return true;
        for (auto& x : e.args) if (x && containsAddrOfLocal(*x, locals)) return true;
        return false;
    }

    bool rootedAtGlobal(const Expr& e,
                        const std::unordered_map<std::string, Ty>& locals) const {
        if (e.kind == Expr::Ident) {
            if (locals.find(e.text) != locals.end()) return false;
            return globals.find(e.text) != globals.end();
        }
        if ((e.kind == Expr::Member || e.kind == Expr::Index) && e.a)
            return rootedAtGlobal(*e.a, locals);
        if (e.kind == Expr::Unary && e.a && (e.op == "*" || e.op == "&"))
            return rootedAtGlobal(*e.a, locals);
        return false;
    }

    Ty declaredOrInferredType(const Field& f,
                              const std::unordered_map<std::string, Ty>& locals) {
        const bool declared = f.type.hasInline || !f.type.name.empty() ||
                              f.type.ptr > 0 || !f.type.arrayDims.empty() ||
                              f.type.fnKind != TypeRef::FncKind::None;
        if (declared) return fromTypeRef(f.type);
        if (!f.init) return Ty{"char", 1, 0, true, false};  // 兼容既有默认语义

        Ty t = inferExpr(*f.init, locals, f.line);
        if (t.isNil) err(f.line, "nil 不能用于无类型推断，请显式声明指针类型");
        return t;
    }

    void checkVarDecls(const std::vector<Field>& ds,
                       std::unordered_map<std::string, Ty>& locals) {
        for (auto& f : ds) {
            Ty lhs = declaredOrInferredType(f, locals);
            if (f.init) {
                Ty rhs = inferExpr(*f.init, locals, f.line);
                checkAssignable(lhs, rhs, f.line);
            }
            locals[f.name] = lhs;
        }
    }

    Ty funcRetType(const Decl& d) const {
        if (!d.funcTypeName.empty()) {
            auto it = funcTypes.find(d.funcTypeName);
            if (it != funcTypes.end()) {
                return fromTypeRef(it->second->retType);
            }
            return Ty{"i4", 0, 0, true, false};
        }
        Ty t = fromTypeRef(d.retType);
        if (!t.valid || (t.name.empty() && t.ptr == 0 && t.arr == 0)) return Ty{"i4", 0, 0, true, false};
        return t;
    }

    void checkStmt(const Stmt& s,
                   std::unordered_map<std::string, Ty>& locals,
                   const Ty& retTy) {
        switch (s.kind) {
            case Stmt::ExprS:
                if (s.expr && s.expr->kind == Expr::Binary && isAssignOp(s.expr->op)) {
                    if (containsAddrOfLocal(*s.expr->b, locals) &&
                        rootedAtGlobal(*s.expr->a, locals)) {
                        err(s.line, "禁止将局部变量地址写入全局存储");
                    }
                }
                (void)inferExpr(*s.expr, locals, s.line);
                break;
            case Stmt::VarS:
            case Stmt::LetS:
                checkVarDecls(s.decls, locals);
                break;
            case Stmt::ReturnS:
                if (s.expr) {
                    if (containsAddrOfLocal(*s.expr, locals))
                        err(s.line, "禁止返回局部变量地址");
                    Ty rt = inferExpr(*s.expr, locals, s.line);
                    checkAssignable(retTy, rt, s.line);
                }
                break;
            case Stmt::IfS: {
                (void)inferExpr(*s.expr, locals, s.line);
                auto a = locals, b = locals;
                for (auto& x : s.body) checkStmt(*x, a, retTy);
                for (auto& x : s.elseBody) checkStmt(*x, b, retTy);
                break;
            }
            case Stmt::WhileS: {
                (void)inferExpr(*s.expr, locals, s.line);
                auto a = locals;
                for (auto& x : s.body) checkStmt(*x, a, retTy);
                break;
            }
            case Stmt::DoWhileS: {
                auto a = locals;
                for (auto& x : s.body) checkStmt(*x, a, retTy);
                (void)inferExpr(*s.expr, a, s.line);
                break;
            }
            case Stmt::ForS: {
                if (s.forInit) (void)inferExpr(*s.forInit, locals, s.line);
                if (s.forCond) (void)inferExpr(*s.forCond, locals, s.line);
                if (s.forStep) (void)inferExpr(*s.forStep, locals, s.line);
                auto a = locals;
                for (auto& x : s.body) checkStmt(*x, a, retTy);
                break;
            }
            case Stmt::CaseS: {
                (void)inferExpr(*s.expr, locals, s.line);
                for (auto& arm : s.caseArms) {
                    auto a = locals;
                    for (auto& lab : arm.labels) (void)inferExpr(*lab, a, s.line);
                    for (auto& x : arm.body) checkStmt(*x, a, retTy);
                }
                break;
            }
            case Stmt::GotoS:
                break;
            case Stmt::LabelS: {
                auto a = locals;
                for (auto& x : s.body) checkStmt(*x, a, retTy);
                break;
            }
            case Stmt::BreakS:
            case Stmt::ContinueS:
                break;
            case Stmt::DeclS:
                break;
        }
    }

    void collectTop() {
        for (auto& d : prog.decls) {
            if (d->kind == Decl::FuncTypeD && !d->isRpc) funcTypes[d->name] = d.get();
            if (d->kind == Decl::StructD || d->kind == Decl::UnionD) structs[d->name] = d.get();
            if (d->kind == Decl::AliasD) aliases[d->name] = d->type.name;
        }

        for (auto& d : prog.decls) {
            if (d->kind != Decl::VarD && d->kind != Decl::LetD) continue;
            for (auto& f : d->fields) {
                Ty lhs = fromTypeRef(f.type);
                if (!lhs.valid || (lhs.name.empty() && lhs.ptr == 0 && lhs.arr == 0)) {
                    lhs = f.init ? inferExpr(*f.init, globals, f.line) : Ty{"char", 1, 0, true, false};
                }
                if (f.init) {
                    Ty rhs = inferExpr(*f.init, globals, f.line);
                    checkAssignable(lhs, rhs, f.line);
                }
                globals[f.name] = lhs;
            }
        }
    }

    void checkFunctions() {
        for (auto& d : prog.decls) {
            if (d->kind != Decl::FuncD) continue;
            std::unordered_map<std::string, Ty> locals;

            const Decl* sig = d.get();
            if (!d->funcTypeName.empty()) {
                auto it = funcTypes.find(d->funcTypeName);
                if (it != funcTypes.end()) sig = it->second;
            }
            for (auto& p : sig->fields) locals[p.name] = fromTypeRef(p.type);
            if (!d->methodOwner.empty()) locals["this"] = Ty{d->methodOwner, 1, 0, true, false};

            Ty ret = funcRetType(*d);
            for (auto& s : d->body) checkStmt(*s, locals, ret);
        }
    }

    void run() {
        collectTop();
        checkAggregateByValueCycles();
        checkFunctions();
    }
};

} // namespace

void semanticCheck(const Program& prog) {
    Checker c(prog);
    c.run();
}
