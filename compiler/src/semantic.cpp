// ============================================================
// 语义分析器 —— 基于 AST 的类型兼容和安全性检查
// ============================================================
// 在 parse 完成、codegen 之前执行。不修改 AST，仅做验证和报错。
//
// 检查项目：
//   - 赋值/初始化/返回的类型兼容（标量⇄指针不可混用）
//   - nil 只能赋给指针/数组类型
//   - 解引用 *x / 下标 x[] 的操作数必须是指针或数组
//   - 结构/联合按值递归包含检测（会导致无限大类型）
//   - 禁止返回局部变量地址、禁止将局部地址写入全局存储
//   - void 值不能作为表达式使用
//   - 无返回值函数（省略返回类型）不能 return 表达式
//
// 注意：语义检查不与 import 符号做硬绑定 —— 无法推导类型时返回
// invalid Ty，跳过该检查，避免因缺少模块依赖信息而误报错误。
// ============================================================
#include "semantic.h"
#include "error.h"

#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

// ---------------- 内部类型表示 ----------------
// 轻量级类型描述，比完整的 TypeRef 更紧凑、便于比较。
// valid=false 表示类型未知（如调用未登记的模块函数），跳过后续检查。
struct Ty {
    std::string name;   // 基类型名（"i4", "char", ""=void* 规则）
    int ptr = 0;        // 指针层数（& 个数）
    int arr = 0;        // 数组维度数（[] 个数）
    bool valid = false; // true=能确定类型，false=跳过检查
    bool isNil = false; // 字面量 nil，只能赋给指针/数组
    bool project = false; // 分身/切片句柄 T[...]：可整体被赋值为本体或 nil（语法糖）
    bool fat = false;   // 自动指针 T@（胖指针）：指针类，可与 T()/T@/nil 互赋
};

// ---------------- 运算符辅助函数 ----------------

// 赋值类运算符（包括复合赋值 += -= 等）
bool isAssignOp(const std::string& op) {
    return op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" ||
           op == "%=" || op == "&=" || op == "|=" || op == "^=" ||
           op == "<<=" || op == ">>=";
}

// 指针或数组：均可解引用（胖指针 T@ 亦视为指针类）
bool isPointerLike(const Ty& t) { return t.ptr > 0 || t.arr > 0 || t.fat; }

// TypeRef → Ty 转换
Ty fromTypeRef(const TypeRef& t) {
    Ty ty;
    ty.name = t.name;
    ty.ptr = t.ptr;
    ty.arr = (int)t.arrayDims.size();
    ty.valid = true;
    ty.project = t.project;
    ty.fat = t.fat;
    // 函数指针字段（普通函数指针 / 每对象方法指针）作为值即一个指针：
    // 视为指针类，使其能与通用指针 & 互相赋值（如把 alloc 透传的 & 存入 MethodPtr 字段）。
    if (t.fnKind != TypeRef::FncKind::None && ty.ptr == 0) ty.ptr = 1;
    return ty;
}

// ---------------- 语义检查器 ----------------
// 单次语义分析的状态。先 collectTop() 登记所有顶层符号，
// 再做结构体按值环检测，最后遍历所有函数体 checkFunctions()。
struct Checker {
    const Program& prog;

    // 顶层符号表
    std::unordered_map<std::string, const Decl*> funcTypes; // 函数类型声明（用于查找 -> 引用）
    std::unordered_map<std::string, const Decl*> structs;   // 结构/联合定义
    std::unordered_map<std::string, std::string> aliases;   // 类型别名（name → 目标类型）
    std::unordered_map<std::string, Ty> globals;            // 全局 var/let/tls 的类型
    // 容器类型 C（def T: <C, I>）→ 元素节点类型 I：下标糖 t[key] → find 结果为 I&
    std::unordered_map<std::string, std::string> containerItem;

    explicit Checker(const Program& p) : prog(p) {}

    [[noreturn]] void err(int line, const std::string& msg) const {
        throw CompileError{msg, line};
    }

    // ---- 符号解析 ----

    // 解析结构体名：沿别名链最多追踪 8 步，防止循环别名死循环
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

    // 将别名展开为最终类型名（最多 16 步），用于按值包含图的结点去重
    std::string resolveAliasToName(std::string name) const {
        for (int i = 0; i < 16 && !name.empty(); i++) {
            auto al = aliases.find(name);
            if (al == aliases.end()) return name;
            name = al->second;
        }
        return name;
    }

    // ---- 按值递归包含检测 ----
    // 结构体 A 的字段按值包含结构体 B（非指针/非数组），则建立边 A→B。
    // 图中的回边意味着存在按值递归环，会导致 C 中类型无限大，编译报错。

    // 从一个 TypeRef 递归收集其按值包含的结构体
    void collectByValueDepsFromType(const TypeRef& t,
                                    std::vector<std::pair<std::string, int>>& out,
                                    int line) const {
        // 函数字段不参与按值包含图（函数指针大小固定）
        if (t.fnKind != TypeRef::FncKind::None) return;

        // 指针或数组字段不会形成"按值递归包含"（它们只是引用，大小固定）。
        // 胖指针 T@（fat）同理：C 侧为 sc_fat（24 字节固定），不按值嵌入目标类型。
        if (t.ptr == 0 && !t.fat && t.arrayDims.empty() && !t.name.empty()) {
            const std::string base = resolveAliasToName(t.name);
            if (structs.find(base) != structs.end()) out.push_back({base, line});
        }

        // 内联结构/联合：递归检查其字段
        if (t.hasInline) {
            for (auto& f : t.structCommon.fields)
                collectByValueDepsFromType(f.type, out, f.line ? f.line : line);
        }
    }

