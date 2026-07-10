#include "shader_spec.h"

#include <algorithm>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "error.h"

// ============================================================
// shader_spec 实现 —— 词法层单态化（见 shader_spec.h / spec.md）
// ============================================================

namespace {

// 有体 spec 的接口同构检查（spec.md §6.2）：扫描分支体中的 @def 定义，
// 同名 @def 跨分支的**字段名序列**必须一致（字段类型允许不同 —— 这正是
// 分岔的意义）。token 级轻量扫描：KwDef Ident ... { 行首 Ident ':' 为字段 }。
std::vector<std::pair<std::string, std::vector<std::string>>>
scanDefs(const std::vector<Token>& body) {
    std::vector<std::pair<std::string, std::vector<std::string>>> out;
    for (size_t i = 0; i < body.size(); i++) {
        if (body[i].kind != Tok::KwDef) continue;
        if (!(i + 1 < body.size() && body[i + 1].kind == Tok::Ident)) continue;
        std::string dn = body[i + 1].text;
        size_t j = i + 2;
        while (j < body.size() && body[j].kind != Tok::LBrace &&
               body[j].kind != Tok::Newline) j++;
        if (j >= body.size() || body[j].kind != Tok::LBrace) continue;   // 非结构体 def
        int bd = 1;
        j++;
        std::vector<std::string> fields;
        bool atLineStart = true;   // 行首（前一有效 token 是 Newline/LBrace/Indent）
        while (j < body.size() && bd > 0) {
            const Token& t = body[j];
            if (t.kind == Tok::LBrace) { bd++; atLineStart = false; }
            else if (t.kind == Tok::RBrace) { bd--; atLineStart = false; }
            else if (t.kind == Tok::Newline || t.kind == Tok::Indent ||
                     t.kind == Tok::Dedent || t.kind == Tok::Comma) atLineStart = true;
            else {
                if (bd == 1 && atLineStart && t.kind == Tok::Ident &&
                    j + 1 < body.size() && body[j + 1].kind == Tok::Colon)
                    fields.push_back(t.text);
                atLineStart = false;
            }
            j++;
        }
        out.emplace_back(std::move(dn), std::move(fields));
        i = j - 1;
    }
    return out;
}

void checkBodyIsomorphism(const ShaderSpecDim& d) {
    // defName → (首见分支下标, 字段名序列)
    std::unordered_map<std::string, std::pair<size_t, std::vector<std::string>>> first;
    for (size_t bi = 0; bi < d.bodies.size(); bi++) {
        for (auto& [dn, fields] : scanDefs(d.bodies[bi])) {
            auto it = first.find(dn);
            if (it == first.end()) { first.emplace(dn, std::make_pair(bi, fields)); continue; }
            if (it->second.second != fields)
                throw CompileError("spec 维度 '" + d.name + "' 分支 '" +
                                   d.values[it->second.first] + "'/'" + d.values[bi] +
                                   "' 的 @def " + dn + " 接口不同构（字段名集合不一致）",
                                   d.line);
        }
    }
}

}  // namespace

