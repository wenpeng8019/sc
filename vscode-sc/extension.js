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
    ['tls', '定义线程局部变量（static 存储期，每线程独立实例）'],
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
    ['print', 'C 风格日志输出（print("E: %d", n)，级别前缀 F/E/W/I/D/V，需 inc io.sc）'],
    ['break', '跳出循环'],
    ['continue', '继续下一次循环'],
    ['run', '以 rpc 调用创建线程或入池（run 调用[, &t|pool]，需 inc mt.sc）'],
    ['sync', '同步驱动 rpc 流程：sync work(args) 在当前线程直接执行并返回结果（替代裸 rpc 调用）'],
    ['this', '方法体内的接收者指针（fnc T::m 中访问 this->字段）'],
    ['cls', '定义类（单一分派器 + 全局维度选择子 + 三态应答 + 类型擦除引用）'],
    ['dim', '声明维度（类的方法，返回值恒 tril，真正输出经指针出参回填）'],
    ['instanceOf', 'O(1) 身份判定：instanceOf(o: object, TypeName) -> bool'],
];

const LITERALS = [
    ['true', '布尔真'],
    ['false', '布尔假'],
    ['nil', '空指针常量'],
    ['positive', '三态 tril：正/应答（+1）'],
    ['negative', '三态 tril：负/否定（-1）'],
    ['unknown', '三态 tril：未知/不应答（0）'],
];