    // 构建包含图并用 DFS 检测环
    void checkAggregateByValueCycles() const {
        // 建图：每个结构体 A → 字段按值包含的结构体列表
        std::unordered_map<std::string, std::vector<std::pair<std::string, int>>> g;
        for (auto& kv : structs) g[kv.first] = {};

        for (auto& kv : structs) {
            const Decl* d = kv.second;
            auto& edges = g[d->name];
            for (auto& f : d->structCommon.fields) {
                collectByValueDepsFromType(f.type, edges, f.line ? f.line : d->line);
            }
        }

        // 三色 DFS 检测回边
        std::unordered_map<std::string, int> state;  // 0=未访问, 1=栈上, 2=完成
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

    // ---- 表达式类型推导 ----
    // 遍历表达式树，推导出表达式的类型 Ty。
    // 推导失败返回 valid=false（多发生在调用未登记函数/模块时），
    // 调用方应跳过后续类型检查，避免误报。
    Ty inferExpr(const Expr& e,
                 const std::unordered_map<std::string, Ty>& locals,
                 int line) {
        switch (e.kind) {
            // -- 字面量：类型固定 ------------------------------------------------
            case Expr::IntLit:  return Ty{"i4", 0, 0, true, false};
            case Expr::FloatLit: return Ty{"f8", 0, 0, true, false};
            case Expr::StrLit:  return Ty{"char", 1, 0, true, false};  // 字符串字面量 = char*
            case Expr::CharLit: return Ty{"i1", 0, 0, true, false};
            case Expr::FncLit: return Ty{"fnc", 0, 0, true, false};  // 匿名函数字面量类型从 lhs 推断
            // -- await E：挂起等 future 就绪；结果类型擦除（void*），跳过赋值检查 --
            case Expr::Await: {
                if (e.a) (void)inferExpr(*e.a, locals, line);
                return Ty{};  // 类型擦除：由 codegen 按 LHS 声明类型强转还原
            }
            // -- async E：登记 rpc 调用进事件循环，返回 future& --
            case Expr::Async: {
                if (e.a) (void)inferExpr(*e.a, locals, line);
                return Ty{"future", 1, 0, true, false};
            }
            // -- 标识符：查找 locals → globals，nil/true/false 特殊处理 ----------
            case Expr::Ident: {
                if (e.text == "nil") return Ty{"", 0, 0, true, true};
                if (e.text == "true" || e.text == "false") return Ty{"bool", 0, 0, true, false};
                if (e.text == "ok" && locals.find("ok") == locals.end()
                    && globals.find("ok") == globals.end())
                    return Ty{"ret", 0, 0, true, false};  // ADT 接口成功返回码（= 0）
                auto it = locals.find(e.text);
                if (it != locals.end()) return it->second;
                it = globals.find(e.text);
                if (it != globals.end()) return it->second;
                return Ty{};  // 未找到 = C 宏/未登记符号，跳过检查
            }
            // -- 一元运算：* 解引用 → ptr/arr-1; & 取地址 → ptr+1 ----------------
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
                return a;  // ! ~ - + 等保持类型不变
            }
            // -- 下标：相当于 *，操作数必须是指针/数组 --------------------------
            case Expr::Index: {
                Ty a = inferExpr(*e.a, locals, line);
                // 容器下标糖：t[key,...] → find(...)，结果为元素节点类型 I&（未命中为 nil）
                if (a.valid && a.arr == 0 && a.ptr <= 1) {
                    auto ci = containerItem.find(resolveAliasToName(a.name));
                    if (ci != containerItem.end()) {
                        (void)inferExpr(*e.b, locals, line);
                        for (auto& k : e.args) (void)inferExpr(*k, locals, line);
                        return Ty{ci->second, 1, 0, true, false};
                    }
                }
                if (a.valid && !isPointerLike(a)) err(e.line, "非法下标：操作数不是指针/数组");
                if (a.valid) {
                    if (a.arr > 0) a.arr--;
                    else if (a.ptr > 0) a.ptr--;
                }
                (void)inferExpr(*e.b, locals, line);  // 下标表达式本身不参与类型推导
                return a;
            }
            // -- 成员访问：在结构体中查找字段名返回其类型 -----------------------
            case Expr::Member: {
                Ty base = inferExpr(*e.a, locals, line);
                if (!base.valid) return Ty{};
                const Decl* sd = resolveStruct(base.name);
                if (!sd) return Ty{};
                // prev/next 在链表结构体上映射为内部维护的 _prev/_next 指针字段
                std::string fn = e.text;
                if (sd->linked && (fn == "prev" || fn == "next")) fn = "_" + fn;
                for (auto& f : sd->structCommon.fields) {
                    if (f.name == fn) return fromTypeRef(f.type);
                }
                return Ty{};
            }
            // -- sizeof 返回 u8（size_t）---------------------------------------
            case Expr::Sizeof:
                (void)inferExpr(*e.a, locals, line);
                return Ty{"u8", 0, 0, true, false};
            // -- offsetof 返回 u8（size_t）-------------------------------------
            case Expr::Offsetof:
                return Ty{"u8", 0, 0, true, false};
            // -- 函数调用：多种内建伪调用优先，然后是普通函数调用 ----------------
            case Expr::Call: {
                // base(x) 伪函数：等价于 *(T*)&x，结果类型为 T*+1 层指针
                if (e.a && e.a->kind == Expr::Ident && e.a->text == "base"
                    && !locals.count("base") && !globals.count("base")) {
                    if (e.args.size() != 1) err(e.line, "base 需要 1 个实参");
                    if (e.args[0]->kind == Expr::Cast) {
                        Ty t = inferExpr(*e.args[0]->a, locals, line);
                        if (!t.valid) return Ty{};
                        return Ty{e.args[0]->op, e.args[0]->castPtr + 1, 0, true, false};
                    }
                    (void)inferExpr(*e.args[0], locals, line);
                    return Ty{"void", 1, 0, true, false};
                }
                // prev(x)/next(x) 伪函数：等价于链表 _prev/_next 的 base
                if (e.a && e.a->kind == Expr::Ident && (e.a->text == "prev" || e.a->text == "next")
                    && !locals.count(e.a->text) && !globals.count(e.a->text)) {
                    if (e.args.size() != 1) err(e.line, "prev/next 需要 1 个实参");
                    if (e.args[0]->kind == Expr::Cast) {
                        Ty t = inferExpr(*e.args[0]->a, locals, line);
                        if (!t.valid) return Ty{};
                        return Ty{e.args[0]->op, e.args[0]->castPtr + 1, 0, true, false};
                    }
                    (void)inferExpr(*e.args[0], locals, line);
                    return Ty{"void", 1, 0, true, false};
                }
                // T() 无参伪调用：堆分配构造糖，结果类型为 T&
                if (e.a->kind == Expr::Ident && e.args.empty()
                    && locals.find(e.a->text) == locals.end()
                    && globals.find(e.a->text) == globals.end()
                    && resolveStruct(e.a->text))
                    return Ty{resolveAliasToName(e.a->text), 1, 0, true, false};
                // stringify 格式化关键字：
                //   stringify(x) → string（单参数格式化）
                //   stringify(x, buf, n) → char&（三参数，结果写入 buf）
                if (e.a->kind == Expr::Ident && e.a->text == "stringify" && !e.args.empty()
                    && locals.find(e.a->text) == locals.end()
                    && globals.find(e.a->text) == globals.end()) {
                    for (auto& a : e.args) (void)inferExpr(*a, locals, line);
                    if (e.args.size() == 3) return Ty{"char", 1, 0, true, false};
                    return Ty{"string", 0, 0, true, false};
                }
                // 普通函数调用
                Ty callee = inferExpr(*e.a, locals, line);
                for (auto& a : e.args) (void)inferExpr(*a, locals, line);
                if (callee.valid && callee.name == "v" && callee.ptr == 0 && callee.arr == 0)
                    err(e.line, "void 值不能作为表达式使用");
                // 调用结果类型未登记（语义层不跟踪函数返回类型；
                // stdin 单文件解析时 T() 的 T 也可能来自未合并依赖），
                // 返回 invalid 跳过后续检查，避免误报
                return Ty{};
            }
            // -- 后缀 ++/--：类型不变 -------------------------------------------
            case Expr::PostUnary:
                return inferExpr(*e.a, locals, line);
            // -- 三元条件 a ? b : c：取第一个有类型的边 -------------------------
            case Expr::Ternary: {
                (void)inferExpr(*e.a, locals, line);
                Ty b = inferExpr(*e.b, locals, line);
                Ty c = inferExpr(*e.c, locals, line);
                if (b.valid) return b;
                return c;
            }
            // -- 二元运算：赋值类做兼容检查并返回左值类型，其他返回左侧类型 -----
            case Expr::Binary: {
                Ty l = inferExpr(*e.a, locals, line);
                Ty r = inferExpr(*e.b, locals, line);
                if (isAssignOp(e.op)) {
                    // 分身/切片句柄：s = 本体 / s = nil 均为语法糖，跳过常规赋值兼容检查
                    if (!l.project) checkAssignable(l, r, e.line);
                    return l;
                }
                return l.valid ? l : r;
            }
            // -- 类型强转 (T)x：返回声明类型 -----------------------------------
            case Expr::Cast:
                (void)inferExpr(*e.a, locals, line);
                return Ty{e.op, e.castPtr, 0, true, false};
            // -- 初始化列表 {a, b, c}：类型由赋值目标决定，此处返回 invalid -----
            case Expr::InitList:
                for (auto& a : e.args) (void)inferExpr(*a, locals, line);
                return Ty{};
        }
        return Ty{};
    }