ShaderSpecSet extractShaderSpecs(std::vector<Token>& toks) {
    ShaderSpecSet set;
    std::unordered_set<std::string> seen;   // 维度重名检查
    std::vector<Token> out;
    out.reserve(toks.size());
    int depth = 0;              // Indent/Dedent 配对深度（spec/use 仅顶层有效）
    bool atStmtStart = true;    // 语句首（行首）判定

    for (size_t i = 0; i < toks.size();) {
        const Token& t = toks[i];
        if (t.kind == Tok::Indent)  { depth++; out.push_back(t); i++; atStmtStart = true; continue; }
        if (t.kind == Tok::Dedent)  { depth--; out.push_back(t); i++; atStmtStart = true; continue; }
        if (t.kind == Tok::Newline) { out.push_back(t); i++; atStmtStart = true; continue; }

        // ---- spec 维度声明（无体 P1 / 有体 P3）----
        if (depth == 0 && atStmtStart && t.kind == Tok::Ident && t.text == "spec") {
            auto fail = [&](const std::string& m) -> void { throw CompileError(m, t.line); };
            size_t j = i + 1;
            if (!(j < toks.size() && toks[j].kind == Tok::Ident))
                fail("spec 后期望维度名，如 spec TEX_2D in [sampler2D, samplerExternalOES]");
            ShaderSpecDim d;
            d.name = toks[j].text;
            d.line = t.line;
            j++;
            std::unordered_set<std::string> vset;   // 取值/标签重复检查

            if (j < toks.size() && toks[j].kind == Tok::Colon) {
                // ---- 有体 spec：spec NAME: + 缩进分支块（标签即取值集合）----
                d.hasBody = true;
                j++;
                if (j < toks.size() && toks[j].kind == Tok::Newline) j++;
                if (!(j < toks.size() && toks[j].kind == Tok::Indent))
                    fail("有体 spec 期望缩进分支块（标签: + 缩进体）");
                j++;
                while (true) {
                    while (j < toks.size() && toks[j].kind == Tok::Newline) j++;
                    if (j < toks.size() && toks[j].kind == Tok::Dedent) { j++; break; }
                    if (!(j < toks.size() && toks[j].kind == Tok::Ident))
                        fail("有体 spec 分支期望标签标识符");
                    std::string lbl = toks[j].text;
                    int lblLine = toks[j].line;
                    if (!vset.insert(lbl).second)
                        fail("spec 维度 '" + d.name + "' 分支标签重复：" + lbl);
                    j++;
                    if (!(j < toks.size() && toks[j].kind == Tok::Colon))
                        fail("分支标签 '" + lbl + "' 后期望 ':'");
                    j++;
                    if (j < toks.size() && toks[j].kind == Tok::Newline) j++;
                    if (!(j < toks.size() && toks[j].kind == Tok::Indent))
                        throw CompileError("spec 分支 '" + lbl + "' 体为空", lblLine);
                    j++;
                    std::vector<Token> body;
                    int bd = 0;                       // 分支体内嵌套缩进平衡
                    while (j < toks.size()) {
                        if (toks[j].kind == Tok::Indent) bd++;
                        else if (toks[j].kind == Tok::Dedent) {
                            if (bd == 0) { j++; break; }   // 分支体结束
                            bd--;
                        }
                        body.push_back(toks[j]);
                        j++;
                    }
                    if (!body.empty() && body.back().kind != Tok::Newline)
                        body.push_back(Token{Tok::Newline, "", lblLine, false});
                    d.values.push_back(lbl);
                    d.bodies.push_back(std::move(body));
                }
                checkBodyIsomorphism(d);              // 同名 @def 接口同构（spec.md §6.2）
            } else {
                // ---- 无体 spec：spec NAME in [值, ...] ----
                if (!(j < toks.size() && toks[j].kind == Tok::Ident && toks[j].text == "in"))
                    fail("spec 维度声明期望 in [值, ...] 或 ':' 分支块");
                j++;
                if (!(j < toks.size() && toks[j].kind == Tok::LBracket))
                    fail("spec ... in 后期望 '['");
                j++;
                while (true) {
                    while (j < toks.size() && toks[j].kind == Tok::Newline) j++;   // 括号内换行忽略
                    if (j < toks.size() && toks[j].kind == Tok::RBracket) { j++; break; }
                    if (!(j < toks.size() && toks[j].kind == Tok::Ident))
                        fail("spec 取值列表期望标识符（类型名/符号名）");
                    if (!vset.insert(toks[j].text).second)
                        fail("spec 维度 '" + d.name + "' 取值重复：" + toks[j].text);
                    d.values.push_back(toks[j].text);
                    j++;
                    while (j < toks.size() && toks[j].kind == Tok::Newline) j++;
                    if (j < toks.size() && toks[j].kind == Tok::Comma) { j++; continue; }
                    if (j < toks.size() && toks[j].kind == Tok::RBracket) { j++; break; }
                    fail("spec 取值列表期望 ',' 或 ']'");
                }
                if (j < toks.size() && toks[j].kind == Tok::Newline) j++;   // 吃掉行尾
            }

            if (d.values.empty())
                fail("spec 维度 '" + d.name + "' 取值集合为空");
            if (!seen.insert(d.name).second)
                fail("spec 维度重名：" + d.name);
            d.insertPos = out.size();                 // 有体：分支体回插位置（顶层原位）
            set.dims.push_back(std::move(d));
            i = j;
            atStmtStart = true;
            continue;
        }

        // ---- use 白名单表（P2）：use 维度序 + 缩进数据行 ----
        if (depth == 0 && atStmtStart && t.kind == Tok::Ident && t.text == "use") {
            auto fail = [&](const std::string& m) -> void { throw CompileError(m, t.line); };
            if (set.hasUse) fail("use 表重复声明（全源仅一张白名单表）");
            size_t j = i + 1;
            while (true) {
                if (!(j < toks.size() && toks[j].kind == Tok::Ident))
                    fail("use 表头期望维度名");
                set.use.dims.push_back(toks[j].text);
                j++;
                if (j < toks.size() && toks[j].kind == Tok::Comma) { j++; continue; }
                break;
            }
            if (j < toks.size() && toks[j].kind == Tok::Newline) j++;
            if (!(j < toks.size() && toks[j].kind == Tok::Indent))
                fail("use 期望缩进数据行（每行一个实例组合）");
            j++;
            while (true) {
                while (j < toks.size() && toks[j].kind == Tok::Newline) j++;
                if (j < toks.size() && toks[j].kind == Tok::Dedent) { j++; break; }
                std::vector<std::string> row;
                int rowLine = j < toks.size() ? toks[j].line : t.line;
                while (true) {
                    if (!(j < toks.size() && toks[j].kind == Tok::Ident))
                        throw CompileError("use 数据行期望取值标识符", rowLine);
                    row.push_back(toks[j].text);
                    j++;
                    if (j < toks.size() && toks[j].kind == Tok::Comma) { j++; continue; }
                    break;
                }
                if (j < toks.size() && toks[j].kind == Tok::Newline) j++;
                set.use.rows.push_back(std::move(row));
                set.use.rowLines.push_back(rowLine);
            }
            set.use.line = t.line;
            set.hasUse = true;
            i = j;
            atStmtStart = true;
            continue;
        }

        out.push_back(t);
        i++;
        atStmtStart = false;
    }

    toks.swap(out);
    return set;
}

