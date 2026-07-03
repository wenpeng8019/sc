// sc 着色方言（.sg）VS Code 支持
//
// 一期目标：可进行 .sg 语言开发的基础能力
//   1. 补全：stage/关键字、标量与 vec/mat/sampler 类型、GLSL 内建函数、
//      绑定属性（loc/builtin/uniform/storage/push/set/binding）、内建语义，
//      再叠加本文件内扫描到的符号（@def 类型名、stage/fnc 名、var 名）。
//   2. 诊断：编辑/保存时调用 scc 编译 .sg，把 stderr 里的错误显示为红波浪线。
//
// 语法高亮由 syntaxes/sg.tmLanguage.json（TextMate 语法）提供，无需本文件。
const vscode = require('vscode');
const cp = require('child_process');
const fs = require('fs');
const path = require('path');

const STAGES = [
    ['vert', '顶点着色阶段入口（vert 名: 输出类型[, in: 输入类型]）'],
    ['frag', '片元着色阶段入口（frag 名: 输出类型[, in: varying 类型]）'],
    ['comp', '计算着色阶段入口'],
];

const KEYWORDS = [
    ['def', '定义类型（结构体，用作 stage I/O 或资源块）'],
    ['fnc', '定义辅助函数（着色器内的普通函数）'],
    ['var', '定义变量（或资源全局，如 sampler）'],
    ['let', '定义常量（含常量数组，如 let a[3]: vec2 = {..}）'],
    ['inc', '引入依赖'],
    ['return', '从阶段/函数返回'],
    ['if', '条件分支'], ['else', '否则分支'],
    ['while', 'while 循环'], ['do', 'do-while 循环'], ['for', 'for 循环'],
    ['break', '跳出循环'], ['continue', '继续下一次循环'],
    ['sizeof', '类型/表达式字节大小'], ['offsetof', '字段偏移'],
];

const ATTRS = [
    ['loc', '顶点属性/varying 的 location（跟在字段后：field: type loc N）'],
    ['builtin', '内建语义（field: type builtin position 等）'],
    ['uniform', '资源块：uniform（@def 结构体后标注，配 set/binding）'],
    ['storage', '资源块：storage buffer（std430）'],
    ['push', '资源块：push constant'],
    ['set', '描述符集编号（set N）'],
    ['binding', '绑定编号（binding N）'],
];

const SEMANTICS = [
    ['position', '裁剪空间位置 → gl_Position（vert 输出）/ gl_FragCoord（frag 输入）'],
    ['frag_coord', '片元窗口坐标 → gl_FragCoord'],
    ['frag_depth', '片元深度 → gl_FragDepth'],
    ['vertex_id', '顶点索引 → gl_VertexIndex'],
    ['instance_id', '实例索引 → gl_InstanceIndex'],
    ['point_size', '点大小 → gl_PointSize'],
    ['local_id', '工作组内局部 id → gl_LocalInvocationID'],
    ['global_id', '全局调用 id → gl_GlobalInvocationID'],
    ['workgroup_id', '工作组 id → gl_WorkGroupID'],
];

const TYPES = [
    ['i1', 'int8_t'], ['i2', 'int16_t'], ['i4', 'int（GLSL int）'], ['i8', 'int64_t'],
    ['u1', 'uint8_t'], ['u2', 'uint16_t'], ['u4', 'uint（GLSL uint）'], ['u8', 'uint64_t'],
    ['bool', '布尔'], ['f4', 'float'], ['f8', 'double（仅桌面 GL）'], ['void', '无返回'],
    ['vec2', '二维浮点向量'], ['vec3', '三维浮点向量'], ['vec4', '四维浮点向量'],
    ['ivec2', 'int 向量'], ['ivec3', 'int 向量'], ['ivec4', 'int 向量'],
    ['uvec2', 'uint 向量'], ['uvec3', 'uint 向量'], ['uvec4', 'uint 向量'],
    ['bvec2', 'bool 向量'], ['bvec3', 'bool 向量'], ['bvec4', 'bool 向量'],
    ['mat2', '2x2 矩阵'], ['mat3', '3x3 矩阵'], ['mat4', '4x4 矩阵'],
    ['sampler2D', '2D 采样器'], ['sampler3D', '3D 采样器'], ['samplerCube', '立方体采样器'],
    ['sampler2DArray', '2D 数组采样器'],
];

const BUILTIN_FNS = [
    'texture', 'textureLod', 'texelFetch', 'textureSize',
    'normalize', 'length', 'distance', 'dot', 'cross', 'reflect', 'refract',
    'pow', 'exp', 'log', 'sqrt', 'inversesqrt', 'abs', 'sign', 'floor', 'ceil',
    'fract', 'mod', 'min', 'max', 'clamp', 'mix', 'step', 'smoothstep',
    'sin', 'cos', 'tan', 'asin', 'acos', 'atan', 'radians', 'degrees',
    'transpose', 'inverse', 'determinant', 'dFdx', 'dFdy', 'fwidth',
];

// ---------------- scc 定位 ----------------
function findScc() {
    const cfg = vscode.workspace.getConfiguration('sg').get('sccPath');
    if (cfg) return cfg;
    for (const f of vscode.workspace.workspaceFolders || []) {
        const p = path.join(f.uri.fsPath, 'compiler', 'build', 'scc');
        if (fs.existsSync(p)) return p;
    }
    return 'scc';
}