    // ---- 赋值兼容检查 ----
    // 核心规则：
    //   - nil 只能赋给指针/数组（nil 是空指针，不能赋给标量）
    //   - 指针/数组不能赋值为非指针标量（不允许隐式地址→整数转换）
    //   - 标量不能赋值为指针/数组（不允许隐式整数→地址转换）
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

    // ---- 逃逸分析辅助函数 ----

    // 判断表达式是否是对局部变量取地址 &localVar 或 &localVar.field
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

    // 递归检查表达式树中是否包含对局部变量取地址的操作
    bool containsAddrOfLocal(const Expr& e,
                             const std::unordered_map<std::string, Ty>& locals) const {
        if (isAddrOfLocalExpr(e, locals)) return true;
        if (e.a && containsAddrOfLocal(*e.a, locals)) return true;
        if (e.b && containsAddrOfLocal(*e.b, locals)) return true;
        if (e.c && containsAddrOfLocal(*e.c, locals)) return true;
        for (auto& x : e.args) if (x && containsAddrOfLocal(*x, locals)) return true;
        return false;
    }

    // 判断表达式根是否为全局变量（*p, a[i], p->f 递归查看根，&x 也穿透）
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

    // ---- 变量声明 ----

    // 确定变量声明的类型：优先用显式声明的类型，否则从初值的字面量推导。
    // 无类型且无初值无法推断 → 报错（强制显式类型，与主流语言一致）。
    Ty declaredOrInferredType(const Field& f,
                              const std::unordered_map<std::string, Ty>& locals) {
        const bool declared = f.type.hasInline || !f.type.name.empty() ||
                              f.type.ptr > 0 || !f.type.arrayDims.empty() ||
                              f.type.fnKind != TypeRef::FncKind::None;
        if (declared) return fromTypeRef(f.type);
        if (!f.init)
            err(f.line, "变量缺少类型：无类型且无初值无法推断，请显式声明类型（如 var x: i4）");

        Ty t = inferExpr(*f.init, locals, f.line);
        if (t.isNil) err(f.line, "nil 不能用于无类型推断，请显式声明指针类型");
        return t;
    }

