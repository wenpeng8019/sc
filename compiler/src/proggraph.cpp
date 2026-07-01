// ============================================================
// 程序结构依赖图 —— 建图 + 激活分析 + JSON/HTML 导出
// ============================================================
// 见 graph.h 与子项目 proggraph/docs/design.md。
// ============================================================
#include "proggraph.h"
#include <algorithm>
#include <filesystem>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace {

// ---------- JSON 字符串转义 ----------
std::string jesc(const std::string& s) {
    std::string r;
    for (char c : s) {
        switch (c) {
            case '"':  r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n"; break;
            case '\t': r += "\\t"; break;
            case '\r': break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof buf, "\\u%04x", (unsigned char)c);
                    r += buf;
                } else r += c;
        }
    }
    return r;
}

std::string stemOf(const std::string& path) {
    return std::filesystem::path(path).stem().string();
}

// ---------- 图数据结构 ----------
struct GNode {
    std::string id;         // 稳定唯一键：顶层=name；方法=owner.method；模块=路径
    std::string kind;       // enum/struct/union/alias/functype/fnc/rpc/var/let/tls/
                            //   method/dim/tok/dep/test/macro/module
    std::string name;       // 显示名
    std::string module;     // 所属单元路径（模块折叠/着色用）
    std::string file;       // 源文件
    int         line = 0;
    bool        exported = false;
    bool        external = false;
    std::string parent;     // 容器节点 id（方法→owner、顶层→模块、模块→空）
    // 激活分析结果（后填）
    bool        active = false;
    int         depth = -1;
    int         fanIn = 0;
    int         fanOut = 0;
};

struct GEdge {
    std::string from, to, kind;
    int line = 0;
};

// ---------- 建图器 ----------
struct Builder {
    std::vector<GNode> nodes;
    std::vector<GEdge> edges;
    std::unordered_map<std::string, int> idIndex;              // node id → nodes 下标
    std::unordered_map<std::string, int> symIndex;            // 顶层符号名 → nodes 下标
    std::unordered_map<std::string, std::vector<int>> methodIndex; // 方法名 → 候选下标
    std::unordered_map<std::string, int> tokIdIndex;          // tok 字符串 id → nodes 下标
    std::unordered_map<std::string, const Decl*> externMap;   // 单元模式：外部符号名 → decl（按需建叶子）
    std::unordered_set<std::string> edgeSeen;                 // 去重 from|to|kind
    bool whole = true;

    int ensureNode(const std::string& id) {
        auto it = idIndex.find(id);
        return it == idIndex.end() ? -1 : it->second;
    }

    int addNode(const GNode& n) {
        auto it = idIndex.find(n.id);
        if (it != idIndex.end()) return it->second;
        int idx = (int)nodes.size();
        nodes.push_back(n);
        idIndex[n.id] = idx;
        return idx;
    }

    // 模块节点（容器）
    std::string moduleNode(const std::string& path, bool external = false) {
        const std::string id = "mod:" + path;
        if (!idIndex.count(id)) {
            GNode n;
            n.id = id; n.kind = "module";
            n.name = stemOf(path); n.module = path; n.file = path;
            n.external = external;
            addNode(n);
        }
        return id;
    }

    // ---- Decl → 节点 kind 映射 ----
    static std::string declKind(const Decl& d) {
        switch (d.kind) {
            case Decl::EnumD:     return "enum";
            case Decl::StructD:   return "struct";
            case Decl::UnionD:    return "union";
            case Decl::AliasD:    return "alias";
            case Decl::FuncTypeD: return "functype";
            case Decl::FuncD:
                if (!d.methodOwner.empty()) return d.isDim ? "dim" : "method";
                return d.isRpc ? "rpc" : "fnc";
            case Decl::VarD:      return d.isTok ? "tok" : "var";
            case Decl::LetD:      return "let";
            case Decl::TlsD:      return "tls";
            case Decl::TestD:     return "test";
            case Decl::MacroD:    return "macro";
            case Decl::DepD:      return "dep";
            default:              return "";   // IncD/AddD/MixD 不建节点
        }
    }

    static std::string nodeId(const Decl& d) {
        if (d.kind == Decl::FuncD && !d.methodOwner.empty())
            return d.methodOwner + "." + d.methodName;
        return d.name;
    }