const TYPES = [
    ['i1', 'int8_t'], ['i2', 'int16_t'], ['i4', 'int32_t'], ['i8', 'int64_t'],
    ['u1', 'uint8_t'], ['u2', 'uint16_t'], ['u4', 'uint32_t'], ['u8', 'uint64_t'],
    ['bool', '布尔（u1 语义别名，true/false）'],
    ['char', '字符（与 C 字符串互操作，区别于 i1/u1）'],
    ['f4', 'float'], ['f8', 'double'],
    ['tril', '三态基础类型（int8_t；字面量 positive/negative/unknown）'],
    ['object', '类型擦除引用（指向任意类对象 _class 槽，既是身份又是分派入口）'],
    ['va_list', '可变参数列表类型（透传 stdarg.h）'],
    ['adt_obj', '内置 ADT 公共对象头'],
    ['string', '内置 ADT 字符串对象；string(值[, 缓存, 大小]) 为格式化关键字'],
    ['list', '内置 ADT 列表对象'],
    ['dict', '内置 ADT 字典对象'],
    ['dim', '内置 ADT 多维数组对象'],
    ['json', '内置 ADT JSON 对象'],
    ['thread', '内置线程对象（run 语句出参，需 inc mt.sc）'],
    ['mutex', '内置互斥锁对象（需 inc mt.sc）'],
    ['cond', '内置条件变量对象（c.wait(&mu) 等待，需 inc mt.sc）'],
    ['pool', '内置线程池对象（run 语句第二参入池，需 inc mt.sc）'],
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

// 文件模式解析：stdin 单步模式（scc -）不走模块图，inc 的 builtins/模块外部声明
// 不在 AST 中；改为写同目录隐藏临时文件走文件模式（inc 解析与真实文件一致）
let astSeq = 0;
async function runAstOnText(doc, text) {
    const dir = path.dirname(doc.uri.fsPath);
    const tmp = path.join(dir, `.sc_ast_${process.pid}_${++astSeq}.tmp.sc`);
    try {
        fs.writeFileSync(tmp, text);
        const out = await runScc([tmp, '--ast'], '');
        return JSON.parse(out);
    } finally {
        try { fs.unlinkSync(tmp); } catch { /* 已删除 */ }
    }
}

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
            const ast = await runAstOnText(doc, t);
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

// 顶层一遍扫描：类型表 / 全局对象表 / 函数表 / 方法表（"T::m"，含 builtins 外部声明）
function buildIndex(ast) {
    const idx = { types: new Map(), globals: new Map(), funcs: new Map(), methods: new Map(), topLevel: [], externSymbols: new Set(ast.e || []) };
    for (const n of ast.c || []) {
        idx.topLevel.push(n);
        switch (n.k) {
            case 'struct': case 'cls': case 'union': case 'alias': case 'fnctype':
                // fnctype 含两类：方法/函数声明（C 侧实现，如 builtins）与函数类型定义
                if (n.k === 'fnctype' && n.n && n.n.includes('::')) idx.methods.set(n.n, n);
                else idx.types.set(n.n, n);
                break;
            case 'enum':
                idx.types.set(n.n, n);
                // 枚举项是全局可见常量（与 C 对齐）
                for (const it of n.c || [])
                    idx.globals.set(it.n, { kind: 'enum-item', detail: n.n, line: it.l });
                break;
            case 'fnc':
            case 'dim':
            case 'rpc':
                if (n.n && n.n.includes('::')) idx.methods.set(n.n, n);
                else idx.funcs.set(n.n, n);
                break;
            case 'var': case 'let': case 'tls':
                for (const it of n.c || [])
                    idx.globals.set(it.n, { kind: n.k, detail: it.d, line: it.l });
                break;
        }
    }
    return idx;
}

// 某类型名下的全部方法：[ [短名, 节点], ... ]
function methodsOf(idx, typeName) {
    const out = [];
    const prefix = typeName + '::';
    for (const [key, m] of idx.methods)
        if (key.startsWith(prefix)) out.push([key.slice(prefix.length), m]);
    return out;
}

// 光标所在的函数节点：顶层声明中起始行 <= 光标行的最后一个，且须是 fnc
// 外部声明（x）行号属于来源文件坐标系，必须跳过，否则错位
function enclosingFunc(idx, line) {
    let last = null;
    for (const n of idx.topLevel) if (!n.x && n.l && n.l <= line) last = n;
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
            } else if (ch.k === 'var' || ch.k === 'let' || ch.k === 'tls') {
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
        const m = doc.lineAt(i).text.match(/^\s*@?(def|fnc|rpc|var|let|tls)\s+([A-Za-z_]\w*)/);
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

// 函数节点 → sc 风格签名：fnc name[: ret][, a: i4, ...]
// 详情中的导出标记 "@ " 与 ast-view 的 "T::" 前缀不入签名
// fnctype 声明形态（C 侧实现）按 fnc 呈现
function funcSignature(f) {
    const params = (f.c || []).filter(ch => ch.k === 'param')
        .map(p => p.n === '...' ? '...' : p.n + (p.d || ''));
    let d = (f.d || '').replace(/^@\s*/, '');
    if (f.n && f.n.includes('::')) d = d.replace(/^[A-Za-z_]\w*::/, '');
    const kw = f.k === 'fnctype' ? 'fnc' : f.k;
    let sig = `${kw} ${f.n}${d}`;
    if (params.length) sig += ', ' + params.join(', ');
    return sig;
}

// 类型节点 → sc 风格定义块（struct/union/enum 列成员，alias/fnctype 单行）
function typeDefinition(t, idx) {
    const head = `def ${t.n}${(t.d || '').replace(/^@\s*/, '')}`;
    const members = (t.c || []).filter(ch => ch.k === 'field' || ch.k === 'item' || ch.k === 'param');
    let body = members.map(m =>
        '    ' + (m.n === '...' ? '...' : m.n + (m.d || ''))).join('\n');
    // 附该类型的方法签名（builtins/本模块 fnc T::m）
    if (idx)
        for (const [, m] of methodsOf(idx, t.n))
            body += (body ? '\n' : '') + '    ' + funcSignature(m);
    return body ? head + '\n' + body : head;
}

// 悬停 Markdown：代码块呈现签名/定义，附来源说明
function hoverMd(code, note) {
    const md = new vscode.MarkdownString();
    md.appendCodeblock(code, 'sc');
    if (note) md.appendMarkdown(note);
    return new vscode.Hover(md);
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

// ================= IDE 语义增强：引用/重命名/工作区符号/调用层次 =================
// 共用基础：基于 AST 的符号分类 + 作用域感知的源码标识符扫描（跳过注释/字符串）。
// 设计取向与本扩展其它能力一致：能精确处就精确（局部变量按函数体范围、顶层符号按
// 全局命名空间），不能精确处保守宁缺（成员/字段仅当前文件，未知符号拒绝重命名）。

const KW_SET = new Set(KEYWORDS.map(k => k[0]).concat(LITERALS.map(l => l[0])));
const TYPE_SET = new Set(TYPES.map(t => t[0]));

// 行内标识符位置（跳过 # 行注释、"..." 字符串、'...' 字符字面量）。
// 返回 [{ start, name }]，start 为 0-based 列。
function identPositionsInLine(text) {
    const out = [];
    let i = 0;
    const n = text.length;
    while (i < n) {
        const ch = text[i];
        if (ch === '#') break;                          // 行注释直到行尾
        if (ch === '"' || ch === "'") {                 // 跳过字符串/字符字面量
            const q = ch; i++;
            while (i < n) {
                if (text[i] === '\\') { i += 2; continue; }
                if (text[i] === q) { i++; break; }
                i++;
            }
            continue;
        }
        if (/[A-Za-z_]/.test(ch)) {
            let j = i + 1;
            while (j < n && /\w/.test(text[j])) j++;
            out.push({ start: i, name: text.slice(i, j) });
            i = j; continue;
        }
        i++;
    }
    return out;
}

// 在 [startLine, endLine]（0-based，含端点）内查找名为 name 的标识符，返回 Range 列表。
function findOccurrenceRanges(lines, name, startLine, endLine) {
    const out = [];
    for (let ln = startLine; ln <= endLine; ln++) {
        const text = lines[ln];
        if (text === undefined) continue;
        if (text.indexOf(name) === -1) continue;
        for (const id of identPositionsInLine(text))
            if (id.name === name)
                out.push(new vscode.Range(ln, id.start, ln, id.start + name.length));
    }
    return out;
}

// 收集函数节点内所有参数/局部变量名（不按行过滤；sc 无嵌套函数，整体递归即可）。
function allScopeVarNames(fn) {
    const names = new Set();
    const walk = (node) => {
        for (const ch of node.c || []) {
            if (ch.k === 'param') names.add(ch.n);
            else if (ch.k === 'var' || ch.k === 'let' || ch.k === 'tls')
                for (const it of ch.c || []) names.add(it.n);
            walk(ch);
        }
    };
    walk(fn);
    return names;
}

// 光标行所在的非外部顶层函数及其行范围（1-based 起始，0-based 行范围）。
function enclosingFuncSpan(ast, lineCount, line1) {
    const tops = (ast.c || []).filter(n => !n.x && n.l).slice().sort((a, b) => a.l - b.l);
    for (let i = 0; i < tops.length; i++) {
        const nd = tops[i];
        if (nd.k !== 'fnc' && nd.k !== 'rpc') continue;
        const start = nd.l;
        const end = (i + 1 < tops.length) ? tops[i + 1].l - 1 : lineCount;
        if (line1 >= start && line1 <= end)
            return { node: nd, startLine: start - 1, endLine: end - 1 };
    }
    return null;
}

// 是否为某类型的字段名（跨所有已知类型）。
function isFieldName(idx, name) {
    for (const [, t] of idx.types)
        if ((t.c || []).some(f => f.k === 'field' && f.n === name)) return true;
    return false;
}

// 符号分类：返回 { name, scope, globalKind, startLine?, endLine?, word }。
//   scope: 'local' | 'global'
//   globalKind: 'top'（顶层全局：类型/函数/全局变量/枚举项，跨文件） |
//               'member'（字段/方法，仅当前文件） | 'unknown'（拒绝重命名）
function classifySymbol(doc, pos, ast) {
    const w = getWordAt(doc, pos);
    if (!w) return null;
    const idx = buildIndex(ast);
    const name = w.word;
    const span = enclosingFuncSpan(ast, doc.lineCount, pos.line + 1);
    if (span && allScopeVarNames(span.node).has(name))
        return { name, scope: 'local', globalKind: 'local',
                 startLine: span.startLine, endLine: span.endLine, word: w };
    if (idx.types.has(name) || idx.funcs.has(name) || idx.globals.has(name))
        return { name, scope: 'global', globalKind: 'top', word: w };
    const isMethod = [...idx.methods.keys()].some(k => k.endsWith('::' + name));
    if (isMethod || isFieldName(idx, name))
        return { name, scope: 'global', globalKind: 'member', word: w };
    return { name, scope: 'global', globalKind: 'unknown', word: w };
}

// ---- 工作区文件扫描（含顶层声明解析），缓存按磁盘 mtime；打开的脏文档用实时内容 ----
const fileScanCache = new Map();   // fsPath -> { mtime, text, lines, decls }

// 顶层声明（列 0 起，sc 顶层不缩进）解析，含跨到下一顶层声明的行范围。
function parseTopDecls(lines) {
    const heads = [];
    for (let i = 0; i < lines.length; i++) {
        const m = lines[i].match(/^@?(def|fnc|rpc|var|let|tls|inc|add)\b\s*([A-Za-z_]\w*(?:::[A-Za-z_]\w*)?)?/);
        if (m) heads.push({ i, kw: m[1], name: m[2] || '' });
    }
    const decls = [];
    for (let h = 0; h < heads.length; h++) {
        const start = heads[h].i;
        const end = (h + 1 < heads.length) ? heads[h + 1].i - 1 : lines.length - 1;
        decls.push({ kind: heads[h].kw, name: heads[h].name, start, end });
    }
    return decls;
}

function liveDocFor(uri) {
    const s = uri.toString();
    return vscode.workspace.textDocuments.find(
        d => d.uri.toString() === s && d.languageId === 'sc') || null;
}

async function scanFile(uri) {
    const live = liveDocFor(uri);
    if (live) {
        const text = live.getText();
        const lines = text.split('\n');
        return { text, lines, decls: parseTopDecls(lines), uri };
    }
    let stat;
    try { stat = await vscode.workspace.fs.stat(uri); } catch { return null; }
    const cached = fileScanCache.get(uri.fsPath);
    if (cached && cached.mtime === stat.mtime) return Object.assign({ uri }, cached);
    let buf;
    try { buf = await vscode.workspace.fs.readFile(uri); } catch { return null; }
    const text = Buffer.from(buf).toString('utf8');
    const lines = text.split('\n');
    const rec = { mtime: stat.mtime, text, lines, decls: parseTopDecls(lines) };
    fileScanCache.set(uri.fsPath, rec);
    return Object.assign({ uri }, rec);
}

function allScFiles() {
    return vscode.workspace.findFiles('**/*.sc', '**/node_modules/**');
}

// 收集引用：局部仅函数体内；顶层全局跨工作区；成员仅当前文件。
async function collectReferences(doc, info) {
    const out = [];
    if (info.scope === 'local') {
        const lines = doc.getText().split('\n');
        for (const r of findOccurrenceRanges(lines, info.name, info.startLine, info.endLine))
            out.push(new vscode.Location(doc.uri, r));
        return out;
    }
    let uris;
    if (info.globalKind === 'top') {
        uris = await allScFiles();
        const set = new Map(uris.map(u => [u.toString(), u]));
        set.set(doc.uri.toString(), doc.uri);   // 含当前（可能未保存）文档
        uris = [...set.values()];
    } else {
        uris = [doc.uri];                        // 成员：仅当前文件，限制误改范围
    }
    for (const uri of uris) {
        const rec = await scanFile(uri);
        if (!rec || rec.text.indexOf(info.name) === -1) continue;
        for (const r of findOccurrenceRanges(rec.lines, info.name, 0, rec.lines.length - 1))
            out.push(new vscode.Location(uri, r));
    }
    return out;
}

// 调用名归一化：去掉 T:: 方法前缀（源码里方法调用以短名出现）。
function callKey(name) {
    const i = name.indexOf('::');
    return i >= 0 ? name.slice(i + 2) : name;
}

// 工作区函数索引：callKey -> [{ uri, decl, rec }]。
async function buildFuncIndex() {
    const map = new Map();
    for (const uri of await allScFiles()) {
        const rec = await scanFile(uri);
        if (!rec) continue;
        for (const d of rec.decls) {
            if (d.kind !== 'fnc' && d.kind !== 'rpc') continue;
            const k = callKey(d.name);
            if (!map.has(k)) map.set(k, []);
            map.get(k).push({ uri, decl: d, rec });
        }
    }
    return map;
}

// 某行范围内形如 IDENT( 的调用点：返回 [{ name, range }]。
function callsInSpan(lines, start, end) {
    const out = [];
    for (let ln = start; ln <= end; ln++) {
        const text = lines[ln];
        if (text === undefined) continue;
        const ids = identPositionsInLine(text);
        for (const id of ids) {
            const after = text.slice(id.start + id.name.length);
            if (/^\s*\(/.test(after))
                out.push({ name: id.name,
                           range: new vscode.Range(ln, id.start, ln, id.start + id.name.length) });
        }
    }
    return out;
}

// 构造 CallHierarchyItem（选择范围取声明头行中的名字）。
function makeCallItem(uri, decl, headLine) {
    const range = new vscode.Range(decl.start, 0, decl.end, 0);
    const idxName = headLine ? headLine.indexOf(decl.name) : -1;
    const sel = idxName >= 0
        ? new vscode.Range(decl.start, idxName, decl.start, idxName + decl.name.length)
        : new vscode.Range(decl.start, 0, decl.start, 0);
    const kind = decl.kind === 'rpc' ? vscode.SymbolKind.Event : vscode.SymbolKind.Function;
    return new vscode.CallHierarchyItem(kind, decl.name, decl.kind, uri, range, sel);
}

// 注册四项语义增强提供器。
function registerSemanticProviders(context) {
    // 查找引用
    context.subscriptions.push(vscode.languages.registerReferenceProvider('sc', {
        async provideReferences(doc, pos) {
            const ast = await getAst(doc, -1);
            if (!ast) return null;
            const info = classifySymbol(doc, pos, ast);
            if (!info) return null;
            return collectReferences(doc, info);
        }
    }));

    // 重命名
    context.subscriptions.push(vscode.languages.registerRenameProvider('sc', {
        prepareRename(doc, pos) {
            const w = getWordAt(doc, pos);
            if (!w) throw new Error('此处无可重命名的符号');
            if (KW_SET.has(w.word) || TYPE_SET.has(w.word))
                throw new Error('不能重命名关键字或内置类型');
            return w.range;
        },
        async provideRenameEdits(doc, pos, newName) {
            if (!/^[A-Za-z_]\w*$/.test(newName)) throw new Error('无效的标识符名');
            const ast = await getAst(doc, -1);
            if (!ast) throw new Error('源码暂时无法解析，无法重命名');
            const info = classifySymbol(doc, pos, ast);
            if (!info || info.globalKind === 'unknown')
                throw new Error('无法确定该符号的定义，已避免误改');
            const locs = await collectReferences(doc, info);
            if (!locs.length) throw new Error('未找到该符号的任何出现');
            const edit = new vscode.WorkspaceEdit();
            for (const loc of locs) edit.replace(loc.uri, loc.range, newName);
            return edit;
        }
    }));

    // 工作区符号索引（跨文件）
    context.subscriptions.push(vscode.languages.registerWorkspaceSymbolProvider({
        async provideWorkspaceSymbols(query) {
            const K = vscode.SymbolKind;
            const kindFor = (kw) => kw === 'fnc' || kw === 'rpc' ? K.Function
                : kw === 'def' ? K.Class
                : kw === 'let' ? K.Constant : K.Variable;
            const ql = (query || '').toLowerCase();
            const out = [];
            for (const uri of await allScFiles()) {
                const rec = await scanFile(uri);
                if (!rec) continue;
                for (const d of rec.decls) {
                    if (!d.name) continue;
                    if (!['def', 'fnc', 'rpc', 'var', 'let', 'tls'].includes(d.kind)) continue;
                    if (ql && !d.name.toLowerCase().includes(ql)) continue;
                    const p = new vscode.Position(d.start, Math.max(0, rec.lines[d.start].indexOf(d.name)));
                    out.push(new vscode.SymbolInformation(
                        d.name, kindFor(d.kind), '', new vscode.Location(uri, p)));
                }
            }
            return out;
        }
    }));

    // 调用层次
    context.subscriptions.push(vscode.languages.registerCallHierarchyProvider('sc', {
        async prepareCallHierarchy(doc, pos) {
            const w = getWordAt(doc, pos);
            if (!w) return null;
            const funcIndex = await buildFuncIndex();
            let cands = (funcIndex.get(callKey(w.word)) || []).slice();
            // 同名函数可能遍布多文件：优先当前文件，再优先光标所在声明体
            const sameFile = cands.filter(c => c.uri.toString() === doc.uri.toString());
            if (sameFile.length) cands = sameFile;
            const onDecl = cands.filter(c => pos.line >= c.decl.start && pos.line <= c.decl.end);
            if (onDecl.length) cands = onDecl;
            if (!cands.length) {
                const rec = await scanFile(doc.uri);
                const encl = rec && rec.decls.find(d =>
                    (d.kind === 'fnc' || d.kind === 'rpc') && pos.line >= d.start && pos.line <= d.end);
                if (encl) cands = [{ uri: doc.uri, decl: encl, rec }];
            }
            return cands.map(c => makeCallItem(c.uri, c.decl, c.rec.lines[c.decl.start]));
        },
        async provideCallHierarchyIncomingCalls(item) {
            const target = callKey(item.name);
            const results = [];
            for (const uri of await allScFiles()) {
                const rec = await scanFile(uri);
                if (!rec || rec.text.indexOf(target) === -1) continue;
                for (const d of rec.decls) {
                    if (d.kind !== 'fnc' && d.kind !== 'rpc') continue;
                    const ranges = callsInSpan(rec.lines, d.start, d.end)
                        .filter(c => c.name === target).map(c => c.range);
                    if (ranges.length)
                        results.push(new vscode.CallHierarchyIncomingCall(
                            makeCallItem(uri, d, rec.lines[d.start]), ranges));
                }
            }
            return results;
        },
        async provideCallHierarchyOutgoingCalls(item) {
            const funcIndex = await buildFuncIndex();
            const rec = await scanFile(item.uri);
            if (!rec) return [];
            const d = rec.decls.find(x =>
                x.start === item.range.start.line && callKey(x.name) === callKey(item.name));
            if (!d) return [];
            const byCallee = new Map();
            for (const c of callsInSpan(rec.lines, d.start, d.end)) {
                if (!funcIndex.has(c.name)) continue;     // 仅解析到已知函数的调用
                if (!byCallee.has(c.name)) byCallee.set(c.name, []);
                byCallee.get(c.name).push(c.range);
            }
            const results = [];
            for (const [k, ranges] of byCallee) {
                const t = funcIndex.get(k)[0];            // 多候选取首个（无类型解析的歧义取舍）
                results.push(new vscode.CallHierarchyOutgoingCall(
                    makeCallItem(t.uri, t.decl, t.rec.lines[t.decl.start]), ranges));
            }
            return results;
        }
    }));

    // 文件变更时失效扫描缓存
    context.subscriptions.push(vscode.workspace.onDidSaveTextDocument(d => {
        if (d.uri.scheme === 'file') fileScanCache.delete(d.uri.fsPath);
    }));
    context.subscriptions.push(vscode.workspace.onDidDeleteFiles(e => {
        for (const f of e.files) fileScanCache.delete(f.fsPath);
    }));
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

    // 成员补全：字段 + 该类型的方法（含 builtins 外部声明）
    const memberItems = (idx, t) => {
        const items = (t.c || []).map(f =>
                mkItem(f.n, K.Field, (f.d || '').trim(), '0'));
        for (const [short, m] of methodsOf(idx, t.n))
            items.push(mkItem(short, K.Method, funcSignature(m), '0'));
        return items;
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
                return memberItems(idx, t);
            }
            const mem = before.match(/([A-Za-z_]\w*(?:(?:\.|->)[A-Za-z_]\w*)*)(\.|->)\w*$/);
            if (mem) {
                if (!idx) return [];
                const chain = mem[1].split(/\.|->/);
                const t = resolveChain(idx, scopeVars(idx, line), chain);
                if (!t) return [];  // 类型未知时不输出噪音
                return memberItems(idx, t);
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
                cls: vscode.SymbolKind.Class,
                union: vscode.SymbolKind.Struct,
                enum: vscode.SymbolKind.Enum,
                alias: vscode.SymbolKind.TypeParameter,
                fnctype: vscode.SymbolKind.Interface,
                fnc: vscode.SymbolKind.Function,
                dim: vscode.SymbolKind.Method,
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

    // 悬停：函数显示完整签名（含参数），类型显示成员定义，变量显示类型与来源
    context.subscriptions.push(vscode.languages.registerHoverProvider('sc', {
        async provideHover(doc, pos) {
            const w = getWordAt(doc, pos);
            if (!w) return null;
            const ast = await getAst(doc, pos.line);
            if (!ast) return null;
            const idx = buildIndex(ast);
            // 成员上下文：obj.word / ptr->word → 字段或方法（builtins 外部声明在此命中）
            const beforeWord = doc.lineAt(pos.line).text.slice(0, w.range.start.character);
            if (/(\.|->)$/.test(beforeWord)) {
                const mem = beforeWord.match(/([A-Za-z_]\w*(?:(?:\.|->)[A-Za-z_]\w*)*)(\.|->)$/);
                if (mem) {
                    const t = resolveChain(idx, scopeVars(idx, pos.line + 1), mem[1].split(/\.|->/));
                    if (t) {
                        const f = (t.c || []).find(x => x.n === w.word);
                        if (f) return hoverMd(`${w.word}${(f.d || '')}`, `${t.n} 的字段`);
                        const m = idx.methods.get(t.n + '::' + w.word);
                        if (m) return hoverMd(funcSignature(m), `${t.n} 的方法`);
                    }
                }
                // 链类型不可推断：按方法短名在方法表中匫配（可能多类型同名，全部列出）
                const cand = [...idx.methods.values()].filter(m => m.n.endsWith('::' + w.word));
                if (cand.length)
                    return hoverMd(cand.map(funcSignature).join('\n'), '方法（按名称匹配）');
            }
            const locals = scopeVars(idx, pos.line + 1);
            const local = locals.get(w.word);
            if (local) {
                const kw = local.kind === 'param' ? '参数' : local.kind;
                return hoverMd(`${w.word}${(local.detail || '')}`, `${kw} · 局部符号`);
            }
            const g = idx.globals.get(w.word);
            if (g) {
                if (g.kind === 'enum-item')
                    return hoverMd(`${w.word}${(g.detail ? ': ' + g.detail : '')}`, '枚举项');
                return hoverMd(`${g.kind} ${w.word}${(g.detail || '')}`, '全局符号');
            }
            const t = idx.types.get(w.word);
            if (t) {
                // fnctype：函数类型定义或外部函数声明 → 按函数签名呈现
                if (t.k === 'fnctype')
                    return hoverMd(funcSignature(t), t.x ? '函数声明（外部实现）' : '函数类型/函数声明');
                // 展示成员与方法定义；alias 穿透后补充展示目标结构成员
                let code = typeDefinition(t, idx);
                if (t.k === 'alias') {
                    const target = resolveStruct(idx, w.word);
                    if (target && target.n !== w.word) code += '\n\n' + typeDefinition(target, idx);
                }
                return hoverMd(code, `类型定义 (${t.k})`);
            }
            const f = idx.funcs.get(w.word);
            if (f) return hoverMd(funcSignature(f),
                                  (f.k === 'rpc' ? 'rpc 函数' : '函数') + (f.x ? '（外部模块）' : ''));
            return null;
        }
    }));

    // 定义跳转：类型/函数/方法/全局符号；外部声明（builtins/模块）直达来源文件
    context.subscriptions.push(vscode.languages.registerDefinitionProvider('sc', {
        async provideDefinition(doc, pos) {
            const w = getWordAt(doc, pos);
            if (!w) return null;
            const ast = await getAst(doc, pos.line);
            if (!ast) return null;
            const idx = buildIndex(ast);
            // 成员访问：obj.m / ptr->m → 优先按方法/字段解析
            const beforeWord = doc.lineAt(pos.line).text.slice(0, w.range.start.character);
            const hits = [];
            if (/(\.|->)$/.test(beforeWord)) {
                const mem = beforeWord.match(/([A-Za-z_]\w*(?:(?:\.|->)[A-Za-z_]\w*)*)(\.|->)$/);
                if (mem) {
                    const t = resolveChain(idx, scopeVars(idx, pos.line + 1), mem[1].split(/\.|->/));
                    if (t) {
                        hits.push(idx.methods.get(t.n + '::' + w.word));
                        hits.push((t.c || []).find(x => x.n === w.word));
                    }
                }
            }
            hits.push(idx.types.get(w.word), idx.funcs.get(w.word), idx.globals.get(w.word));
            for (const hit of hits) {
                if (!hit) continue;
                // 外部声明带来源文件（o）：行号属于来源文件，直达跳转
                const uri = hit.o && fs.existsSync(hit.o) ? vscode.Uri.file(hit.o) : doc.uri;
                const line = Math.max(0, (hit.l || hit.line || 1) - 1);
                return new vscode.Location(uri, new vscode.Position(line, 0));
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

    // IDE 语义增强：查找引用 / 重命名 / 工作区符号 / 调用层次
    registerSemanticProviders(context);

    // 实时诊断：在编辑时运行 scc --ast，失败时产出问题面板诊断
    const diagnostics = vscode.languages.createDiagnosticCollection('sc');
    context.subscriptions.push(diagnostics);
    const timers = new Map();
    const refreshDiagnostics = async (doc) => {
        if (!doc || doc.languageId !== 'sc') return;
        try {
            // 文件模式：inc 的模块声明可见，避免对 run/thread 等误报
            await runAstOnText(doc, doc.getText());
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