function stripAnsi(s) {
    return String(s || '').replace(/\x1b\[[0-9;]*m/g, '');
}

// ---------------- 补全 ----------------
function makeItem(label, kind, detail, sort) {
    const it = new vscode.CompletionItem(label, kind);
    it.detail = detail;
    if (sort) it.sortText = sort;
    return it;
}

// 扫描本文件里声明的符号（@def 类型、stage/fnc 名、var/let 名）
function documentSymbols(doc) {
    const K = vscode.CompletionItemKind;
    const items = [];
    const seen = new Set();
    const add = (name, kind, detail) => {
        if (!name || seen.has(name)) return;
        seen.add(name);
        items.push(makeItem(name, kind, detail, '2' + name));
    };
    for (let i = 0; i < doc.lineCount; i++) {
        const t = doc.lineAt(i).text;
        let m;
        if ((m = t.match(/^\s*@?def\s+([A-Za-z_]\w*)/))) add(m[1], K.Struct, 'def 类型');
        else if ((m = t.match(/^\s*@?(vert|frag|comp)\s+([A-Za-z_]\w*)/))) add(m[2], K.Function, m[1] + ' 阶段');
        else if ((m = t.match(/^\s*@?fnc\s+([A-Za-z_]\w*)/))) add(m[1], K.Function, 'fnc');
        else if ((m = t.match(/^\s*@?(var|let|tls)\s+([A-Za-z_]\w*)/))) add(m[2], K.Variable, m[1]);
    }
    return items;
}

function provideCompletions(doc, pos) {
    const K = vscode.CompletionItemKind;
    const line = doc.lineAt(pos.line).text;
    const prefix = line.slice(0, pos.character);

    // 类型上下文（冒号后）：只给类型 + 本文件 def 名
    if (/:\s*[A-Za-z_]*$/.test(prefix)) {
        const items = TYPES.map(([n, d]) => makeItem(n, K.TypeParameter, d, '0' + n));
        for (const it of documentSymbols(doc))
            if (it.kind === K.Struct) items.push(it);
        return items;
    }

    const items = [];
    // 行首：stage 与声明关键字优先
    if (/^\s*[A-Za-z_]*$/.test(prefix)) {
        for (const [n, d] of STAGES) items.push(makeItem(n, K.Keyword, d, '0' + n));
    }
    for (const [n, d] of KEYWORDS) items.push(makeItem(n, K.Keyword, d, '3' + n));
    for (const [n, d] of ATTRS) items.push(makeItem(n, K.Property, d, '3' + n));
    for (const [n, d] of SEMANTICS) items.push(makeItem(n, K.EnumMember, d, '3' + n));
    for (const [n, d] of TYPES) items.push(makeItem(n, K.TypeParameter, d, '4' + n));
    for (const n of BUILTIN_FNS) items.push(makeItem(n, K.Function, 'GLSL 内建函数', '4' + n));
    items.push(...documentSymbols(doc));
    return items;
}

// ---------------- 诊断 ----------------
let diagCol;
let seq = 0;
const timers = new Map();

function parseDiagnostics(errText, doc) {
    const out = [];
    for (const raw of stripAnsi(errText).split('\n')) {
        const l = raw.trim();
        if (!l) continue;
        let m = l.match(/:(\d+):\s*(?:着色器编译错误:\s*)?(.+)$/);
        let lineNum, msg;
        if (m) { lineNum = Number(m[1]); msg = m[2].trim(); }
        else if (/^sg:/.test(l)) { lineNum = 1; msg = l.replace(/^sg:\s*/, ''); }
        else continue;
        lineNum = Math.max(1, Math.min(doc.lineCount, lineNum));
        const text = doc.lineAt(lineNum - 1).text;
        const start = text.length - text.trimStart().length;
        const range = new vscode.Range(lineNum - 1, start, lineNum - 1, Math.max(start + 1, text.length));
        out.push(new vscode.Diagnostic(range, msg, vscode.DiagnosticSeverity.Error));
    }
    return out;
}

function runDiagnostics(doc) {
    if (doc.languageId !== 'sg') return;
    if (!vscode.workspace.getConfiguration('sg').get('diagnostics')) {
        diagCol.set(doc.uri, []);
        return;
    }
    const dir = path.dirname(doc.uri.fsPath);
    const tmp = path.join(dir, `.sg_check_${process.pid}_${++seq}.tmp.sg`);
    let text = doc.getText();
    try {
        fs.writeFileSync(tmp, text);
    } catch {
        return;  // 无法写临时文件（如未保存的无目录文档），跳过诊断
    }
    cp.execFile(findScc(), [tmp], { maxBuffer: 8 * 1024 * 1024 }, (err, _stdout, stderr) => {
        try { fs.unlinkSync(tmp); } catch { /* 已删 */ }
        // scc 出错时 err 非空；stderr 承载错误文本
        const diags = (err || stderr) ? parseDiagnostics(stderr, doc) : [];
        diagCol.set(doc.uri, diags);
    });
}

function scheduleDiagnostics(doc) {
    const key = doc.uri.toString();
    clearTimeout(timers.get(key));
    timers.set(key, setTimeout(() => runDiagnostics(doc), 400));
}

function activate(context) {
    diagCol = vscode.languages.createDiagnosticCollection('sg');
    context.subscriptions.push(diagCol);

    context.subscriptions.push(
        vscode.languages.registerCompletionItemProvider(
            { language: 'sg' },
            { provideCompletionItems: provideCompletions },
            '.', ':', ' '
        )
    );

    const consider = (doc) => { if (doc && doc.languageId === 'sg') scheduleDiagnostics(doc); };
    context.subscriptions.push(
        vscode.workspace.onDidOpenTextDocument(consider),
        vscode.workspace.onDidSaveTextDocument(consider),
        vscode.workspace.onDidChangeTextDocument(e => consider(e.document)),
        vscode.workspace.onDidCloseTextDocument(d => diagCol.delete(d.uri))
    );
    for (const doc of vscode.workspace.textDocuments) consider(doc);
}

function deactivate() {}

module.exports = { activate, deactivate };
