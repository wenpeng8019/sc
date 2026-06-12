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
    ['rpc', '定义伪形参函数（参数/返回值展开为同名结构体）'],
    ['var', '定义变量'],
    ['let', '定义常量'],
    ['inc', '引入头文件（对齐 C 的 #include）'],
    ['return', '返回'],
    ['if', '条件分支'],
    ['else', '否则分支'],
    ['while', 'while 循环'],
    ['do', 'do-while 循环起始'],
    ['for', 'for 循环'],
    ['case', '分支匹配（替代 switch）'],
    ['through', 'case 分支贯穿到下一分支'],
    ['goto', '跳转到标签'],
    ['sizeof', '返回表达式或类型的字节大小'],
    ['offsetof', '返回字段在类型中的偏移量'],
    ['break', '跳出循环'],
    ['continue', '继续下一次循环'],
    ['run', '以 rpc 调用创建线程（run 调用[, &t]，需 inc m.sc）'],
];

const LITERALS = [
    ['true', '布尔真'],
    ['false', '布尔假'],
    ['nil', '空指针常量'],
];

const TYPES = [
    ['i1', 'int8_t'], ['i2', 'int16_t'], ['i4', 'int32_t'], ['i8', 'int64_t'],
    ['u1', 'uint8_t'], ['u2', 'uint16_t'], ['u4', 'uint32_t'], ['u8', 'uint64_t'],
    ['bool', '布尔（u1 语义别名，true/false）'],
    ['char', '字符（与 C 字符串互操作，区别于 i1/u1）'],
    ['f4', 'float'], ['f8', 'double'],
    ['va_list', '可变参数列表类型（透传 stdarg.h）'],
    ['adt_obj', '内置 ADT 公共对象头'],
    ['string', '内置 ADT 字符串对象'],
    ['list', '内置 ADT 列表对象'],
    ['dict', '内置 ADT 字典对象'],
    ['dim', '内置 ADT 多维数组对象'],
    ['json', '内置 ADT JSON 对象'],
];

// ---------------- scc 调用与 AST 缓存 ----------------
function findScc() {
    const cfg = vscode.workspace.getConfiguration('sc').get('sccPath') ||
                vscode.workspace.getConfiguration('scAst').get('sccPath');
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
            case 'rpc':
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
    return last && (last.k === 'fnc' || last.k === 'rpc') ? last : null;
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
        const m = doc.lineAt(i).text.match(/^\s*@?(def|fnc|rpc|var|let)\s+([A-Za-z_]\w*)/);
        if (m && !seen.has(m[2])) {
            seen.add(m[2]);
            const kind = m[1] === 'def' ? K.Class : (m[1] === 'fnc' || m[1] === 'rpc') ? K.Function
                       : m[1] === 'let' ? K.Constant : K.Variable;
            const it = new vscode.CompletionItem(m[2], kind);
            it.detail = m[1];
            it.sortText = '1' + m[2];
            items.push(it);
        }
    }
    return items;
}

