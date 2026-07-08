#include "shader_sema.h"
#include "error.h"

#include <string>
#include <unordered_set>
#include <vector>

// ============================================================
// shader_sema 实现（syntax-s 一期）
// ============================================================
// 两类检查：
//   1) 子集强制：递归遍历阶段 / 辅助函数体，遇禁用构造即报错。
//   2) 基础结构检查：指针/胖指针/瘦指针类型禁用、swizzle 字母合法性、
//      资源绑定 (set,binding) 冲突。
// 类型推导与完整类型检查留待后续（需引入向量/矩阵类型系统）。
// ============================================================

namespace {

[[noreturn]] void bad(int line, const std::string& what) {
    throw CompileError{"shader 方言不支持" + what, line};
}

// swizzle 字母集合：xyzw / rgba / stpq。
bool inSet(char c, const char* set) {
    for (const char* p = set; *p; ++p) if (*p == c) return true;
    return false;
}

// 校验一个成员访问名是否为「非法 swizzle」。返回 true 表示确定非法。
// 仅当整个名字都落在某个 swizzle 字母表内时才当作 swizzle 处理；
// 含非 swizzle 字符者视为普通结构体成员，放行（无类型信息，保守不误报）。
bool illegalSwizzle(const std::string& m) {
    if (m.empty() || m.size() > 4) {
        // 全部字符均为 swizzle 字母且长度 >4 才算非法（否则可能是普通成员名）
        if (m.size() > 4) {
            for (char c : m)
                if (!inSet(c, "xyzw") && !inSet(c, "rgba") && !inSet(c, "stpq"))
                    return false;   // 含非 swizzle 字符 → 普通成员，放行
            return true;            // 纯 swizzle 字母但超长 → 非法
        }
        return false;
    }
    const char* sets[] = {"xyzw", "rgba", "stpq"};
    for (const char* s : sets) {
        bool all = true;
        for (char c : m) if (!inSet(c, s)) { all = false; break; }
        if (all) return false;      // 完全落在单一集合 → 合法 swizzle
    }
    // 是否所有字符都属于某个 swizzle 集合（但跨集合混用）？
    for (char c : m)
        if (!inSet(c, "xyzw") && !inSet(c, "rgba") && !inSet(c, "stpq"))
            return false;           // 含非 swizzle 字符 → 普通成员，放行
    return true;                    // 纯 swizzle 字母但跨集合混用 → 非法
}

struct Checker {
    int f8Line = 0;   // 首次出现 f8(double) 类型的行号（0 = 未用）；供能力门控查表。
    int uintLine = 0; // 首次出现 u*/uvec* 无符号类型的行号（GLSL ES 100 无 uint）。

    void expr(const Expr* e) {
        if (!e) return;
        switch (e->kind) {
            case Expr::Await:  bad(e->line, " await（GPU 无异步）");
            case Expr::Async:  bad(e->line, " async（GPU 无异步）");
            case Expr::Sync:   bad(e->line, " sync（GPU 无异步）");
            case Expr::FncLit: bad(e->line, "函数字面量 / 闭包");
            case Expr::Unary:
                if (e->op == "&") bad(e->line, "取址运算符 &（无指针）");
                if (e->op == "*") bad(e->line, "解引用运算符 *（无指针）");
                break;
            case Expr::Member:
                if (e->op == "->") bad(e->line, "指针成员访问 ->（无指针）");
                if (illegalSwizzle(e->text))
                    throw CompileError{"非法 swizzle '." + e->text +
                        "'（分量须同属 xyzw / rgba / stpq 之一且不超过 4 个）", e->line};
                break;
            case Expr::Cast:
                if (e->castPtr > 0 || e->castFat || e->castThin)
                    bad(e->line, "指针类型转换");
                break;
            default: break;
        }
        // 递归子表达式
        expr(e->a.get()); expr(e->b.get()); expr(e->c.get());
        for (auto& x : e->args) expr(x.get());
    }