    static bool isTypeKind(const std::string& k) {
        return k == "enum" || k == "struct" || k == "union" ||
               k == "alias" || k == "functype";
    }
    static bool isDataKind(const std::string& k) {
        return k == "var" || k == "let" || k == "tls" || k == "tok";
    }
    static bool isFuncKind(const std::string& k) {
        return k == "fnc" || k == "rpc" || k == "method" || k == "dim" || k == "macro";
    }

    // ============================================================
    // 阶段 1：建节点 + 符号表
    // ============================================================
    void collectNodes(const GraphUnit& u) {
        const std::string modId = moduleNode(u.path);
        for (auto& dp : u.prog->decls) {
            const Decl& d = *dp;
            if (d.kind == Decl::IncD || d.kind == Decl::AddD || d.kind == Decl::MixD)
                continue;
            const std::string k = declKind(d);
            if (k.empty()) continue;

            if (d.external) {
                // 整程序模式：外部符号只是定义单元节点的影子，跳过（定义单元会建真身）。
                // 单元模式：登记到 externMap，被引用时再按需建叶子。
                if (whole) continue;
                if (d.kind == Decl::FuncD && !d.methodOwner.empty()) continue;
                if ((d.kind == Decl::VarD || d.kind == Decl::LetD || d.kind == Decl::TlsD) &&
                    !d.isTok && !d.structCommon.fields.empty()) {
                    for (auto& f : d.structCommon.fields)
                        if (!f.name.empty() && !externMap.count(f.name)) externMap[f.name] = &d;
                } else if (!d.name.empty() && !externMap.count(d.name)) {
                    externMap[d.name] = &d;
                }
                continue;
            }

            GNode n;
            n.id = nodeId(d);
            n.kind = k;
            n.name = d.methodOwner.empty() ? d.name : d.methodName;
            n.module = u.path;
            n.file = d.inlinedFrom.empty() ? u.path : d.inlinedFrom;
            n.line = d.line;
            n.exported = d.exported;
            n.external = false;
            n.parent = (d.kind == Decl::FuncD && !d.methodOwner.empty())
                           ? d.methodOwner : modId;

            // 全局 var/let/tls：变量名在 structCommon.fields（可一行多项），逐项建节点。
            //   tok 句柄（isTok）名在 d.name，按单节点处理。
            if ((d.kind == Decl::VarD || d.kind == Decl::LetD || d.kind == Decl::TlsD) &&
                !d.isTok && !d.structCommon.fields.empty()) {
                for (auto& f : d.structCommon.fields) {
                    if (f.name.empty()) continue;
                    GNode fn = n;
                    fn.id = f.name; fn.name = f.name;
                    fn.line = f.line ? f.line : d.line;
                    int fidx = addNode(fn);
                    symIndex[f.name] = fidx;
                }
                continue;
            }

            int idx = addNode(n);

            if (d.kind == Decl::FuncD && !d.methodOwner.empty())
                methodIndex[d.methodName].push_back(idx);
            else
                symIndex[d.name] = idx;   // 顶层符号名（全局唯一）
            if (d.kind == Decl::VarD && d.isTok && !d.tokId.empty())
                tokIdIndex[d.tokId] = idx;
        }
    }

    // 单元模式：按需为外部符号建叶子节点
    int materializeExtern(const std::string& name) {
        auto it = externMap.find(name);
        if (it == externMap.end()) return -1;
        const Decl& d = *it->second;
        const std::string k = declKind(d);
        if (k.empty()) return -1;
        GNode n;
        n.id = name; n.kind = k; n.name = name;
        n.module = d.origin; n.file = d.origin; n.line = d.line;
        n.exported = d.exported; n.external = true;
        n.parent = d.origin.empty() ? std::string() : moduleNode(d.origin, true);
        int idx = addNode(n);
        symIndex[name] = idx;
        return idx;
    }

    int resolveSym(const std::string& name) {
        auto it = symIndex.find(name);
        if (it != symIndex.end()) return it->second;
        if (!whole) return materializeExtern(name);
        return -1;
    }