    // var/let/tls 多变量声明：逐项推导类型、检查初值兼容、登记到 locals
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

    // ---- 函数返回类型 ----
    // 返回类型省略 = void（内部标记 "v"，用于检查 return 语句兼容性）。
    // 引用的函数类型在依赖未合并时查不到 → 返回 invalid，调用方跳过检查。
    Ty funcRetType(const Decl& d) const {
        // -> func_type 引用：从 funcTypes 表展开返回类型
        if (!d.funcTypeName.empty()) {
            auto it = funcTypes.find(d.funcTypeName);
            if (it == funcTypes.end()) return Ty{};  // 未合并依赖，跳过检查
            const auto& rt = it->second->structCommon.type;
            if (!rt) return Ty{"v", 0, 0, true, false};
            Ty t = fromTypeRef(*rt);
            if (!t.valid || (t.name.empty() && t.ptr == 0 && t.arr == 0))
                return Ty{"v", 0, 0, true, false};
            return t;
        }
        // 直接声明的返回类型
        const auto& rt = d.structCommon.type;
        if (!rt) return Ty{"v", 0, 0, true, false};
        Ty t = fromTypeRef(*rt);
        if (!t.valid || (t.name.empty() && t.ptr == 0 && t.arr == 0))
            return Ty{"v", 0, 0, true, false};
        return t;
    }