function stripAnsi(s) {
    return String(s || '').replace(/\x1b\[[0-9;]*m/g, '');
}

function getWordAt(doc, pos) {
    const r = doc.getWordRangeAtPosition(pos, /[A-Za-z_]\w*/);
    if (!r) return null;
    return { word: doc.getText(r), range: r };
}

function toMarkdownType(detail) {
    const d = (detail || '').trim();
    return d ? `类型: ${d}` : '类型信息不可用';
}

function parseErrorDiagnostic(errText, doc) {
    const s = stripAnsi(errText);
    const lines = s.split('\n');
    let lineNum = 1;
    let msg = s.trim() || '语法/语义错误';

    for (const l of lines) {
        const m = l.match(/:(\d+):\s*错误:\s*(.+)$/);
        if (m) {
            lineNum = Number(m[1]);
            msg = m[2].trim();
            break;
        }
    }
    lineNum = Math.max(1, Math.min(doc.lineCount, lineNum));
    const range = new vscode.Range(lineNum - 1, 0, lineNum - 1, doc.lineAt(lineNum - 1).text.length);
    return new vscode.Diagnostic(range, msg, vscode.DiagnosticSeverity.Error);
}

function lineToPos(doc, oneBasedLine) {
    const l = Math.max(1, Math.min(doc.lineCount, oneBasedLine || 1));
    return new vscode.Position(l - 1, 0);
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
            // 1a. 强转后成员访问：(expr: Type&)-> / (expr: Type).
            const cast = before.match(/\(\s*[^()]*:\s*([A-Za-z_]\w*)\s*&*\s*\)\s*(\.|->)\w*$/);
            if (cast) {
                if (!idx) return [];
                const t = resolveStruct(idx, cast[1]);
                if (!t) return [];
                return (t.c || []).map(f =>
                        mkItem(f.n, K.Field, (f.d || '').trim(), '0'));
            }
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

    // 文档符号：展示顶层声明，支持大纲导航
    context.subscriptions.push(vscode.languages.registerDocumentSymbolProvider('sc', {
        async provideDocumentSymbols(doc) {
            const ast = await getAst(doc, -1);
            if (!ast) return [];
            const kindMap = {
                struct: vscode.SymbolKind.Struct,
                union: vscode.SymbolKind.Struct,
                enum: vscode.SymbolKind.Enum,
                alias: vscode.SymbolKind.TypeParameter,
                fnctype: vscode.SymbolKind.Interface,
                fnc: vscode.SymbolKind.Function,
                var: vscode.SymbolKind.Variable,
                let: vscode.SymbolKind.Constant,
            };
            const out = [];
            for (const n of ast.c || []) {
                const p = lineToPos(doc, n.l || 1);
                const r = new vscode.Range(p, p);
                const name = n.n || n.k;
                out.push(new vscode.DocumentSymbol(name, n.d || n.k,
                    kindMap[n.k] || vscode.SymbolKind.Object, r, r));
            }
            return out;
        }
    }));

    // 悬停：显示符号类型与来源
    context.subscriptions.push(vscode.languages.registerHoverProvider('sc', {
        async provideHover(doc, pos) {
            const w = getWordAt(doc, pos);
            if (!w) return null;
            const ast = await getAst(doc, pos.line);
            if (!ast) return null;
            const idx = buildIndex(ast);
            const locals = scopeVars(idx, pos.line + 1);
            const local = locals.get(w.word);
            if (local) {
                return new vscode.Hover(new vscode.MarkdownString(
                    `**${w.word}**\n\n局部符号\n\n${toMarkdownType(local.detail)}`));
            }
            const g = idx.globals.get(w.word);
            if (g) {
                return new vscode.Hover(new vscode.MarkdownString(
                    `**${w.word}**\n\n全局符号\n\n${toMarkdownType(g.detail)}`));
            }
            const t = idx.types.get(w.word);
            if (t) {
                return new vscode.Hover(new vscode.MarkdownString(
                    `**${w.word}**\n\n类型定义 (${t.k})`));
            }
            const f = idx.funcs.get(w.word);
            if (f) {
                return new vscode.Hover(new vscode.MarkdownString(
                    `**${w.word}**\n\n函数\n\n${(f.d || '').trim()}`));
            }
            return null;
        }
    }));

    // 定义跳转：支持类型/函数/全局符号跳转
    context.subscriptions.push(vscode.languages.registerDefinitionProvider('sc', {
        async provideDefinition(doc, pos) {
            const w = getWordAt(doc, pos);
            if (!w) return null;
            const ast = await getAst(doc, pos.line);
            if (!ast) return null;
            const idx = buildIndex(ast);
            const lookup = [idx.types.get(w.word), idx.funcs.get(w.word), idx.globals.get(w.word)];
            for (const hit of lookup) {
                if (!hit) continue;
                const p = lineToPos(doc, hit.l || hit.line || 1);
                return new vscode.Location(doc.uri, p);
            }
            return null;
        }
    }));

    // 文档格式化：调用 scc --emit-sc 生成规范化源码
    context.subscriptions.push(vscode.languages.registerDocumentFormattingEditProvider('sc', {
        async provideDocumentFormattingEdits(doc) {
            try {
                const formatted = await runScc(['-', '--emit-sc'], doc.getText());
                const full = new vscode.Range(
                    new vscode.Position(0, 0),
                    doc.lineAt(Math.max(0, doc.lineCount - 1)).range.end);
                return [vscode.TextEdit.replace(full, formatted)];
            } catch (e) {
                vscode.window.showErrorMessage('SC 格式化失败: ' + String(e.message || e));
                return [];
            }
        }
    }));

    // 实时诊断：在编辑时运行 scc --ast，失败时产出问题面板诊断
    const diagnostics = vscode.languages.createDiagnosticCollection('sc');
    context.subscriptions.push(diagnostics);
    const timers = new Map();
    const refreshDiagnostics = async (doc) => {
        if (!doc || doc.languageId !== 'sc') return;
        try {
            await runScc(['-', '--ast'], doc.getText());
            diagnostics.set(doc.uri, []);
        } catch (e) {
            diagnostics.set(doc.uri, [parseErrorDiagnostic(String(e.message || e), doc)]);
        }
    };
    const scheduleDiagnostics = (doc) => {
        if (!doc || doc.languageId !== 'sc') return;
        const k = doc.uri.toString();
        const old = timers.get(k);
        if (old) clearTimeout(old);
        timers.set(k, setTimeout(() => {
            timers.delete(k);
            refreshDiagnostics(doc);
        }, 250));
    };

    context.subscriptions.push(vscode.workspace.onDidOpenTextDocument(scheduleDiagnostics));
    context.subscriptions.push(vscode.workspace.onDidChangeTextDocument(e => scheduleDiagnostics(e.document)));
    context.subscriptions.push(vscode.workspace.onDidCloseTextDocument(doc => diagnostics.delete(doc.uri)));

    // 初始化诊断
    for (const d of vscode.workspace.textDocuments) scheduleDiagnostics(d);
}

function deactivate() {}

module.exports = { activate, deactivate };