    void addEdge(const std::string& from, int toIdx, const std::string& kind, int line) {
        if (toIdx < 0 || from.empty()) return;
        const std::string& to = nodes[toIdx].id;
        if (to == from && kind != "call" && kind != "method") return;  // 自环仅调用有意义（递归）
        const std::string key = from + "|" + to + "|" + kind;
        if (!edgeSeen.insert(key).second) return;
        edges.push_back({from, to, kind, line});
    }

    // ============================================================
    // 阶段 2：采集边（带作用域）
    // ============================================================
    struct Scope {
        std::vector<std::unordered_set<std::string>> frames;
        void push() { frames.emplace_back(); }
        void pop()  { if (!frames.empty()) frames.pop_back(); }
        void bind(const std::string& n) { if (!frames.empty() && !n.empty()) frames.back().insert(n); }
        bool has(const std::string& n) const {
            for (auto& f : frames) if (f.count(n)) return true;
            return false;
        }
    };

    enum Ctx { Normal, Callee, Assign };

    void edgeType(const std::string& from, const TypeRef& t, int line) {
        if (!t.name.empty()) {
            int to = resolveSym(t.name);
            if (to >= 0 && isTypeKind(nodes[to].kind)) addEdge(from, to, "type", line);
        }
        for (auto& f : t.structCommon.fields) edgeType(from, f.type, line);
        if (t.structCommon.type) edgeType(from, *t.structCommon.type, line);
    }

    void visitExpr(const std::string& from, const Expr& e, Scope& sc, Ctx ctx) {
        switch (e.kind) {
            case Expr::Ident: {
                if (e.cBridge) return;                 // ::name C 桥接，跳过
                if (sc.has(e.text)) return;            // 局部名，非依赖
                int to = resolveSym(e.text);
                if (to < 0) return;
                const std::string& tk = nodes[to].kind;
                if (isTypeKind(tk))
                    addEdge(from, to, ctx == Callee ? "construct" : "type", e.line);
                else if (isDataKind(tk))
                    addEdge(from, to, ctx == Assign ? "write" : "read", e.line);
                else if (isFuncKind(tk))
                    addEdge(from, to, "call", e.line);
                return;
            }
            case Expr::Call: {
                if (e.a) {
                    if (e.a->kind == Expr::Member) {   // obj.method(...) → method 边
                        for (int mi : methodIndex.count(e.a->text)
                                          ? methodIndex[e.a->text] : std::vector<int>{})
                            addEdge(from, mi, "method", e.line);
                        if (e.a->a) visitExpr(from, *e.a->a, sc, Normal);
                    } else {
                        visitExpr(from, *e.a, sc, Callee);
                    }
                }
                for (auto& a : e.args) if (a) visitExpr(from, *a, sc, Normal);
                return;
            }
            case Expr::Member: {
                // 纯成员访问（非调用）：字段不建节点；仅递归对象。
                // 若对象是类型名（枚举成员 Color.RED）由下面 Ident 归为 type。
                if (e.a) visitExpr(from, *e.a, sc, Normal);
                return;
            }
            case Expr::Cast: {
                if (!e.castIsFmt && !e.op.empty()) {
                    int to = resolveSym(e.op);
                    if (to >= 0 && isTypeKind(nodes[to].kind))
                        addEdge(from, to, "type", e.line);
                }
                if (e.a) visitExpr(from, *e.a, sc, Normal);
                return;
            }
            case Expr::Offsetof: {
                int to = resolveSym(e.text);
                if (to >= 0 && isTypeKind(nodes[to].kind))
                    addEdge(from, to, "type", e.line);
                return;
            }
            case Expr::Binary: {
                const bool assign = e.op == "=" || (e.op.size() >= 2 && e.op.back() == '=' &&
                    e.op != "==" && e.op != "!=" && e.op != "<=" && e.op != ">=");
                if (e.a) visitExpr(from, *e.a, sc, assign ? Assign : Normal);
                if (e.b) visitExpr(from, *e.b, sc, Normal);
                return;
            }
            case Expr::FncLit: {
                sc.push();
                for (auto& p : e.fncSig.fields) sc.bind(p.name);
                if (e.fncSig.type) edgeType(from, *e.fncSig.type, e.line);
                for (auto& p : e.fncSig.fields) edgeType(from, p.type, e.line);
                for (auto& s : e.fncBody) visitStmt(from, *s, sc);
                sc.pop();
                return;
            }
            default:
                if (e.a) visitExpr(from, *e.a, sc, Normal);
                if (e.b) visitExpr(from, *e.b, sc, Normal);
                if (e.c) visitExpr(from, *e.c, sc, Normal);
                for (auto& a : e.args) if (a) visitExpr(from, *a, sc, Normal);
                return;
        }
    }