    // ---- 语句遍历 ----
    // 对一条语句及其子语句做语义检查。locals 传递当前作用域的局部变量表，
    // if/while/for/case 的分支在 locals 副本上检查（不影响外层）。
    void checkStmt(const Stmt& s,
                   std::unordered_map<std::string, Ty>& locals,
                   const Ty& retTy) {
        switch (s.kind) {
            // -- 表达式语句：赋值时检查逃逸（禁止局部地址泄露到全局存储）--------
            case Stmt::ExprS:
                if (s.expr && s.expr->kind == Expr::Binary && isAssignOp(s.expr->op)) {
                    if (containsAddrOfLocal(*s.expr->b, locals) &&
                        rootedAtGlobal(*s.expr->a, locals)) {
                        err(s.line, "禁止将局部变量地址写入全局存储");
                    }
                }
                (void)inferExpr(*s.expr, locals, s.line);
                break;
            // -- 变量/常量/线程局部声明 ----------------------------------------
            case Stmt::VarS:
            case Stmt::LetS:
            case Stmt::TlsS:
                checkVarDecls(s.decls, locals);
                break;
            // -- return：检查返回类型兼容 + 禁止返回局部地址 --------------------
            case Stmt::ReturnS:
                if (s.expr) {
                    if (retTy.valid && retTy.name == "v" && retTy.ptr == 0)
                        err(s.line, "无返回值函数不能 return 表达式（返回类型省略即 void）");
                    if (containsAddrOfLocal(*s.expr, locals))
                        err(s.line, "禁止返回局部变量地址");
                    Ty rt = inferExpr(*s.expr, locals, s.line);
                    checkAssignable(retTy, rt, s.line);
                }
                break;
            // -- if/else：条件检查，两个分支独立作用域 --------------------------
            case Stmt::IfS: {
                (void)inferExpr(*s.expr, locals, s.line);
                auto a = locals, b = locals;
                for (auto& x : s.body) checkStmt(*x, a, retTy);
                for (auto& x : s.elseBody) checkStmt(*x, b, retTy);
                break;
            }
            // -- ret 调用语法糖：登记函数级 $（ret），检查被调用表达式与体 -------
            case Stmt::RetCallS: {
                locals["$"] = Ty{"ret", 0, 0, true, false};   // $ 为 ret 类型结果变量
                (void)inferExpr(*s.expr, locals, s.line);
                auto a = locals;
                for (auto& x : s.body) checkStmt(*x, a, retTy);
                break;
            }
            // -- while：条件检查，体在独立作用域 ---------------------------------
            case Stmt::WhileS: {
                (void)inferExpr(*s.expr, locals, s.line);
                auto a = locals;
                for (auto& x : s.body) checkStmt(*x, a, retTy);
                break;
            }
            // -- do-while：先检查体，再检查条件（条件可引用体中声明的变量）-------
            case Stmt::DoWhileS: {
                auto a = locals;
                for (auto& x : s.body) checkStmt(*x, a, retTy);
                (void)inferExpr(*s.expr, a, s.line);
                break;
            }
            // -- for：init/cond/step 三段，体在独立作用域 ------------------------
            case Stmt::ForS: {
                if (s.forIn) {
                    // for-in：推断集合/范围/选项，登记循环变量类型，再检查体
                    Ty coll;
                    if (s.forIsRange) {
                        if (s.forRangeLo) (void)inferExpr(*s.forRangeLo, locals, s.line);
                        if (s.forRangeHi) (void)inferExpr(*s.forRangeHi, locals, s.line);
                    } else if (s.forColl) {
                        coll = inferExpr(*s.forColl, locals, s.line);
                    }
                    if (s.forStepE)   (void)inferExpr(*s.forStepE, locals, s.line);
                    if (s.forOffsetE) (void)inferExpr(*s.forOffsetE, locals, s.line);
                    if (s.forNumE)    (void)inferExpr(*s.forNumE, locals, s.line);
                    // 循环变量类型：显式注解优先，否则按集合元素类型推断
                    Ty vt;
                    if (s.forVarHasType) {
                        vt = fromTypeRef(s.forVarType);
                    } else if (s.forIsRange) {
                        vt = Ty{"i4", 0, 0, true, false};
                    } else if (coll.valid) {
                        const std::string cn = resolveAliasToName(coll.name);
                        auto ci = containerItem.find(cn);
                        if (coll.arr > 0)                       vt = Ty{coll.name, coll.ptr, 0, true, false};
                        else if (ci != containerItem.end())     vt = Ty{ci->second, 1, 0, true, false};
                        else if (cn == "chain")                 vt = Ty{"", 1, 0, true, false};
                        else if (cn == "char")                  vt = Ty{"char", 0, 0, true, false};
                        else                                    vt = Ty{"i4", 0, 0, true, false};
                    }
                    auto a = locals;
                    if (!s.forVar.empty() && vt.valid) a[s.forVar] = vt;
                    for (auto& iv : s.forIdxVars)               // 索引/坐标变量：整型计数
                        if (!iv.empty()) a[iv] = Ty{"i4", 0, 0, true, false};
                    for (auto& x : s.body) checkStmt(*x, a, retTy);
                    break;
                }
                if (s.forInit) (void)inferExpr(*s.forInit, locals, s.line);
                if (s.forCond) (void)inferExpr(*s.forCond, locals, s.line);
                if (s.forStep) (void)inferExpr(*s.forStep, locals, s.line);
                auto a = locals;
                for (auto& x : s.body) checkStmt(*x, a, retTy);
                break;
            }
            // -- case 多分支：每个 arm 独立作用域 -------------------------------
            case Stmt::CaseS: {
                Ty st = inferExpr(*s.expr, locals, s.line);
                // 标签联合解构：变体名标签不当变量推断；Variant as x 绑定载荷类型
                const Decl* tu = (st.valid && st.ptr == 0 && st.arr == 0)
                                 ? resolveStruct(st.name) : nullptr;
                if (tu && tu->kind == Decl::UnionD && tu->tagged) {
                    for (auto& arm : s.caseArms) {
                        auto a = locals;
                        if (!arm.binding.empty() && !arm.labels.empty()
                            && arm.labels[0]->kind == Expr::Ident) {
                            for (auto& f : tu->structCommon.fields)
                                if (f.name == arm.labels[0]->text) {
                                    a[arm.binding] = fromTypeRef(f.type);
                                    break;
                                }
                        }
                        for (auto& x : arm.body) checkStmt(*x, a, retTy);
                    }
                    break;
                }
                for (auto& arm : s.caseArms) {
                    auto a = locals;
                    for (auto& lab : arm.labels) (void)inferExpr(*lab, a, s.line);
                    for (auto& x : arm.body) checkStmt(*x, a, retTy);
                }
                break;
            }
            // -- goto / label：无额外类型检查 ----------------------------------
            case Stmt::GotoS:
                break;
            case Stmt::LabelS: {
                auto a = locals;
                for (auto& x : s.body) checkStmt(*x, a, retTy);
                break;
            }
            // -- break / continue：无类型数据 ----------------------------------
            case Stmt::BreakS:
            case Stmt::ContinueS:
                break;
            // -- def（内嵌类型声明）：无需检查 ---------------------------------
            case Stmt::DeclS:
                break;
            // -- final 域退出钩子：体在独立作用域检查 ---------------------------
            case Stmt::FinalS: {
                auto a = locals;
                for (auto& x : s.body) checkStmt(*x, a, retTy);
                break;
            }
            // -- run：rpc 线程创建调用的实参与 thread 出参 ----------------------
            case Stmt::RunS:
                (void)inferExpr(*s.expr, locals, s.line);
                if (s.forInit) (void)inferExpr(*s.forInit, locals, s.line);
                break;
            // -- done：future + 可选结果（结果在 codegen 自动 void* 擦除） --------
            case Stmt::DoneS:
                (void)inferExpr(*s.expr, locals, s.line);              // future
                if (s.forInit) (void)inferExpr(*s.forInit, locals, s.line); // 结果
                break;
            // -- print：逐项检查实参表达式（格式覆盖 Cast 解包到被格式化的子表达式）
            case Stmt::PrintS:
                for (auto& a : s.printArgs) {
                    const Expr* v = (a->kind == Expr::Cast && a->castIsFmt) ? a->a.get() : a.get();
                    (void)inferExpr(*v, locals, s.line);
                }
                break;
        }
    }

