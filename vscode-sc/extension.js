// sc 语言自动完成 —— 基于 scc --ast 的作用域感知补全
//
// 补全策略（按优先级）：
//   成员访问上下文（obj. / ptr->）→ 仅列出该结构/联合的字段（子域）
//   类型上下文（: 或 -> 之后）    → 仅列出类型（内置 + 用户 def）
//   普通上下文                    → 可见对象：
//       0. 当前函数的参数与局部变量（仅声明行之前的，子域优先）
//       1. 父域（全局）：var/let、枚举项、函数
//       2. 类型
//       3. 关键字（仅行首）
//
// AST 来源：调用 scc - --ast（与 AST 视图插件相同方式），按文档版本缓存；
// 源码暂时有语法错误时回退到最近一次成功的 AST；scc 不可用时退化为正则扫描。
const vscode = require('vscode');
const cp = require('child_process');
const fs = require('fs');
const path = require('path');

const KEYWORDS = [
    ['def', '定义类型（枚举/结构/联合/别名）'],
    ['fnc', '定义函数或函数类型'],
    ['var', '定义变量'],
    ['let', '定义常量'],
    ['inc', '引入头文件（对齐 C 的 #include）'],
    ['return', '返回'],
    ['if', '条件分支'],
    ['else', '否则分支'],
    ['while', 'while 循环'],
    ['for', 'for 循环'],
    ['case', '分支匹配（替代 switch）'],
    ['through', 'case 分支贯穿到下一分支'],
    ['break', '跳出循环'],
    ['continue', '继续下一次循环'],
];

const LITERALS = [
    ['true', '布尔真'],
    ['false', '布尔假'],
    ['nil', '空指针常量'],
];

const TYPES = [
    ['i1', 'int8_t'], ['i2', 'int16_t'], ['i4', 'int32_t'], ['i8', 'int64_t'],
    ['u1', 'uint8_t'], ['u2', 'uint16_t'], ['u4', 'uint32_t'], ['u8', 'uint64_t'],
    ['b', 'bool / u1'],
    ['f4', 'float'], ['f8', 'double'], ['v', 'void'],
];

// ---------------- scc 调用与 AST 缓存 ----------------
function findScc() {
    const cfg = vscode.workspace.getConfiguration('scAst').get('sccPath');
    if (cfg) return cfg;
    for (const f of vscode.workspace.workspaceFolders || []) {
        const p = path.join(f.uri.fsPath, 'compiler', 'build', 'scc');
        if (fs.existsSync(p)) return p;
    }
    return 'scc';
}

function runScc(args, input) {
    return new Promise((resolve, reject) => {
        const proc = cp.execFile(findScc(), args, { maxBuffer: 16 * 1024 * 1024 },
            (err, stdout, stderr) => {
                if (err) reject(new Error((stderr || err.message).trim()));
                else resolve(stdout);
            });
        proc.stdin.on('error', () => {});
        proc.stdin.write(input);
        proc.stdin.end();
    });
}

// uri → { version, ast }，保留最近一次解析成功的 AST
const astCache = new Map();

// 解析顺序：完整源码 → 抹掉光标行（正在输入的 "p." 等残句会让整个文件解析失败，
// 而声明都在其它行，抹掉后的 AST 足以支撑补全）→ 最近一次成功的 AST
async function getAst(doc, cursorLine) {
    const key = doc.uri.toString();
    const c = astCache.get(key);
    if (c && c.version === doc.version && c.ast) return c.ast;
    const texts = [doc.getText()];
    if (cursorLine >= 0) {
        const lines = doc.getText().split('\n');
        lines[cursorLine] = '';
        texts.push(lines.join('\n'));
    }
    for (const t of texts) {
        try {
            const out = await runScc(['-', '--ast'], t);
            const ast = JSON.parse(out);
            astCache.set(key, { version: doc.version, ast });
            return ast;
        } catch { /* 尝试下一个候选 */ }
    }
    return c ? c.ast : null;  // 编辑中途语法错误时用上一次成功的 AST
}

// ---------------- AST 索引与作用域 ----------------
// 从字段 detail（如 "&: i4"、"[2][3]: Point"）提取类型名
function typeFromDetail(d) {
    const m = (d || '').match(/:\s*([A-Za-z_]\w*)/);
    return m ? m[1] : null;
}

