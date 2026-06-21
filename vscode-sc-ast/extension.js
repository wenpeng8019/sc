// sc AST 视图插件：调用 scc --ast / --emit-sc 实时渲染
const vscode = require('vscode');
const cp = require('child_process');
const fs = require('fs');
const path = require('path');

const TREE_SRC_SCHEME = 'sc-tree-src';

let currentDoc = null; // 当前跟踪的 sc 文档

// ---------------- scc 调用 ----------------
function findScc() {
    const cfg = vscode.workspace.getConfiguration('scAst').get('sccPath') ||
                vscode.workspace.getConfiguration('sc').get('sccPath');
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

// ---------------- AST 树视图 ----------------
const ICONS = {
    inc: 'package',
    add: 'file-binary',
    enum: 'symbol-enum', struct: 'symbol-class', union: 'symbol-class',
    alias: 'symbol-interface', fnctype: 'symbol-method', fnc: 'symbol-function', rpc: 'symbol-event',
    var: 'symbol-variable', let: 'symbol-constant', tls: 'symbol-variable',
    param: 'symbol-parameter', field: 'symbol-field', item: 'symbol-enum-member',
    case: 'list-selection', arm: 'list-flat', through: 'arrow-right',
    if: 'git-branch', else: 'git-branch', while: 'sync', for: 'sync',
    'for-in': 'sync',
    retcall: 'git-branch',
    'do-while': 'sync-ignored', goto: 'arrow-swap', label: 'tag',
    return: 'arrow-left', break: 'debug-step-out', continue: 'debug-continue',
    run: 'play',
    wait: 'watch',
    final: 'shield', done: 'check-all',
    expr: 'symbol-operator', error: 'error',
    section: 'symbol-namespace', module: 'package', warning: 'warning',
};

// 去掉头名两侧的 <>/"" 修饰，用于分组/展示
function bareHeader(s) {
    if (!s) return s;
    if (s.length >= 2 &&
        ((s[0] === '<' && s[s.length - 1] === '>') ||
         (s[0] === '"' && s[s.length - 1] === '"')))
        return s.slice(1, -1);
    return s;
}

// 后处理 AST 根：把外部描述符（C 头 / .sc 模块导入）聚合为
//   1. 顶层"外部描述符"汇总区（按来源分组，标注已用/共计）
//   2. 内联到各 inc 节点下
// 并把编译器返回的非致命警告（root.w）渲染为顶层"警告"区。
function transformRoot(root) {
    if (!root) return root;
    const top = root.c || [];

    // 1. 收集顶层外部描述符节点（x===1 且非 inc），按来源 origin 分组
    const groups = new Map();   // origin -> [descNode]
    const rest = [];
    for (const n of top) {
        if (n.x === 1 && n.k !== 'inc' && n.o) {
            if (!groups.has(n.o)) groups.set(n.o, []);
            groups.get(n.o).push(n);
        } else {
            rest.push(n);
        }
    }

    const sortDescs = a => a.slice().sort((x, y) =>
        (y.u || 0) - (x.u || 0) || String(x.n).localeCompare(String(y.n)));

    // 2. 外部 inc 节点：内联其来源描述符，标注计数
    const sources = [];   // 汇总区用
    for (const n of rest) {
        if (n.k === 'inc' && n.x === 1) {
            const descs = sortDescs(groups.get(n.o) || []);
            const usedCount = descs.filter(d => d.u === 1).length;
            const total = (typeof n.t === 'number') ? n.t : descs.length;
            n.c = descs;
            n._used = usedCount;
            n._total = total;
            n._collapsed = true;
            sources.push({ origin: n.o, label: bareHeader(n.n), total, usedCount, descs });
        }
    }

    const newTop = [];

    // 3. 顶层"外部描述符"汇总区
    if (sources.length) {
        const usedSum = sources.reduce((s, e) => s + e.usedCount, 0);
        const moduleNodes = sources.map(e => ({
            k: 'module', n: e.label,
            d: `已用 ${e.usedCount} / 共 ${e.total < 0 ? '?' : e.total}`,
            x: 1, _used: e.usedCount > 0 ? 1 : 0, _collapsed: true,
            // 克隆描述符节点，避免与 inc 内联子节点共享同一对象
            c: e.descs.map(d => Object.assign({}, d)),
        }));
        newTop.push({
            k: 'section', n: '外部描述符',
            d: `${sources.length} 个来源 · 已用 ${usedSum}`,
            c: moduleNodes,
        });
    }

    // 4. 警告区
    if (root.w && root.w.length) {
        newTop.push({
            k: 'section', n: '警告', _warn: 1, d: `${root.w.length} 条`,
            c: root.w.map(w => ({ k: 'warning', n: '', d: w.m, l: w.l })),
        });
    }

    // 5. 其余顶层节点（含已内联描述符的 inc 与本地声明），保持原顺序
    for (const n of rest) newTop.push(n);

    return Object.assign({}, root, { c: newTop });
}

class AstProvider {
    constructor(treeSrcProvider) {
        this._em = new vscode.EventEmitter();
        this.onDidChangeTreeData = this._em.event;
        this.root = null;
        this.error = null;
        this.treeSrcProvider = treeSrcProvider;
    }

    async refresh(doc) {
        currentDoc = doc || null;
        if (!doc) {
            this.root = this.error = null;
            this._em.fire();
            return;
        }
        try {
            const args = ['-', '--ast'];
            // 文件已落盘时传 --from，使 inc 解析（含 C 头/.sc 模块）以源文件目录为基准；
            // 仍从 stdin 读取实时编辑内容（未保存修改即时反映）
            if (doc.uri.scheme === 'file') args.push('--from', doc.uri.fsPath);
            // clangLibPath: 空=退化文本匹配；"auto"=自动检测平台默认 libclang；其余=显式路径
            const clang = (vscode.workspace.getConfiguration('scAst').get('clangLibPath') || '').trim();
            if (clang === 'auto') args.push('--clang');
            else if (clang) args.push('--clang', clang);
            const out = await runScc(args, doc.getText());
            this.root = transformRoot(JSON.parse(out));
            this.error = null;
        } catch (e) {
            this.error = String(e.message || e);
            this.root = null;
        }
        this._em.fire();
        this.treeSrcProvider.fireChange();
    }

    getChildren(el) {
        if (!el) {
            if (this.error) return [{ k: 'error', d: this.error }];
            return this.root ? this.root.c || [] : [];
        }
        return el.c || [];
    }

    getTreeItem(n) {
        const hasKids = n.c && n.c.length;
        const collapsible = !hasKids ? vscode.TreeItemCollapsibleState.None
            : n._collapsed ? vscode.TreeItemCollapsibleState.Collapsed
                           : vscode.TreeItemCollapsibleState.Expanded;
        const item = new vscode.TreeItem(
            n.k === 'error' ? '解析错误' : (n.n || n.k), collapsible);
        item.description = n.d || (n.n ? n.k : '');
        item.tooltip = `${n.k}${n.n ? ' ' + n.n : ''}${n.d ? ' ' + n.d : ''}` +
                       (n.l ? `  (第 ${n.l} 行)` : '');

        // 颜色：警告=警告色；外部描述符 已用=橙、仅导入未用=灰；其余按既有规则
        let color;
        if (n.k === 'warning' || n._warn)
            color = new vscode.ThemeColor('list.warningForeground');
        else if (n.x && (n.k === 'inc' || n.k === 'module'))
            color = new vscode.ThemeColor(n._used ? 'charts.orange' : 'disabledForeground');
        else if (n.x)
            color = new vscode.ThemeColor(n.u === 1 ? 'charts.orange' : 'disabledForeground');
        item.iconPath = new vscode.ThemeIcon(ICONS[n.k] || 'circle-outline', color);

        // 外部 inc 节点：展示"已用 N / 共 M"
        if (n.k === 'inc' && n.x) {
            item.description = `已用 ${n._used || 0} / 共 ${n._total < 0 ? '?' : n._total}`;
        } else if (n.x && n.o && n.k !== 'module') {
            item.description = `${item.description}  [${path.basename(n.o)}]`;
        }
        if (n.l) {
            item.command = {
                command: 'scAst.reveal', title: '跳转到源码', arguments: [n.l],
            };
        }
        return item;
    }
}

// ---------------- 树结构源码（虚拟文档） ----------------
class TreeSrcProvider {
    constructor() {
        this._em = new vscode.EventEmitter();
        this.onDidChange = this._em.event;
    }

    uriFor(doc) {
        const name = path.basename(doc.fileName).replace(/\.sc$/, '') + '.tree.sc';
        return vscode.Uri.parse(`${TREE_SRC_SCHEME}:/${name}`);
    }

    fireChange() {
        if (currentDoc) this._em.fire(this.uriFor(currentDoc));
    }

    async provideTextDocumentContent() {
        if (!currentDoc) return '# 没有活动的 sc 文件';
        try {
            return await runScc(['-', '--emit-sc'], currentDoc.getText());
        } catch (e) {
            return '# 解析错误，无法生成树结构源码\n# ' +
                   String(e.message || e).replace(/\n/g, '\n# ');
        }
    }
}

// ---------------- 激活 ----------------
function activate(context) {
    const treeSrc = new TreeSrcProvider();
    const ast = new AstProvider(treeSrc);

    context.subscriptions.push(
        vscode.window.createTreeView('scAstView', { treeDataProvider: ast, showCollapseAll: true }),
        vscode.workspace.registerTextDocumentContentProvider(TREE_SRC_SCHEME, treeSrc));

    // 命令：刷新
    context.subscriptions.push(vscode.commands.registerCommand('scAst.refresh', () => {
        ast.refresh(activeScDoc());
    }));

    // 命令：AST 节点跳转源码
    context.subscriptions.push(vscode.commands.registerCommand('scAst.reveal', async (line) => {
        if (!currentDoc) return;
        const editor = await vscode.window.showTextDocument(currentDoc,
            { viewColumn: vscode.ViewColumn.One, preserveFocus: false });
        const pos = new vscode.Position(line - 1, 0);
        editor.selection = new vscode.Selection(pos, pos);
        editor.revealRange(new vscode.Range(pos, pos), vscode.TextEditorRevealType.InCenter);
    }));

    // 命令：切换树结构源码视图
    context.subscriptions.push(vscode.commands.registerCommand('scAst.toggleTreeSource', async () => {
        // 已打开则关闭（切换）
        for (const tab of vscode.window.tabGroups.all.flatMap(g => g.tabs)) {
            const input = tab.input;
            if (input && input.uri && input.uri.scheme === TREE_SRC_SCHEME) {
                await vscode.window.tabGroups.close(tab);
                return;
            }
        }
        if (!currentDoc) {
            vscode.window.showInformationMessage('请先打开一个 .sc 文件');
            return;
        }
        const doc = await vscode.workspace.openTextDocument(treeSrc.uriFor(currentDoc));
        await vscode.languages.setTextDocumentLanguage(doc, 'sc');
        await vscode.window.showTextDocument(doc,
            { viewColumn: vscode.ViewColumn.Beside, preview: false, preserveFocus: true });
    }));

    // 实时刷新：编辑（防抖）与切换编辑器
    let timer = null;
    context.subscriptions.push(vscode.workspace.onDidChangeTextDocument(e => {
        if (e.document.languageId !== 'sc') return;
        if (vscode.window.activeTextEditor &&
            vscode.window.activeTextEditor.document !== e.document) return;
        clearTimeout(timer);
        timer = setTimeout(() => ast.refresh(e.document), 300);
    }));
    context.subscriptions.push(vscode.window.onDidChangeActiveTextEditor(ed => {
        if (ed && ed.document.languageId === 'sc' &&
            ed.document.uri.scheme !== TREE_SRC_SCHEME) {
            ast.refresh(ed.document);
        }
    }));

    ast.refresh(activeScDoc());
}

function activeScDoc() {
    const ed = vscode.window.activeTextEditor;
    return ed && ed.document.languageId === 'sc' ? ed.document : null;
}

function deactivate() {}

module.exports = { activate, deactivate };