std::vector<ShaderSpecCombo> shaderSpecCombos(const ShaderSpecSet& set) {
    const auto& dims = set.dims;

    if (!set.hasUse) {
        // 默认：全笛卡尔积（声明序）；无维度 = 单个平凡实例。
        std::vector<ShaderSpecCombo> out{ ShaderSpecCombo{} };
        for (const auto& d : dims) {
            std::vector<ShaderSpecCombo> next;
            next.reserve(out.size() * d.values.size());
            for (const auto& c : out)
                for (const auto& v : d.values) {
                    ShaderSpecCombo c2 = c;
                    c2.emplace_back(d.name, v);
                    next.push_back(std::move(c2));
                }
            out.swap(next);
        }
        return out;
    }

    // ---- use 白名单模式：校验 + 白名单行 × 未列维度全集 ----
    const auto& u = set.use;
    std::unordered_map<std::string, const ShaderSpecDim*> dm;
    for (const auto& d : dims) dm[d.name] = &d;
    std::unordered_set<std::string> cols;
    for (const auto& n : u.dims) {
        if (!dm.count(n))
            throw CompileError("use 表头维度 '" + n + "' 未声明", u.line);
        if (!cols.insert(n).second)
            throw CompileError("use 表头维度 '" + n + "' 重复", u.line);
    }
    if (u.rows.empty())
        throw CompileError("use 表为空（至少一行实例组合）", u.line);

    std::set<std::vector<std::string>> seenRows;   // 重复行检测
    std::set<ShaderSpecCombo> seenCombos;          // 跨行展开撞车去重（保留首见）
    std::vector<ShaderSpecCombo> out;
    for (size_t ri = 0; ri < u.rows.size(); ri++) {
        const auto& row = u.rows[ri];
        int rl = u.rowLines[ri];
        if (row.size() != u.dims.size())
            throw CompileError("use 数据行取值个数（" + std::to_string(row.size()) +
                               "）与表头维度数（" + std::to_string(u.dims.size()) + "）不符", rl);
        std::unordered_map<std::string, std::string> pin;   // 该行钉住的维度取值
        for (size_t k = 0; k < row.size(); k++) {
            const ShaderSpecDim* d = dm[u.dims[k]];
            if (std::find(d->values.begin(), d->values.end(), row[k]) == d->values.end())
                throw CompileError("use 取值 '" + row[k] + "' 不属于维度 '" + d->name + "'", rl);
            pin[u.dims[k]] = row[k];
        }
        if (!seenRows.insert(row).second)
            throw CompileError("use 数据行重复", rl);
        // 未列维度取全集，与钉住值做积（组合内按声明序）
        std::vector<ShaderSpecCombo> part{ ShaderSpecCombo{} };
        for (const auto& d : dims) {
            std::vector<ShaderSpecCombo> next;
            auto itp = pin.find(d.name);
            for (const auto& c : part) {
                if (itp != pin.end()) {
                    ShaderSpecCombo c2 = c;
                    c2.emplace_back(d.name, itp->second);
                    next.push_back(std::move(c2));
                } else {
                    for (const auto& v : d.values) {
                        ShaderSpecCombo c2 = c;
                        c2.emplace_back(d.name, v);
                        next.push_back(std::move(c2));
                    }
                }
            }
            part.swap(next);
        }
        for (auto& c : part)
            if (seenCombos.insert(c).second) out.push_back(std::move(c));
    }
    return out;
}

