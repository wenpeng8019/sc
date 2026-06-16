// ============================================================
// C 代码生成器 —— Program AST → C 源码字符串
// ============================================================
// 核心工作：
//   1. 类型映射：sc 内置类型 → C 标准类型（i4→int32_t 等）
//   2. 默认类型推断：
//        - 有初值的无类型 var/let：按初值字面量推断（见 inferLiteralType）
//        - 其余无类型对象→char*，无类型指针→void*
//   3. 函数类型展开：fnc name -> func_type 从函数类型表查找签名
//   4. 输出顺序：类型定义 → 全局变量 → 函数原型 → 函数实现
// 两遍扫描：第一遍输出类型/变量/原型，第二遍输出函数体。
// ============================================================
#include "codegen_c.h"
#include "error.h"
#include <cctype>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace {

// sc 内置基本类型 → C 标准类型映射
std::string mapBase(const std::string& n) {
    static const std::unordered_map<std::string, std::string> m = {
        {"i1", "int8_t"},  {"i2", "int16_t"}, {"i4", "int32_t"}, {"i8", "int64_t"},
        {"u1", "uint8_t"}, {"u2", "uint16_t"}, {"u4", "uint32_t"}, {"u8", "uint64_t"},
        {"f4", "float"},   {"f8", "double"},
        {"bool", "uint8_t"},  // 布尔：u1 的语义别名（true/false 即 1/0）
        {"char", "char"},     // 字符：与 C 字符串字面量/接口互操作用（区别于 i1/u1）
        {"va_list", "va_list"},  // 透传：可变参数列表类型
    };
    auto it = m.find(n);
    return it == m.end() ? n : it->second;
}