    // ---- 顶层遍历：收集符号 + 检查全局变量初值 -------------------------------
    void collectTop() {

        // 第一遍：登记函数类型、结构体、别名到符号表
        for (auto& d : prog.decls) {
            if (d->kind == Decl::FuncTypeD && !d->isRpc && !d->cImpl && d->methodOwner.empty())
                funcTypes[d->name] = d.get();
            if (d->kind == Decl::StructD || d->kind == Decl::UnionD)
                structs[d->name] = d.get();
            if (d->kind == Decl::AliasD)
                aliases[d->name] = d->structCommon.type->name;
        }

        // 容器映射 C → I（def T: <C, I>）：下标糖 t[key] 推导 find 结果类型 I&
        for (auto& d : prog.decls)
            if (d->kind == Decl::StructD && !d->adtColl.empty())
                containerItem[resolveAliasToName(d->adtColl)] = d->adtItem;

        // 第二遍：检查全局 var/let/tls 声明
        for (auto& d : prog.decls) {            
            if (d->kind != Decl::VarD && d->kind != Decl::LetD && d->kind != Decl::TlsD)
                continue;

            for (auto& f : d->structCommon.fields) {

                // 优先使用显式声明的类型，否则从初值推导或默认 char*
                Ty lhs = fromTypeRef(f.type);
                if (!lhs.valid || (lhs.name.empty() && lhs.ptr == 0 && lhs.arr == 0)) {
                    lhs = f.init ? inferExpr(*f.init, globals, f.line)
                                 : Ty{"char", 1, 0, true, false};
                }
                if (f.init) {
                    Ty rhs = inferExpr(*f.init, globals, f.line);
                    checkAssignable(lhs, rhs, f.line);
                }
                globals[f.name] = lhs;
            }
        }
    }

    // ---- 遍历所有函数体 ---------------------------------
    void checkFunctions() {

        for (auto& d : prog.decls) {
            if (d->kind != Decl::FuncD) continue;

            std::unordered_map<std::string, Ty> locals;

            // 建立函数的形参符号表（含 -> func_type 展开）
            const Decl* sig = d.get();
            if (!d->funcTypeName.empty()) {
                auto it = funcTypes.find(d->funcTypeName);
                if (it != funcTypes.end()) sig = it->second;
            }
            for (auto& p : sig->structCommon.fields)
                locals[p.name] = fromTypeRef(p.type);

            // com 通道末参校验（Q5）：rpc 的 com& 参数（设备通讯端点）必须位于参数表末位，
            // 以便后续 << / >> 通讯语法糖将 com 通道隐式补足为最末实参。
            // 仅约束 rpc：MethodPtr 实现（read/write/error）是 fnc，其 _this: com& 为首参接收者，不在此列。
            if (d->isRpc) {
                const auto& ps = sig->structCommon.fields;
                for (size_t i = 0; i + 1 < ps.size(); i++) {
                    if (ps[i].type.fnKind == TypeRef::FncKind::None &&
                        ps[i].type.ptr >= 1 &&
                        resolveAliasToName(ps[i].type.name) == "com")
                        err(ps[i].line ? ps[i].line : d->line,
                            "com& 通讯通道参数 '" + ps[i].name +
                            "' 必须位于 rpc '" + d->name + "' 的参数表末位");
                }
            }
            // 方法：隐式 this 参数
            if (!d->methodOwner.empty()) {
                locals["this"] = Ty{d->methodOwner, 1, 0, true, false};
                // 分身/切片类型的方法：额外提供 self 上下文（指向本体实体 T）
                auto so = structs.find(d->methodOwner);
                if (so != structs.end() && !so->second->projectEntity.empty())
                    locals["self"] = Ty{so->second->projectEntity, 1, 0, true, false};
            }

            Ty ret = funcRetType(*d);
            for (auto& s : d->body) checkStmt(*s, locals, ret);
        }
    }

    // ---- 自动指针 T@ 边界检查（§13.5）----
    // T@ 数组：局部（一维/多维）已实现（元素逐个根边、退域逐元素嵌套清理、下标赋值记账）→ 放行；
    // 非局部（字段/全局/参数/返回）仍未实现引用图清理 → 报错（静默会泄露）。
    void checkFatTypeRef(const TypeRef& t, int line, const char* where,
                         bool allowLocalArray = false) const {
        if (t.fat && !t.arrayDims.empty()) {
            if (!allowLocalArray)
                err(line, std::string("暂不支持 T@ 数组（") + where +
                    "）：该位置的引用图清理与下标赋值记账尚未实现；"
                    "如需指针数组请用裸指针 T& 数组，或改用局部 T@ 数组");
        }
        // 内联结构/联合字段递归
        if (t.hasInline)
            for (auto& f : t.structCommon.fields)
                checkFatTypeRef(f.type, f.line ? f.line : line, "内联字段");
    }