    void visitStmt(const std::string& from, const Stmt& s, Scope& sc) {
        switch (s.kind) {
            case Stmt::VarS:
            case Stmt::LetS:
            case Stmt::TlsS:
                for (auto& f : s.decls) {
                    edgeType(from, f.type, f.line ? f.line : s.line);
                    if (f.init) visitExpr(from, *f.init, sc, Normal);
                    sc.bind(f.name);
                }
                return;
            case Stmt::ExprS:
            case Stmt::ReturnS:
                if (s.expr) visitExpr(from, *s.expr, sc, Normal);
                return;
            case Stmt::IfS:
            case Stmt::WhileS:
            case Stmt::DoWhileS:
                if (s.expr) visitExpr(from, *s.expr, sc, Normal);
                sc.push(); for (auto& b : s.body) visitStmt(from, *b, sc); sc.pop();
                sc.push(); for (auto& b : s.elseBody) visitStmt(from, *b, sc); sc.pop();
                return;
            case Stmt::ForS:
                sc.push();
                if (s.forIn) {
                    sc.bind(s.forVar);
                    for (auto& v : s.forIdxVars) sc.bind(v);
                    if (s.forVarHasType) edgeType(from, s.forVarType, s.line);
                    if (s.forColl) visitExpr(from, *s.forColl, sc, Normal);
                    if (s.forRangeLo) visitExpr(from, *s.forRangeLo, sc, Normal);
                    if (s.forRangeHi) visitExpr(from, *s.forRangeHi, sc, Normal);
                    if (s.forStepE) visitExpr(from, *s.forStepE, sc, Normal);
                    if (s.forOffsetE) visitExpr(from, *s.forOffsetE, sc, Normal);
                    if (s.forNumE) visitExpr(from, *s.forNumE, sc, Normal);
                } else {
                    if (s.forInit) visitExpr(from, *s.forInit, sc, Normal);
                    if (s.forCond) visitExpr(from, *s.forCond, sc, Normal);
                    if (s.forStep) visitExpr(from, *s.forStep, sc, Normal);
                }
                for (auto& b : s.body) visitStmt(from, *b, sc);
                sc.pop();
                return;
            case Stmt::CaseS:
                if (s.expr) visitExpr(from, *s.expr, sc, Normal);
                for (auto& arm : s.caseArms) {
                    for (auto& l : arm.labels) if (l) visitExpr(from, *l, sc, Normal);
                    sc.push();
                    sc.bind(arm.binding);
                    for (auto& b : arm.body) visitStmt(from, *b, sc);
                    sc.pop();
                }
                return;
            case Stmt::MixS:
                if (s.expr) {   // mix name(args) → macro 边
                    if (s.expr->kind == Expr::Call && s.expr->a &&
                        s.expr->a->kind == Expr::Ident) {
                        int to = resolveSym(s.expr->a->text);
                        if (to >= 0) addEdge(from, to, "macro", s.line);
                    }
                    for (auto& a : s.expr->args) if (a) visitExpr(from, *a, sc, Normal);
                }
                return;
            case Stmt::InlineDefS:
                if (s.decl) {
                    sc.push();
                    for (auto& p : s.decl->structCommon.fields) {
                        edgeType(from, p.type, s.line); sc.bind(p.name);
                    }
                    if (s.decl->structCommon.type) edgeType(from, *s.decl->structCommon.type, s.line);
                    for (auto& b : s.decl->body) visitStmt(from, *b, sc);
                    sc.pop();
                }
                return;
            case Stmt::FinalS:
                sc.push(); for (auto& b : s.body) visitStmt(from, *b, sc); sc.pop();
                return;
            case Stmt::RunS:
            case Stmt::DoneS:
            case Stmt::BackS:
                if (s.expr) visitExpr(from, *s.expr, sc, Normal);
                if (s.forInit) visitExpr(from, *s.forInit, sc, Normal);
                if (s.runTarget) visitExpr(from, *s.runTarget, sc, Normal);
                return;
            case Stmt::FormS:
                if (s.expr) visitExpr(from, *s.expr, sc, Normal);
                if (s.forInit) visitExpr(from, *s.forInit, sc, Normal);
                if (s.forCond) visitExpr(from, *s.forCond, sc, Normal);
                if (s.forStep) visitExpr(from, *s.forStep, sc, Normal);
                return;
            case Stmt::PrintS:
                for (auto& a : s.printArgs) if (a) visitExpr(from, *a, sc, Normal);
                return;
            case Stmt::AssertS:
                if (s.expr) visitExpr(from, *s.expr, sc, Normal);
                if (s.assertMsg) visitExpr(from, *s.assertMsg, sc, Normal);
                return;
            case Stmt::RetCallS:
                if (s.expr) visitExpr(from, *s.expr, sc, Normal);
                sc.push(); for (auto& b : s.body) visitStmt(from, *b, sc); sc.pop();
                return;
            case Stmt::DeclS:
                if (s.decl) {
                    for (auto& f : s.decl->structCommon.fields) edgeType(from, f.type, s.line);
                    if (s.decl->structCommon.type) edgeType(from, *s.decl->structCommon.type, s.line);
                }
                return;
            default:
                return;
        }
    }