    void checkType(const TypeRef& t, int line) {
        if (t.ptr > 0)   bad(line, "裸指针 T&");
        if (t.fat)       bad(line, t.thin ? "瘦指针 T*" : "自动指针 T@");
        if (t.autoFree)  bad(line, "单例指针 T@1");
        if (t.fnKind != TypeRef::FncKind::None) bad(line, "函数指针字段");
        if (t.name == "f8" && !f8Line) f8Line = line;   // f8 双精度：能力门控用（见 shaderSemaCheck）
        if (!uintLine &&
            (t.name == "u1" || t.name == "u2" || t.name == "u4" ||
             t.name == "uvec2" || t.name == "uvec3" || t.name == "uvec4"))
            uintLine = line;                            // uint 族：同上
    }

    void stmt(const Stmt* s) {
        if (!s) return;
        switch (s->kind) {
            case Stmt::TlsS:    bad(s->line, "线程局部变量 tls");
            case Stmt::RunS:    bad(s->line, "线程 run");
            case Stmt::DoneS:   bad(s->line, "异步 done");
            case Stmt::FormS:   bad(s->line, "分布式 token form");
            case Stmt::BackS:   bad(s->line, "反向遍历 back");
            case Stmt::PrintS:  bad(s->line, "print 输出");
            case Stmt::AssertS: bad(s->line, "assert 断言");
            case Stmt::RetCallS:bad(s->line, "ret 调用糖");
            case Stmt::GotoS:   bad(s->line, "goto 跳转");
            case Stmt::LabelS:  bad(s->line, "标签 label");
            case Stmt::MixS:    bad(s->line, "mix 宏展开");
            case Stmt::InlineDefS: bad(s->line, "inl 内联块");
            case Stmt::FinalS:  bad(s->line, "final 退出钩子");
            case Stmt::VarS:
            case Stmt::LetS:
                for (const auto& f : s->decls) {
                    checkType(f.type, f.line ? f.line : s->line);
                    expr(f.init.get());
                }
                break;
            case Stmt::ExprS:
            case Stmt::ReturnS:
                expr(s->expr.get());
                break;
            case Stmt::IfS:
            case Stmt::WhileS:
            case Stmt::DoWhileS:
                expr(s->expr.get());
                for (auto& b : s->body) stmt(b.get());
                for (auto& b : s->elseBody) stmt(b.get());
                break;
            case Stmt::ForS:
                expr(s->forInit.get()); expr(s->forCond.get()); expr(s->forStep.get());
                expr(s->forColl.get()); expr(s->forRangeLo.get()); expr(s->forRangeHi.get());
                for (auto& b : s->body) stmt(b.get());
                break;
            case Stmt::CaseS:
                expr(s->expr.get());
                for (auto& arm : s->caseArms) {
                    for (auto& l : arm.labels) expr(l.get());
                    for (auto& b : arm.body) stmt(b.get());
                }
                break;
            default:
                // BreakS/ContinueS 等无需检查
                break;
        }
    }

    void func(const Decl& d) {
        // 参数与返回类型的指针/函数指针禁用
        if (d.structCommon.type) checkType(*d.structCommon.type, d.line);
        for (const auto& p : d.structCommon.fields) checkType(p.type, d.line);
        for (const auto& s : d.body) stmt(s.get());
    }
};

} // namespace