    // 类型按值是否内嵌自动指针 T@（直接 T@ 字段，或按值聚合/内联结构递归含 T@）。
    // 用于跨 C ABI 守卫：含 T@ 的结构体按值跨边界（C 侧/传输）会破坏胖指针引用图与 ARC。
    bool typeEmbedsFat(const TypeRef& t, std::unordered_set<std::string>& visited) const {
        if (t.fnKind != TypeRef::FncKind::None) return false;   // 函数指针固定大小，不内嵌
        if (t.fat) return true;                                 // T@ 字段：内嵌 sc_fat
        if (t.hasInline)
            for (auto& f : t.structCommon.fields)
                if (typeEmbedsFat(f.type, visited)) return true;
        // 按值聚合（非指针/非数组）：递归其字段
        if (t.ptr == 0 && t.arrayDims.empty() && !t.name.empty()) {
            const std::string base = resolveAliasToName(t.name);
            if (!visited.insert(base).second) return false;     // 环：已访问
            const Decl* sd = resolveStruct(base);
            if (sd && (sd->kind == Decl::StructD || sd->kind == Decl::UnionD))
                for (auto& f : sd->structCommon.fields)
                    if (typeEmbedsFat(f.type, visited)) return true;
        }
        return false;
    }

    // 跨 C ABI 守卫：检查导出/rpc/C 实现函数的单个参数/返回类型。
    //   · 直接 T@：仅 rpc / cImpl 拒绝（导出 @fnc 允许以 T@ 移交所有权）。
    //   · 按值结构体内嵌 T@：导出 / rpc / cImpl 一律拒绝（C 侧无法维护 ARC）。
    void checkAbiFatType(const TypeRef& t, int line, const Decl& fn, const char* slot) const {
        const bool transport = fn.isRpc || fn.cImpl;   // 跨传输 / 跨 C 实现
        if (t.fat) {
            if (transport)
                err(line, std::string(fn.isRpc ? "rpc '" : "C 实现接口 '") + fn.name +
                    "' 的" + slot + "为自动指针 T@：跨传输/跨 C ABI 无法维护胖指针引用图与 ARC，"
                    "请改用裸指针 T& 或值类型");
            return;   // 导出 @fnc 直接 T@：允许（所有权移交）
        }
        if (t.ptr == 0 && t.arrayDims.empty()) {
            std::unordered_set<std::string> visited;
            if (typeEmbedsFat(t, visited))
                err(line, std::string("'") + fn.name + "' 的" + slot +
                    "（按值聚合 '" + t.name + "'）内嵌自动指针 T@ 成员：不能跨 C ABI 传递"
                    "（C 侧无法维护胖指针 ARC），请改用裸指针 T& 或移除该成员后传递");
        }
    }

    // 对一个导出/rpc/cImpl 函数声明做参数与返回类型的跨 C ABI 守卫。
    void checkAbiFatFn(const Decl& d) const {
        for (auto& p : d.structCommon.fields)
            checkAbiFatType(p.type, p.line ? p.line : d.line, d, ("参数 '" + p.name + "'").c_str());
        if (d.structCommon.type)
            checkAbiFatType(*d.structCommon.type, d.line, d, "返回类型");
    }


    void checkFatBoundariesStmt(const Stmt& s) const {
        for (auto& d : s.decls)
            checkFatTypeRef(d.type, d.line ? d.line : s.line, "局部变量/常量",
                            /*allowLocalArray*/ true);
        for (auto& b : s.body) checkFatBoundariesStmt(*b);
        for (auto& b : s.elseBody) checkFatBoundariesStmt(*b);
        for (auto& arm : s.caseArms)
            for (auto& b : arm.body) checkFatBoundariesStmt(*b);
        if (s.decl)
            for (auto& f : s.decl->structCommon.fields)
                checkFatTypeRef(f.type, f.line ? f.line : s.line, "局部类型字段");
    }

    void checkFatBoundaries() const {
        for (auto& d : prog.decls) {
            // 结构/联合字段 + 全局变量类型 + 函数参数/返回类型
            for (auto& f : d->structCommon.fields)
                checkFatTypeRef(f.type, f.line ? f.line : d->line,
                                d->kind == Decl::FuncD ? "函数参数" : "结构字段/全局变量");
            if (d->structCommon.type)
                checkFatTypeRef(*d->structCommon.type, d->line, "返回类型");
            if (d->kind == Decl::FuncD)
                for (auto& s : d->body) checkFatBoundariesStmt(*s);
            // 跨 C ABI 守卫：导出 / rpc / C 实现函数的参数/返回不得跨边界携带 T@（详见 §18）
            if ((d->kind == Decl::FuncD || d->kind == Decl::FuncTypeD)
                && (d->exported || d->isRpc || d->cImpl))
                checkAbiFatFn(*d);
        }
    }

    // ---- 主入口：三阶段检查 ---------------------------------
    void run() {
        collectTop();                   // 1. 收集顶层符号 + 检查全局初值
        checkAggregateByValueCycles();  // 2. 按值包含环检测
        checkFatBoundaries();           // 2.5 自动指针 T@ 边界检查
        checkFunctions();               // 3. 遍历所有函数体
    }
};

} // namespace

// 对外接口：对 AST 执行完整语义检查，错误通过 CompileError 抛出
void semanticCheck(const Program& prog) {
    Checker c(prog);
    c.run();
}