    // 顶层声明的签名/体 → 边
    void collectEdges(const GraphUnit& u) {
        for (auto& dp : u.prog->decls) {
            const Decl& d = *dp;
            if (d.external) continue;
            const std::string k = declKind(d);
            if (k.empty()) continue;

            // 全局 var/let/tls：逐项以字段名为源节点采集类型/初值边
            if ((d.kind == Decl::VarD || d.kind == Decl::LetD || d.kind == Decl::TlsD) &&
                !d.isTok && !d.structCommon.fields.empty()) {
                for (auto& f : d.structCommon.fields) {
                    if (f.name.empty() || !idIndex.count(f.name)) continue;
                    edgeType(f.name, f.type, f.line ? f.line : d.line);
                    if (f.init) { Scope sc; sc.push(); visitExpr(f.name, *f.init, sc, Normal); }
                }
                continue;
            }

            const std::string from = nodeId(d);
            if (!idIndex.count(from)) continue;

            // 签名类型（函数返回/参数、结构字段、别名目标、枚举基类型）
            for (auto& f : d.structCommon.fields) {
                edgeType(from, f.type, f.line ? f.line : d.line);
            }
            if (d.structCommon.type) edgeType(from, *d.structCommon.type, d.line);

            // 全局 var/let/tls 初值
            Scope sc; sc.push();
            for (auto& f : d.structCommon.fields)
                if (f.init) visitExpr(from, *f.init, sc, Normal);

            // 函数/方法体
            if (d.kind == Decl::FuncD) {
                for (auto& f : d.structCommon.fields) sc.bind(f.name);  // 形参
                for (auto& s : d.body) visitStmt(from, *s, sc);
            }

            // DepD → tokdep 边：按 tokId 串匹配 tok 句柄节点（源依赖项 + map/loop 目标）
            if (d.kind == Decl::DepD) {
                auto linkTok = [&](const std::string& idStr) {
                    auto it = tokIdIndex.find(idStr);
                    if (it != tokIdIndex.end()) addEdge(from, it->second, "tokdep", d.line);
                };
                for (auto& it : d.depItems)   linkTok(it.second);
                for (auto& it : d.depTargets) linkTok(it.second);
            }
        }
    }

    // ---- 模块 import 边（inc/add）----
    void collectImports(const GraphUnit& u) {
        const std::string src = moduleNode(u.path);
        for (auto& dp : u.prog->decls) {
            const Decl& d = *dp;
            if (d.kind != Decl::IncD && d.kind != Decl::AddD) continue;
            std::string tgtPath = d.origin;
            std::string dst;
            if (!tgtPath.empty()) dst = moduleNode(tgtPath, whole && !idIndex.count("mod:" + tgtPath));
            else dst = moduleNode("hdr:" + d.name, true);
            if (dst == src) continue;
            const std::string key = src + "|" + dst + "|import";
            if (edgeSeen.insert(key).second)
                edges.push_back({src, dst, "import", d.line});
        }
    }

