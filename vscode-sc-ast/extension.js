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
    enum: 'symbol-enum', struct: 'symbol-class', union: 'symbol-class',
    alias: 'symbol-interface', fnctype: 'symbol-method', fnc: 'symbol-function', rpc: 'symbol-event',
    var: 'symbol-variable', let: 'symbol-constant', tls: 'symbol-variable',
    param: 'symbol-parameter', field: 'symbol-field', item: 'symbol-enum-member',
    case: 'list-selection', arm: 'list-flat', through: 'arrow-right',
    if: 'git-branch', else: 'git-branch', while: 'sync', for: 'sync',
    'do-while': 'sync-ignored', goto: 'arrow-swap', label: 'tag',
    return: 'arrow-left', break: 'debug-step-out', continue: 'debug-continue',
    run: 'play',
    wait: 'watch',
    expr: 'symbol-operator', error: 'error',
};

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
            const out = await runScc(['-', '--ast'], doc.getText());
            this.root = JSON.parse(out);
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
        const item = new vscode.TreeItem(
            n.k === 'error' ? '解析错误' : (n.n || n.k),
            hasKids ? vscode.TreeItemCollapsibleState.Expanded
                    : vscode.TreeItemCollapsibleState.None);
        item.description = n.d || (n.n ? n.k : '');
        item.tooltip = `${n.k}${n.n ? ' ' + n.n : ''}${n.d ? ' ' + n.d : ''}` +
                       (n.l ? `  (第 ${n.l} 行)` : '');
        const color = n.x ? new vscode.ThemeColor('charts.orange') : undefined;
        item.iconPath = new vscode.ThemeIcon(ICONS[n.k] || 'circle-outline', color);
        if (n.x && n.o) item.description = `${item.description}  [${path.basename(n.o)}]`;
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