// 顶层一遍扫描：类型表 / 全局对象表 / 函数表
function buildIndex(ast) {
    const idx = { types: new Map(), globals: new Map(), funcs: new Map(), topLevel: [], externSymbols: new Set(ast.e || []) };
    for (const n of ast.c || []) {
        idx.topLevel.push(n);
        switch (n.k) {
            case 'struct': case 'union': case 'alias': case 'fnctype':
                idx.types.set(n.n, n);
                break;
            case 'enum':
                idx.types.set(n.n, n);
                // 枚举项是全局可见常量（与 C 对齐）
                for (const it of n.c || [])
                    idx.globals.set(it.n, { kind: 'enum-item', detail: n.n, line: it.l });
                break;
            case 'fnc':
                idx.funcs.set(n.n, n);
                break;
            case 'var': case 'let':
                for (const it of n.c || [])
                    idx.globals.set(it.n, { kind: n.k, detail: it.d, line: it.l });
                break;
        }
    }
    return idx;
}

// 光标所在的函数节点：顶层声明中起始行 <= 光标行的最后一个，且须是 fnc
function enclosingFunc(idx, line) {
    let last = null;
    for (const n of idx.topLevel) if (n.l && n.l <= line) last = n;
    return last && last.k === 'fnc' ? last : null;
}

// 收集函数内可见的参数与局部变量（局部变量须声明于光标行或之前）
function scopeVars(idx, line) {
    const vars = new Map();
    const fn = enclosingFunc(idx, line);
    if (!fn) return vars;
    const walk = (node) => {
        for (const ch of node.c || []) {
            if (ch.k === 'param') {
                vars.set(ch.n, { kind: 'param', detail: ch.d, line: ch.l });
            } else if (ch.k === 'var' || ch.k === 'let') {
                for (const it of ch.c || [])
                    if (!it.l || it.l <= line)
                        vars.set(it.n, { kind: ch.k, detail: it.d, line: it.l });
            } else if (['if', 'else', 'while', 'for'].includes(ch.k)) {
                walk(ch);  // 进入嵌套块继续收集
            }
        }
    };
    walk(fn);
    return vars;
}

// 类型名 → 结构/联合节点（穿透 alias，最多 8 层防环）
function resolveStruct(idx, name) {
    for (let i = 0; name && i < 8; i++) {
        const t = idx.types.get(name);
        if (!t) return null;
        if (t.k === 'struct' || t.k === 'union') return t;
        if (t.k === 'alias') {
            const m = (t.d || '').match(/->\s*([A-Za-z_]\w*)/);
            name = m ? m[1] : null;
            continue;
        }
        return null;
    }
    return null;
}

// 成员链解析：a.b->c 逐级取字段类型，返回最后一级的结构节点
function resolveChain(idx, vars, chain) {
    const base = vars.get(chain[0]) || idx.globals.get(chain[0]);
    if (!base) return null;
    let typeName = typeFromDetail(base.detail);
    for (let i = 1; i < chain.length; i++) {
        const t = resolveStruct(idx, typeName);
        if (!t) return null;
        const f = (t.c || []).find(x => x.n === chain[i]);
        if (!f) return null;
        typeName = typeFromDetail(f.d);
    }
    return resolveStruct(idx, typeName);
}

// ---------------- 无 AST 时的正则退化方案 ----------------
function fallbackItems(doc) {
    const K = vscode.CompletionItemKind;
    const items = [];
    const seen = new Set();
    for (let i = 0; i < doc.lineCount; i++) {
        const m = doc.lineAt(i).text.match(/^\s*@?(def|fnc|var|let)\s+([A-Za-z_]\w*)/);
        if (m && !seen.has(m[2])) {
            seen.add(m[2]);
            const kind = m[1] === 'def' ? K.Class : m[1] === 'fnc' ? K.Function
                       : m[1] === 'let' ? K.Constant : K.Variable;
            const it = new vscode.CompletionItem(m[2], kind);
            it.detail = m[1];
            it.sortText = '1' + m[2];
            items.push(it);
        }
    }
    return items;
}