// 无类型 var/let 声明（var x: = 初值）时，依据初值字面量推断默认类型。
// 规则：
//   字符串字面量 "..."  → char*（ptr=1）
//   字符字面量   '...'   → char
//   浮点字面量   3.14    → 默认 f8（double，避免精度损失）；带 f/F 后缀 → f4
//   整数字面量   42      → 默认 i4（32 位）；数值超出 32 位或带 l/L 后缀 → i8；
//                          带 u/U 后缀取无符号 u4，超出 32 位 → u8
// 穿透前缀正负号（var n: = -5）。返回 true 表示成功推断（填入 base/ptr），
// false 表示初值非字面量，沿用旧默认规则（char*/void*）。
bool inferLiteralType(const Expr& init, std::string& base, int& ptr) {
    const Expr* e = &init;
    while (e->kind == Expr::Unary && (e->op == "-" || e->op == "+") && e->a)
        e = e->a.get();
    switch (e->kind) {
        case Expr::StrLit:  base = "char"; ptr = 1; return true;
        case Expr::CharLit: base = "char"; ptr = 0; return true;
        case Expr::FloatLit: {
            bool f32 = e->text.find_first_of("fF") != std::string::npos;
            base = f32 ? "f4" : "f8"; ptr = 0; return true;
        }
        case Expr::IntLit: {
            const std::string& t = e->text;
            bool uns = t.find_first_of("uU") != std::string::npos;
            bool lng = t.find_first_of("lL") != std::string::npos;
            std::string digits = t.substr(0, t.find_first_of("uUlL"));
            unsigned long long mag = 0;
            try { mag = std::stoull(digits, nullptr, 0); } catch (...) { mag = 0; }
            if (uns) base = (lng || mag > 0xFFFFFFFFull) ? "u8" : "u4";
            else     base = (lng || mag > 0x7FFFFFFFull) ? "i8" : "i4";
            ptr = 0; return true;
        }
        default: return false;
    }
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string moduleFileToken(const std::string& s) {
    std::string out = "scm_";
    for (unsigned char ch : s) out += std::isalnum(ch) ? (char)ch : '_';
    return out;
}

std::string moduleHeaderName(const std::string& s) {
    return moduleFileToken(s) + ".h";
}

// CGen 内部类 —— 封装 C 代码生成的状态
struct CGen {
    const Program& prog;
    std::ostringstream out;     // 输出流
    int depth = 0;              // 当前缩进深度（每级4空格）
    std::string srcFile;        // 非空时输出 #line 指令，调试器映射回 .sc 源码
    std::unordered_map<std::string, const Decl*> funcTypes;  // 函数类型名→Decl 映射
    std::unordered_map<std::string, const Decl*> rpcs;       // rpc 名→Decl（run 语句查询）
    bool usesRun = false;       // 程序中出现 run 语句：需输出 thread_run 原型
    bool usesWait = false;      // 程序中出现 wait 语句：需输出 cond_wait 原型
    int  usesPrint = 0;         // print 关键字首次出现行号（需 inc io.sc + print 原型）
    int  usesStrof = 0;         // stringify(值) 格式化关键字首次出现行号（需 adt string 可见）
    bool usesLPrev = false;     // 出现 prev 链表导航：需输出边界安全前驱 helper sc__lprev

    // ---- 伪 class 支撑：类型注册表与变量类型跟踪 ----
    std::unordered_map<std::string, const Decl*> aggrs;    // struct/union 名 → Decl
    std::unordered_map<std::string, std::string> aliases;  // 别名 → 目标类型名
    // 顶层函数表：函数名 → Decl（缺参调用 0 补全查签名用）
    std::unordered_map<std::string, const Decl*> funcs;
    // 顶层方法表：所属类型 → 方法名 → Decl（结构内实现或 fnc T::m 声明）
    std::unordered_map<std::string, std::unordered_map<std::string, const Decl*>> methods;
    // 变量的轻量类型信息（类型名 + 指针层数 + 数组维数），用于方法调用识别
    struct VType { std::string name; int ptr = 0; int arr = 0; };
    std::unordered_map<std::string, VType> globalsT, localsT;
    // 函数指针变量的内联签名（var cb: fnc: ...）：缺参补全查询用
    std::unordered_map<std::string, const TypeRef*> fnVarsG, fnVarsL;
    // 数组变量的维度表（string 格式化顶层数组需要维度信息）
    std::unordered_map<std::string, std::vector<std::string>> varDimsG, varDimsL;
    // 枚举类型名集合（string 格式化按整数）
    std::unordered_set<std::string> enums;
    bool inFunc = false;        // 当前是否在函数体内（决定变量注册到哪个表）

    // ---- rpc 支撑：实际函数体内，参数引用改写为 _p->name ----
    const Decl* curRpc = nullptr;               // 非空时正在输出 rpc 实际函数体
    std::unordered_set<std::string> rpcParams;  // 当前 rpc 的参数名集合

    // ---- 匿名函数字面量（FncLit）支撑 ----
    // 不捕获外层变量的「伪闭包」：提升为顶层 static 函数，表达式处替换为函数名。
    std::ostringstream lambdaOut;                       // 提升后的 static 函数定义（在函数体前回填）
    std::unordered_map<const Expr*, std::string> lambdaNames;  // FncLit 节点 → 生成的函数名
    int lambdaSeq = 0;                                  // 函数名序号

    // ---- 聚合体定义顺序无关支撑 ----
    // 按值包含要求被包含者「完整类型」先于包含者出现（前置声明仅满足指针引用）。
    // 第一遍输出 struct/union 时惰性前移其按值依赖，使源码定义顺序无关（见 doc §5.3）。
    std::unordered_set<const Decl*> emittedAggr;        // 已输出完整定义的聚合体

    explicit CGen(const Program& p) : prog(p) {}

    void indent() { for (int i = 0; i < depth; i++) out << "    "; }

    // ---------------- 类型处理 ----------------
    // 解析类型引用，确定 C 中的底层类型名和指针层数
    // 默认规则：无类型无指针→char*，无类型有指针→void*
    static void resolveType(const TypeRef& t, std::string& base, int& ptr) {
        if (t.name.empty() && !t.hasInline) {
            if (t.ptr > 0) { base = "void"; ptr = t.ptr; }
            else { base = "char"; ptr = 1; }
        } else {
            base = mapBase(t.name);
            ptr = t.ptr;
        }
    }

    // 声明一个字段/变量：T [*...]name[size]
    void emitDeclarator(const Field& f, bool asConst = false) {
        if (f.type.fnKind != TypeRef::FncKind::None) {
            if (f.type.structCommon.type) {
                std::string base; int ptr;
                resolveType(*f.type.structCommon.type, base, ptr);
                out << base << " ";
                for (int i = 0; i < ptr; i++) out << "*";
            } else out << "void ";  // 省略返回类型 = void
            out << "(*" << f.name << ")(";
            for (size_t i = 0; i < f.type.structCommon.fields.size(); i++) {
                if (i) out << ", ";
                emitDeclarator(f.type.structCommon.fields[i]);
            }
            if (f.type.structCommon.variadic) out << (f.type.structCommon.fields.empty() ? "..." : ", ...");
            out << ")";
            return;
        }
        if (f.type.hasInline) { // 内联/匿名结构联合
            out << (f.type.inlineUnion ? "union" : "struct") << " {\n";
            depth++;
            for (auto& sub : f.type.structCommon.fields) {
                indent();
                emitDeclarator(sub);
                out << ";\n";
            }
            depth--;
            indent();
            out << "}";
            if (!f.name.empty()) out << " " << f.name;
            for (auto& dim : f.type.arrayDims) out << "[" << dim << "]";
            return;
        }
        std::string base; int ptr;
        resolveType(f.type, base, ptr);
        if (asConst) out << "const ";
        out << base << " ";
        for (int i = 0; i < ptr; i++) out << "*";
        out << (f.name == "this" ? "_this" : f.name);  // 参数名 this → _this
        for (auto& dim : f.type.arrayDims) out << "[" << dim << "]";
    }

    bool shouldStaticize(const Decl& d) const {
        return !d.exported && !d.external;
    }

    // ---------------- 伪 class：类型查询与方法识别 ----------------
    // 类型名 → struct/union 节点（穿透别名，最多 8 层防环）
    const Decl* aggrOf(std::string name) const {
        for (int i = 0; i < 8 && !name.empty(); i++) {
            auto it = aggrs.find(name);
            if (it != aggrs.end()) return it->second;
            auto al = aliases.find(name);
            if (al == aliases.end()) return nullptr;
            name = al->second;
        }
        return nullptr;
    }

    // prev/next 上下文关键字：链表结构体上映射到内置 _prev/_next
    static std::string memberFieldName(const Decl& sd, const std::string& name) {
        if (sd.linked && (name == "prev" || name == "next")) return "_" + name;
        return name;
    }

    // 链表结构体首个真实成员（跳过前置注入的 synthetic _prev/_next）
    const Field* firstRealField(const Decl* sd) const {
        if (!sd) return nullptr;
        for (auto& f : sd->structCommon.fields) if (!f.synthetic) return &f;
        return nullptr;
    }

    // ---- T() 伪调用预扫描：收集需要生成 T__new 辅助函数的类型 ----
    std::set<std::string> heapNews;

    void scanExprForNew(const Expr& e) {
        if (e.kind == Expr::Call && e.a && e.a->kind == Expr::Ident && e.args.empty())
            if (const Decl* sd = aggrOf(e.a->text)) heapNews.insert(sd->name);
        // print / stringify 格式化关键字使用标记（原型/辅助函数需先于函数体输出）
        if (e.kind == Expr::Call && e.a && e.a->kind == Expr::Ident) {
            if (e.a->text == "print" && !usesPrint) usesPrint = e.line;
            if (e.a->text == "stringify" && !e.args.empty() && !usesStrof) usesStrof = e.line;
            // prev(o) 导航函数形式：边界安全前驱 helper
            if (e.a->text == "prev" && e.args.size() == 1) usesLPrev = true;
        }
        // it->prev 成员形式：链表前驱导航（边界安全 helper sc__lprev）
        if (e.kind == Expr::Member && e.text == "prev") usesLPrev = true;
        if (e.a) scanExprForNew(*e.a);
        if (e.b) scanExprForNew(*e.b);
        if (e.c) scanExprForNew(*e.c);
        for (auto& a : e.args) scanExprForNew(*a);
    }

    void scanStmtForNew(const Stmt& s) {
        if (s.kind == Stmt::RunS) usesRun = true;
        if (s.kind == Stmt::WaitS) usesWait = true;
        if (s.expr) scanExprForNew(*s.expr);
        for (auto& f : s.decls) if (f.init) scanExprForNew(*f.init);
        if (s.forInit) scanExprForNew(*s.forInit);
        if (s.forCond) scanExprForNew(*s.forCond);
        if (s.forStep) scanExprForNew(*s.forStep);
        for (auto& b : s.body) scanStmtForNew(*b);
        for (auto& b : s.elseBody) scanStmtForNew(*b);
        for (auto& arm : s.caseArms) {
            for (auto& l : arm.labels) scanExprForNew(*l);
            for (auto& b : arm.body) scanStmtForNew(*b);
        }
    }

    // 生成堆构造辅助函数：T *T__new(void) = malloc + 默认值/清零 + init
    void emitNewHelpers() {
        for (auto& tn : heapNews) {
            auto it = aggrs.find(tn);
            if (it == aggrs.end()) continue;
            const Decl* sd = it->second;
            out << "static inline " << tn << " *" << tn << "__new(void) {\n"
                << "    " << tn << " *_p = (" << tn << " *)malloc(sizeof(" << tn << "));\n"
                << "    if (_p) {\n";
            if (sd->kind == Decl::StructD && hasFieldDefaults(sd))
                out << "        *_p = " << tn << "__default();\n";
            else
                out << "        memset(_p, 0, sizeof(" << tn << "));\n";
            const Decl* im = findMethod(tn, "init");
            if (im && im->structCommon.fields.empty())
                out << "        " << im->name << "(_p);\n";
            out << "    }\n    return _p;\n}\n\n";
        }
    }

    // ---------------- stringify 格式化关键字：按静态类型生成格式化器 ----------------
    // stringify(expr) → stringify_KEY(expr)，返回 adt string（调用者负责 drop）。
    // stringify(expr, 缓存, 大小) → stringify_KEY_buf(expr, 缓存, 大小)，在给定缓存内
    // 构建（截断保证 NUL 结尾），返回 char*（即缓存首址，无需 drop）。
    // 输出为 JSON 字符串格式（键加双引号）。按类型生成的静态内联格式化器在函数体
    // 输出后回填（emitSofHelpers）：开启头文件模式时写入独立 stringify.h，由 .c include。
    struct SofReq {
        std::string key;                // 包装函数名后缀（类型唯一键）
        std::string cParam;             // 包装函数形参 C 声明
        std::string name;               // sc 类型名
        int ptr = 0;                    // 指针层数
        bool needBuf = false;           // 需生成缓存变体 stringify_KEY_buf
        std::vector<std::string> dims;  // 数组维度（仅一维）
    };
    std::map<std::string, SofReq> sofReqs;  // key → 顶层格式化请求
    std::set<std::string> sofAggrs;         // 需生成 sc__sof_T 的聚合（不含 string）
    // 非空时：stringify 支撑代码写入独立头文件（此名），.c 在回填处 #include 它；
    // 空时回退为内联进 .c（--emit-c 输出到 stdout 时保持单文件自包含）。
    std::string sofHeaderName;
    std::string sofHeaderOut;           // 头文件模式下回填生成的头文件全文

    // 穿透别名得到最终类型名（最多 8 层防环）
    std::string resolveAliasName(std::string n) const {
        for (int i = 0; i < 8; i++) {
            auto it = aliases.find(n);
            if (it == aliases.end()) break;
            n = it->second;
        }
        return n;
    }

    // 标量分类：'i' 有符号 / 'u' 无符号 / 'f' 浮点 / 'b' bool / 'c' 字符 / 0 非标量
    char scalarClass(const std::string& rawName) const {
        std::string n = resolveAliasName(rawName);
        if (enums.count(n)) return 'i';
        if (n == "i1" || n == "i2" || n == "i4" || n == "i8") return 'i';
        if (n == "u1" || n == "u2" || n == "u4" || n == "u8") return 'u';
        if (n == "f4" || n == "f8") return 'f';
        if (n == "bool") return 'b';
        if (n == "char" || n == "c1") return 'c';
        return 0;
    }

    // sc 类型 → C 类型文本（聚合用规范名，标量经 mapBase）
    std::string cTypeOf(const std::string& name, int ptr) const {
        std::string base;
        if (name.empty()) base = ptr > 0 ? "void" : "char";
        else if (const Decl* sd = aggrOf(name)) base = sd->name;
        else base = mapBase(resolveAliasName(name));
        std::string s = base;
        if (ptr > 0) {
            s += " ";
            for (int i = 0; i < ptr; i++) s += "*";
        }
        return s;
    }

    // 数组表达式的维度（Ident 查变量表，Member 查字段定义）
    bool exprDims(const Expr& e, std::vector<std::string>& dims) const {
        if (e.kind == Expr::Ident) {
            auto it = varDimsL.find(e.text);
            if (inFunc && it != varDimsL.end()) { dims = it->second; return true; }
            it = varDimsG.find(e.text);
            if (it != varDimsG.end()) { dims = it->second; return true; }
            return false;
        }
        if (e.kind == Expr::Member) {
            VType base;
            if (!exprVType(*e.a, base)) return false;
            const Decl* sd = aggrOf(base.name);
            if (!sd) return false;
            for (auto& f : sd->structCommon.fields)
                if (f.name == e.text) { dims = f.type.arrayDims; return true; }
        }
        return false;
    }

    // string 格式化调用点：校验类型并登记格式化请求，输出包装函数调用
    void emitStrofCall(const Expr& e) {
        if (e.args.size() != 1 && e.args.size() != 3)
            throw CompileError{"stringify(值[, 缓存, 大小]) 需要 1 或 3 个实参", e.line};
        if (!aggrOf("string"))
            throw CompileError{"stringify(...) 格式化依赖内置 string，请先 inc adt.sc", e.line};
        VType vt;
        if (!exprVType(*e.args[0], vt))
            throw CompileError{"stringify(...) 无法推断实参类型", e.line};
        SofReq r;
        r.name = vt.name;
        r.ptr = vt.ptr;
        if (vt.arr > 0) {
            if (!exprDims(*e.args[0], r.dims) || (int)r.dims.size() != vt.arr)
                throw CompileError{"string(...) 无法确定数组维度", e.line};
            if (r.dims.size() > 1)
                throw CompileError{"stringify(...) 暂不支持多维数组", e.line};
        }
        const Decl* sd = aggrOf(vt.name);
        if (r.dims.empty() && vt.ptr == 0 && !sd && !scalarClass(vt.name))
            throw CompileError{"stringify(...) 不支持该类型：" +
                (vt.name.empty() ? std::string("(无类型)") : vt.name), e.line};
        // 类型唯一键：规范名 + _p（每层指针）+ _a维度
        std::string key = sd ? sd->name
                             : (vt.name.empty() ? std::string("x") : resolveAliasName(vt.name));
        for (int i = 0; i < vt.ptr; i++) key += "_p";
        for (auto& d : r.dims) {
            key += "_a";
            for (char c : d) key += (isalnum((unsigned char)c) ? c : '_');
        }
        r.key = key;
        std::string ct = cTypeOf(vt.name, vt.ptr);
        bool starEnd = !ct.empty() && ct.back() == '*';
        r.cParam = ct + (starEnd ? "" : " ") + (r.dims.empty() ? "_v" : "*_v");
        bool buf = e.args.size() == 3;
        r.needBuf = buf;
        auto it = sofReqs.find(r.key);
        if (it == sofReqs.end()) sofReqs[r.key] = r;
        else if (buf) it->second.needBuf = true;  // 已登记请求按需追加缓存变体
        // stringify<...> 选项块 → (stringify_t){ ... } 复合字面量（末参传入）
        long long optCompact = 0;
        for (auto& o : e.sofOpts) {
            if (o.first == "compact") optCompact = o.second;
            else throw CompileError{"stringify 选项未知键：'" + o.first + "'（当前仅支持 compact）", e.line};
        }
        std::string optLit = "(stringify_t){ .compact = " + std::to_string(optCompact) + " }";
        out << "stringify_" << r.key << (buf ? "_buf" : "") << "(";
        emitExpr(*e.args[0], true);
        if (buf) {
            out << ", ";
            emitExpr(*e.args[1], true);
            out << ", (uint64_t)(";
            emitExpr(*e.args[2], true);
            out << ")";
        }
        out << ", " << optLit << ")";
    }

    // 收集需要生成格式化器的聚合（递归按值字段；指针打地址不递归）
    void collectSofAggr(const std::string& name) {
        const Decl* sd = aggrOf(name);
        if (!sd || sd->name == "string") return;
        if (!sofAggrs.insert(sd->name).second) return;
        for (auto& f : sd->structCommon.fields) {
            if (f.synthetic || f.type.fnKind != TypeRef::FncKind::None || f.type.hasInline)
                continue;
            if (f.type.ptr > 0) continue;
            collectSofAggr(f.type.name);
        }
    }

    // 生成“把 lv 的值格式化追加到 _o”的语句（递归处理数组维度）
    // depthC：本值所在容器的缩进层级 C 表达式（美化模式换行缩进用；compact 模式忽略）
    void emitSofValue(const std::string& lv, const std::string& name, int ptr,
                      const std::vector<std::string>& dims, size_t di, int ind,
                      const std::string& depthC) {
        auto pad = [&] { for (int i = 0; i < ind; i++) out << "    "; };
        char sc = scalarClass(name);
        if (di < dims.size()) {
            // 一维 char 数组：按文本引用输出
            if (sc == 'c' && ptr == 0 && dims.size() - di == 1) {
                pad(); out << "string_append_char(_o, '\"');\n";
                pad(); out << "string_append_n(_o, (char *)(" << lv << "), strnlen("
                           << lv << ", (size_t)(" << dims[di] << ")));\n";
                pad(); out << "string_append_char(_o, '\"');\n";
                return;
            }
            std::string iv = "_i" + std::to_string(di);
            std::string childDepth = "(" + depthC + ") + 1";
            pad(); out << "string_append_char(_o, '[');\n";
            pad(); out << "for (size_t " << iv << " = 0; " << iv << " < (size_t)("
                       << dims[di] << "); " << iv << "++) {\n";
            pad(); out << "    if (" << iv << ") string_append(_o, \",\");\n";
            pad(); out << "    sc__sof_nl(_o, _opt, " << childDepth << ");\n";
            emitSofValue(lv + "[" + iv + "]", name, ptr, dims, di + 1, ind + 1, childDepth);
            pad(); out << "}\n";
            pad(); out << "if ((size_t)(" << dims[di] << ")) sc__sof_nl(_o, _opt, " << depthC << ");\n";
            pad(); out << "string_append_char(_o, ']');\n";
            return;
        }
        if (ptr > 0) {
            const Decl* sd = aggrOf(name);
            pad();
            if (sc == 'c' && ptr == 1) {
                out << "sc__sof_cstr(_o, " << lv << ");\n";          // char* → "文本"
            } else if (ptr == 1 && sd && sd->name != "string") {
                // 结构体一级指针：类型名@地址（不深递归）
                out << "sc__sof_named_ptr(_o, \"" << sd->name << "\", (const void *)("
                    << lv << "));\n";
            } else if (ptr == 1 && sc && sc != 'c') {
                // 标量一级指针：&值（nil → nil）
                out << "if (!(" << lv << ")) string_append(_o, \"nil\");\n";
                pad();
                switch (sc) {
                    case 'i': out << "else sc__sof_amp_i64(_o, (long long)(*(" << lv << ")));\n"; break;
                    case 'u': out << "else sc__sof_amp_u64(_o, (unsigned long long)(*(" << lv << ")));\n"; break;
                    case 'f': out << "else sc__sof_amp_f64(_o, (double)(*(" << lv << ")));\n"; break;
                    case 'b': out << "else sc__sof_amp_bool(_o, (unsigned char)(*(" << lv << ")));\n"; break;
                }
            } else {
                out << "sc__sof_ptr(_o, (const void *)(" << lv << "));\n";  // void*/多级/string* → 0x地址
            }
            return;
        }
        if (sc) {
            pad();
            switch (sc) {
                case 'i': out << "sc__sof_i64(_o, (long long)(" << lv << "));\n"; break;
                case 'u': out << "sc__sof_u64(_o, (unsigned long long)(" << lv << "));\n"; break;
                case 'f': out << "sc__sof_f64(_o, (double)(" << lv << "));\n"; break;
                case 'b': out << "sc__sof_bool(_o, (unsigned char)(" << lv << "));\n"; break;
                case 'c': out << "sc__sof_char(_o, (char)(" << lv << "));\n"; break;
            }
            return;
        }
        const Decl* sd = aggrOf(name);
        if (sd && sd->name == "string") { pad(); out << "sc__sof_str(_o, &(" << lv << "));\n"; return; }
        if (sd) { pad(); out << "sc__sof_" << sd->name << "(_o, &(" << lv << "), _opt, " << depthC << ");\n"; return; }
        pad(); out << "string_append(_o, \"null\");\n";
    }

    // 聚合格式化器：{"字段": 值, ...}（JSON：键加双引号；synthetic/函数指针字段跳过）
    // compact=1 紧凑单行；否则多行美化（2 空格逐层缩进）。
    void emitSofAggrBody(const Decl& sd) {
        out << "static void sc__sof_" << sd.name << "(string *_o, " << sd.name
            << " *_v, stringify_t _opt, int _depth) {\n"
            << "    string_append(_o, \"{\");\n";
        bool any = false;
        for (auto& f : sd.structCommon.fields) {
            if (f.synthetic || f.type.fnKind != TypeRef::FncKind::None) continue;
            if (any) out << "    string_append(_o, \",\");\n";
            out << "    sc__sof_nl(_o, _opt, _depth + 1);\n";
            out << "    string_append(_o, _opt.compact ? \"\\\"" << f.name << "\\\":\" : \"\\\""
                << f.name << "\\\": \");\n";
            if (f.type.hasInline) out << "    string_append(_o, \"null\");\n";
            else emitSofValue("_v->" + f.name, f.type.name, f.type.ptr,
                              f.type.arrayDims, 0, 1, "_depth + 1");
            any = true;
        }
        if (any) out << "    sc__sof_nl(_o, _opt, _depth);\n";
        out << "    string_append(_o, \"}\");\n}\n\n";
    }

    // 顶层包装：string stringify_KEY(T, stringify_t) —— 构造 string、格式化、按值返回
    void emitSofWrapper(const SofReq& r) {
        out << "static string stringify_" << r.key << "(" << r.cParam << ", stringify_t _opt) {\n"
            << "    string _s;\n    string_init(&_s);\n    string *_o = &_s;\n";
        const Decl* sd = aggrOf(r.name);
        if (r.dims.empty() && r.ptr == 1 && sd) {
            // 聚合一级指针：解引用展开内容（nil → "nil"）
            out << "    if (!_v) string_append(_o, \"nil\");\n";
            if (sd->name == "string") out << "    else sc__sof_str(_o, _v);\n";
            else out << "    else sc__sof_" << sd->name << "(_o, _v, _opt, 0);\n";
        } else {
            emitSofValue("_v", r.name, r.ptr, r.dims, 0, 1, "0");
        }
        out << "    return _s;\n}\n\n";
        if (!r.needBuf) return;
        // 缓存变体：char *stringify_KEY_buf(T, 缓存, 大小, stringify_t) —— 截断拷贝进缓存，返回缓存首址
        out << "static char *stringify_" << r.key << "_buf(" << r.cParam
            << ", char *_buf, uint64_t _n, stringify_t _opt) {\n"
            << "    if (!_buf || !_n) return _buf;\n"
            << "    string _s = stringify_" << r.key << "(_v, _opt);\n"
            << "    uint64_t _l = _s.size < _n - 1 ? _s.size : _n - 1;\n"
            << "    if (_l && _s.data) memcpy(_buf, _s.data, (size_t)_l);\n"
            << "    _buf[_l] = 0;\n"
            << "    string_drop(&_s);\n"
            << "    return _buf;\n}\n\n";
    }

    // string 格式化支撑代码回填：原语 + 聚合格式化器 + 顶层包装
    void emitSofHelpers() {
        if (sofReqs.empty()) return;
        // 头文件模式：先把支撑代码生成到临时流，包上 include guard 后存入
        // sofHeaderOut，并在当前位置向 .c 输出 #include；否则直接内联进 .c。
        std::ostringstream savedOut;
        const bool toHeader = !sofHeaderName.empty();
        if (toHeader) {
            savedOut = std::move(out);
            out = std::ostringstream();
            std::string guard;
            for (char c : sofHeaderName) guard += std::isalnum((unsigned char)c)
                ? (char)std::toupper((unsigned char)c) : '_';
            out << "#ifndef " << guard << "\n#define " << guard << "\n"
                << "/* 由 scc 生成：stringify 关键字按类型生成的 JSON 格式化器。\n"
                   "   需在 string 类型、io 的 stringify_t 与各结构体 typedef 之后 #include\n"
                   "   （由生成的 .c 自动完成）。 */\n";
        }
        out << "/* ---- stringify 关键字支撑：格式化原语与按类型生成的格式化器（JSON） ---- */\n"
            << "static inline void sc__sof_i64(string *_o, long long _v) {\n"
            << "    char _b[24]; snprintf(_b, sizeof(_b), \"%lld\", _v); string_append(_o, _b); }\n"
            << "static inline void sc__sof_u64(string *_o, unsigned long long _v) {\n"
            << "    char _b[24]; snprintf(_b, sizeof(_b), \"%llu\", _v); string_append(_o, _b); }\n"
            << "static inline void sc__sof_f64(string *_o, double _v) {\n"
            << "    char _b[40]; snprintf(_b, sizeof(_b), \"%g\", _v); string_append(_o, _b); }\n"
            << "static inline void sc__sof_bool(string *_o, unsigned char _v) {\n"
            << "    string_append(_o, _v ? \"true\" : \"false\"); }\n"
            << "static inline void sc__sof_char(string *_o, char _v) {\n"
            << "    string_append_char(_o, '\\''); string_append_char(_o, _v); string_append_char(_o, '\\''); }\n"
            << "static inline void sc__sof_cstr(string *_o, const char *_v) {\n"
            << "    if (!_v) { string_append(_o, \"nil\"); return; }\n"
            << "    string_append_char(_o, '\"'); string_append(_o, (char *)_v); string_append_char(_o, '\"'); }\n"
            << "static inline void sc__sof_ptr(string *_o, const void *_v) {\n"
            << "    if (!_v) { string_append(_o, \"nil\"); return; }\n"
            << "    char _b[24]; snprintf(_b, sizeof(_b), \"0x%llx\", (unsigned long long)(uintptr_t)_v);\n"
            << "    string_append(_o, _b); }\n"
            << "static inline void sc__sof_named_ptr(string *_o, const char *_tn, const void *_v) {\n"
            << "    if (!_v) { string_append(_o, \"nil\"); return; }\n"
            << "    char _b[28]; snprintf(_b, sizeof(_b), \"@0x%llx\", (unsigned long long)(uintptr_t)_v);\n"
            << "    string_append_char(_o, '\"'); string_append(_o, (char *)_tn);\n"
            << "    string_append(_o, _b); string_append_char(_o, '\"'); }\n"
            << "static inline void sc__sof_amp_i64(string *_o, long long _v) {\n"
            << "    char _b[28]; snprintf(_b, sizeof(_b), \"\\\"&%lld\\\"\", _v); string_append(_o, _b); }\n"
            << "static inline void sc__sof_amp_u64(string *_o, unsigned long long _v) {\n"
            << "    char _b[28]; snprintf(_b, sizeof(_b), \"\\\"&%llu\\\"\", _v); string_append(_o, _b); }\n"
            << "static inline void sc__sof_amp_f64(string *_o, double _v) {\n"
            << "    char _b[44]; snprintf(_b, sizeof(_b), \"\\\"&%g\\\"\", _v); string_append(_o, _b); }\n"
            << "static inline void sc__sof_amp_bool(string *_o, unsigned char _v) {\n"
            << "    string_append(_o, _v ? \"\\\"&true\\\"\" : \"\\\"&false\\\"\"); }\n"
            << "static inline void sc__sof_str(string *_o, string *_v) {\n"
            << "    string_append_char(_o, '\"');\n"
            << "    if (_v->data) string_append_n(_o, _v->data, _v->size);\n"
            << "    string_append_char(_o, '\"'); }\n"
            << "static inline void sc__sof_nl(string *_o, stringify_t _opt, int _depth) {\n"
            << "    if (_opt.compact) return;\n"
            << "    string_append_char(_o, '\\n');\n"
            << "    for (int _i = 0; _i < _depth; _i++) string_append(_o, \"  \"); }\n\n";
        // 聚合传递闭包（仅按需：按值聚合 / 一维数组元素 / 一级指针解引用）
        for (auto& kv : sofReqs) {
            const SofReq& r = kv.second;
            if (!r.dims.empty()) { if (r.ptr == 0) collectSofAggr(r.name); }
            else if (r.ptr <= 1) collectSofAggr(r.name);
        }
        for (auto& n : sofAggrs)
            out << "static void sc__sof_" << n << "(string *_o, " << n << " *_v, stringify_t _opt, int _depth);\n";
        if (!sofAggrs.empty()) out << "\n";
        for (auto& n : sofAggrs) emitSofAggrBody(*aggrs.at(n));
        for (auto& kv : sofReqs) emitSofWrapper(kv.second);
        if (toHeader) {
            out << "#endif\n";
            sofHeaderOut = out.str();
            out = std::move(savedOut);
            out << "#include \"" << sofHeaderName << "\"\n\n";
        }
    }

    // 查找顶层方法：类型名（穿透别名）→ 方法 Decl（未找到 nullptr）
    const Decl* findMethod(std::string typeName, const std::string& m) const {
        for (int i = 0; i < 8 && !typeName.empty(); i++) {
            auto it = methods.find(typeName);
            if (it != methods.end()) {
                auto mit = it->second.find(m);
                if (mit != it->second.end()) return mit->second;
            }
            auto al = aliases.find(typeName);
            if (al == aliases.end()) return nullptr;
            typeName = al->second;
        }
        return nullptr;
    }

    // T() 类型伪调用：被调对象是聚合类型名（无参、未被变量遮蔽）
    // → 堆构造糖，返回解析后的聚合类型 Decl（否则 nullptr）
    const Decl* typeCallee(const Expr& call) const {
        if (!call.a || call.a->kind != Expr::Ident || !call.args.empty()) return nullptr;
        const std::string& n = call.a->text;
        if (localsT.count(n) || globalsT.count(n)) return nullptr;
        return aggrOf(n);
    }

    static bool hasFieldDefaults(const Decl* d) {
        for (auto& f : d->structCommon.fields) if (f.init) return true;
        return false;
    }

    // 表达式的轻量类型推断（仅覆盖方法调用需要的场景）
    bool exprVType(const Expr& e, VType& vt) const {
        switch (e.kind) {
            case Expr::Ident: {
                auto it = localsT.find(e.text);
                if (it != localsT.end()) { vt = it->second; return true; }
                it = globalsT.find(e.text);
                if (it != globalsT.end()) { vt = it->second; return true; }
                return false;
            }
            case Expr::Member: {
                VType base;
                if (!exprVType(*e.a, base)) return false;
                const Decl* sd = aggrOf(base.name);
                if (!sd) return false;
                const std::string fn = memberFieldName(*sd, e.text);
                for (auto& f : sd->structCommon.fields)
                    if (f.name == fn) {
                        vt = {f.type.name, f.type.ptr, (int)f.type.arrayDims.size()};
                        return true;
                    }
                return false;
            }
            case Expr::Index:
                if (!exprVType(*e.a, vt)) return false;
                if (vt.arr) vt.arr--; else if (vt.ptr) vt.ptr--;
                return true;
            case Expr::Unary:
                if (e.op == "*") {
                    if (!exprVType(*e.a, vt)) return false;
                    if (vt.ptr) vt.ptr--;
                    return true;
                }
                if (e.op == "&") {
                    if (!exprVType(*e.a, vt)) return false;
                    vt.ptr++;
                    return true;
                }
                return false;
            case Expr::Cast:
                vt = {e.op, e.castPtr, 0};
                return true;
            case Expr::Binary:
                // 赋值表达式的结果类型 = 左操作数类型（支持 (p = T())->m() 等）
                if (!e.op.empty() && e.op.back() == '='
                    && e.op != "==" && e.op != "!=" && e.op != "<=" && e.op != ">=")
                    return exprVType(*e.a, vt);
                return false;
            case Expr::Call:
                // T() 伪调用结果类型：T&（使链式方法调用可推断）
                if (const Decl* td = typeCallee(e)) {
                    vt = {td->name, 1, 0};
                    return true;
                }
                // string(值[, 缓存, 大小]) 结果类型：string / char*（使声明初值/方法调用可推断）
                if (e.a && e.a->kind == Expr::Ident && e.a->text == "string" && !e.args.empty()
                    && !localsT.count("string") && !globalsT.count("string")) {
                    if (e.args.size() == 3) vt = {"char", 1, 0};
                    else vt = {"string", 0, 0};
                    return true;
                }
                return false;
            default: return false;
        }
    }

    // 若 m 是成员访问且该成员是函数指针字段，返回字段指针（否则 nullptr）
    const Field* callableField(const Expr& m) const {
        if (m.kind != Expr::Member) return nullptr;
        VType base;
        if (!exprVType(*m.a, base)) return nullptr;
        const Decl* sd = aggrOf(base.name);
        if (!sd) return nullptr;
        for (auto& f : sd->structCommon.fields)
            if (f.name == m.text && f.type.fnKind != TypeRef::FncKind::None) return &f;
        return nullptr;
    }

    // ---------------- 缺参调用 0 补全 ----------------
    // 允许实参少于形参：缺少的参数按类型补默认值
    //   指针/数组/函数指针 → NULL；聚合按值 → (T){0}；其余标量 → 0
    void emitDefaultArg(const Field& p) {
        if (p.type.fnKind != TypeRef::FncKind::None) { out << "NULL"; return; }
        std::string base; int ptr;
        resolveType(p.type, base, ptr);
        if (ptr > 0 || !p.type.arrayDims.empty()) { out << "NULL"; return; }
        if (const Decl* sd = aggrOf(p.type.name)) {
            out << "(" << sd->name << "){0}";
            return;
        }
        out << "0";
    }

    // 查询被调目标的形参表（用于缺参补全）：
    //   函数指针变量/参数（内联签名或命名函数类型）→ 顶层 fnc → rpc 包装
    const std::vector<Field>* calleeParams(const std::string& n) const {
        // 变量（含参数）优先：遮蔽同名函数
        const bool isLocal = inFunc && localsT.count(n);
        if (isLocal || globalsT.count(n)) {
            auto& fnVars = isLocal ? fnVarsL : fnVarsG;
            auto fv = fnVars.find(n);
            if (fv != fnVars.end()) return &fv->second->structCommon.fields;
            // 命名函数类型的变量：var cb&: my_fnc_type
            std::string tn = (isLocal ? localsT : globalsT).at(n).name;
            for (int i = 0; i < 8 && !tn.empty(); i++) {
                auto it = funcTypes.find(tn);
                if (it != funcTypes.end()) return &it->second->structCommon.fields;
                auto al = aliases.find(tn);
                if (al == aliases.end()) break;
                tn = al->second;
            }
            return nullptr;
        }
        auto f = funcs.find(n);
        if (f != funcs.end()) {
            const Decl* sig = f->second;
            if (!sig->funcTypeName.empty()) {  // fnc name -> func_type：签名从类型展开
                auto it = funcTypes.find(sig->funcTypeName);
                if (it == funcTypes.end()) return nullptr;
                sig = it->second;
            }
            return &sig->structCommon.fields;
        }
        auto r = rpcs.find(n);
        if (r != rpcs.end()) return &r->second->structCommon.fields;
        return nullptr;
    }


    // ---------------- 表达式 ----------------
    void emitExpr(const Expr& e, bool top = false) {
        switch (e.kind) {
            case Expr::IntLit: case Expr::FloatLit:
            case Expr::StrLit: case Expr::CharLit:
                out << e.text;
                break;
            case Expr::Ident:
                if (e.text == "this") out << "_this";      // 方法内接收者
                else if (e.text == "nil") out << "NULL";   // 空指针常量
                else if (curRpc && rpcParams.count(e.text))
                    out << "_p->" << e.text;               // rpc 实际函数内：参数即结构体成员
                else out << e.text;                        // true/false 由 stdbool.h 提供
                break;
            case Expr::Unary:
                out << e.op;
                out << "(";
                emitExpr(*e.a);
                out << ")";
                break;
            case Expr::PostUnary:
                emitExpr(*e.a);
                out << e.op;
                break;
            case Expr::Binary:
                if (!top) out << "(";
                emitExpr(*e.a);
                out << " " << e.op << " ";
                emitExpr(*e.b);
                if (!top) out << ")";
                break;
            case Expr::Ternary:
                if (!top) out << "(";
                emitExpr(*e.a);
                out << " ? ";
                emitExpr(*e.b);
                out << " : ";
                emitExpr(*e.c);
                if (!top) out << ")";
                break;
            case Expr::Call: {
                // base/prev/next 内置函数：链表结构体字段导航
                if (e.a && e.a->kind == Expr::Ident && !localsT.count(e.a->text) &&
                    !globalsT.count(e.a->text) && !funcs.count(e.a->text)) {
                    const std::string kw = e.a->text;
                    if ((kw == "base" || kw == "prev" || kw == "next") && e.args.size() == 1) {
                        const Expr* src = &*e.args[0];
                        bool typed = src->kind == Expr::Cast;
                        if (typed) src = src->a.get();
                        VType vt;
                        bool hasVt = exprVType(*src, vt);
                        const Decl* sd = hasVt ? aggrOf(vt.name) : nullptr;

                        auto emitRawSrc = [&]() {
                            if (vt.ptr > 0 || vt.arr > 0) emitExpr(*src, true);
                            else {
                                out << "&(";
                                emitExpr(*src, true);
                                out << ")";
                            }
                        };

                        // base(o: T) 直接把节点首址重解释为 T*
                        auto emitBaseCast = [&](const Expr& castExpr) {
                            out << "((" << mapBase(castExpr.text);
                            for (int i = 0; i < castExpr.castPtr + 1; i++) out << "*";
                            out << ")(";
                            emitRawSrc();
                            out << "))";
                        };

                        if (kw == "base") {
                            if (typed) { emitBaseCast(*e.args[0]); break; }
                            if (hasVt && sd && sd->linked) {
                                const Field* rf = firstRealField(sd);
                                if (rf) {
                                    if (vt.ptr > 0 || vt.arr > 0) {
                                        out << "((void *)&((";
                                        emitExpr(*src, true);
                                        out << ")->" << rf->name << "))";
                                    } else {
                                        out << "((void *)&((";
                                        emitExpr(*src, true);
                                        out << ")." << rf->name << "))";
                                    }
                                    break;
                                }
                            }
                            out << "((void *)";
                            emitRawSrc();
                            out << ")";
                            break;
                        }

                        if (kw == "prev" || kw == "next") {
                            // _prev 在偏移 0，_next 在偏移 sizeof(void*)
                            const char* off = kw == "prev" ? "0" : "sizeof(void *)";
                            // prev：边界安全前驱（head→NULL），经 adt 契约 chain_prev
                            if (kw == "prev") {
                                if (typed) {
                                    const Expr& c = *e.args[0];
                                    out << "((" << mapBase(c.text);
                                    for (int i = 0; i < c.castPtr + 1; i++) out << "*";
                                    out << ")chain_prev(";
                                    emitRawSrc();
                                    out << "))";
                                    break;
                                }
                                out << "((void *)chain_prev(";
                                emitRawSrc();
                                out << "))";
                                break;
                            }
                            if (typed) {
                                // next(o: T) → 读链接字段并转为 T*
                                const Expr& c = *e.args[0];
                                out << "((" << mapBase(c.text);
                                for (int i = 0; i < c.castPtr + 1; i++) out << "*";
                                out << ")*(void **)((char *)(";
                                emitRawSrc();
                                out << ") + " << off << "))";
                                break;
                            }
                            out << "((void *)*(void **)((char *)(";
                            emitRawSrc();
                            out << ") + " << off << "))";
                            break;
                        }
                    }
                }
                // 类型伪调用糖：T() → 堆构造 T__new()（malloc + 默认值 + init）
                if (const Decl* td = typeCallee(e)) {
                    out << td->name << "__new()";
                    break;
                }
                // print 关键字：C 风格日志输出（io 子项目 print，未被同名定义遮蔽时）
                if (e.a->kind == Expr::Ident && e.a->text == "print"
                    && !localsT.count("print") && !globalsT.count("print") && !funcs.count("print")) {
                    if (e.args.empty())
                        throw CompileError{"print 需要格式串实参", e.line};
                    out << "print(";
                    for (size_t i = 0; i < e.args.size(); i++) {
                        if (i) out << ", ";
                        emitExpr(*e.args[i], true);
                    }
                    out << ")";
                    break;
                }
                // stringify 格式化关键字：stringify(值) → adt string；stringify(值, 缓存, 大小) → char*
                // （无参 string() 走上面的 T() 堆构造糖；被同名定义遮蔽时按普通调用）
                if (e.a->kind == Expr::Ident && e.a->text == "stringify" && !e.args.empty()
                    && !localsT.count("stringify") && !globalsT.count("stringify") && !funcs.count("stringify")) {
                    emitStrofCall(e);
                    break;
                }
                // 顶层方法调用糖：o.m(...) / p->m(...) → T_m(&o/p, ...)
                if (e.a->kind == Expr::Member && !callableField(*e.a)) {
                    VType base;
                    if (exprVType(*e.a->a, base) && base.arr == 0 && base.ptr <= 1) {
                        if (const Decl* md = findMethod(base.name, e.a->text)) {
                            out << md->name << "(";   // 修饰名 T_m
                            if (e.a->op == ".") out << "&";
                            emitExpr(*e.a->a);
                            for (auto& a : e.args) {
                                out << ", ";
                                emitExpr(*a, true);
                            }
                            // 缺参 0 补全（接收者已占首参，后续皆需逗号）
                            for (size_t i = e.args.size(); i < md->structCommon.fields.size(); i++) {
                                out << ", ";
                                emitDefaultArg(md->structCommon.fields[i]);
                            }
                            out << ")";
                            break;
                        }
                    }
                }
                // 函数指针字段/变量/顶层函数调用（含缺参 0 补全）
                const Field* mf = callableField(*e.a);
                const std::vector<Field>* params = nullptr;
                if (mf) params = &mf->type.structCommon.fields;
                else if (e.a->kind == Expr::Ident && e.a->text != "this")
                    params = calleeParams(e.a->text);
                emitExpr(*e.a);
                out << "(";
                for (size_t i = 0; i < e.args.size(); i++) {
                    if (i) out << ", ";
                    emitExpr(*e.args[i], true);
                }
                if (params)
                    for (size_t i = e.args.size(); i < params->size(); i++) {
                        if (i) out << ", ";
                        emitDefaultArg((*params)[i]);
                    }
                out << ")";
                break;
            }
            case Expr::Index:
                emitExpr(*e.a);
                out << "[";
                emitExpr(*e.b, true);
                out << "]";
                break;
            case Expr::Member: {
                // prev 上下文关键字（链表结构体）：边界安全前驱 → head 返回 NULL
                if (e.text == "prev") {
                    VType base;
                    if (exprVType(*e.a, base)) {
                        const Decl* sd = aggrOf(base.name);
                        if (sd && sd->linked) {
                            out << "((void *)chain_prev(";
                            if (e.op == "->") emitExpr(*e.a);
                            else { out << "&("; emitExpr(*e.a, true); out << ")"; }
                            out << "))";
                            break;
                        }
                    }
                }
                emitExpr(*e.a);
                std::string fn = e.text;
                if (e.text == "next") {
                    // 上下文关键字：链表结构体上 next 映射到内置 _next（rear 的 _next=NULL 自然终止）
                    VType base;
                    if (exprVType(*e.a, base)) {
                        const Decl* sd = aggrOf(base.name);
                        if (sd && sd->linked) fn = "_next";
                    }
                }
                out << e.op << fn;
                break;
            }
            case Expr::Sizeof: {
                out << "sizeof(";
                // 若内层是单纯标识符且是 sc 内置类型名，做类型映射再输出
                if (e.a && e.a->kind == Expr::Ident) {
                    const std::string mapped = mapBase(e.a->text);
                    if (mapped != e.a->text) { out << mapped; }
                    else emitExpr(*e.a, true);
                } else {
                    emitExpr(*e.a, true);
                }
                out << ")";
                break;
            }
            case Expr::Offsetof:
                out << "offsetof(" << e.text << ", " << e.op << ")";
                break;
            case Expr::Cast: {
                // (expr: type&) → ((T*)(expr))
                out << "((" << mapBase(e.op);
                for (int i = 0; i < e.castPtr; i++) out << "*";
                out << ")(";
                emitExpr(*e.a, true);
                out << "))";
                break;
            }
            case Expr::InitList: {
                out << "{";
                for (size_t i = 0; i < e.args.size(); i++) {
                    if (i) out << ", ";
                    emitExpr(*e.args[i], true);
                }
                out << "}";
                break;
            }
            case Expr::FncLit: {
                // 匿名函数字面量（不捕获外层变量）：首次遇到时提升为顶层 static
                // 函数（定义写入 lambdaOut，在所有函数体前回填），表达式处输出函数名。
                auto it = lambdaNames.find(&e);
                if (it == lambdaNames.end()) {
                    std::string name = "sc__lambda_" + std::to_string(lambdaSeq++);
                    lambdaNames.emplace(&e, name);
                    emitLambdaDef(e, name);
                    out << name;
                } else {
                    out << it->second;
                }
                break;
            }
        }
    }

    // ---------------- 语句 ----------------
    void emitVarDecls(const std::vector<Field>& decls, bool asConst,
                      bool isStatic = false, bool isTls = false) {
        for (auto& f : decls) {
            // 无类型 var/let（var x: = 初值）：依据初值字面量推断默认类型；
            // 推断成功时跳过 emitDeclarator，按推断结果输出声明并登记轻量类型
            std::string infBase; int infPtr = 0; bool inferred = false;
            if (f.init && f.type.name.empty() && !f.type.hasInline
                && f.type.fnKind == TypeRef::FncKind::None
                && f.type.ptr == 0 && f.type.arrayDims.empty())
                inferred = inferLiteralType(*f.init, infBase, infPtr);

            if (inferred) (inFunc ? localsT : globalsT)[f.name] = VType{infBase, infPtr, 0};
            else regVar(f);
            indent();
            if (isTls) out << "static TLS ";   // tls：必为 static（C 规范），TLS 宏见 platform.h
            else if (isStatic) out << "static ";
            if (inferred) {
                if (asConst) out << "const ";
                out << mapBase(infBase) << " ";
                for (int i = 0; i < infPtr; i++) out << "*";
                out << f.name;
            } else emitDeclarator(f, asConst);
            if (f.init) {
                out << " = ";
                emitExpr(*f.init, true);
            } else {
                const Decl* sd = aggrOf(f.type.name);
                if (sd && (sd->kind == Decl::StructD || sd->kind == Decl::UnionD)) {
                    // tls 为 static 存储期：初始化须常量表达式，不能调 __default()
                    if (!isTls && sd->kind == Decl::StructD && hasFieldDefaults(sd)) {
                        out << " = " << sd->name << "__default()";
                    } else {
                        out << " = {0}";
                    }
                }
            }
            out << ";\n";
            // 声明即构造：函数内无初值的结构变量，若类型有无参 init 方法则自动调用
            // （tls 除外：static 存储期只初始化一次，此处会每次进函数重执行）
            if (inFunc && !isTls && !f.init && f.type.ptr == 0 && f.type.arrayDims.empty()
                && !f.type.hasInline) {
                const Decl* im = findMethod(f.type.name, "init");
                if (im && im->structCommon.fields.empty()) {
                    indent();
                    out << im->name << "(&" << f.name << ");\n";
                }
            }
        }
    }

    // 记录变量的轻量类型（函数内→局部表，否则→全局表）
    void regVar(const Field& f) {
        VType vt{f.type.name, f.type.ptr, (int)f.type.arrayDims.size()};
        (inFunc ? localsT : globalsT)[f.name] = vt;
        if (!f.type.arrayDims.empty())
            (inFunc ? varDimsL : varDimsG)[f.name] = f.type.arrayDims;
        // 函数指针变量：额外记录内联签名（缺参补全查询用）
        if (f.type.fnKind != TypeRef::FncKind::None)
            (inFunc ? fnVarsL : fnVarsG)[f.name] = &f.type;
    }

    void emitStmts(const std::vector<StmtPtr>& stmts) {
        for (auto& s : stmts) emitStmt(*s);
    }

    void emitStmt(const Stmt& s) {
        // 行号映射：指定了源文件时输出 #line 指令（调试器断点/单步/堆栈
        // 直接落在 .sc 源码）；否则输出注释供人工对照
        if (s.line > 0) {
            if (!srcFile.empty()) {
                out << "#line " << s.line << " \"" << srcFile << "\"\n";
            } else {
                indent();
                out << "/* line " << s.line << " */\n";
            }
        }
        
        switch (s.kind) {
            case Stmt::ExprS:
                indent();
                emitExpr(*s.expr, true);
                out << ";\n";
                break;
            case Stmt::VarS: emitVarDecls(s.decls, false); break;
            case Stmt::LetS: emitVarDecls(s.decls, true); break;
            case Stmt::TlsS: emitVarDecls(s.decls, false, false, true); break;
            case Stmt::ReturnS:
                indent();
                if (curRpc) {
                    // rpc 实际函数：返回值写入结构体首个默认成员 _
                    if (s.expr && rpcHasRet(*curRpc)) {
                        out << "_p->_ = ";
                        emitExpr(*s.expr, true);
                        out << "; return;\n";
                    } else out << "return;\n";
                    break;
                }
                out << "return";
                if (s.expr) { out << " "; emitExpr(*s.expr, true); }
                out << ";\n";
                break;
            case Stmt::BreakS: indent(); out << "break;\n"; break;
            case Stmt::ContinueS: indent(); out << "continue;\n"; break;
            case Stmt::IfS:
                indent();
                out << "if (";
                emitExpr(*s.expr, true);
                out << ") {\n";
                depth++; emitStmts(s.body); depth--;
                indent(); out << "}";
                if (!s.elseBody.empty()) {
                    // else if 折叠
                    if (s.elseBody.size() == 1 && s.elseBody[0]->kind == Stmt::IfS) {
                        out << " else ";
                        emitElseIf(*s.elseBody[0]);
                    } else {
                        out << " else {\n";
                        depth++; emitStmts(s.elseBody); depth--;
                        indent(); out << "}\n";
                    }
                } else out << "\n";
                break;
            case Stmt::WhileS:
                indent();
                out << "while (";
                emitExpr(*s.expr, true);
                out << ") {\n";
                depth++; emitStmts(s.body); depth--;
                indent(); out << "}\n";
                break;
            case Stmt::DoWhileS:
                indent(); out << "do {\n";
                depth++; emitStmts(s.body); depth--;
                indent(); out << "} while (";
                emitExpr(*s.expr, true);
                out << ");\n";
                break;
            case Stmt::ForS:
                indent();
                out << "for (";
                if (s.forInit) emitExpr(*s.forInit, true);
                out << "; ";
                if (s.forCond) emitExpr(*s.forCond, true);
                out << "; ";
                if (s.forStep) emitExpr(*s.forStep, true);
                out << ") {\n";
                depth++; emitStmts(s.body); depth--;
                indent(); out << "}\n";
                break;
            case Stmt::CaseS:
                indent();
                out << "switch (";
                emitExpr(*s.expr, true);
                out << ") {\n";
                depth++;
                for (auto& arm : s.caseArms) {
                    if (arm.labels.empty()) {
                        indent(); out << "default:\n";
                    } else {
                        for (auto& lab : arm.labels) {
                            indent();
                            out << "case ";
                            emitExpr(*lab, true);
                            out << ":\n";
                        }
                    }
                    indent(); out << "{\n";
                    depth++;
                    emitStmts(arm.body);
                    if (!arm.through) {
                        indent(); out << "break;\n";
                    }
                    depth--;
                    indent(); out << "}\n";
                }
                depth--;
                indent(); out << "}\n";
                break;
            case Stmt::GotoS:
                indent(); out << "goto " << s.text << ";\n";
                break;
            case Stmt::LabelS:
                indent(); out << s.text << ":\n";
                depth++; emitStmts(s.body); depth--;
                break;
            case Stmt::DeclS:
                emitTypeDecl(*s.decl);
                break;
            case Stmt::RunS:
                emitRunStmt(s);
                break;
            case Stmt::WaitS:
                emitWaitStmt(s);
                break;
        }
    }

    // run 语句 → 装填 rpc 参数结构体 + thread_run 调用
    //   run worker(a, b), &t →
    //   { struct worker _rp = {0}; _rp.x = a; ...;
    //     thread_run((void (*)(void *))worker_rpc, &_rp, sizeof(_rp), (thread **)(&t)); }
    // thread_run 在 m_impl 中实现：单次 alloc(sizeof(thread)+sizeof(参数)+实现私有区)，
    // 参数 memcpy 到 thread 对象紧随位置；出参为空时 detach 自释放。
    // run 语句 → 装填 rpc 参数结构体 + 线程原语调用（第二参按类型静态分派）
    //   run worker(a, b), &t →
    //   { struct worker _rp = {0}; _rp.x = a; ...;
    //     thread_run((void (*)(void *))worker_rpc, &_rp, sizeof(_rp), (thread **)(&t)); }
    //   run worker(a, b), p（p 为 pool 对象或指针，对象自动取地址）→
    //     pool_run(&p, (void (*)(void *))worker_rpc, &_rp, sizeof(_rp));
    // thread_run 在 m_impl 中实现：单次 alloc(sizeof(thread)+sizeof(参数)+实现私有区)，
    // 参数 memcpy 到 thread 对象紧随位置；出参为空时 detach 自释放。
    // pool_run 同哲学：参数拷贝入任务节点，调用点无需保活。
    void emitRunStmt(const Stmt& s) {
        const Expr& call = *s.expr;
        auto it = rpcs.find(call.a->text);
        if (it == rpcs.end())
            throw CompileError{"run 的目标必须是 rpc: " + call.a->text, s.line};
        const Decl* r = it->second;
        if (call.args.size() > r->structCommon.fields.size())
            throw CompileError{"rpc 实参数量超出: " + r->name + " 期望至多 " +
                               std::to_string(r->structCommon.fields.size()) + " 个", s.line};
        if (!aggrOf("thread"))
            throw CompileError{"run 语句需要 thread 类型，请先 inc m.sc", s.line};
        // run<stack:N, prio:M> 选项：透传给 C（stack=u4 栈字节数，prio=u1 优先级；
        //   0 表示由 C 取默认）。键在此校验，值越界（u4/u1）报错。
        long long optStack = 0, optPrio = 0;
        bool hasOpts = !s.runOpts.empty();
        for (auto& o : s.runOpts) {
            if (o.first == "stack") {
                if (o.second < 0 || o.second > 0xFFFFFFFFLL)
                    throw CompileError{"run 选项 stack 超出 u4 范围", s.line};
                optStack = o.second;
            } else if (o.first == "prio") {
                if (o.second < 0 || o.second > 0xFF)
                    throw CompileError{"run 选项 prio 超出 u1 范围", s.line};
                optPrio = o.second;
            } else {
                throw CompileError{"run 选项未知键：'" + o.first +
                                   "'（当前仅支持 stack、prio）", s.line};
            }
        }
        // 第二参类型分派：pool（对象/指针）→ 入池；其余 → thread 出参
        bool toPool = false;
        if (s.forInit) {
            VType vt;
            if (exprVType(*s.forInit, vt) && vt.arr == 0 && vt.ptr <= 1) {
                const Decl* sd = aggrOf(vt.name);
                if (sd && sd->name == "pool") toPool = true;
            }
        }
        // pool 工作线程为预创建，逐任务的 stack/prio 不适用 → 显式报错
        if (toPool && hasOpts)
            throw CompileError{"run 选项（stack/prio）不适用于 pool 目标", s.line};
        indent(); out << "{\n";
        depth++;
        indent(); out << "struct " << r->name << " _rp = {0};\n";
        for (size_t i = 0; i < call.args.size(); i++) {
            indent();
            out << "_rp." << r->structCommon.fields[i].name << " = ";
            emitExpr(*call.args[i], true);
            out << ";\n";
        }
        indent();
        if (toPool) {
            out << "pool_run(";
            emitAutoAddr(*s.forInit);
            out << ", (void (*)(void *))" << r->name << "_rpc, &_rp, sizeof(_rp));\n";
        } else {
            out << "thread_run((void (*)(void *))" << r->name << "_rpc, &_rp, sizeof(_rp), ";
            if (s.forInit) {
                out << "(thread **)(";
                emitExpr(*s.forInit, true);
                out << ")";
            } else out << "NULL";
            out << ", (uint32_t)" << optStack << "u, (uint8_t)" << optPrio << "u);\n";
        }
        depth--;
        indent(); out << "}\n";
    }

    // wait 语句 → cond_wait 调用（cond_wait 在 m_impl 中实现）
    //   wait c, m            → cond_wait(&c, &m, 0, 0);        无限等待
    //   wait c, m, ns, s     → cond_wait(&c, &m, ns, s);       超时等待
    // cond/mutex 实参可为对象或指针：对象自动取地址（与方法调用糖一致）
    void emitWaitStmt(const Stmt& s) {
        if (!aggrOf("cond") || !aggrOf("mutex"))
            throw CompileError{"wait 语句需要 cond/mutex 类型，请先 inc m.sc", s.line};
        indent();
        out << "cond_wait(";
        emitAutoAddr(*s.expr);     // cond
        out << ", ";
        emitAutoAddr(*s.forInit);  // mutex
        out << ", ";
        if (s.forCond) emitExpr(*s.forCond, true); else out << "0";
        out << ", ";
        if (s.forStep) emitExpr(*s.forStep, true); else out << "0";
        out << ");\n";
    }

    // 对象 → &(对象)，指针 → 原样（按轻量类型推断；不可推断时默认按对象取地址）
    void emitAutoAddr(const Expr& e) {
        VType vt;
        bool isPtr = exprVType(e, vt) && vt.ptr > 0 && vt.arr == 0;
        if (!isPtr) out << "&(";
        emitExpr(e, true);
        if (!isPtr) out << ")";
    }

    void emitElseIf(const Stmt& s) { // "} else " 之后接 if，不缩进首行
        out << "if (";
        emitExpr(*s.expr, true);
        out << ") {\n";
        depth++; emitStmts(s.body); depth--;
        indent(); out << "}";
        if (!s.elseBody.empty()) {
            if (s.elseBody.size() == 1 && s.elseBody[0]->kind == Stmt::IfS) {
                out << " else ";
                emitElseIf(*s.elseBody[0]);
            } else {
                out << " else {\n";
                depth++; emitStmts(s.elseBody); depth--;
                indent(); out << "}\n";
            }
        } else out << "\n";
    }

    // ---------------- 类型定义 ----------------
    void emitFieldList(const std::vector<Field>& fields) {
        depth++;
        for (auto& f : fields) {
            indent();
            emitDeclarator(f);
            out << ";\n";
        }
        depth--;
    }

    // 收集 sc 内按值（非指针、非内联）直接引用的命名聚合体依赖
    void collectValueDeps(const StructCommon& sc, std::vector<const Decl*>& deps) {
        for (auto& f : sc.fields) {
            if (f.type.ptr == 0 && !f.type.hasInline) {
                if (const Decl* dep = aggrOf(f.type.name)) deps.push_back(dep);
            }
            if (f.type.hasInline) collectValueDeps(f.type.structCommon, deps);
        }
    }

    // 输出聚合体完整定义，先递归前移其按值依赖（定义顺序无关）。
    // 标记先于递归，天然防御非法的相互按值包含（由语义阶段另行报错）。
    void emitAggrWithDeps(const Decl& d) {
        if (emittedAggr.count(&d)) return;
        emittedAggr.insert(&d);
        std::vector<const Decl*> deps;
        collectValueDeps(d.structCommon, deps);
        for (auto* dep : deps)
            if ((dep->kind == Decl::StructD || dep->kind == Decl::UnionD)
                && !dep->external && !dep->isRpc)
                emitAggrWithDeps(*dep);
        emitTypeDecl(d);
    }

    void emitTypeDecl(const Decl& d) {
        switch (d.kind) {
            case Decl::EnumD:
                indent();
                out << "typedef enum { /* base: " << mapBase(d.structCommon.type->name) << " */\n";
                depth++;
                for (size_t i = 0; i < d.structCommon.fields.size(); i++) {
                    indent();
                    out << d.structCommon.fields[i].name;
                    if (d.structCommon.fields[i].init) {
                        out << " = ";
                        emitExpr(*d.structCommon.fields[i].init, true);
                    }
                    if (i + 1 < d.structCommon.fields.size()) out << ",";
                    out << "\n";
                }
                depth--;
                indent();
                out << "} " << d.name << ";\n\n";
                break;
            case Decl::StructD:
            case Decl::UnionD:
                indent();
                out << "typedef " << (d.kind == Decl::UnionD ? "union" : "struct")
                    << " " << d.name << " {\n";
                emitFieldList(d.structCommon.fields);
                indent();
                out << "} " << d.name << ";\n\n";
                if (d.kind == Decl::StructD && hasFieldDefaults(&d)) {
                    indent();
                    out << "static inline " << d.name << " " << d.name << "__default(void) {\n";
                    depth++;
                    indent(); out << d.name << " _v = {0};\n";
                    for (auto& f : d.structCommon.fields) {
                        if (!f.init) continue;
                        indent();
                        out << "_v." << f.name << " = ";
                        emitExpr(*f.init, true);
                        out << ";\n";
                    }
                    indent(); out << "return _v;\n";
                    depth--;
                    indent(); out << "}\n\n";
                }
                break;
            case Decl::AliasD: {
                std::string base; int ptr;
                resolveType(*d.structCommon.type, base, ptr);
                indent();
                out << "typedef " << base << " ";
                for (int i = 0; i < ptr; i++) out << "*";
                out << d.name << ";\n\n";
                break;
            }
            case Decl::FuncTypeD: {
                if (d.cImpl) break;  // C 实现的接口：不生成 typedef，在 pass 1 中生成 extern 声明
                indent();
                out << "typedef ";
                emitRetType(d);
                out << " (*" << d.name << ")(";
                emitParams(d.structCommon.fields, d.structCommon.variadic);
                out << ");\n\n";
                break;
            }
            default:
                throw CompileError{"内部错误：非类型定义", d.line};
        }
    }

    // ---------------- 函数 ----------------
    void emitRetType(const Decl& d) {
        const auto& rt = d.structCommon.type;
        if (!rt || (rt->name.empty() && rt->ptr == 0)) {
            out << "void"; // 空指针 / 省略返回类型 = void
            return;
        }
        std::string base; int ptr;
        resolveType(*rt, base, ptr);
        out << base;
        for (int i = 0; i < ptr; i++) out << " *";
    }

    void emitParams(const std::vector<Field>& params, bool variadic = false) {
        if (params.empty() && !variadic) { out << "void"; return; }
        for (size_t i = 0; i < params.size(); i++) {
            if (i) out << ", ";
            emitDeclarator(params[i]);
        }
        if (variadic) out << (params.empty() ? "..." : ", ...");
    }

    // 函数签名（实现预定义类型的函数，从函数类型表展开签名）
    void emitFuncSig(const Decl& d) {
        const Decl* sig = &d;
        if (!d.funcTypeName.empty()) {
            auto it = funcTypes.find(d.funcTypeName);
            if (it == funcTypes.end())
                throw CompileError{"未定义的函数类型: " + d.funcTypeName, d.line};
            sig = it->second;
        }
        emitRetType(*sig);
        out << " " << d.name << "(";
        if (!d.methodOwner.empty()) {
            out << d.methodOwner << " *_this";
            if (!sig->structCommon.fields.empty() || sig->structCommon.variadic) {
                out << ", ";
                emitParams(sig->structCommon.fields, sig->structCommon.variadic);
            }
        } else {
            emitParams(sig->structCommon.fields, sig->structCommon.variadic);
        }
        out << ")";
    }

    void emitFunc(const Decl& d) {
        // 函数定义行映射回 .sc 源码（函数序言断点落在 fnc 行）
        if (!srcFile.empty() && d.line > 0)
            out << "#line " << d.line << " \"" << srcFile << "\"\n";
        if (d.isRpc) { emitRpcWorker(d); return; }
        if (d.name != "main" && shouldStaticize(d)) out << "static ";
        emitFuncSig(d);
        out << " {\n";
        // 函数作用域：注册参数类型（含预定义函数类型展开的签名）
        localsT.clear();
        fnVarsL.clear();
        varDimsL.clear();
        inFunc = true;
        const Decl* sig = &d;
        if (!d.funcTypeName.empty()) {
            auto it = funcTypes.find(d.funcTypeName);
            if (it != funcTypes.end()) sig = it->second;
        }
        for (auto& p : sig->structCommon.fields) regVar(p);
        depth++;
        emitStmts(d.body);
        depth--;
        inFunc = false;
        out << "}\n\n";
    }

    // 提升匿名函数字面量为顶层 static 函数，定义追加到 lambdaOut。
    // 保存/恢复外层函数的代码生成状态（输出流 + 局部符号表 + 缩进），可重入。
    void emitLambdaDef(const Expr& e, const std::string& name) {
        // 保存外层状态
        auto savedLocalsT  = std::move(localsT);
        auto savedFnVarsL  = std::move(fnVarsL);
        auto savedVarDimsL = std::move(varDimsL);
        const bool savedInFunc = inFunc;
        const int  savedDepth  = depth;
        std::ostringstream savedOut = std::move(out);
        out = std::ostringstream();

        // 干净的函数作用域
        localsT.clear(); fnVarsL.clear(); varDimsL.clear();
        inFunc = true;
        depth = 0;

        Decl sigDecl;                                   // 仅承载返回类型给 emitRetType
        sigDecl.structCommon.type = e.fncSig.type;
        out << "static ";
        emitRetType(sigDecl);
        out << " " << name << "(";
        emitParams(e.fncSig.fields, e.fncSig.variadic);
        out << ") {\n";
        for (auto& p : e.fncSig.fields) regVar(p);
        depth++;
        emitStmts(e.fncBody);
        depth--;
        out << "}\n\n";

        lambdaOut << out.str();                         // 收集定义（嵌套 lambda 已先行追加）

        // 恢复外层状态
        out = std::move(savedOut);
        localsT  = std::move(savedLocalsT);
        fnVarsL  = std::move(savedFnVarsL);
        varDimsL = std::move(savedVarDimsL);
        inFunc = savedInFunc;
        depth  = savedDepth;
    }

    // ---------------- rpc：伪形参函数糖 ----------------
    // rpc add: i4, a: i4, b: i4 展开为三件套：
    //   struct add { int32_t _; int32_t a; int32_t b; };  // 同名参数结构体
    //   void add_rpc(struct add *_p);                      // 实际函数
    //   static inline int32_t add(int32_t a, int32_t b);   // 调用包装
    // 结构体仅用 tag（不 typedef）：C 中 struct tag 与函数名分属不同
    // 命名空间，故二者可同名，调用形式与 fnc 完全一致。

    // 是否有返回值（与 fnc 一致：空指针 / 省略返回类型 = void 无返回值）
    static bool rpcHasRet(const Decl& d) {
        const auto& rt = d.structCommon.type;
        return rt && (!rt->name.empty() || rt->ptr > 0);
    }

    // 同名参数结构体：返回槽 _ 为首个默认成员（C 侧可用 _ 访问）
    void emitRpcStruct(const Decl& d) {
        out << "struct " << d.name << " {\n";
        depth++;
        if (rpcHasRet(d)) {
            indent();
            emitRetType(d);
            out << " _;\n";
        } else if (d.structCommon.fields.empty()) {
            indent();
            out << "char _;\n";  // C 不允许空结构体：占位
        }
        for (auto& f : d.structCommon.fields) {
            indent();
            emitDeclarator(f);
            out << ";\n";
        }
        depth--;
        out << "};\n";
    }

    // 实际函数签名：void name_rpc(struct name *_p)
    void emitRpcWorkerSig(const Decl& d) {
        out << "void " << d.name << "_rpc(struct " << d.name << " *_p)";
    }

    // 调用包装：装填结构体 → 执行实际函数 → 取返回槽
    void emitRpcWrapper(const Decl& d) {
        out << "static inline ";
        emitRetType(d);
        out << " " << d.name << "(";
        emitParams(d.structCommon.fields, d.structCommon.variadic);
        out << ") {\n";
        depth++;
        indent(); out << "struct " << d.name << " _p = {0};\n";
        for (auto& f : d.structCommon.fields) {
            indent();
            out << "_p." << f.name << " = "
                << (f.name == "this" ? "_this" : f.name) << ";\n";
        }
        indent(); out << d.name << "_rpc(&_p);\n";
        if (rpcHasRet(d)) { indent(); out << "return _p._;\n"; }
        depth--;
        out << "}\n\n";
    }

    // rpc 接口三件套：结构体 + 实际函数原型 + 调用包装
    // workerStatic：本模块定义且未导出时 static；仅声明/导出时 extern
    void emitRpcInterface(const Decl& d, bool workerStatic) {
        emitRpcStruct(d);
        if (workerStatic) out << "static ";
        emitRpcWorkerSig(d);
        out << ";\n";
        emitRpcWrapper(d);
    }

    // rpc 实际函数体：参数引用由 emitExpr/ReturnS 改写为 _p->xxx
    void emitRpcWorker(const Decl& d) {
        if (shouldStaticize(d)) out << "static ";
        emitRpcWorkerSig(d);
        out << " {\n";
        localsT.clear();
        fnVarsL.clear();
        varDimsL.clear();
        inFunc = true;
        for (auto& p : d.structCommon.fields) regVar(p);
        curRpc = &d;
        rpcParams.clear();
        for (auto& p : d.structCommon.fields) rpcParams.insert(p.name);
        depth++;
        emitStmts(d.body);
        depth--;
        curRpc = nullptr;
        rpcParams.clear();
        inFunc = false;
        out << "}\n\n";
    }

    // inc 头文件引入 → #include 行
    //   inc stdio.h    → #include <stdio.h>
    //   inc "my.h"     → #include "my.h"
    //   inc <stdio.h>  → #include <stdio.h>（原样）
    // platform.h 已带入的标准头（stdio/stdlib/string 等）会被跳过，避免重复引入。
    void emitInclude(const Decl& d) {
        const std::string& h = d.name;
        if (endsWith(h, ".sc")) {
            const std::string key = d.origin.empty() ? h : d.origin;
            // 带手写 C ABI 头的子项目模块（<root>/<name>/<name>.sc + 同目录 <name>.h，
            // 如 builtins 的 adt/io）：直接引用其手写头，路径含根目录名以明确归属
            // （<root>/<name>/<name>.h，如 "builtins/adt/adt.h"），随 -I <root的上级>
            // 可见，无需生成/复制内部 scm_<token>.h。
            const std::filesystem::path p(key);
            const std::string stem = p.stem().string();
            if (p.has_parent_path() && p.parent_path().filename() == stem &&
                std::filesystem::exists(p.parent_path() / (stem + ".h"))) {
                const std::filesystem::path root = p.parent_path().parent_path();
                const std::string rootName = root.empty() ? std::string()
                                                          : root.filename().string();
                out << "#include \"" << (rootName.empty() ? "" : rootName + "/")
                    << stem << "/" << stem << ".h\"\n";
                return;
            }
            out << "#include \"" << moduleHeaderName(key) << "\"\n";
            return;
        }
        // platform.h 已带入的标准 C 头：跳过（避免与文件头部 #include "platform.h" 重复）
        {
            static const std::unordered_set<std::string> kPlatformHdrs = {
                "stdint.h", "stddef.h", "stdbool.h", "stdarg.h",
                "stdio.h", "stdlib.h", "string.h", "time.h",
            };
            std::string bare = h;
            if (!bare.empty() && (bare.front() == '<' || bare.front() == '"'))
                bare = bare.substr(1, bare.size() - 2);
            if (kPlatformHdrs.count(bare)) return;
        }
        if (!h.empty() && (h[0] == '"' || h[0] == '<')) out << "#include " << h << "\n";
        else out << "#include <" << h << ">\n";
    }

    // 为所有结构/联合生成前置 typedef 声明：
    //   typedef struct X X;
    //   typedef union U U;
    // 目的：让结构/函数定义顺序与源码解耦，先使用再定义也可通过 C 编译。
    void emitForwardAggrDecls(bool exportedOnly = false) {
        std::unordered_set<std::string> emitted;
        for (auto& d : prog.decls) {
            if (d->kind != Decl::StructD && d->kind != Decl::UnionD) continue;
            if (exportedOnly && !d->exported) continue;
            if (d->external) continue;  // 外部模块类型由其模块头提供
            if (!emitted.insert(d->name).second) continue;
            out << "typedef " << (d->kind == Decl::UnionD ? "union" : "struct")
                << " " << d->name << " " << d->name << ";\n";
        }
        if (!emitted.empty()) out << "\n";
    }

    // ---------------- 主流程：两遍扫描输出 ----------------
    // 第一遍：类型定义 + 全局变量 + 函数原型声明（forward declaration）
    // 第二遍：函数体实现
    // 这样做的目的是支持函数间的任意引用顺序（包括递归/互递归）
    std::string run() {
        // 标准 C 头统一由 builtins/platform.h 提供（该目录默认在 -I 路径），
        // 同时带入 TLS 宏等跨平台适配
        out << "/* 由 scc 生成，请勿手工修改 */\n"
            << "#include \"platform.h\"\n";

        // 用户 inc 引入的头文件
        for (auto& d : prog.decls)
            if (d->kind == Decl::IncD) emitInclude(*d);
        out << "\n";

        // 最简策略：默认为所有结构/联合输出前置声明，消除定义顺序依赖
        emitForwardAggrDecls();

        // 收集函数类型、聚合类型、顶层函数与方法注册表（含外部模块合并声明，供语法糖识别）
        for (auto& d : prog.decls) {
            if (d->kind == Decl::FuncTypeD && !d->isRpc && !d->cImpl && d->methodOwner.empty())
                funcTypes[d->name] = d.get();
            else if (d->kind == Decl::StructD || d->kind == Decl::UnionD)
                aggrs[d->name] = d.get();
            else if (d->kind == Decl::AliasD) aliases[d->name] = d->structCommon.type->name;
            if (!d->methodOwner.empty() && !d->isRpc)
                methods[d->methodOwner][d->methodName] = d.get();
            else if (d->kind == Decl::FuncD && !d->isRpc)
                funcs[d->name] = d.get();  // 顶层函数（缺参补全查签名）
            if (d->isRpc) rpcs[d->name] = d.get();  // run 语句目标查询
        }

        // 预扫描 T() 伪调用（仅本单元代码，外部合并声明不扫），顺带标记 run/print/string
        heapNews.clear();
        for (auto& d : prog.decls) {
            if (d->external) continue;
            for (auto& f : d->structCommon.fields) if (f.init) scanExprForNew(*f.init);
            for (auto& s : d->body) scanStmtForNew(*s);
        }

        // 枚举类型名集合（string 格式化按整数）
        for (auto& d : prog.decls)
            if (d->kind == Decl::EnumD) enums.insert(d->name);

        // print 关键字：需 io 模块实现 print（inc io.sc 参与链接）
        if (usesPrint && !funcs.count("print")) {
            bool hasIo = false;
            for (auto& d : prog.decls)
                if (d->kind == Decl::IncD && endsWith(d->name, "io.sc")) hasIo = true;
            if (!hasIo)
                throw CompileError{"print 需要先 inc io.sc", usesPrint};
            out << "extern void print(const char *, ...);\n\n";
        }
        // stringify 格式化关键字：依赖 adt string 与 io 的 stringify_t 选项类型
        if (usesStrof && !funcs.count("stringify")) {
            if (!aggrOf("string"))
                throw CompileError{"stringify(...) 格式化依赖内置 string，请先 inc adt.sc", usesStrof};
            bool hasIo = false;
            for (auto& d : prog.decls)
                if (d->kind == Decl::IncD && endsWith(d->name, "io.sc")) hasIo = true;
            if (!hasIo)
                throw CompileError{"stringify(...) 选项依赖 io 的 stringify_t，请先 inc io.sc", usesStrof};
        }

        // run 语句线程原语：thread 对象与 rpc 参数联合分配，实现在 m 子项目（m_impl）
        if (usesRun) {
            out << "typedef struct thread thread;\n"
                << "extern uint8_t thread_run(void (*)(void *), const void *, size_t, thread **, uint32_t, uint8_t);\n";
            // 第二参可能是 pool：pool 类型可见即一并输出 pool_run 原型
            if (aggrOf("pool"))
                out << "typedef struct pool pool;\n"
                    << "extern uint8_t pool_run(pool *, void (*)(void *), const void *, size_t);\n";
            out << "\n";
        }

        // wait 语句条件等待原语：实现在 m 子项目（m_impl）
        if (usesWait)
            out << "typedef struct cond cond;\n"
                << "typedef struct mutex mutex;\n"
                << "extern int32_t cond_wait(cond *, mutex *, uint64_t, uint64_t);\n\n";

        // prev 链表导航：边界安全前驱由 adt 的 C 契约 chain_prev 提供（rear 约定属 chain 概念）。
        // head 无前驱 → NULL（rear 仅经 last() 获取）。codegen 只生成调用，要求先 inc adt.sc。
        if (usesLPrev && !aggrOf("chain")) {
            bool hasAdt = false;
            for (auto& d : prog.decls)
                if (d->kind == Decl::IncD && endsWith(d->name, "adt.sc")) hasAdt = true;
            if (!hasAdt)
                throw CompileError{"prev 边界安全前驱依赖内置 chain，请先 inc adt.sc", 0};
        }

        // 第一遍：类型、全局变量、函数原型（外部模块声明不参与输出，由模块头提供）
        for (auto& d : prog.decls) {
            if (d->external && d->kind != Decl::IncD) continue;
            switch (d->kind) {
                case Decl::EnumD: case Decl::AliasD:
                    emitTypeDecl(*d);
                    break;
                case Decl::StructD: case Decl::UnionD:
                    emitAggrWithDeps(*d);  // 惰性前移按值依赖，定义顺序无关
                    break;
                case Decl::FuncTypeD:
                    // rpc 仅声明：接口三件套，实际函数在外部（C 侧）实现
                    if (d->isRpc) emitRpcInterface(*d, false);
                    // C 实现接口（fnc name:: 或成员 fnc m::）：extern 原型，实现在 C 侧
                    else if (d->cImpl || !d->methodOwner.empty()) {
                        out << "extern ";
                        emitFuncSig(*d); out << ";\n";
                    }
                    else emitTypeDecl(*d);
                    break;
                case Decl::VarD:
                    emitVarDecls(d->structCommon.fields, false, shouldStaticize(*d));
                    break;
                case Decl::LetD:
                    emitVarDecls(d->structCommon.fields, true, shouldStaticize(*d));
                    break;
                case Decl::TlsD:
                    emitVarDecls(d->structCommon.fields, false, false, true);  // 始终 static TLS
                    break;
                case Decl::FuncD:
                    if (d->isRpc) { emitRpcInterface(*d, shouldStaticize(*d)); break; }
                    if (d->name != "main") {
                        if (shouldStaticize(*d)) out << "static ";
                        emitFuncSig(*d);
                        out << ";\n";
                    }
                    break;
                case Decl::IncD: break;  // 已在顶部输出
                case Decl::AddD: break;  // 构建指令，不产生 C 输出
            }
        }
        out << "\n";
        // 堆构造辅助函数（T() 伪调用糖使用）
        emitNewHelpers();
        // 第二遍：函数定义先写入暂存流（string 格式化调用点按需登记格式化请求），
        // 随后回填支撑代码（原语/格式化器/包装）再拼接函数体
        std::ostringstream mainOut = std::move(out);
        out = std::ostringstream();
        for (auto& d : prog.decls)
            if (d->kind == Decl::FuncD && !d->external) emitFunc(*d);
        std::string funcsPart = out.str();
        out = std::move(mainOut);
        emitSofHelpers();
        out << lambdaOut.str();   // 提升的匿名函数定义（在函数体前，确保被引用前已定义）
        out << funcsPart;
        return out.str();
    }

    // ---------------- 头文件生成（@导出对象） ----------------
    // 导出类型 → 完整 typedef；导出变量/常量 → extern；导出函数 → 原型
    std::string runHeader(const std::string& guard) {
        bool any = false;
        for (auto& d : prog.decls) if (d->exported && !d->external) { any = true; break; }
        if (!any) return "";

        out << "/* 由 scc 生成，请勿手工修改 —— @导出对象声明 */\n"
            << "#ifndef " << guard << "\n"
            << "#define " << guard << "\n\n"
            << "#include \"platform.h\"\n\n";

        // 头文件同样先输出导出结构/联合的前置声明，减少声明顺序耦合
        emitForwardAggrDecls(true);

        // 函数类型表与方法表（导出函数可能引用未导出的函数类型签名）
        for (auto& d : prog.decls) {
            if (d->kind == Decl::FuncTypeD && !d->isRpc && !d->cImpl && d->methodOwner.empty())
                funcTypes[d->name] = d.get();
            if (!d->methodOwner.empty() && !d->isRpc)
                methods[d->methodOwner][d->methodName] = d.get();
        }

        for (auto& d : prog.decls) {
            if (!d->exported || d->external) continue;
            switch (d->kind) {
                case Decl::EnumD: case Decl::StructD:
                case Decl::UnionD: case Decl::AliasD:
                    emitTypeDecl(*d);
                    break;
                case Decl::FuncTypeD:
                    if (d->isRpc) emitRpcInterface(*d, false);
                    else if (d->cImpl || !d->methodOwner.empty()) {
                        out << "extern ";
                        emitFuncSig(*d); out << ";\n";
                    }
                    else emitTypeDecl(*d);
                    break;
                case Decl::VarD: emitExternVars(d->structCommon.fields, false); break;
                case Decl::LetD: emitExternVars(d->structCommon.fields, true); break;
                case Decl::TlsD: break;  // tls 不可导出（parser 已拦截）
                case Decl::FuncD:
                    if (d->isRpc) { emitRpcInterface(*d, false); break; }
                    emitFuncSig(*d);
                    out << ";\n";
                    break;
                case Decl::IncD:
                    emitInclude(*d);
                    break;
                case Decl::AddD: break;  // 构建指令，不产生 C 输出
            }
        }
        out << "\n#endif /* " << guard << " */\n";
        return out.str();
    }

    // extern 变量声明（头文件用：不带初值）
    void emitExternVars(const std::vector<Field>& decls, bool asConst) {
        for (auto& f : decls) {
            indent();
            out << "extern ";
            emitDeclarator(f, asConst);
            out << ";\n";
        }
    }
};

} // namespace

std::string emitC(const Program& prog, const std::string& srcFile) {
    CGen g(prog);
    g.srcFile = srcFile;
    return g.run();
}

std::string emitC(const Program& prog, const std::string& srcFile,
                  const std::string& stringifyHeaderName, std::string* stringifyHeaderOut) {
    CGen g(prog);
    g.srcFile = srcFile;
    g.sofHeaderName = stringifyHeaderName;
    std::string c = g.run();
    if (stringifyHeaderOut) *stringifyHeaderOut = g.sofHeaderOut;
    return c;
}

std::string emitCHeader(const Program& prog, const std::string& guardName) {
    CGen g(prog);
    return g.runHeader(guardName);
}