// ============================================================
// 外部描述符使用分析
// ============================================================
// 思路：先扫描"本单元自身代码"（非 external 声明）引用到的所有名字（标识符、
// 类型名、成员名），汇成引用集；再据此判定每个 external 描述符是否被用到，
// 最后对"贡献了描述符却整体未被引用"的 .sc 模块给出导入未使用警告。
//
// 采集采取"宁多勿少"策略（连同表达式 text/op 一并收集）——过度收集只会让更多
// 描述符被判为已用、从而少报警告，方向偏宽松，避免误报，符合半宽松检查诉求。
namespace {

void usageType(const TypeRef& t, std::unordered_set<std::string>& refs);
void usageExpr(const Expr& e, std::unordered_set<std::string>& refs);
void usageStmt(const Stmt& s, std::unordered_set<std::string>& refs);

void usageFields(const std::vector<Field>& fs, std::unordered_set<std::string>& refs) {
    for (auto& f : fs) {
        usageType(f.type, refs);
        if (f.init) usageExpr(*f.init, refs);
    }
}

// 收集类型名（含内联结构/联合成员、内联函数指针的参数与返回类型）
void usageType(const TypeRef& t, std::unordered_set<std::string>& refs) {
    if (!t.name.empty()) refs.insert(t.name);
    usageFields(t.structCommon.fields, refs);
    if (t.structCommon.type) usageType(*t.structCommon.type, refs);
}

// 收集表达式涉及的名字：Ident 名、成员名（Member.text）、强转/offsetof 类型名等
void usageExpr(const Expr& e, std::unordered_set<std::string>& refs) {
    if (!e.text.empty()) refs.insert(e.text);
    if (!e.op.empty()) refs.insert(e.op);
    if (e.a) usageExpr(*e.a, refs);
    if (e.b) usageExpr(*e.b, refs);
    if (e.c) usageExpr(*e.c, refs);
    for (auto& a : e.args) if (a) usageExpr(*a, refs);
}

void usageStmt(const Stmt& s, std::unordered_set<std::string>& refs) {
    if (s.expr) usageExpr(*s.expr, refs);
    if (s.forInit) usageExpr(*s.forInit, refs);
    if (s.forCond) usageExpr(*s.forCond, refs);
    if (s.forStep) usageExpr(*s.forStep, refs);
    usageFields(s.decls, refs);
    for (auto& b : s.body) usageStmt(*b, refs);
    for (auto& b : s.elseBody) usageStmt(*b, refs);
    for (auto& arm : s.caseArms) {
        for (auto& l : arm.labels) if (l) usageExpr(*l, refs);
        for (auto& b : arm.body) usageStmt(*b, refs);
    }
    if (s.decl) {                                   // 函数体内内嵌 def
        usageFields(s.decl->structCommon.fields, refs);
        if (s.decl->structCommon.type) usageType(*s.decl->structCommon.type, refs);
        for (auto& b : s.decl->body) usageStmt(*b, refs);
    }
}

} // namespace

// 采集本单元自身代码（非 external 声明）引用到的所有名字
std::unordered_set<std::string> collectExternalRefs(const Program& prog) {
    std::unordered_set<std::string> refs;
    for (auto& d : prog.decls) {
        if (d->external) continue;                  // 只看本单元自己写的代码
        usageFields(d->structCommon.fields, refs);  // 签名/字段/参数类型
        if (d->structCommon.type) usageType(*d->structCommon.type, refs);  // 返回/别名/枚举基类型
        for (auto& s : d->body) usageStmt(*s, refs);  // 函数体
    }
    return refs;
}

std::vector<Diagnostic> analyzeExternalUsage(Program& prog) {

    // 1. 采集本单元自身代码引用到的名字
    std::unordered_set<std::string> refs = collectExternalRefs(prog);

    // 2. 标记每个 external 描述符的 used 状态
    //    方法：以方法名是否被调用（成员访问名进入 refs）判定；
    //    其余（类型/函数/全局量）：以其名字是否被引用判定。
    for (auto& d : prog.decls) {
        if (!d->external || d->kind == Decl::IncD) continue;
        d->used = !d->methodOwner.empty() ? refs.count(d->methodName) > 0
                                          : refs.count(d->name) > 0;
    }

    // 3. 按来源汇总；对"声明了描述符却整体未被引用"的来源给出导入未使用警告。
    //    来源总数优先取 Decl::externDeclared（C 头由 libclang 给出真实总数，可能
    //    因降噪阈值未逐个合成 Decl）；.sc / 退化模式回退为已合成描述符计数。
    std::unordered_map<std::string, int> originCount;  // 来源 → 已合成描述符数
    std::unordered_map<std::string, int> originUsed;   // 来源 → 其中已用数
    for (auto& d : prog.decls) {
        if (!d->external || d->kind == Decl::IncD) continue;
        originCount[d->origin]++;
        if (d->used) originUsed[d->origin]++;
    }
    std::vector<Diagnostic> warns;
    for (auto& d : prog.decls) {
        if (d->kind != Decl::IncD || !d->external) continue;
        const int used = originUsed.count(d->origin) ? originUsed[d->origin] : 0;
        const bool isSc = d->name.size() >= 3 &&
                          d->name.compare(d->name.size() - 3, 3, ".sc") == 0;
        if (isSc) {
            // .sc 模块：合并的导出声明即其符号全集，按已合成描述符计数填充统计字段
            d->externDeclared = originCount.count(d->origin) ? originCount[d->origin] : 0;
            d->externAnalyzed = true;
        }
        // 已确定符号全集、来源确有描述符、且无一被引用 → 警告
        if (d->externAnalyzed && d->externDeclared != 0 && used == 0) {
            std::string disp = d->name;  // 去掉 C 头两侧的 <>/"" 修饰，避免重复引号
            if (disp.size() >= 2 &&
                ((disp.front() == '<' && disp.back() == '>') ||
                 (disp.front() == '"' && disp.back() == '"')))
                disp = disp.substr(1, disp.size() - 2);
            warns.push_back({"外部来源 \"" + disp + "\" 已导入，但其描述符均未被引用", d->line});
        }
    }
    return warns;
}