std::vector<Token> applyShaderSpec(const std::vector<Token>& toks,
                                   const ShaderSpecSet& set,
                                   const ShaderSpecCombo& combo) {
    std::vector<Token> out = toks;

    // 有体维度：选中分支体按 insertPos 降序回插原位（降序避免位移干扰）。
    std::vector<std::pair<size_t, const std::vector<Token>*>> ins;
    for (const auto& d : set.dims) {
        if (!d.hasBody) continue;
        for (const auto& kv : combo) {
            if (kv.first != d.name) continue;
            for (size_t vi = 0; vi < d.values.size(); vi++)
                if (d.values[vi] == kv.second) { ins.emplace_back(d.insertPos, &d.bodies[vi]); break; }
            break;
        }
    }
    std::sort(ins.begin(), ins.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& [pos, body] : ins)
        out.insert(out.begin() + pos, body->begin(), body->end());

    if (combo.empty()) return out;

    // 标识符替换：维度名 → 取值（含回插分支体内的其他维度直代）。
    std::unordered_map<std::string, std::string> m;
    for (const auto& kv : combo) m.emplace(kv.first, kv.second);
    for (auto& t : out) {
        if (t.kind != Tok::Ident) continue;
        auto it = m.find(t.text);
        if (it != m.end()) t.text = it->second;
    }
    return out;
}

std::string shaderSpecLabel(const ShaderSpecCombo& combo) {
    std::string s;
    for (const auto& kv : combo) {
        if (!s.empty()) s += ".";
        s += kv.second;
    }
    return s;
}