    // ============================================================
    // 阶段 3：激活分析（根可达）+ 度数 + 环
    // ============================================================
    std::string entryKind;
    std::vector<std::string> roots;

    void analyze() {
        // 邻接（排除 import 与模块节点）
        std::unordered_map<std::string, std::vector<std::string>> adj, radj;
        for (auto& e : edges) {
            if (e.kind == "import") continue;
            adj[e.from].push_back(e.to);
            radj[e.to].push_back(e.from);
            int fi = ensureNode(e.from), ti = ensureNode(e.to);
            if (fi >= 0) nodes[fi].fanOut++;
            if (ti >= 0) nodes[ti].fanIn++;
        }

        // 根集合：可执行=main；库=全部 @导出
        int mainIdx = -1;
        for (auto& n : nodes)
            if (n.kind == "fnc" && n.name == "main" && !n.external) { mainIdx = idIndex[n.id]; break; }
        if (mainIdx >= 0) {
            entryKind = "main";
            roots.push_back(nodes[mainIdx].id);
        } else {
            entryKind = "exports";
            for (auto& n : nodes)
                if (n.exported && !n.external && n.kind != "module")
                    roots.push_back(n.id);
        }

        // 正向 BFS：active + depth
        std::vector<std::string> frontier = roots;
        for (auto& r : roots) { int i = ensureNode(r); if (i >= 0) { nodes[i].active = true; nodes[i].depth = 0; } }
        while (!frontier.empty()) {
            std::vector<std::string> next;
            for (auto& u : frontier) {
                int ui = ensureNode(u);
                int d = ui >= 0 ? nodes[ui].depth : 0;
                for (auto& v : adj[u]) {
                    int vi = ensureNode(v);
                    if (vi < 0 || nodes[vi].active) continue;
                    nodes[vi].active = true;
                    nodes[vi].depth = d + 1;
                    next.push_back(v);
                }
            }
            frontier.swap(next);
        }

        // 模块节点：含任一激活子节点则激活
        for (auto& n : nodes) {
            if (n.kind != "module") continue;
            for (auto& c : nodes)
                if (c.parent == n.id && c.active) { n.active = true; break; }
        }
    }

    // Tarjan SCC → 依赖环（size>1 或自环递归）
    std::vector<std::vector<std::string>> cycles;
    void findCycles() {
        std::unordered_map<std::string, std::vector<std::string>> adj;
        std::unordered_set<std::string> selfLoop;
        for (auto& e : edges) {
            if (e.kind == "import") continue;
            if (e.from == e.to) { selfLoop.insert(e.from); continue; }
            adj[e.from].push_back(e.to);
        }
        std::unordered_map<std::string, int> idx, low;
        std::unordered_set<std::string> onStk;
        std::vector<std::string> stk;
        int counter = 0;
        std::function<void(const std::string&)> dfs = [&](const std::string& u) {
            idx[u] = low[u] = counter++;
            stk.push_back(u); onStk.insert(u);
            for (auto& v : adj[u]) {
                if (!idx.count(v)) { dfs(v); low[u] = std::min(low[u], low[v]); }
                else if (onStk.count(v)) low[u] = std::min(low[u], idx[v]);
            }
            if (low[u] == idx[u]) {
                std::vector<std::string> comp;
                while (true) {
                    std::string w = stk.back(); stk.pop_back(); onStk.erase(w);
                    comp.push_back(w);
                    if (w == u) break;
                }
                if (comp.size() > 1) cycles.push_back(comp);
                else if (selfLoop.count(u)) cycles.push_back({u});
            }
        };
        for (auto& n : nodes)
            if (n.kind != "module" && !idx.count(n.id)) dfs(n.id);
    }
};