void shaderSemaCheck(Program& prog) {
    Checker c;

    // 资源绑定冲突检测：同一 (set,binding) 不可重复占用。
    std::unordered_set<std::string> seenBind;

    // 已用能力集（Cap + 首次出现行号）：供后续按声明目标查能力表门控。
    std::vector<std::pair<Cap, int>> usedCaps;
    auto useCap = [&](Cap cap, int line) {
        for (auto& u : usedCaps) if (u.first == cap) return;   // 每种能力只记首次
        usedCaps.push_back({cap, line});
    };

    for (const auto& d : prog.decls) {
        if (!d) continue;

        // 结构体字段（含 I/O 与资源块）的类型子集检查 + 内建语义能力采集
        if (d->kind == Decl::StructD || d->kind == Decl::UnionD) {
            for (const auto& f : d->structCommon.fields) {
                int fl = f.line ? f.line : d->line;
                c.checkType(f.type, fl);
                if (f.shaderAttr && !f.shaderAttr->builtin.empty()) {
                    const std::string& b = f.shaderAttr->builtin;
                    if (b == "vertex_id" || b == "instance_id")
                        useCap(Cap::VertexIdBuiltin, fl);
                    else if (b == "frag_depth")
                        useCap(Cap::FragDepthBuiltin, fl);
                }
            }
        }

        // 资源块 / 全局资源（结构体或 var）的绑定冲突 + 能力采集
        if (d->kind == Decl::StructD || d->kind == Decl::UnionD || d->kind == Decl::VarD) {
            if (d->shaderAttr) {
                const auto* a = d->shaderAttr.get();
                if (a->res != ShaderDeclAttr::Push &&
                    a->set >= 0 && a->binding >= 0) {
                    std::string key = std::to_string(a->set) + ":" +
                                      std::to_string(a->binding);
                    if (!seenBind.insert(key).second)
                        throw CompileError{"资源绑定冲突：(set=" +
                            std::to_string(a->set) + ", binding=" +
                            std::to_string(a->binding) + ") 已被占用", d->line};
                }
                if (a->res == ShaderDeclAttr::Storage) useCap(Cap::StorageBuffer, d->line);
                if (a->res == ShaderDeclAttr::Push)    useCap(Cap::PushConstant, d->line);
                if (a->set >= 1)                       useCap(Cap::DescriptorSet, d->line);
            }
        }

        // 所有函数（阶段入口 + 辅助函数）走子集检查
        if (d->kind == Decl::FuncD) {
            c.func(*d);
            if (d->shaderStage == ShaderStage::Comp) useCap(Cap::ComputeStage, d->line);
            // frag 多输出（返回结构体含 ≥2 个非 builtin 字段）→ MRT 能力
            if (d->shaderStage == ShaderStage::Frag && d->structCommon.type) {
                for (const auto& rd : prog.decls) {
                    if (!rd || rd->kind != Decl::StructD ||
                        rd->name != d->structCommon.type->name) continue;
                    int outs = 0;
                    for (const auto& f : rd->structCommon.fields)
                        if (!f.shaderAttr || f.shaderAttr->builtin.empty()) outs++;
                    if (outs >= 2) useCap(Cap::MultiRenderTarget, d->line);
                    break;
                }
            }
        }
    }

    if (c.f8Line)   useCap(Cap::DoubleType, c.f8Line);   // f8 双精度（由 checkType 捕获）
    if (c.uintLine) useCap(Cap::UintType, c.uintLine);   // uint 族（同上）

    // 已用能力集存入 prog：codegen_glsl 据此对经扩展满足的能力发射 #extension。
    prog.shaderUsedCaps.clear();
    for (const auto& u : usedCaps) prog.shaderUsedCaps.push_back(u.first);

    // ---- 能力门控（syntax-s §13.1）----
    // 契约制：声明的每个目标都必须支持所用能力（核心版本或声明的替代扩展
    // 任一途径），任一不满足即硬报错；报错文案告知两条途径的补救方式。
    for (const auto& t : prog.shaderTargets) {
        if (t.version == 0 && !t.profile.empty())
            continue;   // profile 待加载（codegen_glsl 加载后会重跑门控）
        for (const auto& u : usedCaps) {
            if (capSupported(u.first, t)) continue;
            const CapReq& q = capReq(u.first, t.api);
            std::string need;
            if (q.core < 0 && !q.ext)
                need = std::string(glApiName(t.api)) + " 永不支持";
            else {
                if (q.core >= 0)
                    need = std::string(glApiName(t.api)) + "≥" + std::to_string(q.core);
                if (q.ext) {
                    if (!need.empty()) need += " 或 ";
                    need += std::string("扩展 ") + q.ext +
                            "（caps profile 声明，需版本≥" +
                            std::to_string(q.extFrom) + "）";
                }
            }
            throw CompileError{"目标 " + glTargetTag(t) +
                " 不支持 " + capRow(u.first).name + "（需 " + need + "）", u.second};
        }
    }
}