// ---------------- 补全提供器 ----------------
function activate(context) {
    const K = vscode.CompletionItemKind;
    const TYPE_KIND = { struct: K.Class, union: K.Class, enum: K.Enum,
                        alias: K.Interface, fnctype: K.Interface };

    const mkItem = (name, kind, detail, sort) => {
        const it = new vscode.CompletionItem(name, kind);
        if (detail) it.detail = detail;
        it.sortText = sort + name;
        return it;
    };

    const provider = {
        async provideCompletionItems(doc, pos) {
            const before = doc.lineAt(pos.line).text.slice(0, pos.character);
            const line = pos.line + 1;  // AST 行号 1-based
            const ast = await getAst(doc, pos.line);
            const idx = ast ? buildIndex(ast) : null;

            // ---- 1. 成员访问上下文：obj. / ptr-> → 仅列子域（字段）----
            const mem = before.match(/([A-Za-z_]\w*(?:(?:\.|->)[A-Za-z_]\w*)*)(\.|->)\w*$/);
            if (mem) {
                if (!idx) return [];
                const chain = mem[1].split(/\.|->/);
                const t = resolveChain(idx, scopeVars(idx, line), chain);
                if (!t) return [];  // 类型未知时不输出噪音
                return (t.c || []).map(f =>
                        mkItem(f.n, K.Field, (f.d || '').trim(), '0'));
            }

            // ---- 2. 类型上下文：: 或 -> 之后 → 仅列类型 ----
            if (/(:|->)\s*\w*$/.test(before)) {
                const items = TYPES.map(([n, c]) => mkItem(n, K.Struct, c, '0'));
                if (idx)
                    for (const [name, t] of idx.types)
                        items.push(mkItem(name, TYPE_KIND[t.k] || K.Class,
                                          'def ' + t.k, '0'));
                return items;
            }

            // ---- 3. 普通上下文：按可见性分层 ----
            if (!idx) {
                const items = fallbackItems(doc);
                for (const [kw, d] of KEYWORDS) items.push(mkItem(kw, K.Keyword, d, '3'));
                for (const [lit, d] of LITERALS) items.push(mkItem(lit, K.Constant, d, '3'));
                return items;
            }
            const items = [];
            // 0层：子域 —— 当前函数的参数与已声明的局部变量
            const locals = scopeVars(idx, line);
            for (const [name, v] of locals)
                items.push(mkItem(name,
                                  v.kind === 'let' ? K.Constant : K.Variable,
                                  (v.kind === 'param' ? '参数 ' : '局部 ') +
                                  (v.detail || '').trim(), '0'));
            // 1层：父域 —— 全局 var/let、枚举项、函数
            for (const [name, v] of idx.globals)
                if (!locals.has(name))
                    items.push(mkItem(name,
                                      v.kind === 'enum-item' ? K.EnumMember
                                    : v.kind === 'let' ? K.Constant : K.Variable,
                                      v.kind === 'enum-item' ? '枚举 ' + v.detail
                                                             : (v.detail || '').trim(),
                                      '1'));
            for (const [name, f] of idx.funcs)
                items.push(mkItem(name, K.Function, ('fnc ' + (f.d || '')).trim(), '1'));
            // 2层：类型
            for (const [name, t] of idx.types)
                items.push(mkItem(name, TYPE_KIND[t.k] || K.Class, 'def ' + t.k, '2'));
            for (const [n, c] of TYPES) items.push(mkItem(n, K.Struct, c, '2'));
            for (const [lit, d] of LITERALS) items.push(mkItem(lit, K.Constant, d, '2'));
            // 3层：关键字（仅行首位置）
            if (/^\s*@?\w*$/.test(before))
                for (const [kw, d] of KEYWORDS) items.push(mkItem(kw, K.Keyword, d, '3'));
            return items;
        },
    };

    context.subscriptions.push(
        vscode.languages.registerCompletionItemProvider('sc', provider, ':', '.', '>'));

    context.subscriptions.push(vscode.languages.registerDocumentSymbolProvider('sc', {
        provideDocumentSymbols(doc) {
            return [];
        }
    }));
}

function deactivate() {}

module.exports = { activate, deactivate };