// ---------- JSON 序列化 ----------
std::string toJson(Builder& b, const std::string& rootPath, bool whole) {
    std::ostringstream o;
    o << "{\n";
    o << "  \"schema\": \"sc-proggraph/1\",\n";
    o << "  \"root\": \"" << jesc(rootPath) << "\",\n";
    o << "  \"mode\": \"" << (whole ? "whole" : "unit") << "\",\n";
    o << "  \"entry\": {\"kind\":\"" << b.entryKind << "\",\"roots\":[";
    for (size_t i = 0; i < b.roots.size(); i++) {
        if (i) o << ",";
        o << "\"" << jesc(b.roots[i]) << "\"";
    }
    o << "]},\n";

    // nodes
    int nActive = 0, nDead = 0;
    o << "  \"nodes\": [\n";
    for (size_t i = 0; i < b.nodes.size(); i++) {
        const GNode& n = b.nodes[i];
        if (n.kind != "module") { n.active ? nActive++ : nDead++; }
        o << "    {\"id\":\"" << jesc(n.id) << "\",\"kind\":\"" << n.kind
          << "\",\"name\":\"" << jesc(n.name) << "\"";
        if (!n.module.empty()) o << ",\"module\":\"" << jesc(stemOf(n.module)) << "\"";
        if (!n.file.empty())   o << ",\"file\":\"" << jesc(n.file) << "\"";
        if (n.line)            o << ",\"line\":" << n.line;
        if (n.exported)        o << ",\"exported\":1";
        if (n.external)        o << ",\"external\":1";
        if (!n.parent.empty()) o << ",\"parent\":\"" << jesc(n.parent) << "\"";
        o << ",\"active\":" << (n.active ? 1 : 0);
        if (n.depth >= 0)      o << ",\"depth\":" << n.depth;
        o << ",\"fanIn\":" << n.fanIn << ",\"fanOut\":" << n.fanOut << "}";
        o << (i + 1 < b.nodes.size() ? ",\n" : "\n");
    }
    o << "  ],\n";

    // edges
    o << "  \"edges\": [\n";
    for (size_t i = 0; i < b.edges.size(); i++) {
        const GEdge& e = b.edges[i];
        o << "    {\"from\":\"" << jesc(e.from) << "\",\"to\":\"" << jesc(e.to)
          << "\",\"kind\":\"" << e.kind << "\"";
        if (e.line) o << ",\"line\":" << e.line;
        o << "}";
        o << (i + 1 < b.edges.size() ? ",\n" : "\n");
    }
    o << "  ],\n";

    // stats
    o << "  \"stats\": {\"nodes\":" << b.nodes.size() << ",\"edges\":" << b.edges.size()
      << ",\"active\":" << nActive << ",\"dead\":" << nDead << ",\"cycles\":[";
    for (size_t i = 0; i < b.cycles.size(); i++) {
        if (i) o << ",";
        o << "[";
        for (size_t j = 0; j < b.cycles[i].size(); j++) {
            if (j) o << ",";
            o << "\"" << jesc(b.cycles[i][j]) << "\"";
        }
        o << "]";
    }
    o << "]}\n";
    o << "}\n";
    return o.str();
}

} // namespace

std::string emitGraphJson(const std::vector<GraphUnit>& units,
                          const std::string& rootPath, bool whole) {
    Builder b;
    b.whole = whole;
    for (auto& u : units) b.collectNodes(u);
    for (auto& u : units) b.collectEdges(u);
    for (auto& u : units) b.collectImports(u);
    b.analyze();
    b.findCycles();
    return toJson(b, rootPath, whole);
}

// ============================================================
// 自包含 HTML 查看器：把 JSON 数据内嵌进无外部依赖的力导向图页面
// ============================================================
static const char* kViewerTemplate =
#ifdef __INTELLISENSE__
    ""   // IntelliSense 占位：其 tag 解析器不展开“#include 进字符串字面量”，
         // 会误报 expected an expression。真实内容见 proggraph_viewer.inc（编译期用下面分支）。
#else
#include "proggraph_viewer.inc"
#endif
;

std::string emitGraphHtml(const std::string& json) {
    std::string tpl = kViewerTemplate;
    const std::string marker = "/*__GRAPH_DATA__*/";
    auto pos = tpl.find(marker);
    if (pos != std::string::npos) tpl.replace(pos, marker.size(), json);
    return tpl;
}

